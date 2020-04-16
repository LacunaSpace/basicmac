# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from .eui import *
import re

__all__ = ['Eui']

def freq_to_str(f:int) -> str:
    if f==0: return '0'
    s = str(int(f))
    l = len(s)
    m = re.search(r'0*$', s)
    z = len(m.group(0) if m else '')
    g = (l-1)//3
    u = ['Hz','kHz','MHz','GHz'][g]
    a = g*3
    if z < a:
        return s[:l-a]+'.'+s[l-a:l-z]+u
    return s[:l-a]+u
