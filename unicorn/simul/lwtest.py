#!/usr/bin/env python3

# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Awaitable, Dict, Iterable, List, MutableMapping, Optional, Set, Tuple
from typing import cast

import argparse
import asyncio
import itertools as it
import os
import struct
import sys
import time
import traceback

from devsimul import LoraMsg, Simulation
from devtest import DeviceTest, LoRaWANTest
from binascii import crc32
from vtimeloop import VirtualTimeLoop

import loracrypto as lc
import loradefs as ld
import loramsg as lm
import loraopts as lo
import rtlib as rt

class LWTest(LoRaWANTest):
    def __init__(self, args:argparse.Namespace) -> None:
        super().__init__(args)

    async def tst_uplink(self, **kwargs:Any) -> lm.Msg:
        return await self.lw_uplink(port=224, **kwargs)

    def get_echo(self, m:lm.Msg, orig:Optional[bytes]=None) -> bytes:
        payload = m['FRMPayload']
        self.assert_equals(payload[0], 0x04)
        echo = payload[1:]
        if orig is not None:
            expect = bytes((x + 1) & 0xff for x in orig)
            assert expect == echo, 'original: %r, received,: %r, expected: %r' % (
                    orig, echo, expect)
        return echo

    def get_dnctr(self, m:lm.Msg, expect:Optional[int]=None,
            explain:Optional[str]=None) -> int:
        payload = m['FRMPayload']
        try:
            (dnctr,) = cast(Tuple[int], struct.unpack('>H', payload))
        except Exception as e:
            raise ValueError(f'invalid payload: {payload.hex()}') from e
        if expect is not None:
            assert dnctr == expect, 'received: %d, expected: %d%s' % (
                    dnctr, expect, '' if explain is None else (' (%s)' % explain))
        return dnctr

    def req_test(self, m:lm.Msg, **kwargs:Any) -> None:
        self.lw_dnlink(m, port=224, payload=b'\1\1\1\1', **kwargs)

    def req_mode(self, m:lm.Msg, mode_conf:bool=False, **kwargs:Any) -> None:
        self.lw_dnlink(m, port=224, payload=b'\x02' if mode_conf else b'\x03', **kwargs)

    def req_rejoin(self, m:lm.Msg, **kwargs:Any) -> None:
        self.lw_dnlink(m, port=224, payload=b'\x06', **kwargs)

    def req_echo(self, m:lm.Msg, echo:bytes, **kwargs:Any) -> None:
        self.lw_dnlink(m, port=224, payload=b'\x04' + echo, **kwargs)

    async def req_check_echo(self, m:LoraMsg, echo:bytes, **kwargs:Any) -> None:
        self.req_echo(m, echo, **kwargs)
        m = await self.tst_uplink()
        self.get_echo(m, orig=echo)
        return m

    async def ul_stats(self, m:lm.Msg, count:int,
            fstats:Optional[Dict[int,int]]=None, pstats:Optional[List[int]]=None) -> lm.Msg:
        for _ in range(count):
            if fstats is not None:
                f = m['upmsg'].freq
                fstats[f] = fstats.get(f, 0) + 1
            if pstats is not None:
                rssi = m['upmsg'].rssi
                pstats[0] += rssi
                pstats[1] += 1
            m = await self.tst_uplink()
        return m

    async def check_freqs(self, m:lm.Msg, freqs:Set[int], count:Optional[int]=None) -> lm.Msg:
        if count is None:
            count = 8 * len(freqs)
        fstats = dict()
        m = await self.ul_stats(m, count, fstats=fstats)
        self.assert_equals(fstats.keys(), freqs)
        return m

    # join network (with kwargs), start test mode, return first test upmsg
    async def start_testmode(self, explain:Optional[str]=None, **kwargs:any) -> LoraMsg:
        await self.lw_join(**kwargs)

        m = await self.lw_uplink()
        self.req_test(m)

        m = await self.tst_uplink(explain=explain)
        self.get_dnctr(m, expect=0)

        return m

    # check a NewChannelAns
    def check_ncr_o(self, o:lo.Opt, ChnlAck:Optional[int]=1, DRAck:Optional[int]=1,
            explain:Optional[str]=None):
        self.assert_type(o, lo.NewChannelAns, explain)
        if ChnlAck is not None:
            self.assert_equals(o.ChnlAck.value, ChnlAck, explain)
        if DRAck is not None:
            self.assert_equals(o.DRAck.value, DRAck, explain)

    # check a LinkADRAns
    def check_laa_o(self, o:lo.Opt, ChAck:Optional[int]=1, DRAck:Optional[int]=1,
            TXPowAck:Optional[int]=1, explain:Optional[str]=None) -> None:
        self.assert_equals(type(o), lo.LinkADRAns, explain)
        if ChAck is not None:
            self.assert_equals(o.ChAck.value, ChAck,
                    self.explain_more('ChAck', explain=explain))
        if DRAck is not None:
            self.assert_equals(o.DRAck.value, DRAck,
                    self.explain_more('DRAck', explain=explain))
        if TXPowAck is not None:
            self.assert_equals(o.TXPowAck.value, TXPowAck,
                    self.explain_more('TXPowAck', explain=explain))

    # 2.1 Device activation
    @DeviceTest.test()
    async def enter_testmode(self) -> bool:
        await self.start_testmode()

        return True

    # 2.2 Test application functionality
    @DeviceTest.test()
    async def app_functionality(self) -> bool:
        m = await self.start_testmode()
        dc = self.get_dnctr(m)
        echo = b'\x04\x01'
        self.req_echo(m, echo)

        m = await self.tst_uplink()
        self.get_echo(m, orig=echo)

        m = await self.tst_uplink()
        self.get_dnctr(m, expect=dc+1)
        self.req_mode(m, mode_conf=False)

        m = await self.tst_uplink()
        self.get_dnctr(m, expect=dc+2)

        return True

    # 2.3 Over The Air activation
    @DeviceTest.test()
    async def otaa(self) -> bool:
        await self.start_testmode()

        # create new region with additional channel (for test part 3)
        extra_ch = [ ld.ChDef(freq=867850000, minDR=0, maxDR=5) ]
        reg = ld.Region_EU868()
        reg.upchannels += extra_ch

        joinopts = [
                { 'dlset': lm.DLSettings.pack(rx1droff=2, rx2dr=3, optneg=False) },
                { 'rxdly': 2 },
                { 'cflist': reg.get_cflist() },
                { 'rx2': True } ]

        for jo in joinopts:
            self.req_rejoin(await self.tst_uplink())

            await self.start_testmode(**jo, explain='%r' % jo)

            # test rx1 and rx2
            for rx2 in [ False, True ]:
                m = await self.tst_uplink()
                dc = self.get_dnctr(m)
                self.req_mode(m, mode_conf=False, rx2=rx2)

                m = await self.tst_uplink()
                self.get_dnctr(m, expect=dc+1, explain=f'{jo}/{"rx2" if rx2 else "rx1"}')

            # test used frequencies
            expectedchans = self.region.upchannels.copy()
            if 'cflist' in jo:
                expectedchans += extra_ch
            usedfreqs = await self.freq_count(16 * len(expectedchans))
            DeviceTest.assert_equals(
                    set(usedfreqs.keys()), set(ch[0] for ch in expectedchans),
                    explain=f'{jo}')

        return True

    # 2.4 Packet error rate RX2 default DR
    @DeviceTest.test()
    async def rx2_per(self) -> bool:
        m = await self.start_testmode()
        dc0 = self.get_dnctr(m)

        # Note: this test does not make a lot of sense in the simulation...
        ct = 60
        for i in range(ct):
            self.req_mode(m, mode_conf=False, rx2=True)
            m = await self.tst_uplink()
        per = (ct - (self.get_dnctr(m) - dc0)) / ct
        print(f'Packet error rate: {(per/100):.1f} %')
        assert per < .05

        return True

    # 2.5 Cryptography
    @DeviceTest.test()
    async def crypto(self) -> bool:
        m = await self.start_testmode()

        # a. AES Encryption
        for i in range(1,19):
            m = await self.req_check_echo(m, bytes(range(1, i + 1)))

        # b. MIC
        m = await self.tst_uplink()
        dc = self.get_dnctr(m)
        for i in range(3):
            self.req_echo(m, b'\1\2\3', invalidmic=True)
            m = await self.tst_uplink()
            self.get_dnctr(m, expect=dc)

        return True

    # 2.6 Downlink window timing
    @DeviceTest.test()
    async def dl_timing(self) -> bool:
        m = await self.start_testmode()

        for rx2, toff in it.product([False, True], [20e-6, -20e-6]):
            m = await self.req_check_echo(m, b'\1\2\3', rx2=rx2, toff=toff)

        return True

    # 2.7 Frame sequence number
    @DeviceTest.test()
    async def frame_seqno(self) -> bool:
        m = await self.start_testmode()

        # c. [sic] Uplink sequence number
        for i in range(10):
            up0 = m['FCnt']
            m = await self.tst_uplink()
            self.assert_equals(m['FCnt'], up0+1)

        # d. [sic] Downlink sequence number
        dc = self.get_dnctr(m)
        for i in range(10):
            self.req_mode(m, mode_conf=False)
            m = await self.tst_uplink()
            dc = self.get_dnctr(m, expect=dc+1)
        for adj in [-3, -4, -2]:
            self.req_mode(m, mode_conf=False, fcntdn_adj=adj)
            m = await self.tst_uplink()
            self.get_dnctr(m, expect=dc)

        return True

    # 2.8 DevStatusReq MAC command
    @DeviceTest.test()
    async def dev_status_req(self) -> bool:
        m = await self.start_testmode()

        self.lw_dnlink(m, port=0, payload=lo.pack_opts([lo.DevStatusReq()]))
        m = await self.lw_uplink()

        opts = self.get_opts(m)
        assert len(opts) == 1
        assert type(opts[0]) is lo.DevStatusAns
        print(opts[0])

        return True

    # 2.9 MAC commands
    @DeviceTest.test()
    async def mcmd_invalid(self) -> bool:
        m = await self.start_testmode()
        dc = self.get_dnctr(m)

        for _ in range(2):
            cmd = lo.pack_opts([lo.DevStatusReq()])
            self.lw_dnlink(m, port=0, payload=cmd, fopts=cmd)
            m = await self.lw_uplink()

            opts = self.get_opts(m)
            assert len(opts) == 0 and m['FPort'] != 0

        m = await self.lw_uplink()
        self.get_dnctr(m, expect=dc)

        return True

    # 2.10 NewChannelReq MAC command
    @DeviceTest.test()
    async def new_channel_req(self) -> bool:
        # helper function
        async def ncr_add(m:lm.Msg, chans:List[Tuple[int,int]]) -> lm.Msg:
            opts = [lo.NewChannelReq(Chnl=ch, Freq=f//100, MinDR=0, MaxDR=5)
                    for ch, f in chans]
            self.lw_dnlink(m, port=0, payload=lo.pack_opts(opts))

            m = await self.lw_uplink()
            opts = self.get_opts(m)
            self.assert_equals(len(opts), len(chans))
            for i,o in enumerate(opts):
                if chans[i][0] < len(self.region.upchannels):
                    self.check_ncr_o(o, ChnlAck=0, DRAck=None)
                else:
                    self.check_ncr_o(o)

            return await self.check_freqs(m, frozenset(it.chain(
                (ch[1] for ch in chans if ch[1]),
                (ch.freq for ch in self.region.upchannels))))

        m = await self.start_testmode()

        # e. [sic] Read-only default channels
        m = await ncr_add(m, list(zip(range(0,3), [0,0,0])))

        # f. [sic] Addition and removal of multiple channels
        m = await ncr_add(m, list(zip(range(3,6), [867100000, 867300000, 867500000])))
        m = await ncr_add(m, list(zip(range(3,6), [0, 0, 0])))

        # g. [sic] Addition of a single channel
        m = await ncr_add(m, [(3, 868850000)])

        # h. [sic] Removal of a single channel
        m = await ncr_add(m, [(3, 0)])

        return True

    # 2.11 DlChannelReq MAC command
    @DeviceTest.test()
    async def dl_channel_req(self) -> bool:
        m = await self.start_testmode()

        for f in [ 868500000, self.region.upchannels[1].freq, 0 ]:
            # modify channel 1 RX1 frequency
            self.lw_dnlink(m, port=0,
                    payload=lo.pack_opts([lo.DlChannelReq(Chnl=1, Freq=f//100)]))

            # wait until message is received on channel 1 AND
            # simultaneously ensure that the DlChannelAns is being
            # repeated while no DL is received
            for i in range(20):
                m = await self.lw_uplink()
                opts = self.get_opts(m)
                self.assert_equals(len(opts), 1, explain=f'i={i}, f={f}')
                o = opts[0]
                assert type(o) is lo.DlChannelAns
                self.assert_equals(o.ChnlAck.value, 1)
                self.assert_equals(o.FreqAck.value, 1 if f else 0)
                if i and self.getupch(m) == 1:
                    break
            else:
                assert False, 'no message received on modified channel'

            # save current DL counter and send DL on modified channel freq
            dc = self.get_dnctr(m)
            self.req_mode(m, mode_conf=False, freq=f or None)

            # check uplink -- DlChannelAns must be cleared
            # *and* DL counter incremented
            m = await self.tst_uplink()
            opts = self.get_opts(m)
            self.assert_equals(len(opts), 0)
            self.get_dnctr(m, expect=dc+1)

            # make sure we get a message on an unmodified channel, otherwise
            # next command won't be received..
            while self.getupch(m) == 1:
                m = await self.tst_uplink()

        # Note: the following part of the test expands upon what's required...
        for ch, f in [(1, 333333333), (3, 868500000)]:
            # attempt to modify channel
            self.lw_dnlink(m, port=0,
                    payload=lo.pack_opts([lo.DlChannelReq(Chnl=ch, Freq=f//100)]))

            # check that the command is rejected for the correct reason, and
            # simultaneously ensure that the DlChannelAns is being repeated
            # while no DL is received
            for i in range(16):
                m = await self.lw_uplink()
                opts = self.get_opts(m)
                self.assert_equals(len(opts), 1)
                o = opts[0]
                assert type(o) is lo.DlChannelAns
                if ch < len(self.region.upchannels):
                    self.assert_equals(o.ChnlAck.value, 1)
                    self.assert_equals(o.FreqAck.value, 0)
                else:
                    self.assert_equals(o.ChnlAck.value, 0)
                    self.assert_equals(o.FreqAck.value, 1)
                if i and self.getupch(m) == 1:
                    break
            else:
                assert False, 'no message received on channel 1'

            # save current DL counter and send DL
            dc = self.get_dnctr(m)
            self.req_mode(m, mode_conf=False)

            # check uplink -- DlChannelAns must be cleared
            # *and* DL counter incremented
            m = await self.tst_uplink()
            opts = self.get_opts(m)
            self.assert_equals(len(opts), 0)
            self.get_dnctr(m, expect=dc+1)

            # make sure invalid channel didn't get enabled somehow
            m = await self.check_freqs(m, frozenset(ch.freq for ch in self.region.upchannels))

        return True

    # 2.12 Confirmed packets
    @DeviceTest.test()
    async def conf_packets(self) -> bool:
        m = await self.start_testmode()

        assert not self.isconfirmed(m)
        dc = self.get_dnctr(m)
        self.req_mode(m, mode_conf=True)

        # a. Uplink confirmed packets
        m = await self.tst_uplink()
        assert self.isconfirmed(m)
        dc = self.get_dnctr(m, expect=dc+1)

        self.lw_dnlink(m, fctrl=lm.FCtrl.ACK) # empty downlink as ACK

        m = await self.tst_uplink()
        assert self.isconfirmed(m)
        dc = self.get_dnctr(m, expect=dc+1)

        # b. Uplink retransmission
        m2 = await self.tst_uplink()
        self.assert_equals(m2['upmsg'].pdu, m['upmsg'].pdu)
        m = m2

        self.lw_dnlink(m, fctrl=lm.FCtrl.ACK) # empty downlink as ACK

        m = await self.tst_uplink()
        assert self.isconfirmed(m)
        dc = self.get_dnctr(m, expect=dc+1)

        # switch back to unconfirmed
        self.req_mode(m, mode_conf=False)
        m = await self.tst_uplink()
        assert not self.isconfirmed(m)
        dc = self.get_dnctr(m, expect=dc+1)

        # c. Downlink confirmed packet
        # d. Downlink retransmission
        for fcntdn_adj in [0, 0, -1]:
            self.req_mode(m, mode_conf=False,
                    confirmed=True, fcntdn_adj=fcntdn_adj)

            m = await self.tst_uplink()
            assert self.isack(m)

            # counter should also be increased (unless it was a repeat)
            dc = self.get_dnctr(m, expect=(dc if fcntdn_adj else dc+1), explain=f'fcntdn_adj={fcntdn_adj}')

            if fcntdn_adj:
                return True

        # unreachable
        return False

    # 2.13 RXParamSetupReq MAC command
    @DeviceTest.test()
    async def rx_param_setup_req(self) -> bool:
        def check_rpsa(m:lm.Msg, explain:Optional[str]=None) -> None:
            opts = self.get_opts(m)
            self.assert_equals(len(opts), 1, explain)
            self.assert_equals(type(opts[0]), lo.RXParamSetupAns, explain)
            self.assert_equals(opts[0].FreqAck.value, 1, explain)
            self.assert_equals(opts[0].RX2DRAck.value, 1, explain)
            self.assert_equals(opts[0].RX1DRoffAck.value, 1, explain)

        m = await self.start_testmode()

        # Make sure we are DR5 so we can see the rx1droff effect
        self.assert_equals(self.rps2dr(m['upmsg'].rps), 5)

        # -- Modify RX1 and RX2 downlink parameters
        opt = lo.RXParamSetupReq(RX2DR=2, RX1DRoff=2, Freq=868525000//100)
        self.lw_dnlink(m, port=0, payload=lo.pack_opts([opt]))

        m = await self.lw_uplink()
        check_rpsa(m)

        m = await self.req_check_echo(m, b'\1\2\3', rx1droffset=opt.RX1DRoff.value)
        m = await self.req_check_echo(m, b'\4\5\6', rx2=True,
                rps=self.dndr2rps(opt.RX2DR.value), freq=opt.Freq.value * 100)

        # -- Restore default downlink parameters
        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts(
                    [lo.RXParamSetupReq(RX2DR=self.region.RX2DR, RX1DRoff=0, Freq=self.region.RX2Freq//100)]),
                rx1droffset=opt.RX1DRoff.value)

        # -- Test reply transmission
        for i in range(2):
            m = await self.lw_uplink()
            check_rpsa(m, explain=f'iteration {i+1}')

        m = await self.req_check_echo(m, b'\1\2\3')
        self.assert_equals(len(self.get_opts(m)), 0)
        m = await self.req_check_echo(m, b'\4\5\6', rx2=True)

        return True

    # 2.14 RXTimingSetupReq command
    @DeviceTest.test()
    async def rx_timing_setup_req(self) -> bool:
        def check_rtsa(m:lm.Msg, explain:Optional[str]=None) -> None:
            opts = self.get_opts(m)
            self.assert_equals(len(opts), 1, explain)
            self.assert_equals(type(opts[0]), lo.RXTimingSetupAns, explain)

        m = await self.start_testmode()

        # -- Modify RX1 and RX2 timing to X second delay
        for delay in range(1, 16):
            self.lw_dnlink(m, port=0,
                    payload=lo.pack_opts([lo.RXTimingSetupReq(Delay=delay)]))
            self.session['rx1delay'] = delay
            m = await self.lw_uplink()
            check_rtsa(m)
            m = await self.req_check_echo(m, b'\1\2\3')
            self.assert_equals(len(self.get_opts(m)), 0)
            m = await self.req_check_echo(m, b'\4\5\6', rx2=True)

        # -- Restore default timing
        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.RXTimingSetupReq(Delay=0)]))
        self.session['rx1delay'] = 1

        # -- Test reply transmission
        for i in range(2):
            m = await self.lw_uplink()
            check_rtsa(m, explain=f'iteration {i+1}')

        m = await self.req_check_echo(m, b'\1\2\3')
        self.assert_equals(len(self.get_opts(m)), 0)
        m = await self.req_check_echo(m, b'\4\5\6', rx2=True)

        return True

    # 2.15 LinkADRReq MAC command
    @DeviceTest.test()
    async def link_adr_req(self) -> bool:
        def check_laa(m:lm.Msg, explain:Optional[str]=None) -> None:
            opts = self.get_opts(m)
            self.assert_equals(len(opts), 1, explain)
            self.check_laa_o(opts[0], explain=explain)

        def check_laa_block(m:lm.Msg, n:int, ChAck:Optional[int]=1, DRAck:Optional[int]=1,
                TXPowAck:Optional[int]=1, explain:Optional[str]=None) -> None:
            opts = self.get_opts(m)
            self.assert_equals(len(opts), n, explain)
            # check that all are of the same type
            self.assert_equals(len(set(type(o) for o in opts)), 1, explain)
            # check that all have the same value
            self.assert_equals(len(set(a.value for o in opts for a in o.args)), 1, explain)
            # verify last one (others are identical)
            self.check_laa_o(opts[-1], ChAck, DRAck, TXPowAck, explain)

        async def ncr_optdr(m:lm.Msg, freq:int) -> lm.Msg:
            self.lw_dnlink(m, port=0, payload=lo.pack_opts(
                [lo.NewChannelReq(Chnl=3, Freq=freq//100, MinDR=0, MaxDR=7)]))
            m = await self.lw_uplink()
            opts = self.get_opts(m)
            self.assert_equals(len(opts), 1)
            self.check_ncr_o(opts[0])
            return m

        m = await self.start_testmode()

        # a. ADR bit
        assert self.isadren(m)

        # b. TXPower
        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.LinkADRReq(TXPow=7, DR=5, ChMaskCntl=6)]))
        m = await self.lw_uplink()
        check_laa(m)

        pstats = [0, 0]
        m = await self.ul_stats(m, 3, pstats=pstats)
        rssi0 = pstats[0] / pstats[1]

        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.LinkADRReq(TXPow=0, DR=5, ChMaskCntl=6)]))
        m = await self.lw_uplink()
        check_laa(m)

        pstats = [0, 0]
        m = await self.ul_stats(m, 3, pstats=pstats)
        rssi1 = pstats[0] / pstats[1]

        print(f'RSSI @  2dBm: {rssi0:6.1f} dBm')
        print(f'RSSI @ 16dBm: {rssi1:6.1f} dBm')
        print(f'Difference:   {rssi1-rssi0:6.1f} dBm')
        self.assert_range(rssi0, -80, -10)
        self.assert_range(rssi1, -80, -10)
        self.assert_ge(rssi1 - rssi0, 6)

        # c. Required DataRates
        for dr in range(6):
            self.lw_dnlink(m, port=0,
                    payload=lo.pack_opts([lo.LinkADRReq(TXPow=0, DR=dr, ChMaskCntl=6)]))
            m = await self.lw_uplink()
            check_laa(m)
            self.assert_equals(self.rps2dr(m['upmsg'].rps), dr)

        # d. Optional DataRates
        nchfreq = 869100000
        m = await ncr_optdr(m, nchfreq)
        for dr in range(6, 8):
            self.lw_dnlink(m, port=0,
                    payload=lo.pack_opts([lo.LinkADRReq(TXPow=0, DR=dr, ChMaskCntl=6)]))
            m = await self.lw_uplink()
            check_laa(m)
            self.assert_equals(m['upmsg'].freq, nchfreq)
            self.assert_equals(self.rps2dr(m['upmsg'].rps), dr)
        m = await ncr_optdr(m, 0)
        self.assert_in(m['upmsg'].freq, list(ch.freq for ch in self.region.upchannels))

        # e. ChannelMask
        self.lw_dnlink(m, port=0, payload=lo.pack_opts(
            [lo.NewChannelReq(Chnl=3, Freq=nchfreq//100, MinDR=0, MaxDR=5),
                lo.LinkADRReq(TXPow=5, DR=5, ChMaskCntl=0, ChMask=7)]))

        m = await self.lw_uplink()
        opts = self.get_opts(m)
        self.assert_equals(len(opts), 2)
        self.check_ncr_o(opts[0])
        self.check_laa_o(opts[1])

        m = await self.check_freqs(m, frozenset(ch.freq for ch in self.region.upchannels))

        self.lw_dnlink(m, port=0, payload=lo.pack_opts(
            [lo.LinkADRReq(TXPow=5, DR=5, ChMaskCntl=0, ChMask=0xf)]))

        m = await self.lw_uplink()
        opts = self.get_opts(m)
        self.assert_equals(len(opts), 1)
        self.check_laa_o(opts[0])

        m = await self.check_freqs(m, frozenset(it.chain([nchfreq],
            (ch.freq for ch in self.region.upchannels))))

        self.lw_dnlink(m, port=0, payload=lo.pack_opts(
            [lo.LinkADRReq(TXPow=5, DR=5, ChMaskCntl=0, ChMask=0)]))

        m = await self.lw_uplink()
        opts = self.get_opts(m)
        self.assert_equals(len(opts), 1)
        self.check_laa_o(opts[0], ChAck=0, DRAck=None, TXPowAck=None)

        self.lw_dnlink(m, port=0, payload=lo.pack_opts(
            [lo.NewChannelReq(Chnl=3, Freq=0)]))

        m = await self.lw_uplink()
        opts = self.get_opts(m)
        self.assert_equals(len(opts), 1)
        self.check_ncr_o(opts[0])

        # f. Redundancy
        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6, NbTrans=2)]))
        m = await self.lw_uplink()
        check_laa(m)

        l = [await self.lw_uplink() for _ in range(3)]

        self.assert_equals(l[0]['MIC'], m['MIC'])
        self.assert_equals(l[2]['MIC'], l[1]['MIC'])

        m = l[-1]
        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6, NbTrans=1)]))
        m = await self.lw_uplink()
        check_laa(m)

        # g. ADRACKReq bit
        self.lw_dnlink(m)

        for i in range(64):
            m = await self.lw_uplink()
            self.assert_equals(self.isadrarq(m), False, explain=f'iter={i}')
            self.assert_equals(self.rps2dr(m['upmsg'].rps), 5)
        for dr in [5, 4, 3]:
            for i in range(32):
                m = await self.lw_uplink()
                self.assert_equals(self.isadrarq(m), True, explain=f'dr={dr}, iter={i}')
                self.assert_equals(self.rps2dr(m['upmsg'].rps), dr)

        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6)]))
        m = await self.lw_uplink()
        check_laa(m)
        self.assert_equals(self.rps2dr(m['upmsg'].rps), 5)

        # h. a.. Successful LinkADRReq commands block
        self.lw_dnlink(m, port=0, payload=lo.pack_opts([
            lo.LinkADRReq(ChMaskCntl=0, ChMask=0),
            lo.LinkADRReq(TXPow=4, DR=4, ChMaskCntl=0, ChMask=3, NbTrans=1),
            lo.LinkADRReq(TXPow=0, DR=3, ChMaskCntl=6, ChMask=0, NbTrans=1)]))

        m = await self.lw_uplink()
        check_laa_block(m, 3)

        m = await self.check_freqs(m, frozenset(ch.freq for ch in self.region.upchannels))

        self.lw_dnlink(m, port=0,
                payload=lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6)]))
        m = await self.lw_uplink()
        check_laa(m)
        self.assert_equals(self.rps2dr(m['upmsg'].rps), 5)

        # h. b.. Unsuccessful LinkADRReq commands block
        self.lw_dnlink(m, port=0, payload=lo.pack_opts([
            lo.LinkADRReq(ChMask=0x07, DR=4, TXPow=4),
            lo.LinkADRReq(ChMaskCntl=0, ChMask=0)]))

        m = await self.lw_uplink()
        check_laa_block(m, 2, ChAck=0, DRAck=None, TXPowAck=None)

        self.assert_equals(self.rps2dr(m['upmsg'].rps), 5)

        self.lw_dnlink(m) # empty downlink to avoid timeout (?)
        m = await self.check_freqs(m, frozenset(ch.freq for ch in self.region.upchannels))

        return True


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    LoRaWANTest.stdargs(p)
    args = p.parse_args()

    if args.virtual_time:
        asyncio.set_event_loop(VirtualTimeLoop()) # type:ignore

    lwt = LWTest(args)

    if not asyncio.get_event_loop().run_until_complete(lwt.run()):
        sys.exit(1)
