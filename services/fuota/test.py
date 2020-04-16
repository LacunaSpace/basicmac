#!/usr/bin/env python3

# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import BinaryIO,Optional,Tuple,Union

import argparse
import asyncio
import hashlib
import numpy as np
import os
import struct
import sys

from Crypto.PublicKey import ECC

from devtest import DeviceTest, LoRaWANTest
from vtimeloop import VirtualTimeLoop
from zfwtool import Update, ZFWArchive

from frag import FragCarousel

import loramsg as lm

class FragPackage:
    PORT                = 201
    ID                  = 3

    PKG_VERSION_REQ     = 0x00
    PKG_VERSION_ANS     = 0x00
    FRAG_STATUS_REQ     = 0x01
    FRAG_STATUS_ANS     = 0x01
    FRAG_SESS_SETUP_REQ = 0x02
    FRAG_SESS_SETUP_ANS = 0x02
    FRAG_SESS_DEL_REQ   = 0x03
    FRAG_SESS_DEL_ANS   = 0x03
    DATA_FRAGMENT       = 0x08
    FRAG_HASH_REQ       = 0x80     # proprietary
    FRAG_HASH_ANS       = 0x80     # proprietary

    SSA_STAT_ENC        = (1 << 0) # encoding not supported
    SSA_STAT_MEM        = (1 << 1) # not enough memory
    SSA_STAT_IDX        = (1 << 2) # session index not supported
    SSA_STAT_DSC        = (1 << 3) # wrong descriptor

    SDA_STAT_IDX        = (1 << 2) # session index does not exist

class FwManPackage:
    PORT                = 203
    ID                  = 4

    PKG_VERSION_REQ     = 0x00
    PKG_VERSION_ANS     = 0x00
    DEV_VERSION_REQ     = 0x01
    DEV_VERSION_ANS     = 0x01
    DEV_REBOOT_TIME_REQ = 0x02
    DEV_REBOOT_TIME_ANS = 0x02
    DEV_REBOOT_CTDN_REQ = 0x03
    DEV_REBOOT_CTDN_ANS = 0x03
    DEV_UPGRADE_IMG_REQ = 0x04
    DEV_UPGRADE_IMG_ANS = 0x04
    DEV_DELETE_IMG_REQ  = 0x05
    DEV_DELETE_IMG_ANS  = 0x05

    DUI_STAT_NONE       = 0 # no image present
    DUI_STAT_INVALID    = 1 # image is corrupt
    DUI_STAT_MISMATCH   = 2 # image is not compatible
    DUI_STAT_VALID      = 3 # a-ok!

class FuotaTest(LoRaWANTest):
    async def pkg_uplink(self, port:int, explain:Optional[str]=None) -> lm.Msg:
        return await self.lw_uplink(
                filter=lambda m: m['FPort'] == port, limit=2,
                explain=explain)

    async def frag_uplink(self, explain:Optional[str]=None) -> lm.Msg:
        return await self.pkg_uplink(FragPackage.PORT, explain=explain)

    async def fwman_uplink(self, explain:Optional[str]=None) -> lm.Msg:
        return await self.pkg_uplink(FwManPackage.PORT, explain=explain)

    async def do_init(self):
        await self.lw_join()
        return await self.lw_uplink()

    @DeviceTest.test()
    async def frag_pkg_version_req(self) -> bool:
        m = await self.do_init()

        self.lw_dnlink(m,
                payload=struct.pack('B', FragPackage.PKG_VERSION_REQ),
                port=FragPackage.PORT)
        m = await self.frag_uplink()
        cmd,pkg,ver = struct.unpack('BBB', m['FRMPayload'])
        self.assert_eq(cmd, FragPackage.PKG_VERSION_ANS)
        self.assert_eq(pkg, FragPackage.ID)
        self.assert_eq(ver, 1)

        return True

    async def do_frag_sess_setup_req(self, m:lm.Msg, nbfrag:int, fragsz:int,
            fridx:int=0, mcmask:int=0xf, algo:int=1, delay:int=0,
            pad:int=0, desc:int=0, check_status:int=0, explain:Optional[str]=None) -> lm.Msg:
        assert 0 <= fridx < 4
        assert 0 <= mcmask < 16
        assert 0 <= algo < 8
        assert 0 <= delay < 8
        self.lw_dnlink(m,
                payload=struct.pack('<BBHBBBI', FragPackage.FRAG_SESS_SETUP_REQ,
                    (fridx << 4) | mcmask, nbfrag, fragsz,
                    (algo << 3) | delay, pad, desc),
                port=FragPackage.PORT)
        m = await self.frag_uplink(explain=explain)
        cmd,sta = struct.unpack('BB', m['FRMPayload'])
        self.assert_eq(cmd, FragPackage.FRAG_SESS_SETUP_ANS, explain=explain)
        self.assert_eq(sta, check_status | (fridx << 6), explain=explain)
        return m

    async def do_frag_sess_del_req(self, m:lm.Msg, fridx:int=0,
            check_status:int=0, explain:Optional[str]=None) -> lm.Msg:
        assert 0 <= fridx < 4
        self.lw_dnlink(m,
                payload=struct.pack('BB', FragPackage.FRAG_SESS_DEL_REQ, fridx),
                port=FragPackage.PORT)
        m = await self.frag_uplink()
        cmd,sta = struct.unpack('BB', m['FRMPayload'])
        self.assert_eq(cmd, FragPackage.FRAG_SESS_DEL_ANS, explain=explain)
        self.assert_eq(sta, check_status | fridx, explain=explain)
        return m

    def check_sess_status_ans(self, m:lm.Msg, fridx:int=0,
            everybody:bool=False, check_status:int=0, check_total:int=0,
            check_complete:Union[int,Tuple[int,int]]=0,
            explain:Optional[str]=None) -> None:
        cmd,rcvidx,mfr,sta = struct.unpack('<BHBB', m['FRMPayload'])
        idx = rcvidx >> 14
        rcv = rcvidx & 0x3fff
        self.assert_eq(cmd, FragPackage.FRAG_STATUS_ANS, explain=explain)
        self.assert_eq(idx, fridx, explain=explain)
        self.assert_eq(sta, check_status, explain=explain)
        if isinstance(check_complete, int):
            self.assert_eq(rcv, check_complete, explain=explain)
        else:
            self.assert_range(rcv, *check_complete, explain=explain)
        self.assert_eq(mfr, min(255, check_total-rcv), explain=explain)
        print(f'session {fridx}: {rcv}/{check_total}')

    async def do_frag_get_sess_status(self, m:lm.Msg, fridx:int=0,
            everybody:bool=False, **kwargs) -> lm.Msg:
        assert 0 <= fridx < 4
        self.lw_dnlink(m,
                payload=struct.pack('BB', FragPackage.FRAG_STATUS_REQ,
                    (fridx << 1) | int(everybody)),
                port=FragPackage.PORT)
        m = await self.frag_uplink()
        self.check_sess_status_ans(m, fridx=fridx, **kwargs)
        return m

    async def do_frag_hash(self, m:lm.Msg, check_hash:bytes, fridx:int=0,
            explain:Optional[str]=None) -> lm.Msg:
        assert 0 <= fridx < 4
        self.lw_dnlink(m,
                payload=struct.pack('BB', FragPackage.FRAG_HASH_REQ, fridx),
                port=FragPackage.PORT)
        m = await self.frag_uplink()
        pl = m['FRMPayload']
        self.assert_eq(pl[0], FragPackage.FRAG_HASH_ANS, explain=explain)
        self.assert_eq(pl[1:], check_hash, explain=explain)
        return m

    async def do_frag_data_fragment(self, m:lm.Msg, cid:int, fragment:bytes,
            fridx:int=0, **kwargs) -> lm.Msg:
        assert 0 <= fridx < 4
        self.lw_dnlink(m,
                payload=struct.pack('<BH', FragPackage.DATA_FRAGMENT, (fridx << 14) | cid)
                + fragment, port=FragPackage.PORT)
        m = await self.frag_uplink()
        self.check_sess_status_ans(m, fridx=fridx, **kwargs)
        return m

    def progress(self, m:lm.Msg) -> int:
        cmd,rcvidx,mfr,sta = struct.unpack('<BHBB', m['FRMPayload'])
        return rcvidx & 0x3fff

    @DeviceTest.test()
    async def frag_sess_setup_req(self) -> bool:
        m = await self.do_init()

        # Session with unsupported encoding
        m = await self.do_frag_sess_setup_req(m, 20, 160,
                algo=7, check_status=FragPackage.SSA_STAT_ENC,
                explain='session with unsupported encoding')

        # Session with not enough memory
        m = await self.do_frag_sess_setup_req(m, 50000, 160,
                check_status=FragPackage.SSA_STAT_MEM,
                explain='session with not enough memory')

        # Session with invalid index
        m = await self.do_frag_sess_setup_req(m, 20, 160,
                fridx=2, check_status=FragPackage.SSA_STAT_IDX,
                explain='session with invalid index')

        # Good session
        m = await self.do_frag_sess_setup_req(m, 20, 160,
                explain='good session')

        # Session already exists (this answer is proprietary)
        m = await self.do_frag_sess_setup_req(m, 20, 160,
                check_status=FragPackage.SSA_STAT_IDX | FragPackage.SSA_STAT_MEM,
                explain='session already exists')

        # Clean up
        m = await self.do_frag_sess_del_req(m, explain='clean up')

        return True

    @DeviceTest.test()
    async def frag_get_sess_status(self) -> bool:
        m = await self.do_init()

        # create a new session
        cct = 20
        csz = 160
        m = await self.do_frag_sess_setup_req(m, cct, csz)

        # Get session status of new session
        m = await self.do_frag_get_sess_status(m, check_total=cct)

        # Get session status of non-existent session
        m = await self.do_frag_get_sess_status(m, fridx=1)

        # Clean up
        m = await self.do_frag_sess_del_req(m, explain='clean up')

        # Get session status of deleted session
        m = await self.do_frag_get_sess_status(m)

        return True

    @DeviceTest.test()
    async def frag_sess_del_req(self) -> bool:
        m = await self.do_init()

        # Create session
        cct = 20
        csz = 160
        m = await self.do_frag_sess_setup_req(m, cct, csz)

        # Delete non-existent session
        m = await self.do_frag_sess_del_req(m, fridx=1,
               check_status=FragPackage.SDA_STAT_IDX, explain='non-existent session')

        # Make sure session is still there
        m = await self.do_frag_get_sess_status(m, check_total=cct)

        # Delete session
        m = await self.do_frag_sess_del_req(m, explain='current session')

        # Make sure it's gone
        m = await self.do_frag_get_sess_status(m)

        # Delete session again
        m = await self.do_frag_sess_del_req(m,
               check_status=FragPackage.SDA_STAT_IDX, explain='already deleted session')

        return True


    async def do_frag_upload(self, m:lm.Msg, data:Union[bytes,str,BinaryIO],
            startcid:int=1, limit:int=10) -> lm.Msg:
        # Create fragmentation carousel
        fc = FragCarousel.fromfile(data)

        # Create session
        m = await self.do_frag_sess_setup_req(m, fc.cct, fc.csz, pad=fc.pad)

        # Send fragments
        progress = 0
        for i in range(fc.cct + limit):
            cid = startcid + i
            m = await self.do_frag_data_fragment(m, cid, fc.chunk(cid),
                    check_total=fc.cct, check_complete=(progress, progress+1))
            progress = self.progress(m)
            if progress == fc.cct:
                break
        else:
            assert False, 'Fragmentation session did not complete within limit'
        return m


    @DeviceTest.test()
    async def frag_data_fragment(self) -> bool:
        m = await self.do_init()
        data = np.random.bytes(12 * 1024)
        m = await self.do_frag_upload(m, data)
        m = await self.do_frag_hash(m, hashlib.sha256(data).digest())
        m = await self.do_frag_sess_del_req(m, explain='clean up')

        return True

    @DeviceTest.test()
    async def fwman_pkg_version_req(self) -> bool:
        m = await self.do_init()

        self.lw_dnlink(m,
                payload=struct.pack('B', FwManPackage.PKG_VERSION_REQ),
                port=FwManPackage.PORT)
        m = await self.fwman_uplink()
        cmd,pkg,ver = struct.unpack('BBB', m['FRMPayload'])
        self.assert_eq(cmd, FwManPackage.PKG_VERSION_ANS)
        self.assert_eq(pkg, FwManPackage.ID)
        self.assert_eq(ver, 1)

        return True

    async def do_fwman_dev_version_req(self, m:lm.Msg,
            check_crc:int, explain:Optional[str]=None) -> lm.Msg:
        self.lw_dnlink(m,
                payload=struct.pack('B', FwManPackage.DEV_VERSION_REQ),
                port=FwManPackage.PORT)
        m = await self.fwman_uplink(explain=explain)
        cmd,fw,hw = struct.unpack('<BII', m['FRMPayload'])
        self.assert_eq(cmd, FwManPackage.DEV_VERSION_ANS, explain=explain)
        self.assert_eq(fw, check_crc, fmt='#010x', explain=explain)
        self.assert_eq(hw, 0, explain=explain) # simulation hardware ID is always 0
        return m


    @DeviceTest.test()
    async def fwman_dev_version_req(self) -> bool:
        m = await self.do_init()

        zfw = ZFWArchive.fromfile(os.environ['ZFWFILE'])
        m = await self.do_fwman_dev_version_req(m, check_crc=zfw.fw.crc)

        return True

    async def do_fwman_upgrade_img_req(self, m:lm.Msg, check_status:int,
            check_crc:Optional[int]=None, explain:Optional[str]=None) -> bool:
        self.lw_dnlink(m,
                payload=struct.pack('B', FwManPackage.DEV_UPGRADE_IMG_REQ),
                port=FwManPackage.PORT)
        m = await self.fwman_uplink()
        cmd,stat = struct.unpack_from('<BB', m['FRMPayload'])
        if stat == FwManPackage.DUI_STAT_VALID:
            crc, = struct.unpack('<I', m['FRMPayload'][2:])
            if check_crc is not None:
                self.assert_eq(crc, check_crc, fmt='#010x', explain=explain)
        else:
            self.assert_eq(len(m['FRMPayload']), 2, explain=explain)
        self.assert_eq(cmd, FwManPackage.DEV_UPGRADE_IMG_ANS, explain=explain)
        self.assert_eq(stat, check_status, explain=explain)
        return m

    @DeviceTest.test()
    async def fwman_upgrade_img_req(self) -> bool:
        m = await self.do_init()

        # no session
        m = await self.do_fwman_upgrade_img_req(m, check_status=FwManPackage.DUI_STAT_NONE,
                explain='no session')

        # random session
        m = await self.do_frag_upload(m, np.random.bytes(2 * 1024))
        m = await self.do_fwman_upgrade_img_req(m, check_status=FwManPackage.DUI_STAT_INVALID,
                explain='random session')
        m = await self.do_frag_sess_del_req(m, explain='random session')

        upfile = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                'test.up')
        up = Update.fromfile(upfile)

        # update with no signature
        m = await self.do_frag_upload(m, up.tobytes())
        m = await self.do_fwman_upgrade_img_req(m, check_status=FwManPackage.DUI_STAT_INVALID,
                explain='update, no signature')
        m = await self.do_frag_sess_del_req(m, explain='update, no signature')

        # update with invalid signature
        up.sigblob = np.random.bytes(80)
        m = await self.do_frag_upload(m, up.tobytes())
        m = await self.do_fwman_upgrade_img_req(m, check_status=FwManPackage.DUI_STAT_INVALID,
                explain='update, invalid signature')
        m = await self.do_frag_sess_del_req(m, explain='update, invalid signature')

        # update signed with test key
        keyfile = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                'testkey.pem')
        with open(keyfile, 'rb') as f:
            signkey = ECC.import_key(f.read())
        up.sign(signkey)
        m = await self.do_frag_upload(m, up.tobytes())
        m = await self.do_fwman_upgrade_img_req(m, check_status=FwManPackage.DUI_STAT_VALID,
                check_crc=up.fwcrc,
                explain='update, test key signature')
        m = await self.do_frag_sess_del_req(m, explain='update, test key signature')

        return True

    @DeviceTest.test()
    async def fwman_reboot_time_req(self) -> bool:
        m = await self.do_init()

        # load and sign update
        upfile = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                'test.up')
        up = Update.fromfile(upfile)
        keyfile = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                'testkey.pem')

        zfw = ZFWArchive.fromfile(os.environ['ZFWFILE'])
        self.assert_ne(up.fwcrc, zfw.fw.crc,
                'Firmware update CRC matches current firmware CRC', fmt='#010x')

        with open(keyfile, 'rb') as f:
            signkey = ECC.import_key(f.read())
        up.sign(signkey)

        # upload update
        m = await self.do_frag_upload(m, up.tobytes())

        # reboot immediately and install firmware
        self.lw_dnlink(m,
                payload=struct.pack('<BI', FwManPackage.DEV_REBOOT_TIME_REQ, 0),
                port=FwManPackage.PORT)

        m = await self.do_init()
        m = await self.do_fwman_dev_version_req(m, check_crc=up.fwcrc)
        m = await self.do_frag_sess_del_req(m, explain='clean up 1')

        # create delta update back to original firmware
        up = Update.createDelta(zfw.fw, up.unpack(), 4096)
        up.sign(signkey)

        # upload update
        m = await self.do_frag_upload(m, up.tobytes())

        # reboot immediately and install firmware
        self.lw_dnlink(m,
                payload=struct.pack('<BI', FwManPackage.DEV_REBOOT_TIME_REQ, 0),
                port=FwManPackage.PORT)

        m = await self.do_init()
        m = await self.do_fwman_dev_version_req(m, check_crc=zfw.fw.crc)
        m = await self.do_frag_sess_del_req(m, explain='clean up 2')

        return True


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    LoRaWANTest.stdargs(p)
    args = p.parse_args()

    if args.virtual_time:
        asyncio.set_event_loop(VirtualTimeLoop()) # type: ignore

    test = FuotaTest(args)

    if not asyncio.get_event_loop().run_until_complete(test.run()):
        sys.exit(1)
