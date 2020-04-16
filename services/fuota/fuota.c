// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <string.h>

#include "fuota.h"
#include "fuota_hal.h"


// ------------------------------------------------
// Session state

struct _fuota_session {
    uint32_t magic;     // magic
    uint32_t sid;       // session id
    uint32_t chunk_ct;  // chunk count
    uint32_t chunk_nw;  // chunk size (in words)

    uint32_t complete;  // tainted when complete (all fragments received)
    uint32_t unpacking; // tainted when unpacking (this step is not atomic!)
    uint32_t done;      // tainted when completely done

    uint32_t* matrix;   // pointer to matrix
    uint32_t* blocks;   // pointer to data blocks
};

_Static_assert(sizeof(fuota_session) <= fuota_flash_pagesz,
        "fuota_session must fit into single Flash page");

#define FUOTA_MAGIC     0x03291982


// ------------------------------------------------
// Convenience flash operations

#define PAGE_NW         ((fuota_flash_pagesz) >> 2)

#if (fuota_flash_bitdefault == 0)
#define FLASH_UNTAINTED (0)
#else
#define FLASH_UNTAINTED (~0)
#endif

typedef struct {
    uint32_t* base;
    uint32_t off;
    uint32_t buf[PAGE_NW];
} bw_state;

static void buffered_write (bw_state* state, uint32_t* src, uint32_t nwords) {
    if (src == NULL) { // flush
        if (state->off > 0) {
            fuota_flash_write(state->base, state->buf, state->off, true);
        }
    } else {
        while (nwords > 0) {
            uint32_t n = PAGE_NW - state->off;
            if (n > nwords) {
                n = nwords;
            }
            nwords -= n;
            while (n-- > 0) {
                state->buf[state->off++] = *src++;
            }
            if (state->off == PAGE_NW) {
                fuota_flash_write(state->base, state->buf, PAGE_NW, true);
                state->base += PAGE_NW;
                state->off = 0;
            }
        }
    }
}

static void word_taint(void* addr) {
    uint32_t value = ~FLASH_UNTAINTED;
    fuota_flash_write(addr, &value, 1, false);
}


// ------------------------------------------------
// XOR operations

// buffered flash to ram
static void xor_bf2r (uint32_t* dest, uint32_t* src, uint32_t nwords, bw_state* state) {
    while (nwords-- > 0) {
        if (state && src >= state->base) {
            *dest++ ^= state->buf[(((uintptr_t) (src++)) >> 2) & (PAGE_NW - 1)];
        } else {
            *dest++ ^= fuota_flash_rd_u4(src++);
        }
    }
}

static void xor_f2r (uint32_t* dest, uint32_t* src, uint32_t nwords) {
    while (nwords-- > 0) {
        *dest++ ^= fuota_flash_rd_u4(src++);
    }
}

static void xor_mf2r (uint32_t* dest, uint32_t* src, uint32_t nwords) {
    while (nwords-- > 0) {
        *dest++ ^=
#if (fuota_flash_bitdefault != 0)
            ~
#endif
            fuota_flash_rd_u4(src++);
    }
}


// ------------------------------------------------
// Triangular matrix

// returns the bit's word index in a row
#define M_BITIDX(bit) ((bit) >> 5)

// returns the bit's word mask
#define M_BITMSK(bit) (1 << ((bit) & 31))

// returns non-zero if bit is set (i.e. 1)
#if (fuota_flash_bitdefault == 0)
#define M_ISSET(val,msk) (((val) & (msk)) != 0)
#else
#define M_ISSET(val,msk) (((val) & (msk)) == 0)
#endif

// returns the number of words for the row in the matrix
#define M_NWORDS(row) (((row) >> 5) + 1)

// returns the word offset (index) for the row in the matrix
static uint32_t m_offset (uint32_t row) {
    uint32_t d = row >> 5;
    uint32_t f = row & 31;
    uint32_t off;
    off  = (d * (d + 1)) << 4; // <=> ((d * (d + 1)) / 2) * 32 <=> (d'th triangle number * 32 words)
    off += (f * (d + 1));
    return off;
}

// return the row number based on the right-most "set" bit
static uint32_t m_rmb (uint32_t* row, uint32_t nwords) {
    while (nwords-- > 0) {
        uint32_t w = row[nwords];
        if (w != 0) {
            return ((nwords + 1) << 5) - (__builtin_clz(w) + 1);
        }
    }
    return UINT32_MAX;
}

// update the word index and mask for the previous bit
static inline void m_prev (uint32_t* idx, uint32_t* mask) {
    if (*mask & 1) {
        *idx -= 1;
    }
    *mask = (*mask >> 1) | (*mask << 31);
}

// check if matrix is complete
static uint32_t m_complete (uint32_t* matrix, uint32_t chunk_ct) {
    while (chunk_ct > 0) {
        if (fuota_flash_rd_u4(matrix + m_offset(chunk_ct--) - 1) == FLASH_UNTAINTED) {
            return 0;
        }
    }
    return 1;
}

// count completed rows in matrix
static uint32_t m_count (uint32_t* matrix, uint32_t chunk_ct) {
    uint32_t count = 0;
    while (chunk_ct > 0) {
        if (fuota_flash_rd_u4(matrix + m_offset(chunk_ct--) - 1) != FLASH_UNTAINTED) {
            count += 1;
        }
    }
    return count;
}


// ------------------------------------------------
// Check bits generator

// returns the number of checkbit words
#define G_WORDS(n)      ((n + 31) >> 5)

// 32bit pseudo hash
static uint32_t g_avalanche (uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

// generate checkbits for chunk
static void g_checkbits (uint32_t chunk_id, uint32_t* row, uint32_t chunk_ct) {
    uint32_t i, n = G_WORDS(chunk_ct);
    for (i = 0; i < n; i++) {
        row[i] = g_avalanche((chunk_id * n) + i);
    }
    uint32_t mask = M_BITMSK(chunk_ct) - 1;
    if (mask) {
        row[i - 1] &= mask;
    }
}


// ------------------------------------------------
// API

#define s_u4(f)         fuota_flash_rd_u4(&session->f)
#define s_u4ptr(f)      ((uint32_t*) fuota_flash_rd_ptr(&session->f))

static bool check_session (fuota_session* session) {
    return s_u4(magic) == FUOTA_MAGIC
        && !(s_u4(unpacking) != FLASH_UNTAINTED && s_u4(done) == FLASH_UNTAINTED);
}

size_t fuota_matrix_size (uint32_t chunk_ct, uint32_t chunk_nw) {
    uint32_t m_nw = m_offset(chunk_ct); // matrix size in words
    return (m_nw << 2);
}

void fuota_init (void* session, void* matrix, void* data, uint32_t sid,
        uint32_t chunk_ct, uint32_t chunk_nw) {
    fuota_session s;

    s.magic = FUOTA_MAGIC;
    s.sid = sid;
    s.chunk_ct = chunk_ct;
    s.chunk_nw = chunk_nw;

    s.complete = FLASH_UNTAINTED;
    s.unpacking = FLASH_UNTAINTED;
    s.done = FLASH_UNTAINTED;

    s.matrix = matrix;
    s.blocks = data;

    fuota_flash_write(session, &s, sizeof(fuota_session) >> 2, false);
}

void* fuota_unpack (fuota_session* session) {
    if (!check_session(session) || s_u4(complete) == FLASH_UNTAINTED) {
        return NULL;
    }
    if (s_u4(done) == FLASH_UNTAINTED) {
        word_taint(&session->unpacking);

        bw_state buffer;
        buffer.base = s_u4ptr(blocks);
        buffer.off = 0;
        uint32_t i;
        uint32_t chunk_nw = s_u4(chunk_nw);
        for (i = 0; i < s_u4(chunk_ct); i++) {
            uint32_t d[chunk_nw];
            fuota_flash_read(d, s_u4ptr(blocks) + (chunk_nw * i), chunk_nw);
            uint32_t* c = s_u4ptr(matrix) + m_offset(i);
            uint32_t j = i, idx = M_BITIDX(j), mask = M_BITMSK(j);
            while (j-- > 0) {
                m_prev(&idx, &mask);
                if (M_ISSET(fuota_flash_rd_u4(c + idx), mask)) {
                    xor_bf2r(d, s_u4ptr(blocks) + (chunk_nw * j), chunk_nw, &buffer);
                }
            }
            buffered_write(&buffer, d, chunk_nw);
        }
        buffered_write(&buffer, NULL, 0); // flush buffer to flash
        word_taint(&session->done);
    }
    return s_u4ptr(blocks);
}

static void matrix_write (void* dst, uint32_t* c, uint32_t nwords) {
#if (fuota_flash_bitdefault != 0)
    for (int i = 0; i < nwords; i++) {
        c[i] = ~c[i];
    }
#endif
    fuota_flash_write(dst, c, nwords, false);
}

int fuota_process (fuota_session* session, uint32_t chunk_id,
        unsigned char* chunk_buf) {
    // check if session is valid, or if we are already done
    int state = fuota_state(session, NULL, NULL, NULL, NULL);
    if( state != FUOTA_MORE ) {
        return state;
    }
    // get session parameters
    uint32_t chunk_ct = s_u4(chunk_ct);
    uint32_t chunk_nw = s_u4(chunk_nw);
    // copy chunk data to word-aligned buffer
    uint32_t d[chunk_nw];
    memcpy(d, chunk_buf, chunk_nw << 2);
    // generate checkbits
    uint32_t c[G_WORDS(chunk_ct)];
    g_checkbits(chunk_id, c, chunk_ct);
    // process against already received chunks
    uint32_t* matrix = s_u4ptr(matrix);
    uint32_t i = chunk_ct, idx = M_BITIDX(i), mask = M_BITMSK(i);
    while (i-- > 0) {
        m_prev(&idx, &mask);
        if ((c[idx] & mask)
                && M_ISSET(fuota_flash_rd_u4(matrix + m_offset(i) + idx), mask)) {
            // xor checkbits
            xor_mf2r(c, matrix + m_offset(i), idx + 1);
            // xor data block
            xor_f2r(d, s_u4ptr(blocks) + (chunk_nw * i), chunk_nw);
        }
    }
    if ((i = m_rmb(c, G_WORDS(chunk_ct))) < chunk_ct) {
        // store matrix row
        matrix_write(matrix + m_offset(i), c, M_NWORDS(i));
        // store block
        fuota_flash_write(s_u4ptr(blocks) + (chunk_nw * i), d, chunk_nw, false);
        // check if complete
        if (m_complete(matrix, chunk_ct)) {
            word_taint(&session->complete);
            return FUOTA_COMPLETE;
        }
    }

    return FUOTA_MORE;
}

int fuota_state (fuota_session* session, uint32_t* sid,
        uint32_t* chunk_ct, uint32_t* chunk_nw, uint32_t* complete_ct) {
    if( !check_session(session) ) {
        return FUOTA_ERROR;
    }
    if( sid ) {
        *sid = s_u4(sid);
    }
    if( chunk_ct ) {
        *chunk_ct = s_u4(chunk_ct);
    }
    if( chunk_nw ) {
        *chunk_nw = s_u4(chunk_nw);
    }
    if( complete_ct ) {
        *complete_ct = m_count(s_u4ptr(matrix), s_u4(chunk_ct));
    }
    if( s_u4(done) != FLASH_UNTAINTED ) {
        return FUOTA_UNPACKED;
    }
    if( s_u4(complete) != FLASH_UNTAINTED ) {
        return FUOTA_COMPLETE;
    }
    return FUOTA_MORE;
}

int fuota_check_state (fuota_session* session, uint32_t sid,
        uint32_t chunk_ct, uint32_t chunk_nw) {
    uint32_t c_sid, c_chunk_ct, c_chunk_nw;
    int state = fuota_state(session, &c_sid, &c_chunk_ct, &c_chunk_nw, NULL);
    if( state != FUOTA_ERROR &&
            (sid != c_sid || chunk_ct != c_chunk_ct || chunk_nw != c_chunk_nw) ) {
        state = FUOTA_ERROR;
    }
    return state;
}


// ------------------------------------------------
// Chunk Generator

#ifdef FUOTA_GENERATOR

// XOR - ram to ram
static void xor_r2r (uint32_t* dest, uint32_t* src, uint32_t nwords) {
    while (nwords-- > 0) {
        *dest++ ^= *src++;
    }
}

void fuota_gen_chunk (uint32_t* dst, uint32_t* src, uint32_t chunk_id,
        uint32_t chunk_ct, uint32_t chunk_nw) {
    uint32_t c[G_WORDS(chunk_ct)];
    g_checkbits(chunk_id, c, chunk_ct);
    memset(dst, 0x00, chunk_nw * 4);
    uint32_t i = chunk_ct, idx = M_BITIDX(i), mask = M_BITMSK(i);
    while (i-- > 0) {
        m_prev(&idx, &mask);
        if (c[idx] & mask) {
            xor_r2r(dst, src + (chunk_nw * i), chunk_nw);
        }
    }
}

#endif
