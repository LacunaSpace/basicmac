#!/usr/bin/env python3

# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import os
import shlex
import sys
import re
import yaml

from collections import deque, OrderedDict

from typing import Callable,Dict,List,Optional,Set,TextIO,Tuple
from typing import cast
from argparse import Namespace as NS # type alias

from cc import CommandCollection

class Service:
    class Hook:
        STYLE_ALL      = 0
        STYLE_LAST     = 1
        STYLE_FIRST_NZ = 2

        STYLE_LOOKUP = {
                None       : STYLE_ALL,
                'last'     : STYLE_LAST,
                'first_nz' : STYLE_FIRST_NZ,
                }

        def __init__(self, hd:str, fn:str) -> None:
            m = re.match(r'^\s*(?:<(\w+):(\w+)>)?\s*(.+)\s+(\w+)\s*(\([^\)]+\))\s*$', hd)
            if m:
                self.name = m.group(4)
                self.returntype = m.group(3)
                self.returndefault = m.group(2)
                self.style = self.STYLE_LOOKUP[m.group(1)]
                self.template = f'{m.group(3)} %s {m.group(5)}'
            else:
                raise ValueError('%s: invalid function declaration "%s"' % (fn, hd))

        def emit(self, fh:TextIO, hooks:List[str]) -> None:
            for h in hooks:
                fh.write(f'extern {self.template % h};\n')
            if self.style == self.STYLE_ALL:
                self._emit_all(fh, hooks)
            elif self.style == self.STYLE_LAST:
                self._emit_last(fh, hooks)
            elif self.style == self.STYLE_FIRST_NZ:
                self._emit_first_nz(fh, hooks)

        def _emit_all(self, fh:TextIO, hooks:List[str]) -> None:
            fh.write('#define SVCHOOK_%s(...) do { %s } while (0)\n' % (self.name,
                ' '.join([('{ %s(__VA_ARGS__); }' % h) for h in hooks])))

        def _emit_last(self, fh:TextIO, hooks:List[str]) -> None:
            fh.write('#define SVCHOOK_%s(...) ({ %s __SVCHOOK_retval = %s; %s; __SVCHOOK_retval; })\n' % (self.name,
                self.returntype, self.returndefault,
                ' '.join([('{ __SVCHOOK_retval = %s(__VA_ARGS__); }' % h) for h in hooks])))

        def _emit_first_nz(self, fh:TextIO, hooks:List[str]) -> None:
            fh.write('#define SVCHOOK_%s(...) ({ %s __SVCHOOK_retval = %s; do { %s } while (0); __SVCHOOK_retval; })\n' % (self.name,
                self.returntype, self.returndefault,
                ' '.join([('{ %s __SVCHOOK_retval2 = %s(__VA_ARGS__); if( __SVCHOOK_retval2 ) { __SVCHOOK_retval = __SVCHOOK_retval2; break; } }'
                    % (self.returntype, h)) for h in hooks])))


    def __init__(self, svcid:str, fn:str) -> None:
        self.id = svcid
        self.srcs     : List[str]                      = []
        self.hooks    : List[List[str]]                = []
        self.hookdefs : Dict[str,List[str]]            = OrderedDict()
        self.require  : List[str]                      = []
        self.defines  : List[Tuple[str,Optional[str]]] = []
        self.fn = fn
        with open(fn, 'r') as fh:
            d = yaml.safe_load(fh)
        for k, v in d.items():
            if k == 'src':
                if isinstance(v, list):
                    self.srcs.extend(v)
                else:
                    self.srcs.append(v)
            elif k == 'hooks':
                if not isinstance(v, list):
                    v = [ v ]
                self.hooks.extend([Service.Hook(h, fn) for h in v])
            elif k == 'require':
                if not isinstance(v, list):
                    v = [ v ]
                self.require.extend(v)
            elif k == 'define':
                if not isinstance(v, list):
                    v = [ v ]
                self.defines.extend([Service.parse_define(d, fn) for d in v])
            elif k.startswith('hook.'):
                h = k[5:]
                if h not in self.hookdefs:
                    self.hookdefs[h] = []
                if not isinstance(v, list):
                    v = [ v ]
                self.hookdefs[h].extend(v)
            else:
                raise ValueError('%s: unknown key %s' % (fn, k))

    @staticmethod
    def parse_define(dd:str, fn:str) -> Tuple[str,Optional[str]]:
        m = re.match(r'^([^=]+)(?:=(.*))?', dd)
        if m:
            return cast(Tuple[str,Optional[str]], m.groups())
        else:
            raise ValueError('%s: invalid define declaration "%s"' % (fn, dd))

class ServiceCollection:
    def __init__(self) -> None:
        self.svcs : Dict[str,Service] = OrderedDict()

    def add(self, svc:Service) -> None:
        self.svcs[svc.id] = svc

    def validate(self) -> None:
        pass

    def sources(self) -> List[str]:
        return [src for svc in self.svcs.values() for src in svc.srcs]

    def files(self) -> List[str]:
        return [svc.fn for svc in self.svcs.values()]

    def defines(self) -> List[str]:
        return ['SVC_' + s for s in self.svcs.keys()] + [
                '%s%s' % (k, '' if v is None else '=%s' % shlex.quote(v))
                for svc in self.svcs.values() for k,v in svc.defines]

    def hookdefs(self) -> Dict[Service.Hook,List[str]]:
        return { h: [hd for hds in (sv2.hookdefs.get(h.name)
            for sv2 in self.svcs.values()) if hds is not None for hd in hds]
            for sv1 in self.svcs.values() for h in sv1.hooks }

    def unresolved(self) -> List[str]:
        return [s for sl in [svc.require for svc in self.svcs.values()] for s in sl if s not in self.svcs]

    def contains(self, svcid:str) -> bool:
        return svcid in self.svcs

class ServiceToolUtil:
    @staticmethod
    def arg(name:str) -> Callable:
        if name == 'svc':
            return CommandCollection.arg(name, type=str,
                    metavar='svcid',
                    nargs='*', help='service identifier')
        if name == '--path':
            return CommandCollection.arg('-p', '--path', type=str,
                    action='append',
                    help='paths to search for service definitions')
        raise ValueError()

    @staticmethod
    def hook_default(hook, defs) -> None:
        print(f'hook: {hook}')
        print(f'defs: {defs}')


class ServiceTool:
    def run(self) -> None:
        CommandCollection.run(self)

    @staticmethod
    def load(svcid:str, paths:List[str]) -> Optional[Service]:
        for p in paths:
            fn = os.path.join(p, svcid + '.svc')
            if os.path.isfile(fn):
                return Service(svcid, fn)
        return None

    @staticmethod
    def collect(args:NS) -> ServiceCollection:
        sc = ServiceCollection()
        sd = deque(args.svc)
        i = 0
        while len(sd):
            s = sd.popleft()
            if not sc.contains(s):
                i += 1
                svc = ServiceTool.load(s, args.path or ['.'])
                if svc is None:
                    raise ValueError('Cannot find service description for "%s"' % s)
                sc.add(svc)
                sd.extend(sc.unresolved())
        sc.validate()
        return sc

    @ServiceToolUtil.arg('svc')
    @ServiceToolUtil.arg('--path')
    @CommandCollection.cmd(help='validate the service configuration')
    def check(self, args:NS) -> None:
        try:
            ServiceTool.collect(args)
        except:
            print(str(sys.exc_info()))

    @ServiceToolUtil.arg('svc')
    @ServiceToolUtil.arg('--path')
    @CommandCollection.cmd(help='output a list of source files')
    def sources(self, args:NS) -> None:
        sc = ServiceTool.collect(args)
        print(' '.join(sc.sources()))

    @ServiceToolUtil.arg('svc')
    @ServiceToolUtil.arg('--path')
    @CommandCollection.cmd(help='output a list defines for the compiler')
    def defines(self, args:NS) -> None:
        sc = ServiceTool.collect(args)
        print(' '.join(sc.defines()))

    @ServiceToolUtil.arg('svc')
    @CommandCollection.arg('-d', action='store_true', help='create a dependency file for make')
    @CommandCollection.arg('-o', '--output', type=str, help='output file', required=True)
    @ServiceToolUtil.arg('--path')
    @CommandCollection.cmd(help='create the svcdef header file')
    def svcdefs(self, args:NS) -> None:
        sc = ServiceTool.collect(args)
        guard = os.path.basename(args.output).replace('.', '_')
        with open(args.output, 'w') as fh:
            fh.write('// Automatically generated by %s\n\n' % ' '.join(sys.argv))
            fh.write('#ifndef _%s_\n' % guard)
            fh.write('#define _%s_\n' % guard)
            for h, defs in sorted(sc.hookdefs().items(), key=lambda hd: hd[0].name):
                h.emit(fh, defs)

            fh.write('#endif\n')
        if args.d:
            with open(os.path.splitext(args.output)[0] + '.d', 'w') as fh:
                deps = sc.files()
                fh.write('%s: %s\n\n' % (args.output, ' '.join(deps)))
                for d in deps:
                    fh.write('%s:\n\n' % d)

if __name__ == '__main__':
    ServiceTool().run()
