#ifndef _lmic_arduino_hal_config_h_
#define _lmic_arduino_hal_config_h_

// This defines the region(s) to use. You can enable more than one and
// then select the right region at runtime using os_getRegion() and/or
// LMIC_reset_ex().
#if !defined(CFG_eu868) && !defined(CFG_us915)
#define CFG_eu868 1
//#define CFG_us915 1
//#define CFG_as923 1
//#define CFG_il915 1
//#define CFG_kr920 1
//#define CFG_au915 1
#endif // !defined(CFG_eu868) && !defined(CFG_us915)

#define CFG_autojoin

#if !defined(BRD_sx1272_radio) && !defined(BRD_sx1276_radio) && !defined(BRD_sx1261_radio) && !defined(BRD_sx1262_radio)
// This is the SX1272/SX1273 radio, which is also used on the HopeRF
// RFM92 boards.
//#define BRD_sx1272_radio 1
// This is the SX1276/SX1277/SX1278/SX1279 radio, which is also used on
// the HopeRF RFM95 boards.
//#define BRD_sx1276_radio 1
// This is the newer SX1261 radio (up to +15dBM).
//#define BRD_sx1261_radio 1
// This is the newer SX1262 radio (up to +22dBM).
#define BRD_sx1262_radio 1
#endif // !defined(BRD_sx1272_radio) && !defined(BRD_sx1276_radio) && !defined(BRD_sx1262_radio)

// 16 μs per tick
// LMIC requires ticks to be 15.5μs - 100 μs long
#define US_PER_OSTICK_EXPONENT 4
#define US_PER_OSTICK (1 << US_PER_OSTICK_EXPONENT)
#define OSTICKS_PER_SEC (1000000 / US_PER_OSTICK)

// When this is defined, some debug output will be printed and
// debug_printf(...) is available (which is a slightly non-standard
// printf implementation).
// Without this, assertion failures are *not* printed!
#define CFG_DEBUG
// When this is defined, additional debug output is printed.
//#define CFG_DEBUG_VERBOSE
// Debug output (and assertion failures) are printed to this Stream
#define CFG_DEBUG_STREAM Serial
// Define these to add some TX or RX specific debug output (needs
// CFG_DEBUG)
#define DEBUG_TX
#define DEBUG_RX
// Define these to add some job scheduling specific debug output (needs
// CFG_DEBUG_VERBOSE)
//#define DEBUG_JOBS
// Uncomment to display timestamps in ticks rather than milliseconds
//#define CFG_DEBUG_RAW_TIMESTAMPS

// When this is defined, the standard libc printf function will print to
// this Stream. You should probably use CFG_DEBUG and debug_printf()
// instead, though.
//#define LMIC_PRINTF_FO

// Remove/comment this to enable code related to beacon tracking.
#define DISABLE_CLASSB

// This allows choosing between multiple included AES implementations.
// Make sure exactly one of these is uncommented.
//
// This selects the original AES implementation included LMIC. This
// implementation is optimized for speed on 32-bit processors using
// fairly big lookup tables, but it takes up big amounts of flash on the
// AVR architecture.
// #define USE_ORIGINAL_AES
//
// This selects the AES implementation written by Ideetroon for their
// own LoRaWAN library. It also uses lookup tables, but smaller
// byte-oriented ones, making it use a lot less flash space (but it is
// also about twice as slow as the original).
#define USE_IDEETRON_AES

#endif // _lmic_arduino_hal_config_h_
