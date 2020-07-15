// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hal_hpp_
#define _hal_hpp_

#ifdef __cplusplus
extern "C"{
#endif

#ifdef HAL_IMPL_INC
#include HAL_IMPL_INC
#endif

/*
 * initialize hardware (IO, SPI, TIMER, IRQ).
 */
void hal_init (void* bootarg);

/*
 * set watchdog counter (in 2s units)
 */
void hal_watchcount (int cnt);

/*
 * drive antenna switch (and account power consumption)
 */
#define HAL_ANTSW_OFF  0
#define HAL_ANTSW_RX   1
#define HAL_ANTSW_TX   2
#define HAL_ANTSW_TX2  3
void hal_ant_switch (u1_t val);

#if defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)
/*
 * control radio TCXO power (0=off, 1=on)
 * (return if TCXO is present and in use)
 */
bool hal_pin_tcxo (u1_t val);
#endif // defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)

/*
 * control radio RST pin (0=low, 1=high, 2=floating)
 * (return true if reset pin is connected)
 */
bool hal_pin_rst (u1_t val);

/*
 * wait until radio BUSY pin is low
 */
void hal_pin_busy_wait (void);

/*
 * set DIO0/1/2/3 interrupt mask
 */
#define HAL_IRQMASK_DIO0 (1<<0)
#define HAL_IRQMASK_DIO1 (1<<1)
#define HAL_IRQMASK_DIO2 (1<<2)
#define HAL_IRQMASK_DIO3 (1<<3)
void hal_irqmask_set (int mask);

#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
/*
 * Returns true if DIO3 should control the TCXO.
 * TODO: Reconsider this HAL function, maybe integrate with hal_pin_tcxo
 * somehow?
 */
bool hal_dio3_controls_tcxo (void);

/*
 * Returns true if DIO2 should control the RXTX switch.
 * TODO: Reconsider this HAL function, maybe integrate with
 * hal_ant_switch somehow?
 */
bool hal_dio2_controls_rxtx (void);
#endif // defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)

/*
 * drive radio NSS pin (on=low, off=high).
 */
void hal_spi_select (int on);

/*
 * perform 8-bit SPI transaction with radio.
 *   - write given byte 'outval'
 *   - read byte and return value
 */
u1_t hal_spi (u1_t outval);

/*
 * disable all CPU interrupts.
 *   - might be invoked nested
 *   - will be followed by matching call to hal_enableIRQs()
 */
void hal_disableIRQs (void);

/*
 * enable CPU interrupts.
 */
void hal_enableIRQs (void);

/*
 * put system and CPU in low-power mode, sleep until target time / interrupt.
 *   - return 0 if target time is close
 *   - otherwise sleep until target time / interrupt and return non-zero
 */
#define HAL_SLEEP_EXACT         0
#define HAL_SLEEP_APPROX        1
#define HAL_SLEEP_FOREVER       2
u1_t hal_sleep (u1_t type, u4_t targettime);

/*
 * return 32-bit system time in ticks.
 */
u4_t hal_ticks (void);

/*
 * return 64-bit system time in ticks.
 */
u8_t hal_xticks (void);

/*
 * return subticks (1/1024th tick)
 */
s2_t hal_subticks (void);

/*
 * busy-wait until specified timestamp (in ticks) is reached.
 */
void hal_waitUntil (u4_t time);

/*
 * get current battery level
 */
u1_t hal_getBattLevel (void);

/*
 * set current battery level
 */
void hal_setBattLevel (u1_t level);

/*
 * perform fatal failure action.
 *   - called by assertions
 *   - action could be HALT or reboot
 */
void hal_failed (void);

#ifdef CFG_DEBUG

void hal_debug_str (const char* str);
void hal_debug_led (int val);

#endif

typedef struct {
    uint32_t blversion;
    uint32_t version;
    uint32_t crc;
    uint32_t flashsz;
} hal_fwi;

void hal_fwinfo (hal_fwi* fwi);

u1_t* hal_joineui (void);
u1_t* hal_deveui (void);
u1_t* hal_nwkkey (void);
u1_t* hal_appkey (void);
u1_t* hal_serial (void);
u4_t  hal_region (void);
u4_t  hal_hwid (void);
u4_t  hal_unique (void);

u4_t hal_dnonce_next (void);

void hal_reboot (void);
bool hal_set_update (void* ptr);

void hal_logEv (uint8_t evcat, uint8_t evid, uint32_t evparam);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _hal_hpp_
