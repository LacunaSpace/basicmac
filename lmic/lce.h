// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _lce_h_
#define _lce_h_

#include "oslmic.h"

#ifdef __cplusplus
extern "C"{
#endif

// Some keyids:
#define LCE_APPSKEY   (-2)
#define LCE_NWKSKEY   (-1)
#define LCE_MCGRP_0   ( 0)
#define LCE_MCGRP_MAX ( 2)

// Stream cipher categories (lce_cipher(..,cat,..):
// Distinct use of the AppSKey must use different key classes
// or plain text will leak:
enum {
    LCE_SCC_UP   = 0,     // std LoRaWAN uplink frame
    LCE_SCC_DN   = 1,     // std LoRaWAN downlink frame
    LCE_SCC_FUP  = 0x40,  // file upload
    LCE_SCC_DSE  = 0x41,  // data streaming engine
    LCE_SCC_ROSE = 0x42,  // reliable octet streaming engine
};

void lce_encKey0 (u1_t* buf);
u4_t lce_micKey0 (u4_t devaddr, u4_t seqno, u1_t* pdu, int len);
bool lce_processJoinAccept (u1_t* jacc, u1_t jacclen, u2_t devnonce);
void lce_addMicJoinReq (u1_t* pdu, int len);
bool lce_verifyMic (s1_t keyid, u4_t devaddr, u4_t seqno, u1_t* pdu, int len);
void lce_addMic (s1_t keyid, u4_t devaddr, u4_t seqno, u1_t* pdu, int len);
void lce_cipher (s1_t keyid, u4_t devaddr, u4_t seqno, int cat, u1_t* payload, int len);
#if defined(CFG_lorawan11)
void lce_loadSessionKeys (const u1_t* nwkSKey, const u1_t* nwkSKeyDn, const u1_t* appSKey);
#else
void lce_loadSessionKeys (const u1_t* nwkSKey, const u1_t* appSKey);
#endif
void lce_init (void);


typedef struct lce_ctx_mcgrp {
    u1_t nwkSKeyDn[16]; // network session key for down-link
    u1_t appSKey[16];   // application session key
} lce_ctx_mcgrp_t;

typedef struct lce_ctx {
    u1_t nwkSKey[16];   // network session key (LoRaWAN1.1: up-link only)
#if defined(CFG_lorawan11)
    u1_t nwkSKeyDn[16]; // network session key for down-link
#endif
    u1_t appSKey[16];   // application session key
    lce_ctx_mcgrp_t mcgroup[LCE_MCGRP_MAX];
} lce_ctx_t;


#ifdef __cplusplus
} // extern "C"
#endif

#endif // _lce_h_
