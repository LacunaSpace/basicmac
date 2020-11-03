// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "board.h"
#include "hw.h"
#include "lmic.h"

#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)

// ----------------------------------------
// Commands Selecting the Operating Modes of the Radio
#define CMD_SETSLEEP                    0x84
#define CMD_SETSTANDBY                  0x80
#define CMD_SETFS                       0xC1
#define CMD_SETTX                       0x83
#define CMD_SETRX                       0x82
#define CMD_STOPTIMERONPREAMBLE         0x9F
#define CMD_SETRXDUTYCYCLE              0x94
#define CMD_SETCAD                      0xC5
#define CMD_SETTXCONTINUOUSWAVE         0xD1
#define CMD_SETTXINFINITEPREAMBLE       0xD2
#define CMD_SETREGULATORMODE            0x96
#define CMD_CALIBRATE                   0x89
#define CMD_CALIBRATEIMAGE              0x98
#define CMD_SETPACONFIG                 0x95
#define CMD_SETRXTXFALLBACKMODE         0x93

// Commands to Access the Radio Registers and FIFO Buffer
#define CMD_WRITEREGISTER               0x0D
#define CMD_READREGISTER                0x1D
#define CMD_WRITEBUFFER                 0x0E
#define CMD_READBUFFER                  0x1E

// Commands Controlling the Radio IRQs and DIOs
#define CMD_SETDIOIRQPARAMS             0x08
#define CMD_GETIRQSTATUS                0x12
#define CMD_CLEARIRQSTATUS              0x02
#define CMD_SETDIO2ASRFSWITCHCTRL       0x9D
#define CMD_SETDIO3ASTCXOCTRL           0x97

// Commands Controlling the RF and Packets Settings
#define CMD_SETRFFREQUENCY              0x86
#define CMD_SETPACKETTYPE               0x8A
#define CMD_GETPACKETTYPE               0x11
#define CMD_SETTXPARAMS                 0x8E
#define CMD_SETMODULATIONPARAMS         0x8B
#define CMD_SETPACKETPARAMS             0x8C
#define CMD_SETCADPARAMS                0x88
#define CMD_SETBUFFERBASEADDRESS        0x8F
#define CMD_SETLORASYMBNUMTIMEOUT       0xA0

// Commands Returning the Radio Status
#define CMD_GETSTATUS                   0xC0
#define CMD_GETRSSIINST                 0x15
#define CMD_GETRXBUFFERSTATUS           0x13
#define CMD_GETPACKETSTATUS             0x14
#define CMD_GETDEVICEERRORS             0x17
#define CMD_CLEARDEVICEERRORS           0x07
#define CMD_GETSTATS                    0x10
#define CMD_RESETSTATS                  0x00


// ----------------------------------------
// List of Registers

#define REG_WHITENINGMSB        0x06B8
#define REG_WHITENINGLSB        0x06B9
#define REG_CRCINITVALMSB       0x06BC
#define REG_CRCINITVALLSB       0x06BD
#define REG_CRCPOLYVALMSB       0x06BE
#define REG_CRCPOLYVALLSB       0x06BF
#define REG_SYNCWORD0           0x06C0
#define REG_SYNCWORD1           0x06C1
#define REG_SYNCWORD2           0x06C2
#define REG_SYNCWORD3           0x06C3
#define REG_SYNCWORD4           0x06C4
#define REG_SYNCWORD5           0x06C5
#define REG_SYNCWORD6           0x06C6
#define REG_SYNCWORD7           0x06C7
#define REG_NODEADDRESS         0x06CD
#define REG_BROADCASTADDR       0x06CE
#define REG_LORASYNCWORDMSB     0x0740
#define REG_LORASYNCWORDLSB     0x0741
#define REG_RANDOMNUMBERGEN0    0x0819
#define REG_RANDOMNUMBERGEN1    0x081A
#define REG_RANDOMNUMBERGEN2    0x081B
#define REG_RANDOMNUMBERGEN3    0x081C
#define REG_RXGAIN              0x08AC
#define REG_OCPCONFIG           0x08E7
#define REG_XTATRIM             0x0911
#define REG_XTBTRIM             0x0912

// sleep modes
#define SLEEP_COLD              0x00 // (no rtc timeout)
#define SLEEP_WARM              0x04 // (no rtc timeout)

// standby modes
#define STDBY_RC                0x00
#define STDBY_XOSC              0x01

// regulator modes
#define REGMODE_LDO             0x00
#define REGMODE_DCDC            0x01

// packet types
#define PACKET_TYPE_FSK         0x00
#define PACKET_TYPE_LORA        0x01

// crc types
#define CRC_OFF                 0x01
#define CRC_1_BYTE              0x00
#define CRC_2_BYTE              0x02
#define CRC_1_BYTE_INV          0x04
#define CRC_2_BYTE_INV          0x06

// irq types
#define IRQ_TXDONE              (1 << 0)
#define IRQ_RXDONE              (1 << 1)
#define IRQ_PREAMBLEDETECTED    (1 << 2)
#define IRQ_SYNCWORDVALID       (1 << 3)
#define IRQ_HEADERVALID         (1 << 4)
#define IRQ_HEADERERR           (1 << 5)
#define IRQ_CRCERR              (1 << 6)
#define IRQ_CADDONE             (1 << 7)
#define IRQ_CADDETECTED         (1 << 8)
#define IRQ_TIMEOUT             (1 << 9)
#define IRQ_ALL                 0x3FF

// TCXO voltages (limited to VDD - 200mV)
#define TCXO_VOLTAGE1_6V        0x00
#define TCXO_VOLTAGE1_7V        0x01
#define TCXO_VOLTAGE1_8V        0x02
#define TCXO_VOLTAGE2_2V        0x03
#define TCXO_VOLTAGE2_4V        0x04
#define TCXO_VOLTAGE2_7V        0x05
#define TCXO_VOLTAGE3_0V        0x06
#define TCXO_VOLTAGE3_3V        0x07

// XXX: These should probably be configurable
// XXX: The startup time delays TX/RX by 320*15.625=5ms, maybe switch on
// TCXO early?
#define TCXO_VOLTAGE TCXO_VOLTAGE1_7V
#define TCXO_STARTUP_TIME 320 // In multiples of 15.625μs

#define LORA_TXDONE_FIXUP       us2osticks(269) // determined by lwtestapp using device pin wired to sx1301 pps...
#define FSK_TXDONE_FIXUP        us2osticks(0) // XXX
#define FSK_RXDONE_FIXUP        us2osticks(0) // XXX

// XXX
static const u2_t LORA_RXDONE_FIXUP_125[] = {
    [FSK]  =     us2osticks(0),
    [SF7]  =     us2osticks(0),
    [SF8]  =  us2osticks(1648),
    [SF9]  =  us2osticks(3265),
    [SF10] =  us2osticks(7049),
    [SF11] = us2osticks(13641),
    [SF12] = us2osticks(31189),
};

static const u2_t LORA_RXDONE_FIXUP_500[] = {
    [FSK]  = us2osticks(    0),
    [SF7]  = us2osticks(    0),
    [SF8]  = us2osticks(    0),
    [SF9]  = us2osticks(    0),
    [SF10] = us2osticks(    0),
    [SF11] = us2osticks(    0),
    [SF12] = us2osticks(    0),
};

// radio state
static struct {
    unsigned int sleeping:1;
} state;

// ----------------------------------------

static void writecmd (uint8_t cmd, const uint8_t* data, uint8_t len) {
    hal_spi_select(1);
    hal_pin_busy_wait();
    state.sleeping = 0;
    hal_spi(cmd);
    for (u1_t i = 0; i < len; i++) {
        hal_spi(data[i]);
    }
    hal_spi_select(0);
    // busy line will go high after max 600ns
    // eventually during a subsequent hal_spi_select(1)...
}

static void WriteRegs (uint16_t addr, const uint8_t* data, uint8_t len) {
    hal_spi_select(1);
    hal_pin_busy_wait();
    state.sleeping = 0;
    hal_spi(CMD_WRITEREGISTER);
    hal_spi(addr >> 8);
    hal_spi(addr);
    for (uint8_t i = 0; i < len; i++) {
        hal_spi(data[i]);
    }
    hal_spi_select(0);
}

static void WriteReg (uint16_t addr, uint8_t val) __attribute__((__unused__)); // Ok if this is unused
static void WriteReg (uint16_t addr, uint8_t val) {
    WriteRegs(addr, &val, 1);
}

static void WriteBuffer (uint8_t off, const uint8_t* data, uint8_t len) {
    hal_spi_select(1);
    hal_pin_busy_wait();
    state.sleeping = 0;
    hal_spi(CMD_WRITEBUFFER);
    hal_spi(off);
    for (uint8_t i = 0; i < len; i++) {
        hal_spi(data[i]);
    }
    hal_spi_select(0);
}

static uint8_t readcmd (uint8_t cmd, uint8_t* data, uint8_t len) {
    hal_spi_select(1);
    hal_pin_busy_wait();
    state.sleeping = 0;
    hal_spi(cmd);
    uint8_t stat = hal_spi(0x00);
    for (u1_t i = 0; i < len; i++) {
        data[i] = hal_spi(0x00);
    }
    hal_spi_select(0);
    return stat;
}

static void ReadRegs (uint16_t addr, uint8_t* data, uint8_t len) {
    hal_spi_select(1);
    hal_pin_busy_wait();
    state.sleeping = 0;
    hal_spi(CMD_READREGISTER);
    hal_spi(addr >> 8);
    hal_spi(addr);
    hal_spi(0x00); // NOP
    for (uint8_t i = 0; i < len; i++) {
        data[i] = hal_spi(0x00);
    }
    hal_spi_select(0);
}

static uint8_t ReadReg (uint16_t addr) {
    uint8_t val;
    ReadRegs(addr, &val, 1);
    return val;
}

static void ReadBuffer (uint8_t off, uint8_t* data, uint8_t len) {
    hal_spi_select(1);
    hal_pin_busy_wait();
    state.sleeping = 0;
    hal_spi(CMD_READBUFFER);
    hal_spi(off);
    hal_spi(0x00); // NOP
    for (uint8_t i = 0; i < len; i++) {
        data[i] = hal_spi(0x00);
    }
    hal_spi_select(0);
}

// set sleep mode SLEEP_COLD or SLEEP_WARM (from standby mode)
static void SetSleep (uint8_t cfg) {
    writecmd(CMD_SETSLEEP, &cfg, 1);
}

// set standby mode STDBY_RC or STANDBY_XOSC
static void SetStandby (uint8_t cfg) {
    writecmd(CMD_SETSTANDBY, &cfg, 1);
}

// set regulator mode REGMODE_LDO or REGMODE_DCDC
static void SetRegulatorMode (uint8_t mode) {
    writecmd(CMD_SETREGULATORMODE, &mode, 1);
}

// use DIO2 to drive antenna rf switch
static void SetDIO2AsRfSwitchCtrl (uint8_t enable) {
    writecmd(CMD_SETDIO2ASRFSWITCHCTRL, &enable, 1);
}

// use DIO3 to drive crystal enable switch
static void SetDIO3AsTcxoCtrl () {
    uint32_t timeout = TCXO_STARTUP_TIME;
    uint8_t voltage = TCXO_VOLTAGE1_7V;
    uint8_t data[] = {voltage, (timeout >> 16) & 0xff, (timeout >> 8) & 0xff, timeout & 0xff };

    writecmd(CMD_SETDIO3ASTCXOCTRL, data, sizeof(data));
}

// write payload to fifo buffer at offset 0
static void WriteFifo (uint8_t *buf, uint8_t len) {
    static const uint8_t txrxbase[] = { 0, 0 };
    writecmd(CMD_SETBUFFERBASEADDRESS, txrxbase, 2);

    WriteBuffer(0, buf, len);
}

// read payload from fifo, return length
static uint8_t ReadFifo (uint8_t *buf) {
    // get buffer status
    uint8_t status[2];
    readcmd(CMD_GETRXBUFFERSTATUS, status, 2);

    // read buffer
    uint8_t len = status[0];
    uint8_t off = status[1];
    ReadBuffer(off, buf, len);

    // return length
    return len;
}

// set radio in transmit mode (abort after timeout [1/64ms])
static void SetTx (uint32_t timeout64ms) {
    uint8_t timeout[3] = { timeout64ms >> 16, timeout64ms >> 8, timeout64ms };
    writecmd(CMD_SETTX, timeout, 3);
}

// generate continuous (indefinite) wave
static void SetTxContinuousWave (void) {
    writecmd(CMD_SETTXCONTINUOUSWAVE, NULL, 0);
}

// set radio in receive mode (abort after timeout [1/64ms], or with timeout=0 after frame received, or continuous with timeout=FFFFFF)
static void SetRx (uint32_t timeout64ms) {
    uint8_t timeout[3] = { timeout64ms >> 16, timeout64ms >> 8, timeout64ms };
    writecmd(CMD_SETRX, timeout, 3);
}

// set radio in frequency synthesis mode
static void SetFs (void) {
    writecmd(CMD_SETFS, NULL, 0);
}

// set radio to PACKET_TYPE_LORA or PACKET_TYPE_FSK mode
static void SetPacketType (uint8_t type) {
    writecmd(CMD_SETPACKETTYPE, &type, 1);
}

// calibrate the image rejection
static void CalibrateImage (uint32_t freq) {
    static const struct {
        uint32_t min;
        uint32_t max;
        uint8_t freq[2];
    } bands[] = {
        { 430000000, 440000000, { 0x6B, 0x6F } },
        { 470000000, 510000000, { 0x75, 0x81 } },
        { 779000000, 787000000, { 0xC1, 0xC5 } },
        { 863000000, 870000000, { 0xD7, 0xDB } },
        { 902000000, 928000000, { 0xE1, 0xE9 } },
    };
    for (size_t i = 0; i < sizeof(bands) / sizeof(bands[0]); i++) {
        if (freq >= bands[i].min && freq <= bands[i].max) {
            writecmd(CMD_CALIBRATEIMAGE, bands[i].freq, 2);
        }
    }
}

// set rf frequency (in Hz)
static void SetRfFrequency (uint32_t freq) {
    // set frequency
    uint8_t buf[4];
    os_wmsbf4(buf, (uint32_t) (((uint64_t) freq << 25) / 32000000));
    writecmd(CMD_SETRFFREQUENCY, buf, 4);
}

// configure modulation parameters for LoRa
static void SetModulationParamsLora (u2_t rps) {
    uint8_t param[4];
    param[0] = getSf(rps) - SF7 + 7;    // SF (sf7 -> 7)
    param[1] = getBw(rps) - BW125 + 4;  // BW (bw125 -> 4)
    param[2] = getCr(rps) - CR_4_5 + 1; // CR (cr45 -> 1)
    param[3] = enDro(rps);     // low-data-rate-opt (symbol time equal or above 16.38 ms)
    writecmd(CMD_SETMODULATIONPARAMS, param, 4);
}

// configure modulation parameters for FSK
static void SetModulationParamsFsk (void) {
    uint8_t param[8];
    param[0] = 0x00; // bitrate 50kbps (32 * fxtal / bitrate = 32 * 32000000 / 50000 = 0x005000)
    param[1] = 0x50;
    param[2] = 0x00;
    param[3] = 0x09; // TX pulse shape filter gaussian BT 0.5
    param[4] = 0x0B; // RX bandwidth 117.3kHz DSB
    param[5] = 0x00; // TX frequency deviation 25kHz (deviation * 2^25 / fxtal = 25000 * 2^25 / 32000000 = 0x006666)
    param[6] = 0x66;
    param[7] = 0x66;
    writecmd(CMD_SETMODULATIONPARAMS, param, 8);
}

// configure packet handling for LoRa
static void SetPacketParamsLora (u2_t rps, int len, int inv) {
    uint8_t param[6];
    param[0] = 0x00; // 8 symbols preamble
    param[1] = 0x08;
    param[2] = getIh(rps); // implicit header
    param[3] = len;
    param[4] = !getNocrc(rps);
    param[5] = inv; // I/Q inversion
    writecmd(CMD_SETPACKETPARAMS, param, 6);
}

// configure packet handling for FSK
static void SetPacketParamsFsk (u2_t rps, int len) {
    uint8_t param[9];
    param[0] = 0x00; // TX preamble length 40 bits / 5 bytes
    param[1] = 0x28;
    param[2] = 0x05; // RX preamble detector length 16 bits
    param[3] = 0x18; // sync word length 24 bits / 3 bytes
    param[4] = 0x00; // node address filtering disabled
    param[5] = 0x01; // variable size packets, length is included
    param[6] = len;  // payload length
    param[7] = getNocrc(rps) ? CRC_OFF : CRC_2_BYTE_INV; // off or CCITT
    param[8] = 0x01; // whitening enabled
    writecmd(CMD_SETPACKETPARAMS, param, 9);
}

// clear irq register
static void ClearIrqStatus (uint16_t mask) {
    uint8_t buf[2] = { mask >> 8, mask & 0xFF };
    writecmd(CMD_CLEARIRQSTATUS, buf, 2);
}

// stop timer on preamble detection or header/syncword detection
static void StopTimerOnPreamble (uint8_t enable) {
    writecmd(CMD_STOPTIMERONPREAMBLE, &enable, 1);
}

// set number of symbols for reception
static void SetLoRaSymbNumTimeout (uint8_t nsym) {
    writecmd(CMD_SETLORASYMBNUMTIMEOUT, &nsym, 1);
}

// return irq register
static uint16_t GetIrqStatus (void) {
    uint8_t buf[2];
    readcmd(CMD_GETIRQSTATUS, buf, 2);
    return (buf[0] << 8) | buf[1];
}

// get signal quality of received packet for LoRa
static void GetPacketStatusLora (s1_t *rssi, s1_t *snr) {
    uint8_t buf[3];
    readcmd(CMD_GETPACKETSTATUS, buf, 3);
    *rssi = -buf[0] / 2 + RSSI_OFF;
    *snr = buf[1] * SNR_SCALEUP / 4;
}

// get signal quality of received packet for FSK
static s1_t GetPacketStatusFsk (void) {
    uint8_t buf[3];
    readcmd(CMD_GETPACKETSTATUS, buf, 3);
    return -buf[2] / 2 + RSSI_OFF; // RssiAvg
}

// set and enable irq mask for dio1
static void SetDioIrqParams (uint16_t mask) {
    uint8_t param[] = { mask >> 8, mask & 0xFF, mask >> 8, mask & 0xFF, 0x00, 0x00, 0x00, 0x00 };
    writecmd(CMD_SETDIOIRQPARAMS, param, 8);
}

// set tx power (in dBm)
static void SetTxPower (int pw) {
#if defined(BRD_sx1261_radio)
    // low power PA: -17 ... +14 dBm
    if (pw > 14) pw = 14;
    if (pw < -17) pw = -17;
    // set PA config (and reset OCP to 60mA)
    writecmd(CMD_SETPACONFIG, (const uint8_t[]) { 0x04, 0x00, 0x01, 0x01 }, 4);
#elif defined(BRD_sx1262_radio)
    // high power PA: -9 ... +22 dBm
    if (pw > 22) pw = 22;
    if (pw < -9) pw = -9;
    // set PA config (and reset OCP to 140mA)
    writecmd(CMD_SETPACONFIG, (const uint8_t[]) { 0x04, 0x07, 0x00, 0x01 }, 4);
#endif
    // set tx params
    uint8_t txparam[2];
    txparam[0] = (uint8_t) pw;
    txparam[1] = 0x04; // ramp time 200us
    writecmd(CMD_SETTXPARAMS, txparam, 2);
}

// set sync word for LoRa
static void SetSyncWordLora (uint16_t syncword) {
    uint8_t buf[2] = { syncword >> 8, syncword & 0xFF };
    WriteRegs(REG_LORASYNCWORDMSB, buf, 2);
}

// set sync word for FSK
static void SetSyncWordFsk (uint32_t syncword) {
    uint8_t buf[3] = { syncword >> 16, syncword >> 8, syncword & 0xFF };
    WriteRegs(REG_SYNCWORD0, buf, 3);
}

// set seed for FSK data whitening
static void SetWhiteningSeed (uint16_t seed) {
    uint8_t buf[2];
    buf[0] = (ReadReg(REG_WHITENINGMSB) & 0xFE) | ((seed >> 8) & 0x01); // don't modify the top-most 7 bits!
    buf[1] = seed;
    WriteRegs(REG_WHITENINGMSB, buf, 2);
}

// set CRC seed and polynomial for FSK
static void SetCrc16 (uint16_t seed, uint16_t polynomial) {
    // set seed
    uint8_t buf[2] = { seed >> 8, seed & 0xFF };
    WriteRegs(REG_CRCINITVALMSB, buf, 2);
    // set polynomial
    buf[0] = polynomial >> 8;
    buf[1] = polynomial;
    WriteRegs(REG_CRCPOLYVALMSB, buf, 2);
}

void radio_sleep (void) {
    // cache sleep state to avoid unneccessary wakeup (waking up from cold sleep takes about 4ms)
    if (state.sleeping == 0) {
        SetSleep(SLEEP_COLD);
        state.sleeping = 1;
    }
}

// Do config common to all RF modes
static void CommonSetup (void) {
    SetRegulatorMode(REGMODE_DCDC);
    if (hal_dio2_controls_rxtx())
        SetDIO2AsRfSwitchCtrl(1);
    if (hal_dio3_controls_tcxo())
        SetDIO3AsTcxoCtrl();
}

static uint32_t GetRandom (void) __attribute__((__unused__)); // Ok if unused
static uint32_t GetRandom (void) {
    uint32_t value;

    // Set up oscillator and rx/tx
    CommonSetup();

    // continuous rx
    SetRx(0xFFFFFF);
    // wait 1ms
    hal_waitUntil(os_getTime() + ms2osticks(1));
    // read random register
    ReadRegs(REG_RANDOMNUMBERGEN0, (uint8_t*)&value, sizeof(value));
    // standby
    SetStandby(STDBY_RC);
    return value;
}

static void txlora (void) {
    CommonSetup();
    SetStandby(STDBY_RC);
    SetPacketType(PACKET_TYPE_LORA);
    SetRfFrequency(LMIC.freq);
    SetModulationParamsLora(LMIC.rps);
    SetPacketParamsLora(LMIC.rps, LMIC.dataLen, 0);
    SetTxPower(LMIC.txpow + LMIC.brdTxPowOff);
    SetSyncWordLora(0x3444);
    WriteFifo(LMIC.frame, LMIC.dataLen);
    ClearIrqStatus(IRQ_ALL);
    SetDioIrqParams(IRQ_TXDONE | IRQ_TIMEOUT);

    // enable IRQs in HAL
    hal_irqmask_set(HAL_IRQMASK_DIO1);

    // antenna switch / power accounting
    hal_ant_switch(HAL_ANTSW_TX);

    // now we actually start the transmission
    BACKTRACE();
    SetTx(640000); // timeout 10s (should not happen, TXDONE irq will be raised)
}

static void txfsk (void) {
    CommonSetup();
    SetStandby(STDBY_RC);
    SetPacketType(PACKET_TYPE_FSK);
    SetRfFrequency(LMIC.freq);
    SetModulationParamsFsk();
    SetPacketParamsFsk(LMIC.rps, LMIC.dataLen);
    SetCrc16(0x1D0F, 0x1021); // CCITT
    SetWhiteningSeed(0x01FF);
    SetSyncWordFsk(0xC194C1);
    SetTxPower(LMIC.txpow + LMIC.brdTxPowOff);
    WriteFifo(LMIC.frame, LMIC.dataLen);
    ClearIrqStatus(IRQ_ALL);
    SetDioIrqParams(IRQ_TXDONE | IRQ_TIMEOUT);

    // enable IRQs in HAL
    hal_irqmask_set(HAL_IRQMASK_DIO1);

    // antenna switch / power accounting
    hal_ant_switch(HAL_ANTSW_TX);

    // now we actually start the transmission
    BACKTRACE();
    SetTx(64000); // timeout 1s (should not happen, TXDONE irq will be raised)
}

void radio_cw (void) {
    CommonSetup();
    SetStandby(STDBY_RC);
    SetRfFrequency(LMIC.freq);
    SetTxPower(LMIC.txpow + LMIC.brdTxPowOff);
    ClearIrqStatus(IRQ_ALL);

    // antenna switch / power accounting
    hal_ant_switch(HAL_ANTSW_TX);

    // start tx of wave (indefinitely, ended by RADIO_STOP)
    BACKTRACE();
    SetTxContinuousWave();
}

void radio_starttx (bool txcontinuous) {
    if (txcontinuous) {
        // XXX: This is probably not right. In 2.2, Semtech changed the
        // 127x driver to rename txsw to radio_cw, but
        // radio_starttx(true) now uses txfsk/txlora in continuous mode
        // (which is apparently different from radio_cw), so that needs
        // to be impliemented here as well
        radio_cw();
    } else {
        if (isFsk(LMIC.rps)) { // FSK modem
            txfsk();
        } else { // LoRa modem
            txlora();
        }
        // the radio will go back to STANDBY mode as soon as the TX is finished
        // the corresponding IRQ will inform us about completion.
    }
}

static void rxfsk (bool rxcontinuous) {
    // configure radio (needs rampup time)
    ostime_t t0 = os_getTime();
    CommonSetup();
    SetStandby(STDBY_RC);
    SetPacketType(PACKET_TYPE_FSK);
    SetRfFrequency(LMIC.freq);
    SetModulationParamsFsk();
    SetPacketParamsFsk(LMIC.rps, 255);
    SetCrc16(0x1D0F, 0x1021); // CCITT
    SetWhiteningSeed(0x01FF);
    SetSyncWordFsk(0xC194C1);
    StopTimerOnPreamble(0);
    // FSK interrupts: TXDONE, RXDONE, PREAMBLEDETECTED, SYNCWORDVALID, CRCERR, TIMEOUT
    SetDioIrqParams(IRQ_RXDONE | IRQ_TIMEOUT);
    ClearIrqStatus(IRQ_ALL);

    // enter frequency synthesis mode (become ready for immediate rx)
    SetFs();

    // enable IRQs in HAL
    hal_irqmask_set(HAL_IRQMASK_DIO1);

    ostime_t now = os_getTime();
    if (!rxcontinuous && LMIC.rxtime - now < 0) {
        debug_printf("WARNING: rxtime is %ld ticks in the past! (ramp-up time %ld ms / %ld ticks)\r\n",
                     now - LMIC.rxtime, osticks2ms(now - t0), now - t0);
    }

    // now receive (lock interrupts only for final fine tuned rx timing...)
    hal_disableIRQs();
    if (rxcontinuous) { // continous rx
        BACKTRACE();
        // enable antenna switch for RX (and account power consumption)
        hal_ant_switch(HAL_ANTSW_RX);
        // rx infinitely (no timeout, until rxdone, will be restarted)
        SetRx(0);
    } else { // single rx
        BACKTRACE();
        // busy wait until exact rx time
        hal_waitUntil(LMIC.rxtime);
        // enable antenna switch for RX (and account power consumption)
        hal_ant_switch(HAL_ANTSW_RX);
        // rx for max LMIC.rxsyms symbols (rxsyms = nbytes for FSK)
        SetRx((LMIC.rxsyms << 9) / 50); // nbytes * 8 * 64 * 1000 / 50000
    }
    hal_enableIRQs();
}

static void rxlora (bool rxcontinuous) {
    // configure radio (needs rampup time)
    ostime_t t0 = os_getTime();
    CommonSetup();
    SetStandby(STDBY_RC);
    SetPacketType(PACKET_TYPE_LORA);
    SetRfFrequency(LMIC.freq);
    SetModulationParamsLora(LMIC.rps);
    SetPacketParamsLora(LMIC.rps, 255, !LMIC.noRXIQinversion);
    SetSyncWordLora(0x3444);
    StopTimerOnPreamble(0);
    SetLoRaSymbNumTimeout(LMIC.rxsyms);
    SetDioIrqParams(IRQ_RXDONE | IRQ_TIMEOUT);

    ClearIrqStatus(IRQ_ALL);

    // enter frequency synthesis mode (become ready for immediate rx)
    SetFs();

    // enable IRQs in HAL
    hal_irqmask_set(HAL_IRQMASK_DIO1);

    ostime_t now = os_getTime();
    if (!rxcontinuous && LMIC.rxtime - now < 0) {
        // Print before disabling IRQs, to work around deadlock on some
        // Arduino cores that doe not really support printing without IRQs
        debug_printf("WARNING: rxtime is %ld ticks in the past! (ramp-up time %ld ms / %ld ticks)\r\n",
                     now - LMIC.rxtime, osticks2ms(now - t0), now - t0);
    }

    // now receive (lock interrupts only for final fine tuned rx timing...)
    hal_disableIRQs();
    if (rxcontinuous) { // continous rx
        BACKTRACE();
        // enable antenna switch for RX (and account power consumption)
        hal_ant_switch(HAL_ANTSW_RX);
        // rx infinitely (no timeout, until rxdone, will be restarted)
        SetRx(0);
    } else { // single rx
        BACKTRACE();
        // busy wait until exact rx time
        hal_waitUntil(LMIC.rxtime);
        // enable antenna switch for RX (and account power consumption)
        hal_ant_switch(HAL_ANTSW_RX);
        // rx for max LMIC.rxsyms symbols
        SetRx(0); // (infinite, timeout set via SetLoRaSymbNumTimeout)
    }
    hal_enableIRQs();
}

void radio_cca () {
    LMIC.rssi = -127; //XXX:TBD
}

void radio_cad (void) {
    // not yet...
    ASSERT(0);
}

void radio_startrx (bool rxcontinuous) {
    if (isFsk(LMIC.rps)) { // FSK modem
        rxfsk(rxcontinuous);
    } else { // LoRa modem
        rxlora(rxcontinuous);
    }
}

// reset radio
static void radio_reset (void) {
    // drive RST pin low
    bool has_reset = hal_pin_rst(0);

    // If reset is not connected, just continue and hope for the best
    if (!has_reset)
        return;

    // wait > 100us
    hal_waitUntil(os_getTime() + ms2osticks(1));

    // configure RST pin floating
    hal_pin_rst(2);

    // wait 1ms?
    hal_waitUntil(os_getTime() + ms2osticks(1));

    // check reset value
    ASSERT( ReadReg(REG_LORASYNCWORDLSB) == 0x24 );

    // initialize state
    state.sleeping = 0;
}

void radio_init (bool calibrate) {
    hal_disableIRQs();

    // reset radio (FSK/STANDBY)
    radio_reset();

    // check reset value
    ASSERT( ReadReg(REG_LORASYNCWORDLSB) == 0x24 );

    if (calibrate) {
        CalibrateImage(LMIC.freq);
    }

    // go to SLEEP mode
    radio_sleep();

    hal_enableIRQs();
}

void radio_generate_random(u4_t *words, u1_t len) {
    while (len--)
        *words++ = GetRandom ();
}

// (run by irqjob)
bool radio_irq_process (ostime_t irqtime, u1_t diomask) {
    (void)diomask; // unused

    uint16_t irqflags = GetIrqStatus();

    // dispatch modem
    if (isFsk(LMIC.rps)) { // FSK modem
        if (irqflags & IRQ_TXDONE) { // TXDONE
            BACKTRACE();
            // save exact tx time
            LMIC.txend = irqtime - FSK_TXDONE_FIXUP;

        } else if (irqflags & IRQ_RXDONE) { // RXDONE
            BACKTRACE();

            // read rx quality parameters
            LMIC.rssi = GetPacketStatusFsk();
            LMIC.snr = 0; // N/A

            // read FIFO
            LMIC.dataLen = ReadFifo(LMIC.frame);

            // save exact rx timestamps
            LMIC.rxtime  = irqtime - FSK_RXDONE_FIXUP; // end of frame timestamp
            LMIC.rxtime0 = LMIC.rxtime - calcAirTime(LMIC.rps, LMIC.dataLen); // beginning of frame timestamp
#ifdef DEBUG_RX
            debug_printf("RX[rssi=%d,len=%d]: %h\r\n",
                         LMIC.rssi - RSSI_OFF, LMIC.dataLen, LMIC.frame, LMIC.dataLen);
#endif
        } else if (irqflags & IRQ_TIMEOUT) { // TIMEOUT
            BACKTRACE();
            // indicate timeout
            LMIC.dataLen = 0;
#ifdef DEBUG_RX
            debug_printf("RX: TIMEOUT\r\n");
#endif
        } else {
            // unexpected irq
            debug_printf("UNEXPECTED RADIO IRQ %04x (after %ld ticks, %.1Fms)\r\n", irqflags, irqtime - LMIC.rxtime, osticks2us(irqtime - LMIC.rxtime), 3);
            TRACE_VAL(irqflags);
            ASSERT(0);
        }
    } else { // LORA modem
        if (irqflags & IRQ_TXDONE) { // TXDONE
            BACKTRACE();

            // save exact tx time
            LMIC.txend = irqtime - LORA_TXDONE_FIXUP;

        } else if (irqflags & IRQ_RXDONE) { // RXDONE
            BACKTRACE();

            // read rx quality parameters
            GetPacketStatusLora(&LMIC.rssi, &LMIC.snr);

            // read FIFO
            LMIC.dataLen = ReadFifo(LMIC.frame);

            // save exact rx timestamps
            LMIC.rxtime = irqtime; // end of frame timestamp
            if (getBw(LMIC.rps) == BW125) {
                LMIC.rxtime -= LORA_RXDONE_FIXUP_125[getSf(LMIC.rps)];
            }
            else if (getBw(LMIC.rps) == BW500) {
                LMIC.rxtime -= LORA_RXDONE_FIXUP_500[getSf(LMIC.rps)];
            }
            LMIC.rxtime0 = LMIC.rxtime - calcAirTime(LMIC.rps, LMIC.dataLen); // beginning of frame timestamp
#ifdef DEBUG_RX
            debug_printf("RX[rssi=%d,snr=%.2F,len=%d]: %h\r\n",
                         LMIC.rssi - RSSI_OFF, (s4_t)(LMIC.snr * 100 / SNR_SCALEUP), 2,
                         LMIC.dataLen, LMIC.frame, LMIC.dataLen);
#endif
        } else if (irqflags & IRQ_TIMEOUT) { // TIMEOUT
            BACKTRACE();
            // indicate timeout
            LMIC.dataLen = 0;
#ifdef DEBUG_RX
            debug_printf("RX: TIMEOUT\r\n");
#endif
        } else {
            // unexpected irq
            debug_printf("UNEXPECTED RADIO IRQ %04x\r\n", irqflags);
            TRACE_VAL(irqflags);
            ASSERT(0);
        }
    }

    // mask all IRQs
    SetDioIrqParams(0);

    // clear IRQ flags
    ClearIrqStatus(IRQ_ALL);

    // radio operation completed
    return true;
}

#endif
