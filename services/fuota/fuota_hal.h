// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _fuota_hal_h_
#define _fuota_hal_h_

// The functions below must be provided by the HAL. They can be implemented as
// macros.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef FUOTA_HAL_IMPL
#include FUOTA_HAL_IMPL
#else
#include "lmic.h"
#endif


// ------------------------------------------------
// Flash access

// Get flash default state
#ifndef fuota_flash_bitdefault
#error "Macro 'fuota_flash_bitdefault' must be defined by HAL"
#else
#if ((fuota_flash_bitdefault & ~1) != 0)
#error "Macro 'fuota_flash_bitdefault' must be set to 1 or 0"
#endif
#endif

// Get flash page size in bytes
#ifndef fuota_flash_pagesz
#error "Macro 'fuota_flash_pagesz' must be defined by HAL"
#endif

// Write words from RAM to flash memory (aligned-to-aligned)
// If erase is true, erase the page when reaching a page boundary before writing
#ifndef fuota_flash_write
void fuota_flash_write (void* dst, void* src, uint32_t nwords, bool erase);
#endif

// Read words from flash memory to RAM (aligned-to-aligned)
#ifndef fuota_flash_read
void fuota_flash_read (void* dst, void* src, uint32_t nwords);
#endif

// Read a uint32_t from flash memory
#ifndef fuota_flash_rd_u4
uint32_t fuota_flash_rd_u4 (void* addr);
#endif

// Read a void* from flash memory
#ifndef fuota_flash_rd_ptr
void* fuota_flash_rd_ptr (void* addr);
#endif

#endif
