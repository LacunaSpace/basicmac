// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <string.h>

#include "picofs.h"

#define MB_MAGIC 0xfe837080

#if defined(CFG_DEBUG) && defined(CFG_DEBUG_picofs)
#include "lmic.h"
#define PFS_LOG(...)        debug_printf(__VA_ARGS__)
#define PFS_ASSERT(cond)    ASSERT(cond)
#endif

#ifndef PFS_LOG
#define PFS_LOG(...)        do { } while( 0 )
#endif
#ifndef PFS_ASSERT
#define PFS_ASSERT(cond)    do { } while( 0 )
#endif

// first block pointer, and other info
typedef union {
    uint32_t w;
    struct {
        uint8_t blk0;           // first data block number
        uint8_t pad;            // amount of padding in last block
        uint8_t res1;           // reserved
        uint8_t res2;           // reserved
    };
} fbp;

// block structure
union _pfs_block {
    unsigned char raw[32];      // 32 B - block size
    struct {
        uint8_t ufid[12];       // 12 B - unique file identifier
        uint32_t crc[2];        //  8 B - CRC32 of data
        fbp p[2];               //  8 B - first block pointer
        uint32_t magic;         //  4 B - MB_MAGIC
    } meta;
    struct {
        uint8_t data[31];       // 31 B - data
        uint8_t next;           //  1 B - next block number (255=end, 254=metablock)
    } data;
};

_Static_assert(sizeof(pfs_block) == PFS_BLOCKSZ, "block size inconsistent");

static void pfs_write_word (uint32_t* dst, uint32_t value) {
    pfs_write_block(dst, &value, 1);
}

static inline bool isalloc (pfs_alloc* a, int blk) {
    return a->map[blk >> 5] & (1 << (blk & 0x1f));
}

static inline void alloc (pfs_alloc* a, int blk) {
    a->map[blk >> 5] |= (1 << (blk & 0x1f));
}

static inline void dealloc (pfs_alloc* a, int blk) {
    a->map[blk >> 5] &= ~(1 << (blk & 0x1f));
}

typedef int (*walk_cb) (pfs* s, int n, void* ctx);

static int walk (pfs* s, int start, walk_cb cb, void* ctx) {
    while( 1 ) {
        int rv = cb(s, start, ctx);
        if( rv ) {
            return rv;
        }
        start = s->bb[start].data.next;
        if( start == 255 ) {
            return 0;
        }
    }
}

typedef struct {
    uint32_t crc;
    pfs_alloc a;
} vinfo;

static int cb_validate (pfs* s, int n, void* ctx) {
    vinfo* vi = ctx;
    PFS_LOG("%d", n);
    if( n >= s->nblks ) {
        PFS_LOG("[invalid], ");
        return 1;
    }
    if( isalloc(&vi->a, n) ) {
        PFS_LOG("[collision], ");
        return 1;
    }
    alloc(&vi->a, n);
    pfs_crc32(&vi->crc, s->bb[n].data.data, sizeof(s->bb[n].data.data));
    PFS_LOG(", ");
    return 0;
}

static bool v_chain (pfs* s, int start, uint32_t crc) {
    vinfo vi;
    vi.a = s->alloc;

    pfs_crc32(&vi.crc, NULL, 0);
    if( walk(s, start, cb_validate, &vi) == 0 ) {
        PFS_LOG("complete, ");
        if( crc == vi.crc ) {
            PFS_LOG("crc ok, ");
            s->alloc = vi.a;
            return true;
        } else {
            PFS_LOG("crc invalid, ");
        }
    }
    return false;
}

void pfs_init (pfs* s, void* p, int nblks) {
    PFS_ASSERT(nblks < 253);
    s->bb = p;
    s->nblks = nblks;
    s->next = pfs_rnd_block(nblks);
    memset(&s->alloc, 0, sizeof(s->alloc));
    for( int i = 0; i < nblks; i++ ) {
        if( !isalloc(&s->alloc, i )
                && s->bb[i].meta.magic == MB_MAGIC ) {
            PFS_LOG("metablock at %d\n", i);
            for( int j = 0; j < 2; j++ ) {
                PFS_LOG(" chain %d: ", j);
                if( v_chain(s, s->bb[i].meta.p[j].blk0, s->bb[i].meta.crc[j]) ) {
                    alloc(&s->alloc, i);
                    if(~(s->bb[i].meta.p[j^1].w) != 0 ) {
                        // fix dangling entry
                        pfs_write_word(&s->bb[i].meta.p[j^1].w, ~0);
                        PFS_LOG("dangler fixed, ");
                    }
                    PFS_LOG("selected\n");
                    break;
                }
                PFS_LOG("skipped\n");
            }
        }
    }
}

static int pfs_dir (pfs* s, walk_cb cb, void* ctx) {
    for( int i = 0; i < 253; i++ ) {
        if( isalloc(&s->alloc, i) && s->bb[i].meta.magic == MB_MAGIC ) {
            int rv = cb(s, i, ctx);
            if( rv ) {
                return rv;
            }
        }
    }
    return 0;
}

typedef struct {
    void (*cb) (int, const uint8_t*, void*);
    void* ctx;
} linfo;

static int cb_ls (pfs* s, int n, void* ctx) {
    linfo* li = ctx;
    li->cb(n, s->bb[n].meta.ufid, li->ctx);
    return 0;
}

void pfs_ls (pfs* s, void (*cb) (int, const uint8_t*, void*), void* ctx) {
    linfo li = {
        .cb = cb,
        .ctx = ctx
    };
    pfs_dir(s, cb_ls, &li);
}

typedef struct {
    const uint8_t* ufid;
    int fh;
} finfo;

static int cb_find (pfs* s, int n, void* ctx) {
    finfo* fi = ctx;
    if( memcmp(fi->ufid, s->bb[n].meta.ufid, 12) == 0 ) {
        fi->fh = n;
        return 1;
    }
    return 0;
}

int pfs_find (pfs* s, const uint8_t* ufid) {
    finfo fi = {
        .ufid = ufid
    };
    if( pfs_dir(s, cb_find, &fi) ) {
        return fi.fh;
    }
    return -1;
}

static int cb_dealloc (pfs* s, int n, void* ctx) {
    pfs_alloc* a = ctx;
    PFS_LOG("%d, ", n);
    dealloc(a, n);
    return 0;
}

static void chain_clear (pfs* s, pfs_alloc* a, int start) {
    PFS_LOG("chain_clear: ");
    walk(s, start, cb_dealloc, a);
    PFS_LOG("complete\n");
}

void pfs_rm_fh (pfs* s, int fh) {
    if( isalloc(&s->alloc, fh)
            && s->bb[fh].meta.magic == MB_MAGIC ) {
        int j = (~(s->bb[fh].meta.p[0].w) == 0);
        pfs_write_word(&s->bb[fh].meta.magic, 0);
        chain_clear(s, &s->alloc, s->bb[fh].meta.p[j].blk0);
        dealloc(&s->alloc, fh);
    }
}

static int block_alloc (pfs* s, pfs_alloc* a) {
    for( int i = s->next + 1; i != s->next; i = (i >= (s->nblks - 1)) ? 0 : (i + 1) ) {
        if( i >= s->nblks ) {
            i = 0;
        }
        if( !isalloc(a, i) ) {
            alloc(a, i);
            s->next = i;
            return i;
        }
    }
    return -1;
}

static int meta_create (pfs* s, pfs_alloc* a, const uint8_t* ufid) {
    PFS_LOG("meta_create: ");
    pfs_block b;
    int fh = block_alloc(s, a);
    if( fh >= 0 ) {
        memcpy(b.meta.ufid, ufid, 12);
        b.meta.crc[0] = 0;
        b.meta.p[0].w = ~0;
        b.meta.crc[1] = 0;
        b.meta.p[1].w = ~0;
        b.meta.magic = MB_MAGIC;
        pfs_write_block(s->bb[fh].raw, b.raw, sizeof(b.raw) / 4);
        PFS_LOG("%d, complete\n", fh);
    } else {
        PFS_LOG("out of memory\n");
    }
    return fh;
}

static void meta_update (pfs* s, pfs_alloc* a, int fh, uint32_t w, uint32_t crc) {
    int j = (~(s->bb[fh].meta.p[0].w) != 0);
    pfs_write_word(&s->bb[fh].meta.crc[j], crc);
    pfs_write_word(&s->bb[fh].meta.p[j].w, w);
    if( ~(s->bb[fh].meta.p[j^1].w) != 0 ) {
        chain_clear(s, a, s->bb[fh].meta.p[j^1].blk0);
        pfs_write_word(&s->bb[fh].meta.p[j^1].w, ~0);
    }
    PFS_LOG("meta_update: %d, w0=%08x, w1=%08x\n", fh, s->bb[fh].meta.p[0].w, s->bb[fh].meta.p[1].w);
}

static int chain_write (pfs* s, pfs_alloc* a, void* data, int sz, int* pad, uint32_t* crc) {
    PFS_LOG("chain_write: ");
    unsigned char* ptr = data;
    int first = block_alloc(s, a);
    if( first >= 0 ) {
        int i = first;
        while( sz ) {
            pfs_block b;
            int n = (sz < sizeof(b.data.data)) ? sz : sizeof(b.data.data);
            // prepare data block
            memset(b.raw, 0, sizeof(b.raw));
            memcpy(b.data.data, ptr, n);
            pfs_crc32(crc, b.data.data, sizeof(b.data.data));
            ptr += n;
            sz -= n;

            // allocate next block
            if( sz ) {
                n = block_alloc(s, a);
                if( n < 0 ) {
                    return n;
                }
            } else {
                *pad = 31 - n;
                n = 255;
            }

            // write current block
            PFS_LOG("%d, ", i);
            b.data.next = n;
            pfs_write_block(s->bb[i].raw, b.raw, sizeof(b.raw) / 4);
            i = n;
        }
    }
    if( first < 0 ) {
        PFS_LOG("out of memory\n");
    } else {
        PFS_LOG("complete\n");
    }
    return first;
}

typedef struct {
    unsigned char* ptr;
    int sz;
    int sact;
} cinfo;

static int cb_cmp (pfs* s, int n, void* ctx) {
    cinfo* ci = ctx;
    int sz = (ci->sz < 31) ? ci->sz : 31;
    int d = memcmp(ci->ptr, s->bb[n].data.data, sz);
    ci->ptr += sz;
    ci->sz -= sz;
    ci->sact += 31;
    return d;
}

static int chain_cmp (pfs* s, int fh, void* data, int sz) {
    cinfo ci = {
        .ptr = data,
        .sz = sz,
        .sact = 0
    };
    int j = (~(s->bb[fh].meta.p[0].w) == 0);
    int d = walk(s, s->bb[fh].meta.p[j].blk0, cb_cmp, &ci);
    if( d != 0 ) {
        return d;
    }
    return (ci.sact - s->bb[fh].meta.p[j].pad) - sz;
}

int pfs_save (pfs* s, const uint8_t* ufid, void* data, int sz) {
    uint32_t crc;
    pfs_alloc a = s->alloc;
    int fh, first, pad = 0; // initialize to appease compiler
    if( ((fh = pfs_find(s, ufid)) >= 0) ) {
        if( chain_cmp(s, fh, data, sz) == 0 ) {
            PFS_LOG("no change\n");
            return fh;
        }
    } else if( (fh = meta_create(s, &a, ufid)) < 0 ) {
        return fh;
    }
    pfs_crc32(&crc, NULL, 0);
    if( (first = chain_write(s, &a, data, sz, &pad, &crc)) < 0 ) {
        return first;
    }
    fbp p = {
        .blk0 = first,
        .pad = pad
    };
    meta_update(s, &a, fh, p.w, crc);
    s->alloc = a;
    return fh;
}

typedef struct {
    unsigned char* ptr;
    int sz;
    int sact;
} rinfo;

static int cb_read (pfs* s, int n, void* ctx) {
    rinfo* ri = ctx;
    int sz = (ri->sz < 31) ? ri->sz : 31;
    if( sz ) {
        memcpy(ri->ptr, s->bb[n].data.data, sz);
        ri->ptr += sz;
        ri->sz -= sz;
    }
    ri->sact += 31;
    return 0;
}

int pfs_read_fh (pfs* s, int fh, void* data, int sz) {
    if( isalloc(&s->alloc, fh)
            && s->bb[fh].meta.magic == MB_MAGIC ) {
        rinfo ri = {
            .ptr = data,
            .sz = sz,
            .sact = 0
        };
        int j = (~(s->bb[fh].meta.p[0].w) == 0);
        walk(s, s->bb[fh].meta.p[j].blk0, cb_read, &ri);
        return ri.sact - s->bb[fh].meta.p[j].pad;
    } else {
        return -1;
    }
}

int pfs_read (pfs* s, const uint8_t* ufid, void* data, int sz) {
    int fh;
    if( ((fh = pfs_find(s, ufid)) < 0) ) {
        return fh;
    }
    return pfs_read_fh(s, fh, data, sz);
}
