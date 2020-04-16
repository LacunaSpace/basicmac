#!/usr/bin/env python3

# Copyright (C) 2016-2020 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import BinaryIO,Callable,List,Optional,Union

import random
import struct
from bitarray import bitarray

def bitarrayfrombytes(buf:bytes) -> bitarray:
    b = bitarray(endian='little')
    b.frombytes(buf)
    return b

class CBGenerator:
    def generate(self, cct:int, cid:int) -> bitarray:
        raise NotImplementedError()

class TrackNetGenerator(CBGenerator):
    @staticmethod
    def tn_avalanche(x:int) -> int:
        x = x & 0xffffffff
        x = (((x >> 16) ^ x) * 0x45d9f3b) & 0xffffffff
        x = (((x >> 16) ^ x) * 0x45d9f3b) & 0xffffffff
        x = (x >> 16) ^ x
        return x

    def generate(self, cct:int, cid:int) -> bitarray:
        nw = (cct + 31) // 32
        b = bitarrayfrombytes(struct.pack('<%dI' % nw, *(self.tn_avalanche((cid * nw) + i) for i in range(nw))))
        return b[:cct]

class FragCarousel:
    def __init__(self, data:bytes, csz:int, cbg:CBGenerator, pad:int=0) -> None:
        cct,rem = divmod(len(data), csz)
        if rem:
            raise ValueError('data length must be a multiple of chunk size')
        self.data = data
        self.csz = csz
        self.cct = cct
        self.cbg = cbg
        self.pad = pad
        self.blocks = [bitarrayfrombytes(data[b*csz:(b+1)*csz]) for b in range(cct)]

    def chunk(self, cid:int) -> bytes:
        cb = self.cbg.generate(self.cct, cid)
        chunk = bitarray(self.csz * 8)
        chunk.setall(0)
        for c in range(self.cct):
            if cb[c]:
                chunk ^= self.blocks[c]
        return chunk.tobytes()

    @staticmethod
    def fromfile(upf:Union[bytes,str,BinaryIO], csz:int=200,
            cbg:CBGenerator=TrackNetGenerator(), pad:bytes=b'*') -> 'FragCarousel':
        if isinstance(upf, str):
            with open(upf, 'rb') as f:
                upd = bytes(f.read())
        elif isinstance(upf, bytes):
            upd = upf
        else:
            upd = upf.read()

        _,rem = divmod(len(upd), csz)
        padlen = (csz - rem) if rem else 0
        upd += padlen * pad
        return FragCarousel(upd, csz, cbg, padlen)

class DefragSession:
    def __init__(self, cct:int, csz:int, cbg:CBGenerator) -> None:
        self.cct = cct
        self.csz = csz
        self.cbg = cbg
        self.rows:List[Optional[bitarray]] = [None for _ in range(cct)]
        self.dct = 0
        self.unp:Optional[bytes] = None

    def process(self, cid:int, data:bytes) -> bool:
        if self.csz != len(data):
            raise ValueError()
        bits = self.cbg.generate(self.cct, cid) + bitarrayfrombytes(data)
        for c in reversed(range(self.cct)):
            if bits[c]:
                if self.rows[c]:
                    bits ^= self.rows[c]
                else:
                    self.rows[c] = bits
                    self.dct += 1
                    return True
        return False

    def complete(self) -> bool:
        return self.dct == self.cct

    def unpack(self) -> bytes:
        if not self.complete():
            raise RuntimeError()
        if not self.unp:
            data = b''
            for c in range(self.cct):
                for i in range(c):
                    if self.rows[c][i]:
                        self.rows[c] ^= self.rows[i]
                data += self.rows[c][self.cct:].tobytes()
            self.unp = data
        return self.unp

if __name__ == '__main__':
    csz = 8
    cct = 1024-8

    data = bytes(random.randrange(0,256) for _ in range(cct*csz))
    cbg = TrackNetGenerator()
    fc = FragCarousel(data, csz, cbg)
    ds = DefragSession(fc.cct, fc.csz, cbg)

    print('cct=%d' % fc.cct)

    cid = random.randint(0, 100000)
    while not ds.complete():
        s = ds.process(cid, fc.chunk(cid))
        print('.' if s else 'X', end='')
        cid += 1
    print()

    assert data == ds.unpack()
