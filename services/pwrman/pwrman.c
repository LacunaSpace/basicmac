// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <string.h>

#include "lmic.h"

#include "eefs/eefs.h"
#include "pwrman.h"

#include "svcdefs.h" // for type-checking hook functions

// our basic unit is the micro-ampere hour -- 2^32 uAh = 4295 Ah

// 16bc26f9da64e290-05b08ee7
static const uint8_t UFID_PWRMAN_STATS[12] = { 0x90, 0xe2, 0x64, 0xda, 0xf9, 0x26, 0xbc, 0x16, 0xe7, 0x8e, 0xb0, 0x05 };

const char* _pwrman_eefs_fn (const uint8_t* ufid) {
    if( memcmp(ufid, UFID_PWRMAN_STATS, sizeof(UFID_PWRMAN_STATS)) == 0 ) {
        return "com.semtech.svc.pwrman.stats";
    }
    return NULL;
}

typedef struct {
    uint32_t hr;
    uint32_t ticks;
} ptime;

// Volatile state
static struct {
    ptime accu;                // accumulator
    ptime stats[PWRMAN_C_MAX]; // consumption statistics
} state;

// Persistent state (eefs)
typedef struct {
    uint32_t uah_accu;                // accumulator
    uint32_t uah_stats[PWRMAN_C_MAX]; // total micro-amp hours used
} pwrman_pstate;

static void update_rtstats (void); // fwd decl

static void add (ptime* ppt, uint64_t uaticks) {
    uaticks += ppt->ticks;
    uint32_t uah = uaticks / (OSTICKS_PER_SEC * 60  * 60);
    if( uah != 0 ) {
        ppt->hr += uah;
        uaticks -= ((int64_t) uah * (OSTICKS_PER_SEC * 60 * 60));
    }
    ppt->ticks = uaticks;
}

void pwrman_consume (int ctype, uint32_t ticks, uint32_t ua) {
    ASSERT(ctype < PWRMAN_C_MAX);
    uint64_t uaticks = (uint64_t) ticks * ua;
#ifdef CFG_DEBUG_pwrman
    debug_printf("pwrman: adding %u ticks to accu (%d/%d",
            (uint32_t) uaticks,
            state.accu.hr, state.accu.ticks);
#endif
    add(&state.accu, uaticks);
#ifdef CFG_DEBUG_pwrman
    debug_printf(" --> %d/%d)\r\n",
            state.accu.hr, state.accu.ticks);
#endif
    add(&state.stats[ctype], uaticks);
}

uint32_t pwrman_accu_uah (void) {
    update_rtstats();
    return state.accu.hr;
}

void pwrman_commit (void) {
    update_rtstats();
    pwrman_pstate ps;
    ps.uah_accu = state.accu.hr;
    for( int i = 0; i < PWRMAN_C_MAX; i++ ) {
        ps.uah_stats[i] = state.stats[i].hr;
    }
    eefs_save(UFID_PWRMAN_STATS, &ps, sizeof(ps));
}

void pwrman_reset (void) {
    memset(&state, 0x00, sizeof(state));
    pwrman_pstate ps;
    memset(&ps, 0x00, sizeof(ps));
    eefs_save(UFID_PWRMAN_STATS, &ps, sizeof(ps));
}

void _pwrman_init (void) {
    pwrman_pstate ps;
    if( eefs_read(UFID_PWRMAN_STATS, &ps, sizeof(ps)) == sizeof(ps) ) {
        state.accu.hr = ps.uah_accu;
        for( int i = 0; i < PWRMAN_C_MAX; i++ ) {
            state.stats[i].hr = ps.uah_stats[i];
        }
    }
}

static void update_rtstats (void) {
#if defined(STM32L0) && defined(CFG_rtstats)
    hal_rtstats stats;
    hal_rtstats_collect(&stats);
    pwrman_consume(PWRMAN_C_RUN, stats.run_ticks, BRD_PWR_RUN_UA);
    pwrman_consume(PWRMAN_C_SLEEP, stats.sleep_ticks[HAL_SLEEP_S0], BRD_PWR_S0_UA);
    pwrman_consume(PWRMAN_C_SLEEP, stats.sleep_ticks[HAL_SLEEP_S1], BRD_PWR_S1_UA);
    pwrman_consume(PWRMAN_C_SLEEP, stats.sleep_ticks[HAL_SLEEP_S2], BRD_PWR_S2_UA);
#endif
}
