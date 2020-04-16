// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _eefs_h_
#define _eefs_h_

#include <stdint.h>
#include <stdbool.h>

void eefs_init (void* begin, unsigned int size);
int eefs_read (const uint8_t* ufid, void* data, int sz);
int eefs_save (const uint8_t* ufid, void* data, int sz);
bool eefs_rm (const uint8_t* ufid);

#endif
