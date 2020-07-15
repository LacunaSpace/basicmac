/*******************************************************************************
 * Copyright (c) 2015 Matthijs Kooijman
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
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
