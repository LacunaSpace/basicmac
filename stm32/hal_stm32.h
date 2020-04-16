// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hal_stm32_h_
#define _hal_stm32_h_

#include "hw.h"
#include "boottab.h"

// Get current PC
__attribute__((always_inline)) static inline uint32_t hal_getpc (void) {
    uint32_t addr;
    __asm__ volatile ("mov %[addr], pc" : [addr]"=r" (addr) : : );
    return addr;
}

// Macro to place code in RAM
#define __fastcode __attribute__((noinline,section(".fastcode")))

// Sleep modes
enum {
    HAL_SLEEP_S0,       // sleep, full speed clock
    HAL_SLEEP_S1,       // sleep, reduced speed clock
    HAL_SLEEP_S2,       // stop mode

    HAL_SLEEP_CNT       // number of sleep states
};

void hal_setMaxSleep (unsigned int level);
void hal_clearMaxSleep (unsigned int level);

#ifdef CFG_rtstats
typedef struct {
    uint32_t run_ticks;
    uint32_t sleep_ticks[HAL_SLEEP_CNT];
} hal_rtstats;

void hal_rtstats_collect (hal_rtstats* stats);
#endif


// NVIC interrupt definition
typedef struct {
    uint32_t    num;            // NVIC interrupt number
    void*       handler;        // Pointer to handler function
} irqdef;

extern const irqdef HAL_irqdefs[];

// Firmware header -- do not modify (append only)
typedef struct {
    boot_fwhdr boot;

    uint32_t version;
} hal_fwhdr;

// Personalization data (persodata.c)
void pd_init (void);
bool pd_verify (void);

void usart_init (void);
void usart_irq (void);

void i2c_irq (void);

#if defined(SVC_fuota)
// Glue for FUOTA (fountain code) service

#include "peripherals.h"

#define fuota_flash_pagesz FLASH_PAGE_SZ
#define fuota_flash_bitdefault 0

#define fuota_flash_write(dst,src,nwords,erase) \
    flash_write((uint32_t*) (dst), (uint32_t*) (src), nwords, erase)

#define fuota_flash_read(dst,src,nwords) \
    memcpy(dst, src, (nwords) << 2)

#define fuota_flash_rd_u4(addr) \
    (*((uint32_t*) (addr)))

#define fuota_flash_rd_ptr(addr) \
    (*((void**) (addr)))

#endif

#endif
