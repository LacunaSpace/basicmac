// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "hw.h"

static inline void gpio_begin (int port) {
    unsigned int enr = GPIO_RCC_ENR;
    if ((enr & GPIO_EN(port)) == 0) {
        GPIO_RCC_ENR = enr | GPIO_EN(port);
        // dummy read as per errata
        (void) GPIOx(port)->IDR;
    }
}

static inline void gpio_end (int port) {
    GPIO_RCC_ENR &= ~GPIO_EN(port);
}

void gpio_cfg_pin (int port, int pin, int gpiocfg) {
    gpio_begin(port);
    HW_CFG_PIN(GPIOx(port), pin, gpiocfg);
    gpio_end(port);
}

void gpio_set_pin (int port, int pin, int state) {
    gpio_begin(port);
    HW_SET_PIN(GPIOx(port), pin, state);
    gpio_end(port);
}

void gpio_cfg_set_pin (int port, int pin, int gpiocfg, int state) {
    gpio_begin(port);
    HW_SET_PIN(GPIOx(port), pin, state);
    HW_CFG_PIN(GPIOx(port), pin, gpiocfg);
    gpio_end(port);
}

int gpio_get_pin (int port, int pin) {
    int val;
    gpio_begin(port);
    val = HW_GET_PIN(GPIOx(port), pin);
    gpio_end(port);
    return val;
}

int gpio_transition (int port, int pin, int type, int duration, unsigned int config) {
    int val;
    gpio_begin(port);
    val = HW_GET_PIN(GPIOx(port), pin);
    HW_SET_PIN(GPIOx(port), pin, type);
    HW_CFG_PIN(GPIOx(port), pin, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    for (int i = 0; i < duration; i++) __NOP();
    HW_SET_PIN(GPIOx(port), pin, type ^ 1);
    for (int i = 0; i < duration; i++) __NOP();
    HW_CFG_PIN(GPIOx(port), pin, config);
    gpio_end(port);
    return val;
}

void gpio_cfg_extirq_ex (int port, int pin, bool rising, bool falling) {
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; // enable system configuration controller

    // configure external interrupt (every irq line 0-15 can be configured with a 4-bit port index A-G)
    u4_t tmp1 = (pin & 0x3) << 2;
    u4_t tmp2 = 0x0F << tmp1;
    SYSCFG->EXTICR[pin >> 2] = (SYSCFG->EXTICR[pin >> 2] & ~tmp2) | (port << tmp1);

    RCC->APB2ENR &= ~RCC_APB2ENR_SYSCFGEN; // disable system configuration controller

    // configure trigger and enable irq
    u4_t mask = 1 << pin;
    EXTI->RTSR &= ~mask; // clear trigger
    EXTI->FTSR &= ~mask; // clear trigger
    if( rising ) {
        EXTI->RTSR |= mask;
    }
    if( falling ) {
        EXTI->FTSR |= mask;
    }

    // configure the NVIC
#if defined(STM32L0)
    u1_t channel = (pin < 2) ? EXTI0_1_IRQn : (pin < 4) ? EXTI2_3_IRQn : EXTI4_15_IRQn;
#elif defined(STM32L1)
    u1_t channel = (pin < 5) ? (EXTI0_IRQn + pin) : ((pin < 10) ? EXTI9_5_IRQn : EXTI15_10_IRQn);
#endif
    NVIC->IP[channel] = 0x70; // interrupt priority
    NVIC->ISER[channel>>5] = 1 << (channel & 0x1F);  // set enable IRQ
}

void gpio_cfg_extirq (int port, int pin, int irqcfg) {
    gpio_cfg_extirq_ex(port, pin,
            (irqcfg == GPIO_IRQ_CHANGE || irqcfg == GPIO_IRQ_RISING),
            (irqcfg == GPIO_IRQ_CHANGE || irqcfg == GPIO_IRQ_FALLING));
}


void gpio_set_extirq (int pin, int on) {
    if (on) {
        EXTI->PR = (1 << pin);
        EXTI->IMR |= (1 << pin);
    } else {
        EXTI->IMR &= ~(1 << pin);
    }
}

void pio_set (unsigned int pin, int value) {
    if (value < 0) {
        gpio_cfg_pin(BRD_PORT(pin), BRD_PIN(pin), 0
                | (((value & 1) == 0 ? GPIOCFG_PUPD_PUP : 0))
                | (((value & 2) == 0 ? GPIOCFG_PUPD_PDN : 0))
                | (((value & 4) == 0 ? GPIOCFG_MODE_ANA : GPIOCFG_MODE_INP)));
    } else {
        gpio_cfg_set_pin(BRD_PORT(pin), BRD_PIN(pin),
                GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE,
                value);
    }
}

int pio_get (unsigned int pin) {
    return gpio_get_pin(BRD_PORT(pin), BRD_PIN(pin));
}
