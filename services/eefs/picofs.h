// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _picofs_h_
#define _picofs_h_

#include <stdint.h>
#include <stdbool.h>

union _pfs_block;
typedef union _pfs_block pfs_block;

enum {
    PFS_BLOCKSZ = 32,
};

// block allocation map
typedef struct {
    uint32_t map[8];            // 32 B - block allocation map
} pfs_alloc;

// file system state
typedef struct {
    pfs_block* bb;              // base block pointer
    int nblks;                  // number of blocks
    int next;                   // next alloc search start
    pfs_alloc alloc;            // allocation bitmap
} pfs;

// glue functions
extern void pfs_write_block (void* dst, void* src, int nwords);
extern void pfs_crc32 (uint32_t* pcrc, unsigned char* buf, uint32_t len);
extern uint8_t pfs_rnd_block (uint8_t nblks);

// public API
void pfs_init (pfs* s, void* p, int nblks);
int pfs_find (pfs* s, const uint8_t* ufid);
int pfs_read (pfs* s, const uint8_t* ufid, void* data, int sz);
int pfs_save (pfs* s, const uint8_t* ufid, void* data, int sz);
void pfs_ls (pfs* s, void (*cb) (int fh, const uint8_t* ufid, void* ctx), void* ctx);

void pfs_rm_fh (pfs* s, int fh);
int pfs_read_fh (pfs* s, int fh, void* data, int sz);

#endif
