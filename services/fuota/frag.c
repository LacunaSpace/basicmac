// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

// This service is an implementation of the LoRaWAN™ Fragmented Data Block
// Transport Specification v1.0.0.

#include <string.h>
#include <stdint.h>

#include "fuota/fuota.h"
#include "fuota/fuota_hal.h"
#include "lwmux/lwmux.h"
#include "eefs/eefs.h"
#include "frag.h"

#include "svcdefs.h"

#ifndef SVC_FRAG_PORT
#define SVC_FRAG_PORT 201
#endif

// 16b75e2c8ff85440-7414ee53
static const uint8_t UFID_FRAG_SESSION[12] = { 0x40, 0x54, 0xf8, 0x8f, 0x2c, 0x5e, 0xb7, 0x16, 0x53, 0xee, 0x14, 0x74 };

const char* _frag_eefs_fn (const uint8_t* ufid) {
    if( memcmp(ufid, UFID_FRAG_SESSION, sizeof(UFID_FRAG_SESSION)) == 0 ) {
        return "com.semtech.svc.frag.session";
    }
    return NULL;
}

enum {
    SESSION_MAX = 4,            // 4 concurrent sessions are supported
};

enum {
    PKG_ID      = 3,
    PKG_VERSION = 1,
};

enum {
    PKG_VERSION_REQ     = 0x00,
    PKG_VERSION_ANS     = 0x00,
    FRAG_STATUS_REQ     = 0x01,
    FRAG_STATUS_ANS     = 0x01,
    FRAG_SESS_SETUP_REQ = 0x02,
    FRAG_SESS_SETUP_ANS = 0x02,
    FRAG_SESS_DEL_REQ   = 0x03,
    FRAG_SESS_DEL_ANS   = 0x03,
    DATA_FRAGMENT       = 0x08,
#ifdef SVC_FRAG_TEST
    FRAG_HASH_REQ       = 0x80, // proprietary test command
    FRAG_HASH_ANS       = 0x80, // proprietary test command
#endif
};

enum {
    FRAG_ALGO_STDFEC    = 0,
    FRAG_ALGO_TRACKNET  = 1,
};

enum {
    SSA_STAT_ENC        = (1 << 0), // encoding not supported
    SSA_STAT_MEM        = (1 << 1), // not enough memory
    SSA_STAT_IDX        = (1 << 2), // session index not supported
    SSA_STAT_DSC        = (1 << 3), // wrong descriptor
};

enum {
    SDA_STAT_IDX        = (1 << 2), // session index not supported
};

typedef struct {
    struct {
        void* abeg;             // pointer to storage (NULL if unused)
        void* aend;             // pointer to end of storage (NULL if unused)
        uint32_t desc;          // descriptor
        uint16_t fcnt;          // fragment count (NbFrag)
        uint8_t fsz;            // fragment size (FragSize)
        uint8_t pad;            // number of padding bytes (Padding)
        uint8_t mcmask;         // multicast mask (McGroupBitMask)
        uint8_t algo;           // fragmentation algorithm (FragAlgo)
        uint8_t delay;          // ack delay (BlockAckDelay)
    } sessions[SESSION_MAX];
} pstate;

static struct {
    unsigned char resp[64];     // response buffer
    int rlen;                   // response length

    lwm_job lwmjob;             // uplink job

    struct {
        void* beg;              // beginning of storage area
        void* end;              // end of storage area
    } storage[SESSION_MAX];

    pstate ps;                  // persistent state (stored in EEFS)
} state;

// ensure that there is at least n bytes available in the
// response buffer -- if not, drop older responses.
static void resp_makeroom (int n) {
    int skip = 0;
    int rlen = state.rlen;
    while( sizeof(state.resp) - rlen < n ) {
        int cmd = state.resp[skip];
        int csz = ((cmd == PKG_VERSION_ANS) ? 3
                :  (cmd == FRAG_STATUS_ANS) ? 5
                :                             2);
        skip += csz;
        rlen -= csz;
    }
    if( skip ) {
        memmove(state.resp, state.resp + skip, rlen);
        state.rlen = rlen;
    }
}

static fuota_session* get_session (int idx) {
    return (fuota_session*) (((uintptr_t) state.ps.sessions[idx].aend)
            - fuota_flash_pagesz);
}

#define ROUND_PAGE_SZ(sz) \
    (((sz) + (fuota_flash_pagesz - 1)) & ~(fuota_flash_pagesz - 1))
static int calc_session_size (uint32_t cct, uint32_t cnw, int* pmsz, int* pdsz) {
    int msz = ROUND_PAGE_SZ(fuota_matrix_size(cct, cnw));
    int dsz = ROUND_PAGE_SZ(cct * (cnw << 2));
    if( pmsz ) {
        *pmsz = msz;
    }
    if( pdsz ) {
        *pdsz = dsz;
    }
    return msz + dsz + fuota_flash_pagesz;
}

void _frag_restore (void) {
    if( eefs_read(UFID_FRAG_SESSION, &state.ps, sizeof(pstate)) != sizeof(pstate) ) {
        memset(&state.ps, 0, sizeof(pstate));
    }
    for( int i = 0; i < SESSION_MAX; i++ ) {
        if( state.storage[i].beg ) {
            void* beg = state.ps.sessions[i].abeg;
            void* end = state.ps.sessions[i].aend;
            if( beg ) {
                if( beg < end
                        && beg >= state.storage[i].beg
                        && end <= state.storage[i].end
                        && (state.ps.sessions[i].fsz & 3) == 0
                        && ((uintptr_t) end - (uintptr_t) beg) == calc_session_size(
                            state.ps.sessions[i].fcnt, state.ps.sessions[i].fsz >> 2,
                            NULL, NULL)
                        && fuota_check_state(get_session(i),
                            state.ps.sessions[i].desc, state.ps.sessions[i].fcnt,
                            state.ps.sessions[i].fsz >> 2) != FUOTA_ERROR ) {
                    // TODO - should check all saved parameters
                    debug_printf("frag: recovered session 0x%08x\r\n",
                            state.ps.sessions[i].desc);
                } else {
                    goto invalid;
                }
            }
        } else {
invalid:
            state.ps.sessions[i].abeg = NULL;
            state.ps.sessions[i].aend = NULL;
        }
    }
    eefs_save(UFID_FRAG_SESSION, &state.ps, sizeof(pstate));
}

void _frag_init (int nsessions, void** sbeg, void** send) {
    for( int i = 0; i < SESSION_MAX; i++ ) {
        if( i < nsessions ) {
            debug_printf("frag: init session storage %d (%d bytes)\r\n",
                    i, ((uintptr_t) send[i] - (uintptr_t) sbeg[i]));
            state.storage[i].beg = sbeg[i];
            state.storage[i].end = send[i];
        }
    }
}

int frag_get (int idx, void** pdata) {
    fuota_session* fs;
    uint32_t cct, cnw;
    void* ptr;
    if( idx < SESSION_MAX && state.ps.sessions[idx].abeg != NULL
            && fuota_state((fs = get_session(idx)), NULL, &cct, &cnw, NULL) != FUOTA_ERROR
            && (ptr = fuota_unpack(fs)) != NULL ) {
        *pdata = ptr;
        return (cct * (cnw << 2)) - state.ps.sessions[idx].pad;
    } else {
        return -1;
    }
}

static bool txfunc (lwm_txinfo* txi) {
    txi->port = SVC_FRAG_PORT;
    txi->data = state.resp;
    txi->dlen = state.rlen;
    state.rlen = 0;
    return true;
}

static int pkg_version_req (void) {
    resp_makeroom(3);
    state.resp[state.rlen++] = PKG_VERSION_ANS;
    state.resp[state.rlen++] = PKG_ID;
    state.resp[state.rlen++] = PKG_VERSION;
    return 1;
}

static void frag_status_ans (int idx) {
    uint32_t total, complete;
    if( idx >= SESSION_MAX || state.ps.sessions[idx].abeg == NULL
            || fuota_state(get_session(idx), NULL,
                &total, NULL, &complete) == FUOTA_ERROR ) {
        total = complete = 0;
    }
    resp_makeroom(5);
    idx = (idx << 14) | complete; // Received&index
    state.resp[state.rlen++] = FRAG_STATUS_ANS;
    state.resp[state.rlen++] = idx;
    state.resp[state.rlen++] = idx >> 8;
    int m = total - complete; // MissingFrag
    state.resp[state.rlen++] = (m < 255) ? m : 255;
    state.resp[state.rlen++] = 0; // Status
}

static int frag_status_req (unsigned char* data, int dlen) {
    if( dlen < 2 ) {
        return -1;
    }
    int idx = (data[1] >> 1) & 3;
    frag_status_ans(idx);
    return 2;
}

#ifdef SVC_FRAG_TEST
static int frag_hash_req (unsigned char* data, int dlen) {
    if( dlen < 2 ) {
        return -1;
    }
    int len;
    void* ptr;
    if( (len = frag_get(data[1] & 3, &ptr)) >= 0 ) {
        uint32_t hash[8];
        sha256(hash, ptr, len);
        resp_makeroom(33);
        state.resp[state.rlen++] = FRAG_HASH_ANS;
        memcpy(state.resp + state.rlen, hash, 32);
        state.rlen += 32;
    }
    return 2;
}
#endif

static int frag_sess_setup_req (unsigned char* data, int dlen) {
    if( dlen < 11 ) {
        return -1;
    }
    // 1:idx, 2-3:cct, 4:csz, 5:ctl, 6:pad, 7-10:dsc
    int idx = (data[1] >> 4) & 3;
    int status = idx << 6;
    if( idx >= SESSION_MAX || state.storage[idx].beg == NULL ) {
        status |= SSA_STAT_IDX;
    } else if( state.ps.sessions[idx].abeg ) {
        // already allocated -- spec is ambiguous, we'll return an error here
        status |= SSA_STAT_IDX | SSA_STAT_MEM;
    } else if( ((data[5] >> 3) & 3) != FRAG_ALGO_TRACKNET
            || (data[4] & 3) != 0 ) {
        status |= SSA_STAT_ENC;
    } else {
        int cct = os_rlsbf2(data+2);
        int cnw = data[4] >> 2;

        int msz, dsz;
        int size = calc_session_size(cct, cnw, &msz, &dsz);

        if( size > ((uintptr_t) state.storage[idx].end
                - (uintptr_t) state.storage[idx].beg) ) {
            status |= SSA_STAT_MEM;
        } else {
            state.ps.sessions[idx].abeg   = (void*) ((uintptr_t) state.storage[idx].end - size);
            state.ps.sessions[idx].aend   = state.storage[idx].end;
            state.ps.sessions[idx].desc   = os_rlsbf4(data+7);
            state.ps.sessions[idx].fcnt   = cct;
            state.ps.sessions[idx].fsz    = cnw << 2;
            state.ps.sessions[idx].pad    = data[6];
            state.ps.sessions[idx].mcmask = data[1] & 7;
            state.ps.sessions[idx].algo   = (data[5] >> 3) & 3;
            state.ps.sessions[idx].delay  = (data[5] >> 0) & 3;

            fuota_session* fs = get_session(idx);
            void* mtrx = (void*) ((uintptr_t) fs - (dsz + msz));
            void* cdat = (void*) ((uintptr_t) fs - (dsz      ));
            // erase pages
            flash_write(mtrx, NULL, size >> 2, true);
            // initialize state
            fuota_init(fs, mtrx, cdat, state.ps.sessions[idx].desc, cct, cnw);
            // save state to eeprom
            eefs_save(UFID_FRAG_SESSION, &state.ps, sizeof(pstate));
        }
    }
    resp_makeroom(2);
    state.resp[state.rlen++] = FRAG_SESS_SETUP_ANS;
    state.resp[state.rlen++] = status;
    return 11;
}

static int frag_sess_del_req (unsigned char* data, int dlen) {
    if( dlen < 2 ) {
        return -1;
    }
    int idx = (data[1] >> 0) & 3;
    int status = idx;
    if( idx >= SESSION_MAX || state.ps.sessions[idx].abeg == NULL) {
        status |= SDA_STAT_IDX;
    } else {
        state.ps.sessions[idx].abeg = NULL;
        state.ps.sessions[idx].aend = NULL;
        // save state to eeprom
        eefs_save(UFID_FRAG_SESSION, &state.ps, sizeof(pstate));
    }
    resp_makeroom(2);
    state.resp[state.rlen++] = FRAG_SESS_DEL_ANS;
    state.resp[state.rlen++] = status;
    return 2;
}

static int data_fragment (unsigned char* data, int dlen) {
    if( dlen > 3 ) {
        int idx_n = os_rlsbf2(data + 1);
        int idx = idx_n >> 14;
        int cid = idx_n & 0x3fff;
        if( idx < SESSION_MAX && state.storage[idx].beg != NULL ) {
            uint32_t cnw;
            fuota_session* fs = get_session(idx);
            if( fuota_state(fs, NULL, NULL, &cnw, NULL) != FUOTA_ERROR
                    && dlen >= (cnw << 2)) {
                fuota_process(fs, cid, data + 3);

                // TODO - only generate status uplink on unicast
                frag_status_ans(idx);

                return 3 + (cnw << 2);
            }
        }
    }
    return -1;
}

void _frag_dl (int port, unsigned char* data, int dlen, unsigned int flags) {
    if( port == SVC_FRAG_PORT ) {
        while( dlen ) {
            int n;
            switch( *data ) {
                case PKG_VERSION_REQ:
                    n = pkg_version_req();
                    break;

                case FRAG_STATUS_REQ:
                    n = frag_status_req(data, dlen);
                    break;

                case FRAG_SESS_SETUP_REQ:
                    n = frag_sess_setup_req(data, dlen);
                    break;

                case FRAG_SESS_DEL_REQ:
                    n = frag_sess_del_req(data, dlen);
                    break;

                case DATA_FRAGMENT:
                    n = data_fragment(data, dlen);
                    break;

#ifdef SVC_FRAG_TEST
                case FRAG_HASH_REQ:
                    n = frag_hash_req(data, dlen);
                    break;
#endif

                default:
                    // unknown command -- abort processing
                    goto done;
            }
            if( n < 0 ) {
                goto done;
            }
            data += n;
            dlen -= n;
        }
done:
        if( state.rlen ) {
            lwm_request_send(&state.lwmjob, 0, txfunc);
        }
    }
}
