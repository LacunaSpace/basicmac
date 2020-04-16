// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "picofs.h"
#include "eefs.h"

#include "svcdefs.h"

static struct {
    bool initialized;
    pfs fs;

} state;

#if defined(CFG_DEBUG) && CFG_DEBUG != 0
static const char* fn (const uint8_t* ufid) {
    const char* name = SVCHOOK_eefs_fn(ufid);
    return name ?: "unknown";
}

static void cb_debug_ls (int fh, const uint8_t* ufid, void* ctx) {
    unsigned int q1 = os_rlsbf4(ufid);
    unsigned int q2 = os_rlsbf4(ufid + 4);
    unsigned int i  = os_rlsbf4(ufid + 8);
    debug_printf("eefs: %02x %4dB %08x%08x-%08x (%s)\r\n",
            fh, pfs_read_fh(&state.fs, fh, NULL, 0), q2, q1, i, fn(ufid));
}
#endif

void eefs_init (void* begin, unsigned int size) {
    pfs_init(&state.fs, begin, size / PFS_BLOCKSZ);
    state.initialized = true;
#if defined(CFG_DEBUG) && CFG_DEBUG != 0
    pfs_ls(&state.fs, cb_debug_ls, NULL);
#endif
    SVCHOOK_eefs_init();
}

int eefs_read (const uint8_t* ufid, void* data, int sz) {
    ASSERT(state.initialized);
    return pfs_read(&state.fs, ufid, data, sz);
}

int eefs_save (const uint8_t* ufid, void* data, int sz) {
    ASSERT(state.initialized);
    int fh = pfs_save(&state.fs, ufid, data, sz);
    if( fh < 0 ) {
        // TODO: garbage collect
        fh = pfs_save(&state.fs, ufid, data, sz);
    }
    return fh;
}

bool eefs_rm (const uint8_t* ufid) {
    ASSERT(state.initialized);
    int fh = pfs_find(&state.fs, ufid);
    if( fh < 0 ) {
        return false;
    } else {
        pfs_rm_fh(&state.fs, fh);
        return true;
    }
}

// FNV32 hash (1a)
void pfs_crc32 (uint32_t* fnv, unsigned char* buf, uint32_t len) {
    if( buf ) {
        uint32_t h = *fnv;
        while (len-- > 0) {
            h ^= *buf++;
            h += ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24));
        }
        *fnv = h;
    } else {
        *fnv = 0x811c9dc5; // initialization value
    }
}

void pfs_write_block (void* dst, void* src, int nwords) {
    eeprom_copy(dst, src, nwords << 2);
}

uint8_t pfs_rnd_block (uint8_t nblks) {
    uint32_t rnd = (os_getRndU1() << 24) | (os_getRndU1() << 16)
        | (os_getRndU1() << 8) | os_getRndU1();
    return rnd % nblks;
}
