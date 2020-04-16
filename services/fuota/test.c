// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "fuota.h"
#include "fuota_hal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <time.h>

#ifndef __x86_64__
#error "Simulation requires 64-bit platform"
#endif


// ------------------------------------------------
// Flash simulation

#define FLASH_SZ        (128 * 1024) // 128K
#define FLASH_PAGE_SZ   fuota_flash_pagesz

#define FLASH_WORD_CT   (FLASH_SZ >> 2)
#define FLASH_PAGE_CT   (FLASH_SZ / FLASH_PAGE_SZ)
#define FLASH_END       (FLASH.W + (FLASH_SZ / 4))

static union {
    uint32_t W[FLASH_SZ / 4];
    uint32_t P[FLASH_PAGE_CT][FLASH_PAGE_SZ / 4];
} FLASH;

// Fake flash addresses are non-canonical, i.e. they are not valid
// in amd64 virtual address space.
static uint32_t addr2word (void* ptr) {
    uintptr_t addr = (uintptr_t) ptr;
    assert((addr >> 32) == 0xdeadbeef);
    assert((addr & 3) == 0);
    return (addr & 0xffffffff) >> 2;
}

static void* word2addr (uint32_t word) {
    assert(word <= FLASH_WORD_CT);
    return (void*) ((0xdeadbeefULL << 32) | (word << 2));
}

void fuota_flash_write (void* _dst, void* _src, uint32_t nwords, bool erase) {
    assert((((uintptr_t) _src) & 3) == 0);
    uint32_t* src = _src;
    uint32_t w = addr2word(_dst);
    assert((w + nwords) <= FLASH_WORD_CT);
    uint32_t* dst = FLASH.W + w;
    int i;
    for (i = 0; i < nwords; i++) {
        if (((w++ << 2) & (FLASH_PAGE_SZ-1)) == 0 && erase) {
            memset(dst + i, (fuota_flash_bitdefault) ? 0xff : 0x00, FLASH_PAGE_SZ);
        }
        assert(dst[i] == ((fuota_flash_bitdefault) ? ~0 : 0));
        dst[i] = src[i];
    }
}

void fuota_flash_read (void* dst, void* src, uint32_t nwords) {
    uint32_t w = addr2word(src);
    assert((w + nwords) <= FLASH_WORD_CT);
    memcpy(dst, FLASH.W + w, nwords << 2);
}

uint32_t fuota_flash_rd_u4 (void* addr) {
    uint32_t w = addr2word(addr);
    assert(w < FLASH_WORD_CT);
    return(FLASH.W[w]);
}

void* fuota_flash_rd_ptr (void* addr) {
    uint32_t w = addr2word(addr);
    assert(w < FLASH_WORD_CT);
    return *((void**) (FLASH.W + w));
}


// ------------------------------------------------
// Testing

static uint32_t readfile (unsigned char** pbuf, const char* fn, uint32_t chunk_nw) {
    FILE* f = fopen(fn, "rb");
    if (f == NULL) {
        perror("Error opening file");
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t chunk_sz = chunk_nw << 2;
    uint32_t msize = ((fsize + (chunk_sz - 1)) / (chunk_sz)) * chunk_sz;
    *pbuf = malloc(msize);
    assert(*pbuf);
    size_t br = fread(*pbuf, 1, fsize, f);
    assert(br == fsize);
    fclose(f);
    if (msize > fsize) {
        printf("Note: padding input file with %d bytes to full chunk size\n",
                (int) (msize - fsize));
        memset(*pbuf + fsize, 0, msize - fsize);
    }
    return msize >> 2;
}

int main (int argc, char** argv) {
    if (argc != 2) {
        printf("usage: %s <FILE>\n", argv[0]);
        return 1;
    }

    memset(&FLASH, 0xa5, sizeof(FLASH));

    uint32_t chunk_nw = 60; // 60 words = 240 bytes

    unsigned char* inbuf;
    uint32_t data_nw = readfile(&inbuf, argv[1], chunk_nw);
    uint32_t chunk_ct = data_nw / chunk_nw;

    printf("chunk size:   %6d words (%d bytes)\n", chunk_nw, chunk_nw << 2);
    printf("chunk count:  %6d\n", chunk_ct);
    printf("file size:    %6d bytes\n", (chunk_ct * chunk_nw) << 2);
    uint32_t dnp = (((chunk_ct * chunk_nw) << 2) + (FLASH_PAGE_SZ-1)) / FLASH_PAGE_SZ;
    printf("              %6d pages (%d bytes)\n", dnp, dnp * FLASH_PAGE_SZ);

    size_t ms = fuota_matrix_size(chunk_ct, chunk_nw);
    printf("matrix size:  %d bytes\n", (uint32_t) ms);
    uint32_t mnp = (ms + (FLASH_PAGE_SZ-1)) / FLASH_PAGE_SZ;
    printf("              %6d pages (%d bytes)\n", mnp, mnp * FLASH_PAGE_SZ);

    size_t ss = FLASH_PAGE_SZ;
    printf("session size: %d bytes\n", (uint32_t) ss);
    uint32_t snp = (ss + (FLASH_PAGE_SZ-1)) / FLASH_PAGE_SZ;
    printf("              %6d pages (%d bytes)\n", snp, snp * FLASH_PAGE_SZ);

    assert((mnp + dnp + snp) < FLASH_PAGE_CT);
    // Flash:  |........<matrix><data><session>|
    uint32_t mw = (FLASH_PAGE_CT - (mnp + dnp + snp)) * (FLASH_PAGE_SZ >> 2);
    uint32_t dw = (FLASH_PAGE_CT - (dnp + snp)) * (FLASH_PAGE_SZ >> 2);
    uint32_t sw = (FLASH_PAGE_CT - (snp)) * (FLASH_PAGE_SZ >> 2);

    // erase session pages
    memset(FLASH.W + mw, (fuota_flash_bitdefault) ? 0xff : 0x00, mnp * FLASH_PAGE_SZ);
    memset(FLASH.W + dw, (fuota_flash_bitdefault) ? 0xff : 0x00, dnp * FLASH_PAGE_SZ);
    memset(FLASH.W + sw, (fuota_flash_bitdefault) ? 0xff : 0x00, snp * FLASH_PAGE_SZ);

    void* matrix = word2addr(mw);
    void* data = word2addr(dw);
    void* session = word2addr(sw);
    printf("matrix addr:  %p\n", matrix);
    printf("data addr:    %p\n", data);
    printf("session addr: %p\n", session);

    fuota_init(session, matrix, data, 0x123, chunk_ct, chunk_nw);
    fuota_session* s = session;

    srand(time(NULL));

    uint32_t chunk_id = rand();
    uint32_t total = 0;
    uint32_t cc = 0;
    while (1) {
        uint32_t chunk[chunk_nw];
        // skip random number of chunks (simulate packet loss)
        chunk_id += (rand() % 10) + 1;
        // generate a new chunk
        fuota_gen_chunk(chunk, (uint32_t*) inbuf, chunk_id, chunk_ct, chunk_nw);
        // process chunk
        int rv = fuota_process(s, chunk_id, (unsigned char*) chunk);
        assert(rv != FUOTA_ERROR);
        total += 1;
        // get complete count
        uint32_t cc2;
        int rv2 = fuota_state(s, NULL, NULL, NULL, &cc2);
        assert(rv2 == rv);
        printf("processed: 0x%08x: %3d/%d%s\n", chunk_id, cc2, chunk_ct,
                (cc2 == cc) ? " *" : "");
        cc = cc2;
        if (rv == FUOTA_COMPLETE) {
            assert(cc == chunk_ct);
            printf("complete, %d chunks processed (%d useless)\n", total, total - chunk_ct);
            break;
        }
        assert(rv == FUOTA_MORE);
        assert(cc != chunk_ct);
    }

    void* outbuf = fuota_unpack(s);
    assert(outbuf);
    assert(outbuf == data);

    int diff = memcmp(inbuf, FLASH.W + addr2word(outbuf), chunk_ct * chunk_nw * 4);
    assert(!diff);

    printf("all done!\n");

    return 0;
}
