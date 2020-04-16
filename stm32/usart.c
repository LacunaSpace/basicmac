// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_USART

#if BRD_USART == 1
#define USARTx                  USART1
#define USARTx_enable()         do { RCC->APB2ENR |= RCC_APB2ENR_USART1EN; } while (0)
#define USARTx_disable()        do { RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN; } while (0)
#define USARTx_IRQn             USART1_IRQn
#elif BRD_USART == BRD_LPUART(1)
#define USARTx                  LPUART1
#define USARTx_enable()         do { RCC->APB1ENR |= RCC_APB1ENR_LPUART1EN; } while (0)
#define USARTx_disable()        do { RCC->APB1ENR &= ~RCC_APB1ENR_LPUART1EN; } while (0)
#define USARTx_IRQn             LPUART1_IRQn
#endif

enum {
    RX_ON       = (1 << 0),
    TX_ON       = (1 << 1),
};

static struct {
    unsigned int on;

    unsigned int br;

    usart_rx_func rx;
    void* rxarg;

    usart_tx_func tx;
    void* txarg;
} usart;

static void usart_on (unsigned int flag) {
    hal_disableIRQs();
    if (usart.on == 0) {
        // disable sleep (keep clock at full speed during transfer
        hal_setMaxSleep(HAL_SLEEP_S0);
        // enable peripheral clock
        USARTx_enable();
        // set baudrate
        USARTx->BRR = usart.br;
        // usart enable
        USARTx->CR1 = USART_CR1_UE;
        // enable interrupts in NVIC
        NVIC_EnableIRQ(USARTx_IRQn);
    }
    usart.on |= flag;
    hal_enableIRQs();
}

static void usart_off (unsigned int flag) {
    hal_disableIRQs();
    usart.on &= ~flag;
    if (usart.on == 0) {
        // disable USART
        USARTx->CR1 = 0;
        // disable peripheral clock
        USARTx_disable();
        // disable interrupts in NVIC
        NVIC_DisableIRQ(USARTx_IRQn);
        // re-enable sleep
        hal_clearMaxSleep(HAL_SLEEP_S0);
    }
    hal_enableIRQs();
}

static void rx_on (unsigned int noirq) {
    // turn on usart
    usart_on(RX_ON);
    // enable receiver
    USARTx->CR1 |= USART_CR1_RE;
    // setup I/O line
    CFG_PIN_AF(GPIO_USART_RX, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    if (noirq == 0) {
        // flush data, clear ORE and enable receive interrupt
        USARTx->RQR |= USART_RQR_RXFRQ;
        USARTx->ICR |= USART_ISR_ORE;
        USARTx->CR1 |= USART_CR1_RXNEIE;
    }
}

static void rx_off (void) {
    // deconfigure I/O line
    CFG_PIN_DEFAULT(GPIO_USART_RX);
    // disable receiver and interrupts
    USARTx->CR1 &= ~(USART_CR1_RE | USART_CR1_RXNEIE);
    // turn off usart
    usart_off(RX_ON);
}

static void tx_on (void) {
    // turn on usart
    usart_on(TX_ON);
    // enable transmitter
    USARTx->CR1 |= USART_CR1_TE;
    // setup I/O line
    CFG_PIN_AF(GPIO_USART_TX, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    // enable interrupt
    USARTx->CR1 |= USART_CR1_TXEIE;
}

static void tx_off (void) {
    // deconfigure I/O line, activate pullup
    CFG_PIN(GPIO_USART_TX, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    // disable receiver and interrupts
    USARTx->CR1 &= ~(USART_CR1_TE | USART_CR1_TXEIE);
    // turn off usart
    usart_off(TX_ON);
}

void usart_init (void) {
    usart.on = 0;
    // activate pullup on tx line
    CFG_PIN(GPIO_USART_TX, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
}

void usart_cfg (unsigned int br) {
    usart.br = br;
}

void usart_recv (usart_rx_func rx, void* arg) {
    usart.rx = rx;
    usart.rxarg = arg;
    rx_on(0);
}

void usart_abort_recv (void) {
    hal_disableIRQs();
    if (usart.on & RX_ON) {
        rx_off();
        usart.rx(USART_ERROR, usart.rxarg);
    }
    hal_enableIRQs();
}

void usart_send (usart_tx_func tx, void* arg) {
    usart.tx = tx;
    usart.txarg = arg;
    tx_on();
}

void usart_irq (void) {
    unsigned int isr = USARTx->ISR;
    unsigned int cr1 = USARTx->CR1;
    if (cr1 & USART_CR1_RXNEIE) {
        if (isr & USART_ISR_ORE) {
            USARTx->ICR |= USART_ISR_ORE;
            rx_off();
            usart.rx(USART_ERROR, usart.rxarg);
        } else if (isr & USART_ISR_RXNE) {
            if (usart.rx(USARTx->RDR, usart.rxarg) != USART_CONTINUE) { // done
                rx_off();
            }
        }
    }
    if ((cr1 & USART_CR1_TXEIE) && (isr & USART_ISR_TXE)) {
        int ch;
        if ((ch = usart.tx(USART_CONTINUE, usart.txarg)) < 0) { // done
            unsigned int cr1 = USARTx->CR1;
            cr1 = (cr1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
            USARTx->CR1 = cr1;
        } else {
            USARTx->TDR = ch;
        }
    }
    if ((cr1 & USART_CR1_TCIE) && (isr & USART_ISR_TC)) {
        USARTx->CR1 &= ~USART_CR1_TCIE;
        tx_off();
        usart.tx(USART_DONE, usart.txarg);
    }
}

int usart_wait_silence (int silence_ticks, int timeout_ticks) {
    ostime_t deadline = os_getTime() + timeout_ticks;
    ostime_t threshold = os_getTime() + silence_ticks;
    int retval = 0;
    rx_on(1);
    while (1) {
        if (USARTx->ISR & USART_ISR_BUSY) {
            threshold = os_getTime() + silence_ticks;
        }
        if ((deadline - os_getTime()) < 0) {
            retval = -1;
            break;
        }
        if ((threshold - os_getTime()) < 0) {
            retval = 0;
            break;
        }
    }
    rx_off();
    return retval;
}

#endif
