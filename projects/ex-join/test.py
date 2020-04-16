#!/usr/bin/env python3

# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import argparse
import asyncio
import sys

from devtest import DeviceTest, LoRaWANTest
from vtimeloop import VirtualTimeLoop

class ExJoinTest(LoRaWANTest):
    @DeviceTest.test()
    async def join(self) -> bool:
        await self.lw_join()
        await self.lw_uplink()
        return True

    @DeviceTest.test()
    async def uplink(self) -> bool:
        await self.lw_join()
        t1 = None
        for _ in range(5):
            m = await self.lw_uplink()
            self.assert_eq(m['FRMPayload'], b'hello')
            t0 = t1
            t1 = asyncio.get_event_loop().time()
            if t0 is not None:
                self.assert_range((t1 - t0), 5, 10)
        return True

    @DeviceTest.test()
    async def dnlink(self) -> bool:
        await self.lw_join()
        m = await self.lw_uplink()
        self.lw_dnlink(m, port=15, payload=b'hi there!')
        await asyncio.sleep(5)
        return True



if __name__ == '__main__':
    p = argparse.ArgumentParser()
    LoRaWANTest.stdargs(p)
    args = p.parse_args()

    if args.virtual_time:
        asyncio.set_event_loop(VirtualTimeLoop()) # type: ignore

    test = ExJoinTest(args)

    if not asyncio.get_event_loop().run_until_complete(test.run()):
        sys.exit(1)
