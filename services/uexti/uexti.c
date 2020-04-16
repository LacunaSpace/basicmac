// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"

#include "svcdefs.h"
#include "uexti.h"

#include "peripherals.h"

// ------------------------------------------------
// Unified External Interrupt Handler

static void hook_uexti_irq (unsigned int* mask) {
    SVCHOOK_uexti_irq(mask);
}

#if defined(STM32L0)

#include "board.h"

#define uexti_handler 1
#if (CFG_EXTI_IRQ_HANDLER != uexti_handler)
#error "UEXTI requires 'uexti_handler' to be set as EXTI_IRQ_HANDLER!"
#endif
#undef uexti_handler

void uexti_handler (void) {
    unsigned int m0, m1;
    m0 = m1 = EXTI->PR;        // get pending interrupts
    hook_uexti_irq(&m1);       // call hooks
    EXTI->PR = (m0 ^ m1) & m0; // clear handled interrupts
}

void uexti_config (unsigned int gpio, bool rising, bool falling) {
    gpio_cfg_extirq_ex(BRD_PORT(gpio), BRD_PIN(gpio), rising, falling);
}

void uexti_enable (unsigned int gpio, bool enable) {
    gpio_set_extirq(BRD_PIN(gpio), enable);
}

#elif defined(unicorn)

void uexti_handler (void) {
    unsigned int mask = pio_irq_get();
    unsigned int m0 = mask;
    hook_uexti_irq(&mask); // call hooks
    pio_irq_clear(mask ^ m0);
}

void uexti_config (unsigned int gpio, bool rising, bool falling) {
    pio_irq_config(gpio, rising, falling);
}

void uexti_enable (unsigned int gpio, bool enable) {
    pio_irq_enable(gpio, enable);
}

#else
#error "Platform not supported by UEXTI service module"
#endif
