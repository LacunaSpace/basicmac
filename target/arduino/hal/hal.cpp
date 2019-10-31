/*******************************************************************************
 * Copyright (c) 2015 Matthijs Kooijman
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * This the HAL to run LMIC on top of the Arduino environment.
 *******************************************************************************/

#include <Arduino.h>
#include <SPI.h>
#include "../lmic.h"
#include "hal.h"
#include <stdio.h>

// Datasheet defins typical times until busy goes low. Most are < 200us,
// except when waking up from sleep, which typically takes 3500us. Since
// we cannot know here if we are in sleep, we'll have to assume we are.
// Since 3500 is typical, not maximum, wait a bit more than that.
static unsigned long MAX_BUSY_TIME = 5000;

// -----------------------------------------------------------------------------
// I/O

static void hal_io_init () {
    uint8_t i;
    ASSERT(lmic_pins.nss != LMIC_UNUSED_PIN);

#if defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)
    //DIO0 is required, DIO1 is required for LoRa, DIO2 for FSK
    ASSERT(lmic_pins.dio[0] != LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.dio[1] != LMIC_UNUSED_PIN || lmic_pins.dio[2] != LMIC_UNUSED_PIN);
#elif defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
    // Only DIO1 should be specified
    ASSERT(lmic_pins.dio[0] == LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.dio[1] != LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.dio[2] == LMIC_UNUSED_PIN);
#else
    #error "Unknown radio type?"
#endif

#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
    ASSERT(lmic_pins.busy != LMIC_UNUSED_PIN);
#endif

    pinMode(lmic_pins.nss, OUTPUT);
    if (lmic_pins.rxtx != LMIC_UNUSED_PIN)
        pinMode(lmic_pins.rxtx, OUTPUT);
    if (lmic_pins.rst != LMIC_UNUSED_PIN)
        pinMode(lmic_pins.rst, OUTPUT);

    for (i = 0; i < NUM_DIO; ++i) {
        if (lmic_pins.dio[i] != LMIC_UNUSED_PIN)
            pinMode(lmic_pins.dio[i], INPUT);
    }
}

// val == 1  => tx 1
void hal_pin_rxtx (s1_t val) {
    if (lmic_pins.rxtx != LMIC_UNUSED_PIN)
        digitalWrite(lmic_pins.rxtx, val);
}

// set radio RST pin to given value (or keep floating!)
bool hal_pin_rst (u1_t val) {
    if (lmic_pins.rst == LMIC_UNUSED_PIN)
        return false;

    if(val == 0 || val == 1) { // drive pin
        pinMode(lmic_pins.rst, OUTPUT);
        digitalWrite(lmic_pins.rst, val);
    } else { // keep pin floating
        pinMode(lmic_pins.rst, INPUT);
    }
    return true;
}

void hal_irqmask_set (int mask) {
    // Not implemented
}

static bool dio_states[NUM_DIO] = {0};

static void hal_io_check() {
    uint8_t i;
    for (i = 0; i < NUM_DIO; ++i) {
        if (lmic_pins.dio[i] == LMIC_UNUSED_PIN)
            continue;

        if (dio_states[i] != digitalRead(lmic_pins.dio[i])) {
            dio_states[i] = !dio_states[i];
            if (dio_states[i])
                radio_irq_handler(i, hal_ticks());
        }
    }
}

bool hal_pin_tcxo (u1_t val) {
    // Not implemented
    return false;
}

void hal_pin_busy_wait (void) {
    if (lmic_pins.busy == LMIC_UNUSED_PIN) {
        // TODO: We could probably keep some state so we know the chip
        // is in sleep, since otherwise the delay can be much shorter.
        // Also, all delays after commands (rather than waking up from
        // sleep) are measured from the *end* of the previous SPI
        // transaction, so we could wait shorter if we remember when
        // that was.
        delayMicroseconds(MAX_BUSY_TIME);
    } else {
        unsigned long start = micros();

        while((micros() - start) < MAX_BUSY_TIME && digitalRead(lmic_pins.busy)) /* wait */;
    }
}

// -----------------------------------------------------------------------------
// SPI

static const SPISettings settings(10E6, MSBFIRST, SPI_MODE0);

static void hal_spi_init () {
    SPI.begin();
}

void hal_spi_select (int on) {
    if (on)
        SPI.beginTransaction(settings);
    else
        SPI.endTransaction();

    //Serial.println(val?">>":"<<");
    digitalWrite(lmic_pins.nss, !on);
}

// perform SPI transaction with radio
u1_t hal_spi (u1_t out) {
    u1_t res = SPI.transfer(out);
/*
    Serial.print(">");
    Serial.print(out, HEX);
    Serial.print("<");
    Serial.println(res, HEX);
    */
    return res;
}

// -----------------------------------------------------------------------------
// TIME

static void hal_time_init () {
    // Nothing to do
}

u4_t hal_ticks () {
    return micros();
    static_assert(US_PER_OSTICK == 1, "Invalid US_PER_OSTICK value");
}

u8_t hal_xticks (void) {
    // TODO
    return hal_ticks();
}
/* Not actually used now
s2_t hal_subticks (void) {
    // TODO
    return 0;
}
*/

// Returns the number of ticks until time. Negative values indicate that
// time has already passed.
static s4_t delta_time(u4_t time) {
    return (s4_t)(time - hal_ticks());
}

void hal_waitUntil (u4_t time) {
    s4_t delta = delta_time(time);
    // From delayMicroseconds docs: Currently, the largest value that
    // will produce an accurate delay is 16383.
    while (delta > (16000 / US_PER_OSTICK)) {
        delay(16);
        delta -= (16000 / US_PER_OSTICK);
    }
    if (delta > 0)
        delayMicroseconds(delta * US_PER_OSTICK);
}

// check and rewind for target time
u1_t hal_checkTimer (u4_t time) {
    // No need to schedule wakeup, since we're not sleeping
    return delta_time(time) <= 0;
}

static uint8_t irqlevel = 0;

void hal_disableIRQs () {
    noInterrupts();
    irqlevel++;
}

void hal_enableIRQs () {
    if(--irqlevel == 0) {
        interrupts();

        // Instead of using proper interrupts (which are a bit tricky
        // and/or not available on all pins on AVR), just poll the pin
        // values. Since os_runloop disables and re-enables interrupts,
        // putting this here makes sure we check at least once every
        // loop.
        //
        // As an additional bonus, this prevents the can of worms that
        // we would otherwise get for running SPI transfers inside ISRs
        hal_io_check();
    }
}

u1_t hal_sleep (u1_t type, u4_t targettime) {
    // Actual sleeping not implemented, but jobs are only run when this
    // function returns 0, so make sure we only do that when the
    // targettime is close. When asked to sleep forever (until woken up
    // by an interrupt), just return immediately to keep polling.
    if (type == HAL_SLEEP_FOREVER)
        return 0;

    // TODO: What value should we use for "close"?
    return delta_time(targettime) < 10 ? 0 : 1;
}

void hal_watchcount (int cnt) {
    // Not implemented
}

// -----------------------------------------------------------------------------
// DEBUG

#ifdef CFG_DEBUG
static void hal_debug_init() {
    #ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    #endif
}

void hal_debug_str (const char* str) {
    Serial.print(str);
}

void hal_debug_led (int val) {
    #ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, val);
    #endif
}
#endif

// -----------------------------------------------------------------------------

#if defined(LMIC_PRINTF_TO)
static int uart_putchar (char c, FILE *)
{
    LMIC_PRINTF_TO.write(c) ;
    return 0 ;
}

void hal_printf_init() {
    // create a FILE structure to reference our UART output function
    static FILE uartout;
    memset(&uartout, 0, sizeof(uartout));

    // fill in the UART file descriptor with pointer to writer.
    fdev_setup_stream (&uartout, uart_putchar, NULL, _FDEV_SETUP_WRITE);

    // The uart is the standard output device STDOUT.
    stdout = &uartout ;
}
#endif // defined(LMIC_PRINTF_TO)

void hal_init (void *bootarg) {
    // configure radio I/O and interrupt handler
    hal_io_init();
    // configure radio SPI
    hal_spi_init();
    // configure timer and interrupt handler
    hal_time_init();
#if defined(LMIC_PRINTF_TO)
    // printf support
    hal_printf_init();
#endif
#ifdef CFG_DEBUG
    hal_debug_init();
#endif
}

void hal_failed () {
    Serial.flush();
    // keep IRQs enabled, to allow e.g. USB to continue to run and allow
    // firmware uploads on boards with native USB.
    while(1);
}

void hal_reboot (void) {
    // TODO
    hal_failed();
}

u1_t hal_getBattLevel (void) {
    // Not implemented
    return 0;
}

void hal_setBattLevel (u1_t level) {
    // Not implemented
}

void hal_fwinfo (hal_fwi* fwi) {
    // Not implemented
}

u1_t* hal_joineui (void) {
    return nullptr;
}

u1_t* hal_deveui (void) {
    return nullptr;
}

u1_t* hal_nwkkey (void) {
    return nullptr;
}

u1_t* hal_appkey (void) {
    return nullptr;
}

u1_t* hal_serial (void) {
    return nullptr;
}

u4_t  hal_region (void) {
    return 0;
}

u4_t  hal_hwid (void) {
    return 0;
}

u4_t  hal_unique (void) {
    return 0;
}

u4_t hal_dnonce_next (void) {
    return os_getRndU2();
}
