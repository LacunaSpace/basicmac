# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Optional,Set

import asyncio
import itertools
import random
import struct

class PeripheralsUtil:
    @staticmethod
    def setbits(v:int, mask:int, on:bool=True) -> int:
        return v | mask if on else v & ~mask

class InterruptController:
    def __init__(self, ev:'SimEvent') -> None:
        self.ev = ev
        self.irq = 0

    def set(self, line:int) -> None:
        self.irq |= (1 << line)
        if self.irq:
            self.ev.set('irq')

    def clear(self, line:int) -> None:
        self.irq &= ~(1 << line)
        if self.irq == 0:
            self.ev.clear('irq')

    def next(self) -> int:
        if self.irq:
            for i in itertools.count():
                if (self.irq & (1 << i)):
                    return i
        raise RuntimeError('No IRQ lines asserted')


class GPIO:
    def __init__(self, ic:InterruptController, line:int) -> None:
        self.ic   = ic
        self.line = line
        self.reset()
        self.watchers:Set[asyncio.Event] = set()

    def reset(self) -> None:
                       # high  low  inp  ana
        self.cfg1 = 0  #    1    1    0    0
        self.cfg2 = 0  #    1    0    1    0
        self.pup  = 0  # pull-up
        self.pdn  = 0  # pull-down
        self.rise = 0  # rising edge irq
        self.fall = 0  # falling edge irq

        self.inpm = 0  # connected input
        self.inpv = 0  # input value

        self.val  = 0  # calculated value
        self.irq  = 0  # pending irq

        self.epup = 0  # external pull-up
        self.epdn = 0  # external pull-down

        self.ic.clear(self.line)

    def read_irq(self) -> int:
        return self.irq

    def clear_irq(self, mask:int) -> None:
        self.irq &= ~mask
        if self.irq == 0:
            self.ic.clear(self.line)

    def read(self) -> int:
        return self.val

    def __getitem__(self, pio:int) -> bool:
        return bool(self.val & (1 << pio))

    def config(self, config:bytes) -> None:
        (self.cfg1, self.cfg2, self.pup, self.pdn,
                self.rise, self.fall) = struct.unpack('<IIIIII', config)
        self.update()

    def drive(self, pio:int, value:Optional[int]) -> None:
        mask = (1 << pio)
        if value is None:
            self.inpm &= ~mask
        else:
            if value:
                self.inpv |= mask
            else:
                self.inpv &= ~mask
            self.inpm |= mask
        self.update()

    def extconfig(self, pio:int, epup:bool=False, epdn:bool=False) -> None:
        mask = (1 << pio)
        if epup:
            self.epup |= mask
        else:
            self.epup &= ~mask
        if epdn:
            self.epdn |= mask
        else:
            self.epdn &= ~mask

    async def waitfor(self, line:int, lvl:bool) -> None:
        mask = 1 << line
        if (not not (self.val & mask)) != lvl:
            ev = asyncio.Event()
            self.watchers.add(ev)
            while (not not (self.val & mask)) != lvl:
                await ev.wait()
                ev.clear()
            self.watchers.remove(ev)

    def update(self) -> None:
        if self.cfg1 & self.inpm:
            raise RuntimeError('GPIO short circuit!')
        pval = self.val
        self.val = ((self.cfg1 & self.cfg2)
                | ((~self.cfg1 & self.cfg2)
                    & ((self.inpm & self.inpv) | (~self.inpm & (self.pup | self.epup)) |
                        ((~self.inpm & ~self.pdn & ~self.epdn) & random.getrandbits(32)))))
        cval = pval ^ self.val
        if cval:
            self.irq |= ((self.rise & cval & self.val)
                    | (self.fall & cval & ~self.val))
            if self.irq:
                self.ic.set(self.line)
            for e in self.watchers:
                e.set()

class UART:
    def __init__(self, ic:InterruptController, line:int) -> None:
        self.ic   = ic
        self.line = line
        self.buf  = asyncio.Queue() # type: asyncio.Queue[int]
        self.reset()

    SR_RXE = (1 << 0)
    SR_TXE = (1 << 1)
    SR_RXR = (1 << 2)
    SR_TXR = (1 << 3)
    SR_OVR = (1 << 4)

    def reset(self) -> None:
        self.sr = 0
        self.rx = 0
        self.tx = 0
        self.config()
        self.irq_update()

    def setrx(self, on:bool) -> None:
        self.sr = PeripheralsUtil.setbits(self.sr, UART.SR_RXE, on)
        self.irq_update()

    def settx(self, on:bool) -> None:
        self.sr = PeripheralsUtil.setbits(self.sr, UART.SR_TXE | UART.SR_TXR, on)
        self.irq_update()

    def config(self, br:int=9600, db:int=8, sb:int=1, par:int=0) -> None:
        self.ctime = (1 + db + sb + (not not par)) / br

    def irq_update(self) -> None:
        mask = UART.SR_RXR
        if self.sr & UART.SR_TXE:
            mask |= UART.SR_TXR
        if self.sr & mask:
            self.ic.set(self.line)
        else:
            self.ic.clear(self.line)

    def rx_char(self, ch:int) -> None:
        if self.sr & self.SR_RXR:
            self.sr |= UART.SR_OVR
        else:
            self.sr |= UART.SR_RXR
            self.rx = ch
        self.irq_update()

    def read_rx(self) -> int:
        ch = self.rx
        self.sr &= ~(UART.SR_RXR | UART.SR_OVR)
        self.irq_update()
        return ch

    async def _tx_char(self) -> None:
        await asyncio.sleep(self.ctime)
        sr = self.sr
        if sr & UART.SR_TXE:
            await self.buf.put(self.tx)
            self.sr = sr | UART.SR_TXR
            self.irq_update()

    def tx_char(self, ch:int) -> None:
        sr = self.sr
        if sr & UART.SR_TXE:
            if sr & UART.SR_TXR:
                self.sr = sr & ~UART.SR_TXR
                self.tx = ch
            else:
                self.sr = sr | UART.SR_OVR
                self.tx = random.getrandbits(8)
            self.irq_update()
            asyncio.ensure_future(self._tx_char())

    async def xfr_todevice(self, data:bytes) -> None:
        for ch in data:
            sr0 = self.sr
            await asyncio.sleep(self.ctime)
            if (self.sr & sr0) & UART.SR_RXE:
                self.rx_char(ch)

    async def xfr_fromdevice(self, n:int) -> bytes:
        ba = bytearray(n)
        for i in range(n):
            ba[i] = await self.buf.get()
        return ba
