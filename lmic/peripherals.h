// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _peripherals_h_
#define _peripherals_h_

#include "hw.h" // provided by HAL


#ifdef PERIPH_EEPROM
// ------------------------------------------------
// EEPROM

void eeprom_write (void* dest, unsigned int val);
void eeprom_copy (void* dest, const void* src, int len);

#endif

#ifdef PERIPH_FLASH
// ------------------------------------------------
// Flash

void flash_write (void* dst, const void* src, unsigned int nwords, bool erase);

#endif

#ifdef PERIPH_USART
// ------------------------------------------------
// USART

enum {
    USART_CONTINUE = 0,
    USART_DONE = -1,
    USART_ERROR = -1,
};

typedef int (*usart_rx_func) (int ch, void* arg);
typedef int (*usart_tx_func) (int status, void* arg);
void usart_cfg (unsigned int br);
void usart_recv (usart_rx_func rx, void* arg);
void usart_send (usart_tx_func tx, void* arg);
void usart_abort_recv (void);

#endif


#ifdef PERIPH_PIO
// ------------------------------------------------
// Programmable I/O

enum {
    PIO_INP_HIZ = -1,           // ..111b
    PIO_INP_PUP = -2,           // ..110b
    PIO_INP_PDN = -3,           // ..101b
    PIO_INP_ANA = -5,           // ..011b
};

void pio_set (unsigned int pin, int value);
int pio_get (unsigned int pin);

#endif


#ifdef PERIPH_CRC
// ------------------------------------------------
// CRC engine (32bit aligned words only)

unsigned int crc32 (void* ptr, int nwords);

#endif

#ifdef PERIPH_SHA256
// ------------------------------------------------
// SHA-256 engine

void sha256 (uint32_t* hash, const uint8_t* msg, uint32_t len);

#endif


#ifdef PERIPH_TRNG
// ------------------------------------------------
// True RNG engine

void trng_next (uint32_t* dest, int count);

#endif


#ifdef PERIPH_I2C
// ------------------------------------------------
// I²C perpipheral

enum {
    I2C_BUSY    = 1,
    I2C_OK      = 0,
    I2C_NAK     = -1,
    I2C_ABORT   = -2,
};

typedef void (*i2c_cb) (int status);
void i2c_xfer (unsigned int addr, unsigned char* buf, unsigned int wlen, unsigned int rlen,
        i2c_cb cb, ostime_t timeout);
void i2c_xfer_ex (unsigned int addr, unsigned char* buf, unsigned int wlen, unsigned int rlen,
        ostime_t timeout, osjob_t* job, osjobcb_t cb, int* pstatus);
void i2c_abort (void);

#endif


#ifdef PERIPH_ADC
// ------------------------------------------------
// Analog-to-Digital Converter

unsigned int adc_read (unsigned int chnl, unsigned int rate);

#endif

#endif
