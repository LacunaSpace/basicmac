// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

// This service is an implementation of the LoRaWAN™ Firmware Management
// Protocol Specification Release Candidate 2

#include <string.h>
#include <stdint.h>

#include "bootloader.h"

#include "lwmux/lwmux.h"
#include "fuota/micro-ecc/uECC.h"

#include "frag.h"

#ifndef SVC_FWMAN_PORT
#define SVC_FWMAN_PORT 203
#endif

#ifndef SVC_FWMAN_UPDATE_FRAG_IDX
#define SVC_FWMAN_UPDATE_FRAG_IDX 0
#endif

#ifdef SVC_FWMAN_PUBKEY
extern const unsigned char* SVC_FWMAN_PUBKEY (void);
#else
#error "SVC_FWMAN_PUBKEY function (trusted FUOTA public key) must be defined"
#endif

#ifdef SVC_FWMAN_CURVE
extern uECC_Curve SVC_FWMAN_CURVE (void);
#else
#error "SVC_FWMAN_CURVE function (ECC curve used trusted FUOTA public key) must be defined"
#endif

enum {
    PKG_ID      = 4,
    PKG_VERSION = 1,
};

enum {
    PKG_VERSION_REQ     = 0x00,
    PKG_VERSION_ANS     = 0x00,
    DEV_VERSION_REQ     = 0x01,
    DEV_VERSION_ANS     = 0x01,
    DEV_REBOOT_TIME_REQ = 0x02,
    DEV_REBOOT_TIME_ANS = 0x02,
    DEV_REBOOT_CTDN_REQ = 0x03,
    DEV_REBOOT_CTDN_ANS = 0x03,
    DEV_UPGRADE_IMG_REQ = 0x04,
    DEV_UPGRADE_IMG_ANS = 0x04,
    DEV_DELETE_IMG_REQ  = 0x05,
    DEV_DELETE_IMG_ANS  = 0x05,
};

static const char ANS_LENS[] = {
    [PKG_VERSION_ANS]     = 3,
    [DEV_VERSION_ANS]     = 9,
    [DEV_REBOOT_TIME_ANS] = 5,
    [DEV_REBOOT_CTDN_ANS] = 4,
    [DEV_UPGRADE_IMG_ANS] = 2,  // +4 (conditional)
    [DEV_DELETE_IMG_ANS]  = 2,
};

enum {
    DUI_STAT_NONE     = 0, // no image present
    DUI_STAT_INVALID  = 1, // image is corrupt
    DUI_STAT_MISMATCH = 2, // image is not compatible
    DUI_STAT_VALID    = 3, // image is valid and can be installed
};

static struct {
    unsigned char resp[64];     // response buffer
    int rlen;                   // response length

    lwm_job lwmjob;             // uplink job
    osjob_t rebootjob;          // reboot job
} state;

// ensure that there is at least n bytes available in the
// response buffer -- if not, drop older responses.
static void resp_makeroom (int n) {
    int skip = 0;
    int rlen = state.rlen;
    while( sizeof(state.resp) - rlen < n ) {
        int cmd = state.resp[skip];
        ASSERT(cmd < sizeof(ANS_LENS));
        int csz = ANS_LENS[cmd];
        if( cmd == DEV_UPGRADE_IMG_ANS && state.resp[skip+1] == DUI_STAT_VALID ) {
            csz += 4;
        }
        skip += csz;
        rlen -= csz;
    }
    if( skip ) {
        memmove(state.resp, state.resp + skip, rlen);
        state.rlen = rlen;
    }
}

static bool txfunc (lwm_txinfo* txi) {
    txi->port = SVC_FWMAN_PORT;
    txi->data = state.resp;
    txi->dlen = state.rlen;
    state.rlen = 0;
    return true;
}

static int pkg_version_req (void) {
    resp_makeroom(ANS_LENS[PKG_VERSION_ANS]);
    state.resp[state.rlen++] = PKG_VERSION_ANS;
    state.resp[state.rlen++] = PKG_ID;
    state.resp[state.rlen++] = PKG_VERSION;
    return 1;
}

static int dev_version_req (void) {
    hal_fwi fwi;
    hal_fwinfo(&fwi);

    resp_makeroom(ANS_LENS[DEV_VERSION_ANS]);
    state.resp[state.rlen++] = DEV_VERSION_ANS;
    os_wlsbf4(state.resp + state.rlen, fwi.crc);
    state.rlen += 4;
    os_wlsbf4(state.resp + state.rlen, hal_hwid());
    state.rlen += 4;

    return 1;
}

static bool check_sig (void* ptr, int len) {
    const unsigned char* pubkey = SVC_FWMAN_PUBKEY();
    uECC_Curve curve = SVC_FWMAN_CURVE();
    int sigsize = uECC_curve_private_key_size(curve) << 1;

    boot_uphdr* up = ptr;
    unsigned char* sig = ptr;
    uint32_t hash[8];
    sha256(hash, ptr, up->size);

    sig += up->size;
    len -= up->size;
    while( len >= sigsize ) {
        if( uECC_verify(pubkey, (unsigned char*) hash, 32, sig, curve) == 1 ) {
            debug_str("fwman: signature verified\r\n");
            return true;
        } else {
            debug_str("fwman: signature invalid\r\n");
        }
        len -= sigsize;
        sig += sigsize;
    }
    return false;
}

static int check_img (uint32_t* pcrc, void** pdata) {
    void* ptr;
    int len;
    if( (len = frag_get(SVC_FWMAN_UPDATE_FRAG_IDX, &ptr)) < 0 ) {
        return DUI_STAT_NONE;
    } else {
        // check format, length, crc, and signatures
        boot_uphdr* up = ptr;
        if( len < sizeof(boot_uphdr)
                || (up->size & 3) != 0
                || len < up->size
                || crc32((unsigned char*) ptr + 8, (up->size - 8) >> 2) != up->crc
                || !check_sig(ptr, len) ) {
            return DUI_STAT_INVALID;
        } else {
            if( pcrc ) {
                *pcrc = up->fwcrc;
            }
            if( pdata ) {
                *pdata = ptr;
            }
            return DUI_STAT_VALID;
        }
    }
}

static void reboot (osjob_t* j) {
    void* ptr;
    if( check_img(NULL, &ptr) == DUI_STAT_VALID ) {
        if( hal_set_update(ptr) ) {
            debug_str("fwman: update registered\r\n");
        } else {
            debug_str("fwman: update registration failed\r\n");
        }
    }
    debug_str("fwman: rebooting...\r\n");
    hal_reboot();
}

static int dev_reboot_time_req (unsigned char* data, int dlen) {
    if( dlen < 5 ) {
        return -1;
    }
    uint32_t t = os_rlsbf4(data + 1);

    if( t == 0 ) {
        // reboot now (don't send answer)
        os_setCallback(&state.rebootjob, reboot);
    } else {
        if ( t == 0xffffffff ) {
            // cancel any pending reboot
            os_clearCallback(&state.rebootjob);
        } else {
            // exact time reboot not supported for now
            t = 0;
        }
        resp_makeroom(ANS_LENS[DEV_REBOOT_TIME_ANS]);
        state.resp[state.rlen++] = DEV_REBOOT_TIME_ANS;
        os_wlsbf4(state.resp + state.rlen, t);
        state.rlen += 4;
    }

    return 5;
}

static int dev_upgrade_img_req (void) {
    uint32_t crc;

    int status = check_img(&crc, NULL);

    resp_makeroom(ANS_LENS[DEV_UPGRADE_IMG_ANS] + ((status == DUI_STAT_VALID) ? 4 : 0));
    state.resp[state.rlen++] = DEV_UPGRADE_IMG_ANS;
    state.resp[state.rlen++] = status;
    if( status == DUI_STAT_VALID ) {
        os_wlsbf4(state.resp + state.rlen, crc);
        state.rlen += 4;
    }
    return 1;
}

void fwman_dl (int port, unsigned char* data, int dlen, unsigned int flags) {
    // TODO - only accept if unicast
    if( port == SVC_FWMAN_PORT ) {
        while( dlen ) {
            int n;
            switch( *data ) {
                case PKG_VERSION_REQ:
                    n = pkg_version_req();
                    break;

                case DEV_VERSION_REQ:
                    n = dev_version_req();
                    break;

                case DEV_REBOOT_TIME_REQ:
                    n = dev_reboot_time_req(data, dlen);
                    break;

                case DEV_UPGRADE_IMG_REQ:
                    n = dev_upgrade_img_req();
                    break;

                default:
                    // unknown command -- abort processing
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

#ifdef SVC_FWMAN_TEST
const unsigned char* fwman_testkey (void) {
    static const unsigned char pubkey[64] = {
        0xec, 0x70, 0x36, 0xe8, 0xf1, 0xa8, 0xd5, 0x74, 0x4c, 0x9f, 0xd9, 0xfc, 0x34, 0xdf, 0x43, 0xd8,
        0xff, 0x0b, 0xf0, 0x5b, 0xc0, 0xe6, 0x8e, 0xf9, 0x31, 0x40, 0xe8, 0x01, 0x72, 0xfd, 0x06, 0x8e,
        0x36, 0x86, 0x7c, 0x09, 0xa9, 0x28, 0x5e, 0xca, 0x0e, 0x88, 0x67, 0x4a, 0x28, 0x77, 0x34, 0xdc,
        0x04, 0x2e, 0x24, 0x42, 0x02, 0x8a, 0xc8, 0x3a, 0xb3, 0xd1, 0x5d, 0xaf, 0x3d, 0x2f, 0x0f, 0x07,
    };
    return pubkey;
}
#endif
