// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "hw.h"

#if defined(BRD_LED_TIM)

#if BRD_LED_TIM == 2
#define TIMx            TIM2
#define TIMx_enable()   do { RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; } while (0)
#define TIMx_disable()  do { RCC->APB1ENR &= ~RCC_APB1ENR_TIM2EN; } while (0)
#define TIMx_IRQn       TIM2_IRQn
#else
#error "Unsupported timer"
#endif

static struct {
    unsigned int state;
    struct {
        int step;
        unsigned int n;
        unsigned int delay;
        unsigned int min;
        unsigned int max;
    } pulse[4];
} pwm;

#endif

void leds_init (void) {
#if defined(BRD_LED_TIM)
    TIMx_enable();
    TIMx->PSC = 4;
    TIMx->ARR = 0xffff;
    TIMx->CCMR1 =
        TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE |
        TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2PE;
    TIMx->CCMR2 =
        TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE |
        TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
    TIMx_disable();
#endif
}

#if defined(BRD_LED_TIM)
static void pwm_set_gpio (unsigned int gpio, bool enable, bool pulse, unsigned int ccr) {
    unsigned int ch = BRD_GPIO_GET_CHAN(gpio) - 1;
    ASSERT(ch < 4);

    unsigned int state0 = pwm.state;
    unsigned int state1 = state0;

    if (enable) {
        state1 |= (0x01 << ch);
        if (pulse) {
            state1 |= (0x10 << ch);
        }
    } else {
        state1 &= ~(0x11 << ch);
    }

    if (state0 == state1) {
        return;
    }

    hal_disableIRQs();

    if (state1) {
        if (state0 == 0) {
            TIMx_enable();                      // enable peripheral clock
            hal_setMaxSleep(HAL_SLEEP_S0);      // disable sleep (keep clock at full speed)
            TIMx->CR1 |= TIM_CR1_CEN;           // enable timer peripheral
            TIMx->EGR |= TIM_EGR_UG;            // start pwm
        }
        if (state1 & 0xf0) {
            if ((state0 & 0xf0) == 0) {
                TIMx->DIER |= TIM_DIER_UIE;     // enable update interrupt
                NVIC_EnableIRQ(TIMx_IRQn);      // enable interrupt in NVIC
            }
        } else {
            if ((state0 & 0xf0) == 0) {
                TIMx->DIER &= ~TIM_DIER_UIE;    // disable update interrupt
                NVIC_DisableIRQ(TIMx_IRQn);     // disable interrupt in NVIC
            }
        }
    } else if (state0) {
        TIMx->CR1 &= ~TIM_CR1_CEN;              // disable timer
        TIMx->DIER &= ~TIM_DIER_UIE;            // disable update interrupt
        TIMx_disable();                         // disable peripheral clock
        hal_clearMaxSleep(HAL_SLEEP_S0);        // re-enable sleep
        NVIC_DisableIRQ(TIMx_IRQn);             // disable interrupt in NVIC
    }

    if (enable) {
        *((&(TIMx->CCR1)) + ch) = ccr;          // set initial CCR value
        if ((state0 & (1 << ch)) == 0) {
            unsigned int ccer = TIM_CCER_CC1E;
            if (gpio & BRD_GPIO_ACTIVE_LOW) {
                ccer |= TIM_CCER_CC1P;
            }
            TIMx->CCER |= (ccer << (4 * ch));   // enable channel
            CFG_PIN_AF(gpio, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
        }
    }
    pwm.state = state1;

    hal_enableIRQs();
}
#endif

void leds_pwm (unsigned int gpio, int dc) {
#if defined(BRD_LED_TIM)
    pwm_set_gpio(gpio, true, false, dc);
#endif
}

void leds_pulse (unsigned int gpio, unsigned int min, unsigned int max, int step, unsigned int delay) {
#if defined(BRD_LED_TIM)
    unsigned int ch = BRD_GPIO_GET_CHAN(gpio) - 1;
    ASSERT(ch < 4);
    pwm.pulse[ch].n = 0;
    pwm.pulse[ch].min = min;
    pwm.pulse[ch].max = max;
    pwm.pulse[ch].step = step;
    pwm.pulse[ch].delay = delay;
    pwm_set_gpio(gpio, true, true, (step < 0) ? max : min);
#endif
}

void leds_set (unsigned int gpio, int state) {
    if (state) {
        if (gpio & BRD_GPIO_ACTIVE_LOW) {
            SET_PIN(gpio, 0);
        } else {
            SET_PIN(gpio, 1);
        }
        CFG_PIN(gpio, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    } else {
        CFG_PIN(gpio, GPIOCFG_MODE_ANA | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_NONE);
    }
#if defined(BRD_LED_TIM)
    pwm_set_gpio(gpio, false, false, 0);
#endif
}

#if defined(BRD_LED_TIM)
void leds_pwm_irq (void) {
    if (TIMx->SR & TIM_SR_UIF) { // update event
        TIMx->SR = ~TIM_SR_UIF; // clear flag
        unsigned int ps = pwm.state & 0x0f;
        while (ps) {
            unsigned int ch = __builtin_ctz(ps);
            if (pwm.pulse[ch].step) {
                if (pwm.pulse[ch].n < pwm.pulse[ch].delay) {
                    pwm.pulse[ch].n += 1;
                } else {
                    pwm.pulse[ch].n = 0;
                    int ccr = *((&(TIMx->CCR1)) + ch);
                    ccr += pwm.pulse[ch].step;
                    if (ccr <= pwm.pulse[ch].min) {
                        ccr = pwm.pulse[ch].min;
                        pwm.pulse[ch].step = -pwm.pulse[ch].step;
                    } else if (ccr >= pwm.pulse[ch].max) {
                        ccr = pwm.pulse[ch].max;
                        pwm.pulse[ch].step = -pwm.pulse[ch].step;
                    }
                    *((&(TIMx->CCR1)) + ch) = ccr;
                }
            }
            ps &= ~(1 << ch);
        }
    }
}
#endif
