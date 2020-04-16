// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _fuota_h_
#define _fuota_h_

#include <stddef.h>
#include <stdint.h>

enum {
    FUOTA_MORE          = 0,
    FUOTA_COMPLETE      = 1,
    FUOTA_UNPACKED      = 2,
    FUOTA_ERROR         = -1,
};

struct _fuota_session;
typedef struct _fuota_session fuota_session;

// return the required matrix size
// - chunk_ct: chunk count
// - chunk_nw: number of 4-byte words per chunk
size_t fuota_matrix_size (uint32_t chunk_ct, uint32_t chunk_nw);

// initialize a session
// - session:   pointer to a single flash page that will hold the session state
// - matrix:    page-aligned pointer to flash area large enough to
//              hold matrix; use fuota_matrix_size() to determine min. size
// - data:      page-aligned pointer to flash area large enough to
//              hold chunk data; at least chunk_ct*chunk_nw*4
// - sid:       application specific session identifier
// - chunk_ct:  chunk count
// - chunk_nw:  number of 4-byte words per chunk
// NOTE: All flash areas passed to this function (session, matrix, data) must
//       be in erased, unwritten condition.
void fuota_init (void* session, void* matrix, void* data, uint32_t sid,
        uint32_t chunk_ct, uint32_t chunk_nw);

// process a chunk
// - session:   pointer to session
// - chunk_id:  chunk identifier
// - chunk_buf: buffer to chunk data (does not need to be aligned)
int fuota_process (fuota_session* session, uint32_t chunk_id,
        unsigned char* chunk_buf);

// get current session state
int fuota_state (fuota_session* session, uint32_t* sid,
        uint32_t* chunk_ct, uint32_t* chunk_nw, uint32_t* complete_ct);

// convenience function to check if a session has the expected parameters
int fuota_check_state (fuota_session* session, uint32_t sid,
        uint32_t chunk_ct, uint32_t chunk_nw);

// unpack the received chunks and recover the original file
// - session:   pointer to session
void* fuota_unpack (fuota_session* session);

#ifdef FUOTA_GENERATOR
void fuota_gen_chunk (uint32_t* dst, uint32_t* src, uint32_t chunk_id,
        uint32_t chunk_ct, uint32_t chunk_nw);
#endif

#endif
