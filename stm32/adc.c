// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

static void adc_on (void) {
#if defined(STM32L0)
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;         // enable peripheral clock
    ADC1->CR |= ADC_CR_ADCAL;                   // start calibration
    while( (ADC1->CR & ADC_CR_ADCAL) != 0 );    // wait for it
    ADC1->ISR = ADC_ISR_ADRDY;                  // clear ready bit (rc_w1)
    ADC1->CR |= ADC_CR_ADEN;                    // switch on the ADC
    while( (ADC1->ISR & ADC_ISR_ADRDY) == 0 );  // wait until ADC is ready
#elif defined(STM32L1)
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;         // make sure the ADC is clocked
    ADC1->CR2 |= ADC_CR2_ADON;                  // switch on the ADC
#endif
}

static void adc_off (void) {
#if defined(STM32L0)
    ADC1->CR |= ADC_CR_ADDIS;                   // switch off the ADC
    while( (ADC1->CR & ADC_CR_ADEN) != 0 );     // wait for it
    ADC1->CR &= ~ADC_CR_ADVREGEN;               // switch off regulator
#elif defined(STM32L1)
#warning "adc_off() not yet implemented for STM32L0"
    // TODO - implement
#endif
}

unsigned int adc_read (unsigned int chnl, unsigned int rate) {
    adc_on();
#if defined(STM32L0)
    if( chnl == VREFINT_ADC_CH ) {
        ADC->CCR |= ADC_CCR_VREFEN;             // internal voltage reference on channel 17
    } else if( chnl == TEMPINT_ADC_CH ) {
        ADC->CCR |= ADC_CCR_TSEN;               // internal temperature on channel 18
    }
    ADC1->CHSELR = (1 << chnl);                 // select channel
    ADC1->SMPR = rate & 0x7;                    // sample rate
    ADC1->CR |= ADC_CR_ADSTART;                 // start conversion
    while( (ADC1->ISR & ADC_ISR_EOC) == 0 );    // wait for it
    if( chnl == VREFINT_ADC_CH ) {
        ADC->CCR &= ~ADC_CCR_VREFEN;
    } else if( chnl == TEMPINT_ADC_CH ) {
        ADC->CCR &= ~ADC_CCR_TSEN;
    }
#elif defined(STM32L1)
    ADC1->SQR5 = chnl;                          // select the channel for the 1st conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;               // start the conversion
    while( (ADC1->SR & ADC_SR_EOC) == 0 );      // wait for it
#endif
    u2_t v = ADC1->DR;
    adc_off();
    return v;
}
