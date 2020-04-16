# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Awaitable, TextIO, Callable, Dict, Iterable, List, \
        MutableMapping, NamedTuple, Optional, Set, Tuple, Union
from typing import cast

import argparse
import asyncio
import io
import numpy
import os
import struct
import sys
import time
import traceback

from devsimul import LoraMsg, Medium, Rps, Simulation, TraceWriter, TrafficTrace
from binascii import crc32
from colorama import Fore, Style, init as colorama_init
from itertools import chain
from vtimeloop import VirtualTimeLoop

import loracrypto as lc
import loradefs as ld
import loramsg as lm
import loraopts as lo
import rtlib as rt

TestCase = Awaitable[bool]
Session = MutableMapping[str,Any]
DecoratedTestCase = Callable[[Any], TestCase]
TestCaseDecorator = Callable[[DecoratedTestCase], DecoratedTestCase]
CondFn = Callable[[lm.Msg],bool]

class Band(NamedTuple):
    name  : str
    lower : int
    upper : int
    dc    : float

class AirTimeStats:
    #BAND_ETSI_H1_6 = Band('h1.6', 869400000, 869650000, 0.1)
    #BAND_ETSI_H1_4 = Band('h1.4', 868000000, 868600000, 0.01)
    #BAND_ETSI_H1_7 = Band('h1.7', 869700000, 870000000, 0.01)
    #BAND_ETSI_H1_3 = Band('h1.3', 863000000, 869650000, 0.001)

    BAND_ETSI_H1_7 = Band('h1.7', 869400000, 869650000, 0.1)
    BAND_ETSI_H1_4 = Band('h1.4', 865000000, 868000000, 0.01)
    BAND_ETSI_H1_5 = Band('h1.5', 868000000, 868600000, 0.01)
    BAND_ETSI_H1_9 = Band('h1.9', 869700000, 870000000, 0.01)
    BAND_ETSI_H1_3 = Band('h1.3', 863000000, 865000000, 0.001)
    BAND_ETSI_H1_6 = Band('h1.6', 868700000, 869200000, 0.001)

    BANDS_ETSI = [ BAND_ETSI_H1_7, BAND_ETSI_H1_4, BAND_ETSI_H1_5, BAND_ETSI_H1_9, BAND_ETSI_H1_3, BAND_ETSI_H1_6 ]

    def __init__(self, t0:Optional[float]=None,
            bands:List[Band]=[]) -> None:
        self.accu  : float           = 0
        self.mcnt  : int             = 0
        self.baccu : Dict[str,float] = {}
        self.bcnt  : Dict[str,int]   = {}
        self.faccu : Dict[str,float] = {}
        self.fcnt  : Dict[str,int]   = {}

        self.t0    = t0
        self.t1    = t0
        self.bands = bands

    def add(self, msg:LoraMsg) -> None:
        if self.t0 is None:
            self.t0 = msg.xbeg
        self.t1 = msg.xend
        airtime = (msg.xend - msg.xbeg)
        b = self.band(msg.freq)
        if b:
            self.baccu[b.name] = self.baccu.get(b.name, 0) + airtime
            self.bcnt[b.name] = self.bcnt.get(b.name, 0) + 1
        self.accu += airtime
        self.mcnt += 1
        self.faccu[msg.freq] = self.faccu.get(msg.freq, 0) + airtime
        self.fcnt[msg.freq] = self.fcnt.get(msg.freq, 0) + 1

    def tt(self, t1:Optional[float]=None) -> float:
        if t1 is None:
            t1 = self.t1
        return t1 - self.t0

    def dc(self, t1:Optional[float]=None) -> float:
        tt = self.tt(t1)
        return (self.accu / tt) if tt else 0

    def dcs(self, t1:Optional[float]=None) -> Dict[Band,float]:
        tt = self.tt(t1)
        return { b: (self.baccu.get(b.name, 0) / tt) if tt else 0
                for b in self.bands }

    def band(self, freq:int) -> Band:
        for b in self.bands:
            if freq >= b.lower and freq <= b.upper:
                return b
        return None

class DeviceTest(Medium):
    TESTS:List[Callable[['DeviceTest'],TestCase]] = []
    def __init__(self, sim:Simulation) -> None:
        self.topts   : MutableMapping[TestCase,dict] = {}
        self.results : MutableMapping[TestCase,bool] = {}
        self.skip    : List[int] = []
        self.upmsgs  : asyncio.Queue[LoraMsg] = asyncio.Queue()

        self.sim = sim
        self.quit_on_fail = False

        self.collect_tests()

        colorama_init()


    def reset_medium (self) -> None:
        self.sim.medium.reset_medium()

    def get_dn(self, rxon:int, rxtout:int, freq:int, rps:int, nsym:int=4, peek=False) -> Optional[LoraMsg]:
        return self.sim.medium.get_dn(rxon, rxtout, freq, rps, nsym, peek)

    def prune(self, ticks:int) -> List[LoraMsg]:
        return self.sim.medium.prune(ticks)

    def add_dn(self, msg:LoraMsg) -> None:
        self.sim.medium.add_dn(msg)

    def collect_tests(self) -> None:
        self.tests   : List[TestCase] = []
        for t in DeviceTest.TESTS:
            tc = t(self)
            self.topts[tc] = t.options # type: ignore
            self.tests.append(tc)
        print('%d test cases collected' % len(self.tests))

    @staticmethod
    def fmt_timespan(t:float) -> str:
        ms = int(t * 1000)
        d, ms = divmod(ms, 86400000)
        h, ms = divmod(ms, 3600000)
        m, ms = divmod(ms, 60000)
        s, ms = divmod(ms, 1000)
        return '%03dd %02d:%02d:%02d.%03d' % (d,h,m,s,ms)

    @staticmethod
    def getname(tc:TestCase) -> str:
        return cast(Callable, tc).__qualname__

    async def upmsg(self, timeout:Optional[float]=None) -> LoraMsg:
        if timeout is not None:
            return await asyncio.wait_for(asyncio.shield(self.upmsgs.get()), timeout)
        else:
            return await self.upmsgs.get()

    #async def ul(self, port:int, maxmsg:int=3) -> Tuple[LoraMsg,lm.Msg]:
    #    while True:
    #        assert maxmsg, 'expected message not received within limit'
    #        msg = await self.upmsg()
    #        m = self.verify(msg)
    #        if m['FPort'] == port:
    #            return msg, m
    #        maxmsg -= 1

    async def get_evlog(self) -> Tuple[int,int,int]:
        return await self.sim.get_evlog()

    def clear_evlog(self) -> List[Tuple[int,int,int]]:
        return self.sim.clear_evlog()

    async def run(self) -> bool:
        try:
            return await self._run()
        finally:
            await self.sim.shutdown()

    async def _run(self) -> bool:
        self.results.clear()

        rt0 = time.time()
        st0 = asyncio.get_event_loop().time()

        asyncio.ensure_future(self.sim.run())

        for i, t in enumerate(self.tests):
            if i not in self.skip:
                print('_________________________________________________________')
                print('Test#%d - Running %s...' % (i+1, DeviceTest.getname(t)))
                if self.topts[t]['reboot']:
                    await self.sim.reset()
                    self.clear_evlog()           # drop all unconsumed events
                try:
                    self.results[t] = await asyncio.wait_for(t, timeout=self.topts[t]['timeout']) is True
                except:
                    print('%s%s%s' % (Fore.RED, traceback.format_exc(),
                                      Style.RESET_ALL))
                    self.results[t] = False
                if not self.results[t] and self.quit_on_fail:
                    break

        rt1 = time.time()
        st1 = asyncio.get_event_loop().time()

        print('_________________________________________________________')
        print('Real time:      %s' % DeviceTest.fmt_timespan(rt1-rt0))
        print('Simulated time: %s' % DeviceTest.fmt_timespan(st1-st0))
        print('_________________________________________________________')
        print('Results:')
        print()
        passed = 0
        for i, t in enumerate(self.tests):
            result = self.results.get(t)
            if result:
                passed += 1
                msg = Style.BRIGHT + Fore.GREEN + 'pass'
            elif result is False:
                msg = Style.BRIGHT + Fore.RED + 'fail'
            else:
                msg = Fore.BLUE + 'skip'
            print('{:>3} {:.<48} {}{}'.format(i+1,
                DeviceTest.getname(t) + ' ', msg, Style.RESET_ALL))
        print('_________________________________________________________')
        print('%d/%d tests passed.' % (passed, len(self.tests)))

        return passed == len(self.tests) - len(self.skip)

    def put_up(self, msg:LoraMsg) -> None:
        msg.rssi = msg.xpow - 50
        self.upmsgs.put_nowait(msg)

    @staticmethod
    def explain_more(s:str, explain:Optional[str]=None) -> str:
        if explain is not None:
            s += f' ({explain})'
        return s

    @staticmethod
    def assert_eq(rcv:Any, exp:Any, explain:Optional[str]=None, fmt:str='') -> None:
        assert rcv == exp, DeviceTest.explain_more(
                f'received: {rcv:{fmt}}, expected: {exp:{fmt}}',
                explain=explain)
    assert_equals = assert_eq

    @staticmethod
    def assert_ne(rcv:Any, exp:Any, explain:Optional[str]=None, fmt:str='') -> None:
        assert rcv != exp, DeviceTest.explain_more(
                f'received: {rcv:{fmt}}, expected: not {exp:{fmt}}',
                explain=explain)

    @staticmethod
    def assert_type(rcv:Any, exp:Any, explain:Optional[str]=None) -> None:
        DeviceTest.assert_eq(type(rcv), exp, explain=explain)

    @staticmethod
    def assert_range(rcv:Any, exp_min:Any, exp_max:Any, explain:Optional[str]=None, fmt:str='') -> None:
        assert rcv >= exp_min and rcv <= exp_max, DeviceTest.explain_more(
                f'received: {rcv:{fmt}}, expected: {exp_min:{fmt}} <= x <= {exp_max:{fmt}}',
                explain=explain)

    @staticmethod
    def assert_ge(rcv:Any, exp:Any, explain:Optional[str]=None, fmt:str='') -> None:
        assert rcv >= exp, DeviceTest.explain_more(
                f'received: {rcv:{fmt}}, expected: x >= {exp:{fmt}}',
                explain=explain)

    @staticmethod
    def assert_lt(rcv:Any, exp:Any, explain:Optional[str]=None, fmt:str='') -> None:
        assert rcv < exp, DeviceTest.explain_more(
                f'received: {rcv:{fmt}}, expected: x < {exp:{fmt}}',
                explain=explain)

    @staticmethod
    def assert_in(rcv:Any, exp:List[Any], explain:Optional[str]=None, fmt:str='') -> None:
        assert rcv in exp, DeviceTest.explain_more(
                f'received: {rcv:{fmt}}, expected: one of: '
                + ', '.join(f'{e:{fmt}}' for e in exp), explain=explain)

    @staticmethod
    def test(reboot:bool=True,
             region:Optional[ld.Region]=None, timeout:Optional[float]=None) -> TestCaseDecorator:
        def _test(f:DecoratedTestCase) -> DecoratedTestCase:
            DeviceTest.TESTS.append(f)
            f.options = { # type: ignore
                'reboot':  reboot,
                'timeout': timeout,
            }
            return f
        return _test

class ColoramaStream(TraceWriter):
    def __init__(self, io:TextIO, color:str='') -> None:
        super().__init__(io)
        self.color = color

    def write(self, s:str, strike:bool=False, **kwargs) -> None:
        style = self.color
        if strike:
            style += '\x1b[9m'
        super().write(style + s + Style.RESET_ALL)

class SessionManager:
    def __init__(self) -> None:
        self.sessions:Dict[int,Dict[int,Session]] = {}

    def add(self, s:Session) ->None :
        devaddr = s['devaddr']
        if devaddr in self.sessions:
            self.sessions[devaddr]['deveui'] = s
        else:
            self.sessions[devaddr] = { s['deveui']: s }

    def remove(self, s:Session) -> None:
        devaddr = s['devaddr']
        if devaddr in self.sessions:
            d = self.sessions[devaddr]
            if d.pop(s['deveui'], None):
                if not d:
                    self.sessions.pop(s['devaddr'])

    def get(self, devaddr:int) -> List[Session]:
        if devaddr in self.sessions:
            return self.sessions[devaddr].values()
        else:
            return []

    def all(self) -> List[Session]:
        return list(v for d in self.sessions.values() for v in d.values())

class LWTrafficTrace(TrafficTrace):
    def __init__(self, io:Union[TextIO,TraceWriter],
            sm:Optional[SessionManager]=None) -> None:
        super().__init__(io)
        self.sm = sm

    frmtype2str = {
            lm.FrmType.JREQ : 'JREQ',
            lm.FrmType.JACC : 'JACC',
            lm.FrmType.DAUP : 'DAUP',  # data (unconfirmed) up
            lm.FrmType.DADN : 'DADN',  # data (unconfirmed) dn
            lm.FrmType.DCUP : 'DCUP',  # data confirmed up
            lm.FrmType.DCDN : 'DCDN',  # data confirmed dn
            lm.FrmType.REJN : 'REJN',  # rejoin for roaming
            lm.FrmType.PROP : 'PROP',
            }

    @staticmethod
    def format_mhdr(mhdr:int) -> str:
        return (f'{LWTrafficTrace.frmtype2str[mhdr & lm.MHdr.FTYPE]}'
                f'(rfu={(mhdr & lm.MHdr.RFU) >> 2}, mjr={mhdr & lm.MHdr.MAJOR})')

    @staticmethod
    def format_fctrl(fctrl:int, dndir:bool) -> str:
        info = []
        if fctrl & lm.FCtrl.ADREN:
            info.append('ADR')
        if fctrl & lm.FCtrl.ADRARQ:
            info.append('ADRAckReq' if not dndir else 'RFU')
        if fctrl & lm.FCtrl.ACK:
            info.append('ACK')
        if fctrl & lm.FCtrl.CLASSB:
            info.append('ClassB' if not dndir else 'FPending')
        fol = fctrl & lm.FCtrl.OPTLEN
        if fol:
            info.append(f'FOptsLen:{fol}')
        return ','.join(info)

    @staticmethod
    def format_opts(opts:bytes, dndir:bool) -> str:
        try:
            return str(lo.unpack_opts(opts, lo.OPTSDN if dndir else lo.OPTSUP))
        except Exception as e:
            return f'[invalid opts ({e})]'

    @staticmethod
    def format_freq(f:int) -> str:
        return '0[disabled]' if f == 0 else f'{f}[rfu]' if f < 1000000 else f'{f*100/1e6:.6f}MHz'

    @staticmethod
    def format_chmask(cflist:bytes) -> str:
        s = ''
        for m, in struct.iter_unpack('<H', cflist):
            for i in range(16):
                if (i & 7) == 0:
                    s += '|'
                s += 'X' if (m & (1 << i)) else '.'
        return s + '|'

    @staticmethod
    def format_cflist(cflist:bytes) -> str:
        cfltype = cflist[-1]
        if cfltype == 0:    # list of frequencies
            freqs = []
            for i in range(5):
                freqs.append(struct.unpack_from("<I", cflist, i*3)[0] & 0xFFFFFF)
            return 'freqs:' + ','.join(LWTrafficTrace.format_freq(f) for f in freqs)
        elif cfltype == 1:  # channel mask
            return 'chmask:' + LWTrafficTrace.format_chmask(cflist[:10])
        else:               # something else?
            return f'unknown({cflist}):{cflist[:-1].hex()}'

    def format_jreq(self, pdu:bytes, info:List[str]) -> None:
        if len(pdu) != 23:
            info.append(f'-- invalid length {len(pdu)}, expected 23')
            return
        mhdr, aeui, deui, devnonce, mic = struct.unpack("<BQQHi", pdu)
        info.append(f'deveui={rt.Eui(deui)}')
        info.append(f'joineui={rt.Eui(aeui)}')
        info.append(f'devnonce={devnonce}')
        info.append(f'mic={mic}')

    def format_jacc(self, pdu:bytes, info:List[str]) -> None:
        n = len(pdu)
        if n != 17 and n != 33:
            info.append(f'-- invalid length {n}, expected 17 or 23')
            return

        if self.sm:
            for s in self.sm.all():
                ppdu = bytes(pdu[0:1] + lc.crypto.encrypt(s['nwkkey'], pdu[1:]))
                mic = lm.get_mic(ppdu)
                cmic = lc.crypto.calcMicJoin(s['nwkkey'], ppdu)
                if mic == cmic:
                    appnonce = struct.unpack_from("<I", ppdu, 1)[0] & 0xFFFFFF
                    netid    = struct.unpack_from("<I", ppdu, 1+3)[0] & 0xFFFFFF
                    devaddr, dlset, rxdly = struct.unpack_from("<iBB", ppdu, 1+3+3)
                    cflist = None if n == 17 else ppdu[-20:-4]

                    info.append(f'appnonce={appnonce}')
                    info.append(f'netid={netid}')
                    info.append(f'addr=0x{devaddr:08x}')
                    dls = lm.DLSettings.unpack(dlset)
                    info.append(f'dlset=RX1DRoff:{dls[0]},RX2DR:{dls[1]},OptNeg:{dls[2]}')
                    info.append(f'rxdly={rxdly}')

                    info.append(f'mic={mic}:ok')

                    if cflist:
                        info.append('cflist=' + self.format_cflist(cflist))
                    break
            else:
                info.append('-- unknown session')

    def format_da(self, pdu:bytes, info:List[str]) -> None:
        n = len(pdu)
        if n < 8+4:
            info.append(f'-- invalid length {n}, expected at least 12')
            return

        mhdr, addr, fctrl, seqno = struct.unpack_from('<BiBH', pdu)

        dndir = bool(mhdr & lm.MHdr.DNFLAG)

        info.append(f'addr=0x{addr:08x}')
        info.append('fctrl=' + self.format_fctrl(fctrl, dndir))
        info.append(f'fcnt={seqno}')

        fol = fctrl & lm.FCtrl.OPTLEN
        if fol:
            if n < 8+4+fol:
                info.append(f'-- invalid length {n}, expected at least {8+4+fol}')
                return
            info.append('fopts=' + self.format_opts(pdu[8:8+fol], dndir))

        pl = pdu[8+fol:-4]
        mic = lm.get_mic(pdu)


        if pl:
            info.append(f'fport={pl[0]}')
            info.append(f'plen={len(pl)-1}')

        if self.sm:
            sessions = self.sm.get(addr)
            for s in sessions:
                fcnt = seqno # TODO - extend seqno to 32 bit from session context
                cmic = lc.crypto.calcMic(s['nwkskey'], addr, fcnt, int(dndir), pdu)
                if cmic == mic:
                    micstatus = ':ok'
                    if pl:
                        ppl = lc.crypto.cipher(s['appskey'] if pl[0] else s['nwkskey'],
                                addr, seqno, int(dndir), pl[1:])
                        info.append(f'data={ppl.hex()}')
                        if pl[0] == 0:
                            info.append(self.format_opts(ppl, dndir))
                    break
            else:
                micstatus = ':?'
        else:
            micstatus = ''
        info.append(f'mic={mic}{micstatus}')

    def msginfo(self, msg:LoraMsg) -> str:
        pdu = msg.pdu

        if len(pdu) == 0:
            return ''

        info:List[str] = []
        ftype = pdu[0] & lm.MHdr.FTYPE
        info.append(self.format_mhdr(pdu[0]))

        if ftype in (lm.FrmType.DAUP, lm.FrmType.DCUP, lm.FrmType.DADN, lm.FrmType.DCDN):
            self.format_da(pdu, info)
        elif ftype == lm.FrmType.JREQ:
            self.format_jreq(pdu, info)
        elif ftype == lm.FrmType.JACC:
            self.format_jacc(pdu, info)

        return ' -- ' + ' '.join(info)

class LoRaWANTest(DeviceTest):
    def __init__(self, args:argparse.Namespace) -> None:
        self.sm = SessionManager()
        if not args.dns:
            sim = Simulation(self.put_up, args.hexfiles,
                             ColoramaStream(sys.stdout, Fore.BLUE) if args.debug else None,
                             LWTrafficTrace(ColoramaStream(sys.stdout, Fore.CYAN), self.sm) if args.traffic else None)
        else:
            from tcutils import Hardware
            sim = Hardware(self.put_up, args)
        super().__init__(sim)
        self.set_region(args.region)
        self.quit_on_fail = args.quit_on_fail

        self.session:Session = {}
        self.context:Session = {}

        alltests = set(range(len(self.tests)))
        skip:Set[int] = set()
        if args.tests:
            if not alltests.issuperset(args.tests):
                raise ValueError('Invalid test range specified')
            skip.update(alltests.difference(args.tests))
        if args.skip_tests:
            if not alltests.issuperset(args.skip_tests):
                raise ValueError('Invalid skip range specified')
            skip.update(args.skip_tests)
        if skip:
            self.skip = sorted(skip)

    def set_region(self, region:ld.Region) -> None:
        self.region = region
        self.upchannels = region.upchannels.copy()

    def rps2dr(self, rps:int) -> int:
        sf = Rps.getSf(rps)
        bw = Rps.getBw(rps) if sf else 0
        return cast(int, self.region.to_dr(sf, bw).dr)

    def dndr2rps(self, dr:int) -> int:
        dndr = self.region.DRs[dr]
        return Rps.makeRps(sf=dndr.sf, bw=dndr.bw*1000, crc=0)

    def getupch(self, m:Union[LoraMsg,lm.Msg]) -> int:
        if not isinstance(m, LoraMsg):
            m = cast(LoraMsg, m['upmsg'])
        dr = self.rps2dr(m.rps)
        for (idx, ch) in enumerate(self.upchannels):
            if m.freq == ch.freq and dr >= ch.minDR and dr <= ch.maxDR:
                return idx
        raise ValueError('Invalid channel in this region')

    def up2dn_rx1(self, freq:int, rps:int,
            rx1droffset:Optional[int]=None) -> Tuple[int,int]:
        if rx1droffset is None:
            rx1droffset = self.session['rx1droff']
        updr = self.rps2dr(rps)
        return (self.region.get_dnfreq(freq),
                self.dndr2rps(self.region.get_dndr(updr, rx1droffset)))

    def dn_rx2(self) -> Tuple[int,int]:
        return (self.region.RX2Freq, self.dndr2rps(self.session['rx2dr']))

    def process_join(self, msg:LoraMsg, rx2:bool=False, **kwargs:Any) -> lm.Msg:
        m = lm.unpack_nomic(msg.pdu)
        assert m['msgtype'] == 'jreq'

        nwkkey = kwargs.setdefault('nwkkey', b'@ABCDEFGHIJKLMNO')
        appnonce = kwargs.setdefault('appnonce', 0)
        netid = kwargs.setdefault('netid', 1)
        devaddr = kwargs.setdefault('devaddr',
                numpy.int32(crc32(rt.Eui(m['DevEUI']).as_bytes())))
        rxdly = kwargs.setdefault('rxdly', 0)
        (rx1droff, rx2dr, optneg) = lm.DLSettings.unpack(
                kwargs.setdefault('dlset',
                    lm.DLSettings.pack(0, self.region.RX2DR, False)))

        lm.verify_jreq(nwkkey, msg.pdu)

        # check channel validity
        self.getupch(msg)

        # check that devnonce is increasing
        ldn = self.session.get('devnonce')
        devnonce = m['DevNonce']
        if ldn is not None and ldn >= devnonce:
            raise ValueError('DevNonce is not strictly increasing')

        nwkskey = lc.crypto.derive(nwkkey,
                devnonce, appnonce, netid, lm.KD_NwkSKey)
        appskey = lc.crypto.derive(nwkkey,
                devnonce, appnonce, netid, lm.KD_AppSKey)

        jacc = lm.pack_jacc(**kwargs)

        rxdelay = ld.JaccRxDelay
        if rx2:
            rxdelay += 1
            (freq, rps) = self.dn_rx2()
        else:
            (freq, rps) = self.up2dn_rx1(msg.freq, msg.rps, 0)

        self.add_dn(LoraMsg(msg.xend + rxdelay, jacc, freq, rps, 14))

        if self.sm and self.session:
            self.sm.remove(self.session)

        self.session = {
                'deveui'    : m['DevEUI'],
                'devaddr'   : devaddr,
                'nwkkey'    : nwkkey,
                'nwkskey'   : nwkskey,
                'appskey'   : appskey,
                'fcntup'    : 0,
                'fcntdn'    : 0,
                'rx1delay'  : max(rxdly, 1),
                'rx1droff'  : rx1droff,
                'rx2dr'     : rx2dr,
                'devnonce'  : devnonce,
                }
        print('Device session: DevEUI=%s DevAddr=%08X/%d' % (m['DevEUI'], devaddr & 0xFFFFFFFF, devaddr))
        self.sim.filterTraffic(m['DevEUI'], devaddr)

        if self.sm:
            self.sm.add(self.session)

        m['upmsg'] = msg
        return m

    def verify(self, msg:LoraMsg,
            port:Optional[int]=None, explain:Optional[str]=None) -> lm.Msg:
        m = lm.unpack_nomic(msg.pdu)
        assert m['msgtype'] == 'updf'
        m = lm.unpack_dataframe(msg.pdu, self.session['fcntup'],
                self.session['nwkskey'], self.session['appskey'])

        if port is not None:
            assert m['FPort'] == port, 'received: %d, expected: %d%s' % (
                    m['FPort'], port, '' if explain is None
                    else (' (%s)' % explain))

        self.session['fcntup'] = m['FCnt']

        return m

    def dl_pdu(self, upmsg:LoraMsg, pdu:bytes, rx2:bool=False,
            freq:Optional[int]=None, rps:Optional[int]=None, toff:Optional[float]= 0,
            rx1droffset:Optional[int]=None) -> None:
        rxdelay = self.session['rx1delay']
        if rx2:
            rxdelay += 1
            (dfreq, drps) = self.dn_rx2()
        else:
            (dfreq, drps) = self.up2dn_rx1(upmsg.freq, upmsg.rps, rx1droffset)
        if freq is None:
            freq = dfreq
        if rps is None:
            rps = drps
        m = LoraMsg(upmsg.xend + rxdelay + toff, pdu, freq, rps, 14)
        self.add_dn(m)

    def dl(self, upmsg:LoraMsg, port:Optional[int]=None,
            payload:Optional[bytes]=None,
            fctrl:int=0, fopts:Optional[bytes]=None,
            confirmed:bool=False,
            invalidmic:bool=False, fcntdn_adj:int=0, **kwargs:Any) -> None:
        pdu = lm.pack_dataframe(
                mhdr=(lm.FrmType.DCDN if confirmed
                    else lm.FrmType.DADN)|lm.Major.V1,
                devaddr=self.session['devaddr'],
                fcnt=self.session['fcntdn'] + fcntdn_adj,
                fctrl=fctrl,
                fopts=fopts,
                port=port,
                payload=payload,
                nwkskey=self.session['nwkskey'],
                appskey=self.session['appskey'])
        if invalidmic:
            pdu = pdu[:-4] + bytes(map(lambda x: ~x & 0xff, pdu[-4:]))
        if fcntdn_adj >= 0:
            self.session['fcntdn'] += (1 + fcntdn_adj)
        self.dl_pdu(upmsg, pdu, **kwargs)


    def mdl(self, upmsg:LoraMsg, port:Optional[int]=None,
            payload:Optional[bytes]=None,
            fctrl:int=0, fopts:Optional[bytes]=None,
            confirmed:bool=False,
            invalidmic:bool=False, fcntdn_adj:int=0, **kwargs:Any) -> None:
        pdu = lm.pack_dataframe(
                mhdr=(lm.FrmType.DCDN if confirmed
                    else lm.FrmType.DADN)|lm.Major.V1,
                devaddr=self.session['mgrpaddr'],
                fcnt=self.session['mfcntdn'] + fcntdn_adj,
                fctrl=fctrl,
                fopts=fopts,
                port=port,
                payload=payload,
                nwkskey=self.session['mnwkskey'],
                appskey=self.session['mappskey'])
        if invalidmic:
            pdu = pdu[:-4] + bytes(map(lambda x: ~x & 0xff, pdu[-4:]))
        if fcntdn_adj >= 0:
            self.session['mfcntdn'] += (1 + fcntdn_adj)
        self.dl_pdu(upmsg, pdu, **kwargs)

    # get the next uplink, process as join request (with kwargs)
    async def lw_join(self, timeout:Optional[float]=None, **kwargs:Any) -> lm.Msg:
        msg = await self.upmsg(timeout)
        return self.process_join(msg, **kwargs)

    # get the next uplink, verify (with kwargs), and return message
    async def lw_uplink(self, timeout:Optional[float]=None,
            filter:Callable[[lm.Msg],bool]=lambda m: True, limit:int=1,
            explain:Optional[str]=None, **kwargs:Any) -> lm.Msg:
        deadline = timeout and asyncio.get_event_loop().time() + timeout
        for _ in range(limit):
            timeout = deadline and max(0, deadline - asyncio.get_event_loop().time())
            msg = await self.upmsg(timeout)
            m = self.verify(msg, explain=explain, **kwargs)
            m['upmsg'] = msg
            if filter(m):
                return m
        assert False, DeviceTest.explain_more(
                f'No matching message received within limit of {limit} messages',
                explain=explain)

    # consume uplinks until matching a port/condition met - limited by msgs/timeout
    # raise EOFError/TimeoutError if no match
    async def lw_uplink_until(self, port:int=None,
                              condfn:CondFn=None,
                              maxmsgs:int=0,
                              timeout:Optional[float]=None,
                              **kwargs:Any) -> Optional[lm.Msg]:
        deadline = asyncio.get_event_loop().time() + (timeout or 1e13)
        maxmsgs = maxmsgs or 1000
        while maxmsgs > 0:
            maxmsgs -= 1
            m = await self.lw_uplink(deadline-asyncio.get_event_loop().time())
            if ((port is None or m['FPort'] == port) and
                (condfn is None or condfn(m))):
                return m
        raise EOFError()

    def lw_uplink_prune(self) -> None:
        while not self.upmsgs.empty():
            self.upmsgs.get_nowait()

    # enqueue a downlink (with kwargs)
    def lw_dnlink(self, m:Union[LoraMsg,lm.Msg], **kwargs:Any) -> None:
        if not isinstance(m, LoraMsg):
            m = cast(LoraMsg, m['upmsg'])
        self.dl(m, **kwargs)

    def lw_mdnlink(self, m:Union[LoraMsg,lm.Msg], **kwargs:Any) -> None:
        if not isinstance(m, LoraMsg):
            m = cast(LoraMsg, m['upmsg'])
        self.mdl(m, **kwargs)

    @staticmethod
    def get_opts(m:lm.Msg) -> List[lo.Opt]:
        if m['FPort'] is 0:
            opts = m['FRMPayload']
            assert len(m['FOpts']) == 0
        else:
            opts = m['FOpts']
        return lo.unpack_optsup(opts)

    @staticmethod
    def isconfirmed(m:lm.Msg) -> bool:
        return (m['MHdr'] & lm.MHdr.FTYPE) in [lm.FrmType.DCUP, lm.FrmType.DCDN]

    @staticmethod
    def isack(m:lm.Msg) -> bool:
        return cast(int, m['FCtrl'] & lm.FCtrl.ACK) != 0

    @staticmethod
    def isadren(m:lm.Msg) -> bool:
        return cast(int, m['FCtrl'] & lm.FCtrl.ADREN) != 0

    @staticmethod
    def isadrarq(m:lm.Msg) -> bool:
        return cast(int, m['FCtrl'] & lm.FCtrl.ADRARQ) != 0

    async def freq_count(self, count:int) -> Dict[int,int]:
        freqs:Dict[int,int] = dict()
        for i in range(count):
            msg = await self.upmsg()
            m = self.verify(msg)
            freqs[msg.freq] = freqs.get(msg.freq, 0) + 1
        return freqs

    @staticmethod
    def stdargs(p:argparse.ArgumentParser, simopts:bool=True) -> None:
        def region(spec:str) -> ld.Region:
            if spec in ld.REGIONS:
                return cast(ld.Region, ld.REGIONS[spec])
            raise argparse.ArgumentTypeError(
                    'Invalid region, choose from ' +
                    ', '.join(ld.REGIONS.keys()))

        def tests(spec:str) -> List[int]:
            def test_range(r:str) -> Iterable[int]:
                if len(r) == 0:
                    return []
                parts = r.split("-")
                if len(parts) > 2:
                    raise ValueError("Invalid range: {}".format(r))
                return range(int(parts[0])-1, int(parts[-1]))
            return sorted(set(chain.from_iterable(
                map(test_range, spec.split(',')))))

        p.add_argument('-r', '--region', type=region, default=ld.EU868,
                help='Set region')
        p.add_argument('-s', '--tests', type=tests, default=None,
                help='Specify tests to run')
        p.add_argument('-S', '--skip-tests', type=tests, default=None,
                help='Specify tests to skip')
        p.add_argument('-v', '--virtual-time', action='store_true',
                help='Use virtual time')
        p.add_argument('-q', '--quit-on-fail', action='store_true',
                help='Quit on first test failure')

        if simopts:
            p.add_argument('-d', '--debug', action='store_true',
                    help='Enable simulation debug output')
            p.add_argument('-t', '--traffic', action='store_true',
                    help='Show message traffic')
            p.add_argument('hexfiles', metavar='HEXFILE', nargs='+',
                    help='Firmware files to load')

        p.add_argument('--dns', type=str, metavar='hostname[:port]',
                       help='Enable hardware based tests. BasicStation connects to this hostname.')
        p.add_argument('--tty', type=str, metavar='TTYDEV',
                       help='The modem device is attached to this TTY device. Default: %(default)s',
                       default='/dev/ttyUSB0')
        p.add_argument('--ocdargs', type=str, metavar='"arg1 arg2 .."',
                       help='Extra arguments to add to openocd call when reseting device. . Default: %(default)s',
                       default='-f ../../tools/openocd/nucleo-l0.cfg -f ../../tools/openocd/flash.cfg')
        p.add_argument('--station', type=str, metavar='"arg1 arg2 .."',
                       help='Start station process before start of first test.')
