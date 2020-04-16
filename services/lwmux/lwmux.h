// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _lwmux_h_
#define _lwmux_h_

#include "lmic.h"

typedef void (*lwm_complete) (void);
typedef int (*lwm_jit_cb) (unsigned char* data, int dlen);

typedef struct {
    unsigned char* data;
    int dlen;
    int port;
    int confirmed;
    lwm_complete txcomplete;
    lwm_jit_cb jit_cb;
} lwm_txinfo;

typedef bool (*lwm_tx) (lwm_txinfo*);

typedef struct _lwm_job {
    unsigned int prio;
    lwm_tx txfunc;
    lwm_complete completefunc;
    struct _lwm_job* next;
} lwm_job;


enum {
    LWM_MODE_SHUTDOWN,
    LWM_MODE_NORMAL,
#ifdef LWM_SLOTTED
    LWM_MODE_SLOTTED,
#endif
};

#define LWM_PRIO_MIN 0
#define LWM_PRIO_MAX ~0

int lwm_getmode ();
void lwm_setmode (int mode);
unsigned int lwm_setpriority (unsigned int priority);

void lwm_request_send (lwm_job* job, unsigned int priority, lwm_tx txfunc);
bool lwm_clear_send (lwm_job* job);

void lwm_setadrprofile (int txPowAdj, const unsigned char* drlist, int n);

#ifdef LWM_SLOTTED
void lwm_slotparams (u4_t freq, dr_t dr, ostime_t interval, int slotsz, int missed_max, int timeouts_max);

extern unsigned int lwm_slot (void);
extern void lwm_bcn_setup (void);
#endif

enum {
    LWM_FLAG_ACK        = TXRX_ACK,
    LWM_FLAG_NAK        = TXRX_NACK,
    LWM_FLAG_DNW1       = TXRX_DNW1,
    LWM_FLAG_DNW2       = TXRX_DNW2,
    LWM_FLAG_PING       = TXRX_PING,
};
#define LWM_FLAG_MASK (LWM_FLAG_ACK | LWM_FLAG_NAK | LWM_FLAG_DNW1 | LWM_FLAG_DNW2 | LWM_FLAG_PING)
typedef void (*lwm_downlink) (int port, unsigned char* data, int dlen, unsigned int flags);
void lwm_process_dl (lwm_downlink dlfunc);

#endif
