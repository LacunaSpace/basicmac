#!/usr/bin/env python3

# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import random
import time
import struct
import sys

# create unique file identifiers (UFIDs)

class UFID:
    def __init__(self, q:int, i:int) -> None:
        self.q = q
        self.i = i

    def version(self) -> int:
        return self.q & 0xf

    def __str__(self) -> str:
        return '%016x-%08x' % (self.q, self.i)

    def to_bytes(self) -> bytes:
        return struct.pack('<QI', self.q, self.i)

    @staticmethod
    def from_bytes(ufid:bytes) -> 'UFID':
        q, i = struct.unpack('<QI', ufid)
        return UFID(q, i)

    @staticmethod
    def generate() -> 'UFID':
        # Version 0:
        # - Q: time (ms, 44b), random_1 (16b), version (4b)
        # - I: random_2 (32b)

        t = int(time.time() * 1000)
        r1 = random.getrandbits(16)
        v = 0
        r2 = random.getrandbits(32)

        q = (t << 20) | (r1 << 4) | v
        i = r2

        return UFID(q, i)


if __name__ == '__main__':
    for name in sys.argv[1:]:
        u = UFID.generate()
        print('// %s\nstatic const uint8_t UFID_%s[12] = { %s };' % (u, name,
            ', '.join([('0x%02x' % x) for x in u.to_bytes()])))
