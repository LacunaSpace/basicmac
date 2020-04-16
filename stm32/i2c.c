// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_I2C

#if BRD_I2C == 1
#define I2Cx            I2C1
#define I2Cx_enable()   do { RCC->APB1ENR |= RCC_APB1ENR_I2C1EN; } while (0)
#define I2Cx_disable()  do { RCC->APB1ENR &= ~RCC_APB1ENR_I2C1EN; } while (0)
#define I2Cx_IRQn       I2C1_IRQn
#else
#error "Unsupported I2C peripheral"
#endif

static struct {
    unsigned int wlen;
    unsigned char* wptr;
    unsigned int rlen;
    unsigned char* rptr;
    osjob_t* job;
    osjobcb_t cb;
    int* pstatus;
} xfr;

static struct {
    int status;
    osjob_t job;
    i2c_cb cb;
} xfr2;

static void i2c_stop (int status) {
    // generate stop condition
    I2Cx->CR2 |= I2C_CR2_STOP;
    // disable interrupts in NVIC
    NVIC_DisableIRQ(I2Cx_IRQn);
    // disable interrupts/peripheral
    I2Cx->CR1 = 0;
    // reconfigure GPIOs
    CFG_PIN_DEFAULT(GPIO_I2C_SCL);
    CFG_PIN_DEFAULT(GPIO_I2C_SDA);
    // disable peripheral clock
    I2Cx_disable();
    // schedule callback
    *(xfr.pstatus) = status;
    if (xfr.job != NULL) {
        os_setCallback(xfr.job, xfr.cb);
    } else {
        xfr.cb(NULL);
    }
    // re-enable sleep
    hal_clearMaxSleep(HAL_SLEEP_S0);
}

static void i2c_start (int addr) {
    // enable peripheral clock
    I2Cx_enable();
    // set timing
    I2Cx->TIMINGR = 0x40101A22; // from CubeMX tool; t_rise=t_fall=50ns, 100kHz
    // start I2C
    I2Cx->CR1 |= I2C_CR1_PE;
    // setup slave address
    I2Cx->CR2 = (I2Cx->CR2 & ~I2C_CR2_SADD) | (addr & I2C_CR2_SADD);
    // setup GPIOs
    CFG_PIN_AF(GPIO_I2C_SCL, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_NONE);
    CFG_PIN_AF(GPIO_I2C_SDA, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_NONE);
    // disable sleep (keep clock at full speed during transfer
    hal_setMaxSleep(HAL_SLEEP_S0);
    // enable interrupts in NVIC
    NVIC_EnableIRQ(I2Cx_IRQn);
}

static void i2c_cont (void) {
    if (xfr.wlen) {
        // calculate length; TODO: handle >255
        int n = xfr.wlen & 0xff;
        xfr.wlen -= n;
        // set direction & number of bytes
        I2Cx->CR2 = (I2Cx->CR2 & ~(I2C_CR2_RD_WRN | I2C_CR2_NBYTES)) | (n << 16);
        // enable interrupts
        I2Cx->CR1 = (I2Cx->CR1 & ~0xfe) | I2C_CR1_TXIE | I2C_CR1_TCIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
        // start TX
        I2Cx->CR2 |= I2C_CR2_START;
    } else if (xfr.rlen) {
        // calculate length; TODO: handle >255
        int n = xfr.rlen & 0xff;
        xfr.rlen -= n;
        // set direction & number of bytes
        I2Cx->CR2 = (I2Cx->CR2 & ~(I2C_CR2_RD_WRN | I2C_CR2_NBYTES)) | I2C_CR2_RD_WRN | (n << 16);
        // enable interrupts
        I2Cx->CR1 = (I2Cx->CR1 & ~0xfe) | I2C_CR1_RXIE | I2C_CR1_TCIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
        // start RX
        I2Cx->CR2 |= I2C_CR2_START;
    } else {
        // done
        i2c_stop(I2C_OK);
    }
}

void i2c_irq (void) {
    unsigned int isr = I2Cx->ISR;
    if (isr & I2C_ISR_NACKF) {
        // NACK detected, transfer failed!
        i2c_stop(I2C_NAK);
    } else if (isr & I2C_ISR_TC) {
        // transfer complete, move on
        i2c_cont();
    } else if (isr & I2C_ISR_TXIS) {
        // write next byte
        I2Cx->TXDR = *xfr.wptr++;
    } else if (isr & I2C_ISR_RXNE) {
        // next byte received
        *xfr.rptr++ = I2Cx->RXDR;
    } else {
        hal_failed(); // XXX
    }
}

static void i2c_timeout (osjob_t* job) {
    i2c_abort();
}

void i2c_xfer_ex (unsigned int addr, unsigned char* buf, unsigned int wlen, unsigned int rlen, ostime_t timeout,
        osjob_t* job, osjobcb_t cb, int* pstatus) {
    // setup xfr structure
    xfr.wlen = wlen;
    xfr.rlen = rlen;
    xfr.wptr = xfr.rptr = buf;
    xfr.job = job;
    xfr.cb = cb;
    xfr.pstatus = pstatus;
    *xfr.pstatus = I2C_BUSY;
    // set timeout
    if (timeout) {
        os_setTimedCallback(job, os_getTime() + timeout, i2c_timeout);
    }
    // prepare peripheral
    i2c_start(addr);
    // start actual transfer
    i2c_cont();
}

static void i2cfunc (osjob_t* j) {
    xfr2.cb(xfr2.status);
}

void i2c_xfer (unsigned int addr, unsigned char* buf, unsigned int wlen, unsigned int rlen, i2c_cb cb, ostime_t timeout) {
    xfr2.cb = cb;
    i2c_xfer_ex(addr, buf, wlen, rlen, timeout, &xfr2.job, i2cfunc, &xfr2.status);
}

void i2c_abort (void) {
    hal_disableIRQs();
    i2c_stop(I2C_ABORT);
    hal_enableIRQs();
}

#endif
