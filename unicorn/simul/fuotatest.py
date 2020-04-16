# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Optional,Tuple

import argparse
import asyncio
import struct
import sys

from devsimul import LoraMsg, Simulation
from devtest import DeviceTest, LoRaWANTest
from colorama import init as colorama_init
from vtimeloop import VirtualTimeLoop

class FWMan:
    PORT        = 203
    PKG_ID      = 4
    PKG_VERSION = 0x00
    DEV_VERSION = 0x01

class Frag:
    PORT            = 201
    PKG_ID          = 3
    PKG_VERSION     = 0x00
    FRAG_STATUS     = 0x01
    FRAG_SESS_SETUP = 0x02
    FRAG_SESS_DEL   = 0x03
    DATA_FRAGMENT   = 0x08


class FuotaTest(LoRaWANTest):
    @DeviceTest.test(reboot=False)
    async def join(self) -> bool:
        msg = await self.upmsg()
        self.process_join(msg)

        # expect any message
        msg = await self.upmsg()
        m = self.verify(msg)

        return True

    @DeviceTest.test(reboot=False)
    async def fwman_pkgversion(self) -> bool:
        msg = await self.upmsg()
        self.dl(msg, FWMan.PORT, struct.pack('B', FWMan.PKG_VERSION))

        msg, m = await self.ul(FWMan.PORT)
        c, p, v = struct.unpack('BBB', m['FRMPayload'])
        assert c == FWMan.PKG_VERSION and p == FWMan.PKG_ID and v == 1

        return True

    @DeviceTest.test(reboot=False)
    async def fwman_devversion(self) -> bool:
        msg = await self.upmsg()
        self.dl(msg, FWMan.PORT, struct.pack('B', FWMan.DEV_VERSION))

        msg, m = await self.ul(FWMan.PORT)
        c, f, h = struct.unpack('<BII', m['FRMPayload'])
        assert c == FWMan.DEV_VERSION
        print('FW Version: 0x%08x, HW Version: 0x%08x' % (f, h))

        return True

    @DeviceTest.test(reboot=False)
    async def frag_pkgversion(self) -> bool:
        msg = await self.upmsg()
        self.dl(msg, Frag.PORT, struct.pack('B', Frag.PKG_VERSION))

        msg, m = await self.ul(Frag.PORT)
        c, p, v = struct.unpack('BBB', m['FRMPayload'])
        assert c == Frag.PKG_VERSION and p == Frag.PKG_ID and v == 1

        return True


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    LoRaWANTest.stdargs(p)
    args = p.parse_args()

    colorama_init()

    if args.virtual_time:
        asyncio.set_event_loop(VirtualTimeLoop()) # type: ignore

    sim = Simulation(args.hexfiles, args.debug, args.traffic)
    test = FuotaTest(sim, args)

    if not asyncio.get_event_loop().run_until_complete(test.run()):
        sys.exit(1)
