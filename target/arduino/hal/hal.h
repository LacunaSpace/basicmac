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
#ifndef _hal_hal_h_
#define _hal_hal_h_

static const int NUM_DIO = 3;

struct lmic_pinmap {
    u1_t nss;
    // Written HIGH in TX mode, LOW otherwise.
    // Typically used with a single RXTX switch pin.
    u1_t tx;
    // Written HIGH in RX mode, LOW otherwise.
    // Typicaly used with separate RX/TX pins, to allow switching off
    // the antenna switch completely.
    u1_t rx;
    u1_t rst;
    u1_t dio[NUM_DIO];
    u1_t busy;
    u1_t tcxo;
};

// Use this for any unused pins.
const u1_t LMIC_UNUSED_PIN = 0xfd;

#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
// Used for lmic_pinmap.tcxo only
const u1_t LMIC_CONTROLLED_BY_DIO3 = 0xff;
const u1_t LMIC_CONTROLLED_BY_DIO2 = 0xfe;
#endif // defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)

// Declared here, to be defined an initialized by the application
extern const lmic_pinmap lmic_pins;

#endif // _hal_hal_h_
