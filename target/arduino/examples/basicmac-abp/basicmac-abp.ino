/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with payload "Hello,
 * world!", using frequency and encryption settings matching those of
 * the The Things Network.
 *
 * This uses ABP (Activation-by-personalisation), where a DevAddr and
 * Session keys are preconfigured (unlike OTAA, where a DevEUI and
 * application key is configured, while the DevAddr and session keys are
 * assigned/generated in the over-the-air-activation procedure).
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
 * g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
 * violated by this sketch when left running for longer)!
 *
 * To use this sketch, first register your application and device with
 * the things network, to set or generate a DevAddr, NwkSKey and
 * AppSKey. Each device should have their own unique values for these
 * fields.
 *
 * Do not forget to define the radio type correctly in config.h.
 *
 *******************************************************************************/

#include <basicmac.h>
#include <hal/hal.h>
#include <SPI.h>

// LoRaWAN NwkSKey, network session key
// This is the default Semtech key, which is used by the early prototype TTN
// network.
static const PROGMEM u1_t NWKSKEY[16] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };

// LoRaWAN AppSKey, application session key
// This is the default Semtech key, which is used by the early prototype TTN
// network.
static const u1_t PROGMEM APPSKEY[16] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };

// LoRaWAN end-device address (DevAddr)
static const u4_t DEVADDR = 0x03FF0001 ; // <-- Change this address for every node!

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getJoinEui (u1_t* /* buf */) { }
void os_getDevEui (u1_t* /* buf */) { }
void os_getNwkKey (u1_t* /* buf */) { }

// The region to use, this just uses the first one (can be changed if
// multiple regions are enabled).
u1_t os_getRegion (void) { return LMIC_regionCode(0); }

// Schedule TX every this many milliseconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60000;

// When this is defined, a standard pinmap from standard-pinmaps.ino
// will be used.  If you need to use a custom pinmap, comment this line
// and enter the pin numbers in the lmic_pins variable below.
#define USE_STANDARD_PINMAP

#if !defined(USE_STANDARD_PINMAP)
// All pin assignments use Arduino pin numbers (e.g. what you would pass
// to digitalWrite), or LMIC_UNUSED_PIN when a pin is not connected.
const lmic_pinmap lmic_pins = {
    // NSS input pin for SPI communication (required)
    .nss = 0,
    // If needed, these pins control the RX/TX antenna switch (active
    // high outputs). When you have both, the antenna switch can
    // powerdown when unused. If you just have a RXTX pin it should
    // usually be assigned to .tx, reverting to RX mode when idle).
    //
    // The SX127x has an RXTX pin that can automatically control the
    // antenna switch (if internally connected on the transceiver
    // board). This pin is always active, so no configuration is needed
    // for that here.
    // On SX126x, the DIO2 can be used for the same thing, but this is
    // disabled by default. To enable this, set .tx to
    // LMIC_CONTROLLED_BY_DIO2 below (this seems to be common and
    // enabling it when not needed is probably harmless, unless DIO2 is
    // connected to GND or VCC directly inside the transceiver board).
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    // Radio reset output pin (active high for SX1276, active low for
    // others). When omitted, reset is skipped which might cause problems.
    .rst = 1,
    // DIO input pins.
    //   For SX127x, LoRa needs DIO0 and DIO1, FSK needs DIO0, DIO1 and DIO2
    //   For SX126x, Only DIO1 is needed (so leave DIO0 and DIO2 as LMIC_UNUSED_PIN)
    .dio = {/* DIO0 */ 2, /* DIO1 */ 3, /* DIO2 */ 4},
    // Busy input pin (SX126x only). When omitted, a delay is used which might
    // cause problems.
    .busy = LMIC_UNUSED_PIN,
    // TCXO oscillator enable output pin (active high).
    //
    // For SX127x this should be an I/O pin that controls the TCXO, or
    // LMIC_UNUSED_PIN when a crystal is used instead of a TCXO.
    //
    // For SX126x this should be LMIC_CONTROLLED_BY_DIO3 when a TCXO is
    // directly connected to the transceiver DIO3 to let the transceiver
    // start and stop the TCXO, or LMIC_UNUSED_PIN when a crystal is
    // used instead of a TCXO. Controlling the TCXO from the MCU is not
    // supported.
    .tcxo = LMIC_UNUSED_PIN,
};
#endif // !defined(USE_STANDARD_PINMAP)

void onLmicEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
        case EV_SCAN_FOUND:
            Serial.println(F("EV_SCAN_FOUND"));
            break;
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            break;
        case EV_TXDONE:
            Serial.println(F("EV_TXDONE"));
            break;
        case EV_DATARATE:
            Serial.println(F("EV_DATARATE"));
            break;
        case EV_START_SCAN:
            Serial.println(F("EV_START_SCAN"));
            break;
        case EV_ADR_BACKOFF:
            Serial.println(F("EV_ADR_BACKOFF"));
            break;
         default:
            Serial.print(F("Unknown event: "));
            Serial.println(ev);
            break;
    }
}

void setup() {
    Serial.begin(115200);

    // Wait up to 5 seconds for serial to be opened, to allow catching
    // startup messages on native USB boards (that do not reset when
    // serial is opened).
    unsigned long start = millis();
    while (millis() - start < 5000 && !Serial);

    Serial.println();
    Serial.println();
    Serial.println(F("Starting"));
    Serial.println();

    // LMIC init
    os_init(nullptr);
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    #ifdef PROGMEM
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);
    #else
    // If not running an AVR with PROGMEM, just use the arrays directly
    LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
    #endif

    #if defined(CFG_eu868)
    // These are defined by the LoRaWAN specification
    enum {
        EU_DR_SF12 = 0,
        EU_DR_SF11 = 1,
        EU_DR_SF10 = 2,
        EU_DR_SF9 = 3,
        EU_DR_SF8 = 4,
        EU_DR_SF7 = 5,
        EU_DR_SF7_BW250 = 6,
        EU_DR_FSK = 7,
    };

    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set.
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7_BW250)); // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(EU_DR_FSK,  EU_DR_FSK));      // g2-band

    // TTN uses SF9 at 869.525Mhz for its RX2 window (frequency is
    // default LoRaWAN, SF is different, but set them both to be
    // explicit).
    LMIC.dn2Freq = 869525000;
    LMIC.dn2Dr = EU_DR_SF9;

    // Set data rate for uplink
    LMIC_setDrTxpow(EU_DR_SF7, KEEP_TXPOWADJ);
    #elif defined(CFG_us915)
    // NA-US channels 0-71 are configured automatically
    // but only one group of 8 should (a subband) should be active
    // TTN recommends the second sub band, 1 in a zero based count.
    // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
    // TODO: How to configure these channels? LMIC had LMIC_selectSubBand,
    // but it seems BasicMac only has LMIC_disableChannel.
    #endif

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // Enable this to increase the receive window size, to compensate
    // for an inaccurate clock.  // This compensate for +/- 10% clock
    // error, a lower value will likely be more appropriate.
    //LMIC_setClockError(MAX_CLOCK_ERROR * 10 / 100);

    // Queue first packet
    send_packet();
}

uint32_t last_packet = 0;

void loop() {
    // Let LMIC handle background tasks
    os_runstep();

    // If TX_INTERVAL passed, *and* our previous packet is not still
    // pending (which can happen due to duty cycle limitations), send
    // the next packet.
    if (millis() - last_packet > TX_INTERVAL && !(LMIC.opmode & (OP_JOINING|OP_TXRXPEND)))
        send_packet();
}

void send_packet(){
    // Prepare upstream data transmission at the next possible time.
    uint8_t mydata[] = "Hello, world!";
    LMIC_setTxData2(1, mydata, sizeof(mydata)-1, 0);
    Serial.println(F("Packet queued"));

    last_packet = millis();
}

