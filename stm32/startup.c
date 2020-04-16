// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "hw.h"

#if defined(STM32L0)
#define MAX_IRQn                32      /* see PM0223, 2.3.4, pg. 29 */
#elif defined(STM32L1)
#define MAX_IRQn                68      /* see PM0056, 2.3.4, pg. 36 */
#else
#error "Unsupported MCU"
#endif

__attribute__((aligned(512)))
static uint32_t irqvector[16 + MAX_IRQn];

void _start (boot_boottab* boottab) {
    // symbols provided by linker script
    extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

    // initialize data
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while( dst < &_edata ) {
        *dst++ = *src++;
    }

    // initialize bss
    dst = &_sbss;
    while( dst < &_ebss ) {
        *dst++ = 0;
    }

    // copy current Cortex M IRQ + NVIC vector to RAM
    src = (uint32_t*) 0;
    dst = irqvector;
    for( int i = 0; i < (16 + MAX_IRQn); i++ ) {
        *dst++ = *src++;
    }
    // fix-up NVIC vector with handlers from firmware
    for( const irqdef* id = HAL_irqdefs; id->handler; id++ ) {
        irqvector[16 + id->num] = (uint32_t) id->handler;
    }
    // re-map interrupt vector
    SCB->VTOR = (uint32_t) irqvector;

    // call main function
    extern void main (boot_boottab* boottab);
    main(boottab);
}

// Firmware header
__attribute__((section(".fwhdr")))
const volatile hal_fwhdr fwhdr = {
    // CRC and size will be patched by external tool
    .boot.crc           = 0,
    .boot.size          = BOOT_MAGIC_SIZE,
    .boot.entrypoint    = (uint32_t) _start,

    .version            = 0,
};
