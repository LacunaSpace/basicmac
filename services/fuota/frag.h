// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _frag_h_
#define _frag_h_

void _frag_init (int nsessions, void** sbeg, void** send);

int frag_get (int idx, void** pdata);

#endif
