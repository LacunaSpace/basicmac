// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

//! \file
#ifndef _oslmic_h_
#define _oslmic_h_

#if !defined(CFG_simul)
#include "board.h"
#endif // !defined(CFG_simul)

// Dependencies required for the LoRa MAC in C to run.
// These settings can be adapted to the underlying system.
// You should not, however, change the lmic.[hc]

//================================================================================
// Target platform as C library
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t         bit_t;
typedef uint8_t         u1_t;
typedef int8_t          s1_t;
typedef uint16_t        u2_t;
typedef int16_t         s2_t;
typedef uint32_t        u4_t;
typedef int32_t         s4_t;
typedef uint64_t        u8_t;
typedef int64_t         s8_t;
typedef unsigned int    uint;
typedef const char*     str_t;

#include <string.h>
#if !defined(CFG_simul)
#include "debug.h"
#endif
#if !defined(CFG_noassert)
#if defined(CFG_simul) && defined(CFG_DEBUG)
#include <stdio.h>
#define ASSERT(cond) do { \
    if(!(cond)) { fprintf(stderr, "ASSERTION FAILED: %s at %s:%d\n", \
                          #cond, __FILE__, __LINE__); hal_failed(); } } while (0)
#elif defined(CFG_DEBUG)
#define ASSERT(cond) do { if(!(cond)) { hal_enableIRQs(); debug_printf("%s:%d: assertion failed\r\n", __FILE__, __LINE__); hal_failed(); } } while (0)
#else
#define ASSERT(cond) do { if(!(cond)) hal_failed(); } while (0)
#endif
#else
#define ASSERT(cond) do { } while (0)
#endif

#include <assert.h>
#if __cplusplus >= 201103L || defined(static_assert)
#define LMIC_STATIC_ASSERT static_assert
#else
#define LMIC_STATIC_ASSERT(expr, msg)
#endif

#ifdef __cplusplus
extern "C"{
#endif

#define os_max(a,b)                             \
    ({  __typeof__ (a) _a = (a);                \
        __typeof__ (b) _b = (b);                \
        _a > _b ? _a : _b; })

#define os_min(a,b)                             \
    ({  __typeof__ (a) _a = (a);                \
        __typeof__ (b) _b = (b);                \
        _a < _b ? _a : _b; })

#define os_minmax(vmin,v,vmax) (os_max(vmin,os_min(v,vmax)))
#define os_clearMem(a,b)   memset(a,0,b)
#define os_copyMem(a,b,c)  memcpy(a,b,c)
#define os_moveMem(a,b,c)  memmove(a,b,c)

#define ON_LMIC_EVENT(ev)  onLmicEvent(ev)
#define DECL_ON_LMIC_EVENT void onLmicEvent(ev_t e)

#define ON_BUDHA_EVENT(ev)  onBudhaEvent(ev)
#define DECL_ON_BUDHA_EVENT void onBudhaEvent(ev_t e)

#if defined(CFG_bootloader) && defined(CFG_bootloader_aes)
extern uint32_t (*AESFUNC) (uint8_t mode, uint8_t* buf, uint16_t len, uint32_t* key, uint32_t* aux);
#endif
extern u4_t AESAUX[];
extern u4_t AESKEY[];
#define AESkey ((u1_t*)AESKEY)
#define AESaux ((u1_t*)AESAUX)
#define FUNC_ADDR(func) (&(func))

#if defined(CFG_simul)
#define DEFINE_LMIC
#define DECLARE_LMIC extern struct lmic_t* plmic
#define LMIC (*(plmic))
#else
#define DEFINE_LMIC  struct lmic_t LMIC
#define DECLARE_LMIC extern struct lmic_t LMIC
#endif

#if defined(CFG_budha)
#if defined(CFG_simul)
#define DEFINE_BUDHA
#define DECLARE_BUDHA extern struct budha_t* pbudha
#define BUDHA (*(pbudha))
#else
#define DEFINE_BUDHA  struct lmic_t BUDHA
#define DECLARE_BUDHA extern struct budha_t BUDHA
#endif
#endif //defined(CFG_budha)

#if defined(CFG_dse)
#if defined(CFG_simul)
#define DEFINE_DSE
#define DECLARE_DSE extern struct dse_t* pdse
#define DSE (*(pdse))
#else
#define DEFINE_DSE  struct dse_t DSE
#define DECLARE_DSE extern struct dse_t DSE
#endif
#endif //defined(CFG_dse)

#if defined(CFG_rose)
#if defined(CFG_simul)
#define DEFINE_ROSE
#define DECLARE_ROSE extern struct rose_t* prose
#define ROSE (*(prose))
#else
#define DEFINE_ROSE  struct rose_t ROSE
#define DECLARE_ROSE extern struct rose_t ROSE
#endif
#endif //defined(CFG_rose)

#if defined(CFG_simul)
#define DEFINE_APP(t)
#define DECLARE_APP(t) extern void* pappdata
#define APP(t) (*(struct t*)pappdata)
#else
#define DEFINE_APP(t)  struct t APPDATA
#define DECLARE_APP(t) extern struct t APPDATA
#define APP(t) (APPDATA)
#endif

#define LOGCHECK(lvl,block) do {                \
        if( lvl <= log_lvl ) {                  \
            block;                              \
        }                                       \
    } while(0)
#if defined(CFG_simul)
extern int log_lvl;
void LOGIT(int lvl, char* fmt, ...);
#else
#define LOGIT(lvl, fmt, ...) debug_printf(fmt, ## __VA_ARGS__)
#endif
enum { EVCAT_ANY, EVCAT_BUDHA, EVCAT_MAX };
void os_logEv(uint8_t evcat, uint8_t evid, uint32_t evparam);

void os_init (void* bootarg);
void os_runstep (void);
void os_runloop (void);
u1_t os_getRndU1 (void);

//================================================================================

#ifndef RX_RAMPUP
#ifndef CFG_rxrampup
#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
#define RX_RAMPUP  (us2osticks(5000))
#elif defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)
#define RX_RAMPUP  (us2osticksCeil(2800))
#else
#define RX_RAMPUP  (0)
#endif
#else
#define RX_RAMPUP  (us2osticksCeil(CFG_rxrampup))
#endif
#endif
#ifndef TX_RAMPUP
#ifndef CFG_txrampup
#define TX_RAMPUP  (us2osticks(2000))
#else
#define TX_RAMPUP  (us2osticksCeil(CFG_txrampup))
#endif
#endif

#ifndef OSTICKS_PER_SEC
#define OSTICKS_PER_SEC 32768
#elif OSTICKS_PER_SEC < 10000 || OSTICKS_PER_SEC > 64516
#error Illegal OSTICKS_PER_SEC - must be in range [10000:64516]. One tick must be 15.5us .. 100us long.
#endif

typedef s4_t  ostime_t;
typedef s8_t  osxtime_t;

#define OSXTIME_MAX     INT64_MAX
#define OSTIME_MAX_DIFF INT32_MAX

#if !HAS_ostick_conv
#define us2osticks(us)   ((ostime_t)( ((s8_t)(us) * OSTICKS_PER_SEC) / 1000000))
#define ms2osticks(ms)   ((ostime_t)( ((s8_t)(ms) * OSTICKS_PER_SEC)    / 1000))
#define sec2osticks(sec) ((ostime_t)( (s8_t)(sec) * OSTICKS_PER_SEC))
#define osticks2sec(os)  ((s4_t)(((os)               ) / OSTICKS_PER_SEC))
#define osticks2ms(os)   ((s4_t)(((os)*(s8_t)1000    ) / OSTICKS_PER_SEC))
#define osticks2us(os)   ((s4_t)(((os)*(s8_t)1000000 ) / OSTICKS_PER_SEC))
// Special versions
#define us2osticksCeil(us)  ((ostime_t)( ((s8_t)(us) * OSTICKS_PER_SEC + 999999) / 1000000))
#define us2osticksRound(us) ((ostime_t)( ((s8_t)(us) * OSTICKS_PER_SEC + 500000) / 1000000))
#define ms2osticksCeil(ms)  ((ostime_t)( ((s8_t)(ms) * OSTICKS_PER_SEC + 999) / 1000))
#define ms2osticksRound(ms) ((ostime_t)( ((s8_t)(ms) * OSTICKS_PER_SEC + 500) / 1000))
#define osticks2secCeil(os) ((s4_t)(((os) + (OSTICKS_PER_SEC - 1)) / OSTICKS_PER_SEC))
// Extended versions
#define us2osxticks(us)   ((osxtime_t)( ((s8_t)(us) * OSTICKS_PER_SEC) / 1000000))
#define ms2osxticks(ms)   ((osxtime_t)( ((s8_t)(ms) * OSTICKS_PER_SEC)    / 1000))
#define sec2osxticks(sec) ((osxtime_t)( (s8_t)(sec) * OSTICKS_PER_SEC))
#endif

struct osjob_t; // fwd decl
typedef void (*osjobcb_t) (struct osjob_t*);
typedef struct osjob_t {
    struct osjob_t* next;
    ostime_t deadline;
    osjobcb_t  func;
    unsigned int flags;
#if defined(CFG_simul)
    void* ctx;
    int pqidx;
#endif
} osjob_t;

// extended os job wrapper for future events exceeding max range of ostime_t
typedef struct osxjob_t osxjob_t;
struct osxjob_t {
    osjob_t job;
    osxtime_t deadline;
    osjobcb_t func;
};

#include "hal.h"

#ifndef HAS_os_calls

#ifndef os_getNwkKey
void os_getNwkKey (u1_t* buf);
#endif
#ifndef os_getAppKey
void os_getAppKey (u1_t* buf);
#endif
#ifndef os_getJoinEui
void os_getJoinEui (u1_t* buf);
#endif
#ifndef os_getDevEui
void os_getDevEui (u1_t* buf);
#endif
#ifndef os_getRegion
u1_t os_getRegion (void);
#endif
#ifndef os_setTimedCallbackEx
enum {
    OSJOB_FLAG_APPROX      = (1 << 0), // actual time of job may be approximate
    OSJOB_FLAG_IRQDISABLED = (1 << 1), // IRQs will be disabled when job is run -- THE JOB MUST RE-ENABLE IRQs BY CALLING hal_enableIRQs() !!!
    OSJOB_FLAG_NOW         = (1 << 2), // job is immediately runnable (time parameter is ignored)
};
void os_setTimedCallbackEx (osjob_t* job, ostime_t time, osjobcb_t cb, unsigned int flags);
void os_setExtendedTimedCallback (osxjob_t* xjob, osxtime_t xtime, osjobcb_t cb);
// convenience functions (implemented as macros)
#define os_setCallback(job, cb) os_setTimedCallbackEx(job, 0, cb, OSJOB_FLAG_NOW)
#define os_setTimedCallback(job, time, cb) os_setTimedCallbackEx(job, time, cb, 0)
#define os_setApproxTimedCallback(job, time, cb) os_setTimedCallbackEx(job, time, cb, OSJOB_FLAG_APPROX)
#define os_setProtectedTimedCallback(job, time, cb) os_setTimedCallbackEx(job, time, cb, OSJOB_FLAG_IRQDISABLED)
#endif
#ifndef os_clearCallback
int os_clearCallback (osjob_t* job);
#endif
#ifndef os_getTime
ostime_t os_getTime (void);
#endif
#ifndef os_getXTime
osxtime_t os_getXTime (void);
#endif
#ifndef os_time2XTime
osxtime_t os_time2XTime (ostime_t t, osxtime_t context);
#endif
#ifndef os_getTimeSecs
uint os_getTimeSecs (void);
#endif
#ifndef os_radio
void os_radio (u1_t mode);
#endif
#ifndef os_getBattLevel
u1_t os_getBattLevel (void);
#endif

#ifndef os_rlsbf4
//! Read 32-bit quantity from given pointer in little endian byte order.
u4_t os_rlsbf4 (const u1_t* buf);
#endif
#ifndef os_wlsbf4
//! Write 32-bit quantity into buffer in little endian byte order.
void os_wlsbf4 (u1_t* buf, u4_t value);
#endif
#ifndef os_rmsbf4
//! Read 32-bit quantity from given pointer in big endian byte order.
u4_t os_rmsbf4 (const u1_t* buf);
#endif
#ifndef os_wmsbf4
//! Write 32-bit quantity into buffer in big endian byte order.
void os_wmsbf4 (u1_t* buf, u4_t value);
#endif
#ifndef os_rlsbf2
//! Read 16-bit quantity from given pointer in little endian byte order.
u2_t os_rlsbf2 (const u1_t* buf);
#endif
#ifndef os_wlsbf2
//! Write 16-bit quantity into buffer in little endian byte order.
void os_wlsbf2 (u1_t* buf, u2_t value);
#endif
#ifndef os_rmsbf2
//! Read 16-bit quantity from given pointer in big endian byte order.
u2_t os_rmsbf2 (const u1_t* buf);
#endif
#ifndef os_wmsbf2
//! Write 16-bit quantity into buffer in big endian byte order.
void os_wmsbf2 (u1_t* buf, u2_t value);
#endif
#ifndef os_wlsbf3
//! Write 24-bit quantity into buffer in little endian byte order.
void os_wlsbf3 (u1_t* buf, u4_t value);
#endif

//! Get random number (default impl for u2_t).
#ifndef os_getRndU2
#define os_getRndU2() ((u2_t)((os_getRndU1()<<8)|os_getRndU1()))
#endif
#ifndef os_crc16
u2_t os_crc16 (u1_t* d, uint len);
#endif

#if defined(CFG_budha) // XXX:obsoleted by budha2 (service)
// HAL support required by BUDHA:
u1_t* os_getWSleepParams(void);
void  os_setWSleepParams(u1_t* params);
int   os_checkSignature(const u1_t* buf, u1_t len);
u4_t  os_getFWCRC(void);
int   os_usedWakeNonce(u2_t nonce);
void  os_commitWakeNonce(u2_t nonce);
enum { BOOT_WSLEEP=0, BOOT_OS };
void  os_setBootMode(u1_t mode);
void  os_boot(void);
s2_t  os_fwChunk(u1_t* p, u1_t len);
#endif // defined(CFG_budha)

#endif // !HAS_os_calls

// public radio functions
void radio_irq_handler (u1_t dio, ostime_t ticks); // (used by EXTI_IRQHandler)
void radio_init (bool calibrate); // (used by os_init())
void radio_writeBuf (u1_t addr, u1_t* buf, u1_t len); // (used by perso)
void radio_readBuf (u1_t addr, u1_t* buf, u1_t len); // (used by perso)
void radio_set_irq_timeout (ostime_t timeout);

// radio-specific functions
bool radio_irq_process (ostime_t irqtime, u1_t diomask);
void radio_starttx (bool txcontinuous);
void radio_startrx (bool rxcontinuous);
void radio_sleep (void);
void radio_cca (void);
void radio_cad (void);
void radio_cw (void);
void radio_generate_random (u4_t *words, u1_t len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _oslmic_h_
