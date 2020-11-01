/*******************************************************************************
 * Copyright (c) 2015 Matthijs Kooijman
 *
 * --- Revised 3-Clause BSD License ---
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SEMTECH BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This the HAL to run LMIC on top of the Arduino environment.
 *******************************************************************************/

#define _GNU_SOURCE 1 // For fopencookie
// Must be first, otherwise it might have already been included without _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE
// Prevent warning on samd where samd21g18a.h from CMSIS defines this
#undef LITTLE_ENDIAN
#include <Arduino.h>
#include <SPI.h>
#include "../basicmac.h"
#include "hal.h"

// Datasheet defins typical times until busy goes low. Most are < 200us,
// except when waking up from sleep, which typically takes 3500us. Since
// we cannot know here if we are in sleep, we'll have to assume we are.
// Since 3500 is typical, not maximum, wait a bit more than that.
static unsigned long MAX_BUSY_TIME = 5000;

// -----------------------------------------------------------------------------
// I/O

static void hal_io_init () {
    uint8_t i;
    // Checks below assume that all special pin values are >= LMIC_UNUSED_PIN, so check that.
    #if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
    LMIC_STATIC_ASSERT(LMIC_UNUSED_PIN < LMIC_CONTROLLED_BY_DIO3, "Unexpected constant values");
    LMIC_STATIC_ASSERT(LMIC_UNUSED_PIN < LMIC_CONTROLLED_BY_DIO2, "Unexpected constant values");
    #endif // defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)

    ASSERT(lmic_pins.nss < LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.rst <= LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.rx <= LMIC_UNUSED_PIN);

#if defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)
    //DIO0 is required, DIO1 is required for LoRa, DIO2 for FSK
    ASSERT(lmic_pins.dio[0] < LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.dio[1] < LMIC_UNUSED_PIN || lmic_pins.dio[2] < LMIC_UNUSED_PIN);

    ASSERT(lmic_pins.busy == LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.tcxo == LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.tx <= LMIC_UNUSED_PIN);
#elif defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
    // Only DIO1 should be specified
    ASSERT(lmic_pins.dio[0] == LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.dio[1] < LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.dio[2] == LMIC_UNUSED_PIN);

    ASSERT(lmic_pins.busy <= LMIC_UNUSED_PIN);
    ASSERT(lmic_pins.tcxo == LMIC_UNUSED_PIN || lmic_pins.tcxo == LMIC_CONTROLLED_BY_DIO3);
    ASSERT(lmic_pins.tx <= LMIC_UNUSED_PIN || lmic_pins.tx == LMIC_CONTROLLED_BY_DIO2);
#else
    #error "Unknown radio type?"
#endif

    // Write HIGH to deselect (NSS is active low). Do this before
    // setting output, to prevent a moment of OUTPUT LOW on e.g. AVR.
    digitalWrite(lmic_pins.nss, HIGH);
    pinMode(lmic_pins.nss, OUTPUT);
    // Write HIGH again after setting output, for architectures that
    // reset to LOW when setting OUTPUT (e.g. arduino-STM32L4).
    digitalWrite(lmic_pins.nss, HIGH);

    if (lmic_pins.tx < LMIC_UNUSED_PIN)
        pinMode(lmic_pins.tx, OUTPUT);
    if (lmic_pins.rx < LMIC_UNUSED_PIN)
        pinMode(lmic_pins.rx, OUTPUT);
    if (lmic_pins.rst < LMIC_UNUSED_PIN)
        pinMode(lmic_pins.rst, OUTPUT);
    if (lmic_pins.busy < LMIC_UNUSED_PIN)
        pinMode(lmic_pins.busy, INPUT);
    if (lmic_pins.tcxo < LMIC_UNUSED_PIN)
        pinMode(lmic_pins.tcxo, OUTPUT);

    for (i = 0; i < NUM_DIO; ++i) {
        if (lmic_pins.dio[i] < LMIC_UNUSED_PIN)
            pinMode(lmic_pins.dio[i], INPUT);
    }
}

// rx = 0, tx = 1, off = -1
void hal_ant_switch (u1_t val) {
    // TODO: Support separate pin for TX2 (PA_BOOST output)
    if (lmic_pins.tx < LMIC_UNUSED_PIN)
        digitalWrite(lmic_pins.tx, val == HAL_ANTSW_TX || val == HAL_ANTSW_TX2);
    if (lmic_pins.rx < LMIC_UNUSED_PIN)
        digitalWrite(lmic_pins.rx, val == HAL_ANTSW_RX);
}

// set radio RST pin to given value (or keep floating!)
bool hal_pin_rst (u1_t val) {
    if (lmic_pins.rst >= LMIC_UNUSED_PIN)
        return false;

    if(val == 0 || val == 1) { // drive pin
        pinMode(lmic_pins.rst, OUTPUT);
        digitalWrite(lmic_pins.rst, val);
    } else { // keep pin floating
        pinMode(lmic_pins.rst, INPUT);
    }
    return true;
}

void hal_irqmask_set (int /* mask */) {
    // Not implemented
}

static bool dio_states[NUM_DIO] = {0};

static void hal_io_check() {
    uint8_t i;
    for (i = 0; i < NUM_DIO; ++i) {
        if (lmic_pins.dio[i] >= LMIC_UNUSED_PIN)
            continue;

        if (dio_states[i] != digitalRead(lmic_pins.dio[i])) {
            dio_states[i] = !dio_states[i];
            if (dio_states[i])
                radio_irq_handler(i, hal_ticks());
        }
    }
}

#if defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)
bool hal_pin_tcxo (u1_t val) {
    if (lmic_pins.tcxo >= LMIC_UNUSED_PIN)
        return false;

    digitalWrite(lmic_pins.tcxo, val);
    return true;
}
#endif // defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)

void hal_pin_busy_wait (void) {
    if (lmic_pins.busy >= LMIC_UNUSED_PIN) {
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

#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
bool hal_dio3_controls_tcxo (void) {
    return lmic_pins.tcxo == LMIC_CONTROLLED_BY_DIO3;
}
bool hal_dio2_controls_rxtx (void) {
    return lmic_pins.tx == LMIC_CONTROLLED_BY_DIO2;
}
#endif // defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)

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
    // Because micros() is scaled down in this function, micros() will
    // overflow before the tick timer should, causing the tick timer to
    // miss a significant part of its values if not corrected. To fix
    // this, the "overflow" serves as an overflow area for the micros()
    // counter. It consists of three parts:
    //  - The US_PER_OSTICK upper bits are effectively an extension for
    //    the micros() counter and are added to the result of this
    //    function.
    //  - The next bit overlaps with the most significant bit of
    //    micros(). This is used to detect micros() overflows.
    //  - The remaining bits are always zero.
    //
    // By comparing the overlapping bit with the corresponding bit in
    // the micros() return value, overflows can be detected and the
    // upper bits are incremented. This is done using some clever
    // bitwise operations, to remove the need for comparisons and a
    // jumps, which should result in efficient code. By avoiding shifts
    // other than by multiples of 8 as much as possible, this is also
    // efficient on AVR (which only has 1-bit shifts).
    static uint8_t overflow = 0;

    // Scaled down timestamp. The top US_PER_OSTICK_EXPONENT bits are 0,
    // the others will be the lower bits of our return value.
    uint32_t scaled = micros() >> US_PER_OSTICK_EXPONENT;
    // Most significant byte of scaled
    uint8_t msb = scaled >> 24;
    // Mask pointing to the overlapping bit in msb and overflow.
    const uint8_t mask = (1 << (7 - US_PER_OSTICK_EXPONENT));
    // Update overflow. If the overlapping bit is different
    // between overflow and msb, it is added to the stored value,
    // so the overlapping bit becomes equal again and, if it changed
    // from 1 to 0, the upper bits are incremented.
    overflow += (msb ^ overflow) & mask;

    // Return the scaled value with the upper bits of stored added. The
    // overlapping bit will be equal and the lower bits will be 0, so
    // bitwise or is a no-op for them.
    return scaled | ((uint32_t)overflow << 24);

    // 0 leads to correct, but overly complex code (it could just return
    // micros() unmodified), 8 leaves no room for the overlapping bit.
    static_assert(US_PER_OSTICK_EXPONENT > 0 && US_PER_OSTICK_EXPONENT < 8, "Invalid US_PER_OSTICK_EXPONENT value");
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

void hal_watchcount (int /* cnt */) {
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

#if !defined(CFG_DEBUG_STREAM)
#error "CFG_DEBUG needs CFG_DEBUG_STREAM defined in target-config.h"
#endif

void hal_debug_str (const char* str) {
    CFG_DEBUG_STREAM.print(str);
}

void hal_debug_led (int val) {
    #ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, val);
    #endif
}
#endif // CFG_DEBUG

// -----------------------------------------------------------------------------

#if defined(LMIC_PRINTF_TO)
#if defined(__AVR__)
// On AVR, use the AVR-specific fdev_setup_stream to redirect stdout
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
#else
// On non-AVR platforms, use the somewhat more complex "cookie"-based
// approach to custom streams. This is a GNU-specific extension to libc.
static ssize_t uart_putchar (void *, const char *buf, size_t len) {
    auto res = LMIC_PRINTF_TO.write(buf, len);
    // Since the C interface has no meaningful way to flush (fflush() is a
    // no-op on AVR since stdio does not introduce any buffering), just flush
    // every byte.
    LMIC_PRINTF_TO.flush();
    return res;
}

static cookie_io_functions_t functions = {
    .read = NULL,
    .write = uart_putchar,
    .seek = NULL,
    .close = NULL
};

void hal_printf_init() {
    stdout = fopencookie(NULL, "w", functions);
    // Disable buffering, so the callbacks get called right away
    setbuf(stdout, nullptr);
}
#endif // !defined(__AVR__)
#endif // defined(LMIC_PRINTF_TO)

void hal_init (void * /* bootarg */) {
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
#ifdef CFG_DEBUG
    CFG_DEBUG_STREAM.flush();
#endif
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

void hal_setBattLevel (u1_t /* level */) {
    // Not implemented
}

void hal_fwinfo (hal_fwi* /* fwi */) {
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
