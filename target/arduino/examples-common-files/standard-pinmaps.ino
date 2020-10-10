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
    // RST is hardwired to MCU reset
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
// https://github.com/Ideetron/RFM95W_Nexus/blob/master/RFM95W_NEXUS_02.pdf
// Note: BasicMAC is currently too big to fit into the 328p on this board.
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
// Note: BasicMAC is currently too big to fit into the 328p on this board.
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
// https://learn.adafruit.com/adafruit-feather-m0-radio-with-lora-radio-module/pinouts
const lmic_pinmap lmic_pins = {
    .nss = 8,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    .rst = 4,
    // This needs additional external connections: DIO1 (labeled IO1 in
    // docs and board) to 5 and DIO2 (labeled D2 on the board and IO1 in
    // the docs) to 6. DIO0 is internally connected.
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
    // DIO2 connected to TXEN on LS200 board
    .tx = LMIC_CONTROLLED_BY_DIO2,
    .rx = E22_RXEN, // PC4
    .rst = E22_NRST, // PA4
    .dio = {LMIC_UNUSED_PIN, E22_DIO1 /* PC7 */, LMIC_UNUSED_PIN},
    .busy = E22_BUSY, // PB12
    // DIO3 connected to TCXO on E22 board
    .tcxo = LMIC_CONTROLLED_BY_DIO3,
};
#elif defined(ARDUINO_TTGO_LoRa32_V1)
#if !defined(BRD_sx1276_radio)
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
// https://github.com/LilyGO/TTGO-LORA32/tree/LilyGO-868-V1.0
// Pinout: https://github.com/LilyGO/TTGO-LORA32/blob/LilyGO-868-V1.0/images/image1.jpg
const lmic_pinmap lmic_pins = {
    .nss = 18,
    // RX/TX is controlled through RXTX by the SX1272 directly on the RFM95W
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(ARDUINO_TBeam)
// TTGO T-Beam development board
// https://github.com/LilyGO/TTGO-T-Beam
// This board is available with an SX1276 or SX1262 soldered on. The board is
// otherwise identical and does not have a separate board entry in the
// Arduino IDE, so decide based on the radio constant which one to use.
//
// Uses LoRa SPI bus at 5/19/27
// This uses LORA_* constants from the board variant file
#if defined(BRD_sx1276_radio)
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS, // 18
    // RX/TX is controlled through RXTX by the SX1276/8 directly on the HPD13/4A
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    .rst = LORA_RST, // 23
    .dio = {LORA_IO0 /* 26 */ , LORA_IO1 /* 33 */, LORA_IO2 /* 32 */},
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};
#elif defined(BRD_sx1262_radio)
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS, // 18
    // TXEN is controlled through DIO2 by the SX1262 (HPD16A) directly
    .tx = LMIC_CONTROLLED_BY_DIO2,
    .rx = LMIC_UNUSED_PIN,
    .rst = LORA_RST, // 23
    .dio = {LMIC_UNUSED_PIN, LORA_IO1  /* 33 */, LMIC_UNUSED_PIN},
    .busy = LORA_IO2, // 32
    // TCXO is controlled through DIO3 by the SX1262 directly
    .tcxo = LMIC_CONTROLLED_BY_DIO3,
};
#else
#error "Wrong radio defined for this board (fix in BasicMAC target-config.h)"
#endif
#else
#error "Unknown board, no standard pimap available. Define your own in the main sketch file."
#endif
#endif // defined(USE_STANDARD_PINMAP)
