// This file defines pinmaps, which tell basicmac what pins to use to
// talk to the radio.
//
// Below, a lot of different pinmaps for various standard boards are
// defined. These are used when USE_STANDARD_PINMAP is defined,
// otherwise the main sketch file should define its own pinmap.
//
// These pinmaps live in their own file, to make it easier to share them
// between example sketches.

#if defined(USE_STANDARD_PINMAP)

#if defined(BASICMAC_DUMMY_PINMAP)
// Dummy minimal pinmap, just used for CI compile-testing.
const lmic_pinmap lmic_pins = {
    .nss = 10,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    // RST is hardwarid to MCU reset
    .rst = LMIC_UNUSED_PIN,
    .dio = {1, 2, 3},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(ARDUINO_AVR_MINI)
#if !defined(BRD_sx1276_radio)
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
// Assume this is a Nexus board
const lmic_pinmap lmic_pins = {
    .nss = 10,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    // RST is hardwarid to MCU reset
    .rst = LMIC_UNUSED_PIN,
    .dio = {4, 5, 7},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(ARDUINO_MJS_V1)
#if !defined(BRD_sx1276_radio)
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
// https://github.com/meetjestad/mjs_pcb
const lmic_pinmap lmic_pins = {
    .nss = 10,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    .rst = 9,
    .dio = {2, 3, 4},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(ADAFRUIT_FEATHER_M0)
#if !defined(BRD_sx1276_radio)
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
// Assume this a Feather M0 LoRa
const lmic_pinmap lmic_pins = {
    .nss = 8,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {3, 5, 6},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(ARDUINO_STM32L4_LS200)
#if !defined(BRD_sx1262_radio)
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
// LacunaSpace LS200 development board
// Uses SPI bus at PC10/11/12
// This uses E22_* constants from the board variant file
const lmic_pinmap lmic_pins = {
    .nss = E22_NSS, // PD2
    // TXEN is controlled through DIO2 by the SX1262 directly
    .tx = LMIC_UNUSED_PIN,
    .rx = E22_RXEN, // PC4
    .rst = E22_NRST, // PA4
    .dio = {LMIC_UNUSED_PIN, E22_DIO1 /* PC7 */, LMIC_UNUSED_PIN},
    .busy = E22_BUSY, // PB12
    // TCXO is controlled through DIO3 by the SX1262 directly
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(ARDUINO_TTGO_LoRa32_V1)
#if !defined(BRD_sx1276_radio)
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
// Assume this is a Nexus board
const lmic_pinmap lmic_pins = {
    .nss = 18,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    // RST is hardwarid to MCU reset
    .rst = 14,
    .dio = {26, 33, 32},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#else
#error "Unknown board, no standard pimap available. Define your own in the main sketch file."
#endif
#endif // defined(USE_STANDARD_PINMAP)
