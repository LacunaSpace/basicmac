// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"

#include "lwmux/lwmux.h"

#define TESTMODE_PORT        224

#define TESTMODE_MAXCNT      192
#define TESTMODE_TIMEOUT    (30*60) // (sec)
#define TESTMODE_INTERVAL    2      // (sec)

// commands
#define TESTCMD_STOP         0x00
#define TESTCMD_START        0x01
#define TESTCMD_CONFIRMED    0x02
#define TESTCMD_UNCONFIRMED  0x03
#define TESTCMD_ECHO         0x04
#define TESTCMD_LINKCHECK    0x05
#define TESTCMD_JOIN         0x06
#define TESTCMD_CW           0x07
#define TESTCMD_RFU          0x08 // 0x08-0x7F
#define TESTCMD_VENDOR       0x80 // 0x80-0xFF

static struct {
    uint8_t active;    // test mode active
    uint8_t confirmed; // request confirmation for uplinks
    uint16_t retrans;  // confirmed frame retransmission counter
    uint16_t dncnt;    // downlink counter (to be echoed)
    uint16_t upcnt;    // number of uplinks w/o downlink (max 192)
    uint32_t uptotal;  // total number of up messages since start of test mode
    uint32_t lastdf;   // last down fcnt
    ostime_t dntime;   // time of last downlink (max 30 minutes)
    osjob_t timer;     // cw timeout or uplink timer
    lwm_job lwmjob;    // uplink job
} testmode;

static void starttestmode (void) {
    debug_printf("TEST MODE START\r\n");
    testmode.active = 1;
    testmode.uptotal = 0;
    testmode.dncnt = 0;
    testmode.retrans = 0;
    testmode.lastdf = 0;
    // disable duty cycle
    LMIC_enableFastJoin();
    LMIC_disableDC();
    // disable ADR profiles
    lwm_setadrprofile(0, NULL, 0);
    // don't send MAC options immediately in empty frame, wait for next uplink
    LMIC.polltimeout = sec2osticks(30);
    // disable application messages
    lwm_setpriority(LWM_PRIO_MAX - 2);
}

static void stoptestmode (void) {
    debug_printf("TEST MODE END\r\n");
    testmode.active = 0;
    // stop uplinks
    os_clearCallback(&testmode.timer);
    // reset duty cycle limitations (XXX)
#if defined(CFG_eu868)
    LMIC.noDC = 0;
#endif // defined(CFG_eu868)
    LMIC.opmode &= ~OP_TESTMODE;
    // send MAC options immediately
    LMIC.polltimeout = 0;
    // enable application traffic
    lwm_setpriority(LWM_PRIO_MIN);
}

static bool txfunc (lwm_txinfo* txi) {
    if (testmode.confirmed && (LMIC.txrxFlags & TXRX_NACK) && testmode.retrans < 8) {
        // no ACK received - retransmit last uplink data in LMIC.pendTxData with same seqnoUp
        txi->dlen = LMIC.pendTxLen;
        LMIC.seqnoUp -= 1;
        testmode.retrans += 1;
    } else if (LMIC.frame[LMIC.dataBeg] == TESTCMD_ECHO) {
        txi->data[0] = TESTCMD_ECHO;
        for( int i = 1; i < LMIC.dataLen; i++ ) {
            txi->data[i] = LMIC.frame[LMIC.dataBeg + i] + 1;
        }
        txi->dlen = LMIC.dataLen;
        testmode.retrans = 0;
    } else {
        txi->data[0] = testmode.dncnt >> 8; // fill in downlink_counter
        txi->data[1] = testmode.dncnt;      // (2 bytes, big endian)
        txi->dlen = 2;
        testmode.retrans = 0;
    }
    txi->port = TESTMODE_PORT;
    txi->confirmed = testmode.confirmed;
    LMIC_setAdrMode(1);
    debug_printf("TESTMODE UPLINK #%d (%sconfirmed, seq=%d, len=%d): %h\r\n",
                 testmode.uptotal++, (testmode.confirmed) ? "" : "un", LMIC.seqnoUp, txi->dlen, txi->data, txi->dlen);
    return true;
}

static void uplink (osjob_t* job) {
    lwm_request_send(&testmode.lwmjob, LWM_PRIO_MAX - 2, txfunc);
}

static void stopcw (osjob_t* job) {
    // stop continuous wave
    os_radio(RADIO_STOP);
    // continue frame reporting...
    uplink(job);
}

// referenced by tabs / rm_event()
void testmode_handleEvent (ev_t ev) {
    switch (ev) {
        case EV_TXCOMPLETE: {
            // check for downlink
            if (LMIC.txrxFlags & (TXRX_DNW1|TXRX_DNW2)) {
                unsigned char *buf = LMIC.frame + LMIC.dataBeg;
                int port = -1;
                if (LMIC.txrxFlags & TXRX_PORT) {
                    port = LMIC.frame[LMIC.dataBeg - 1];
                }

                // save timestamp of last downlink
                testmode.dntime = os_getTime();

                // reset uplink-without-downlink counter
                testmode.upcnt = 0;

                if (testmode.active) {
                    debug_printf("TESTMODE DOWNLINK (seq=%d, port=%d, len=%d%s%s%s%s): %h\r\n",
                                 LMIC.seqnoDn, port, LMIC.dataLen,
                                 (LMIC.txrxFlags & TXRX_DNW1) ? ", RX1" : "",
                                 (LMIC.txrxFlags & TXRX_DNW2) ? ", RX2" : "",
                                 (LMIC.txrxFlags & TXRX_ACK) ? ", ACK" : "",
                                 (LMIC.txrxFlags & TXRX_NACK) ? ", NACK" : "",
                                 buf, LMIC.dataLen);

                    // update downlink counter
                    if( testmode.lastdf != LMIC.seqnoDn) {
                        testmode.lastdf = LMIC.seqnoDn;
                        testmode.dncnt += 1;
                    }

                    if (port == TESTMODE_PORT && LMIC.dataLen > 0) {

                        // dispatch test commands
                        switch (buf[0]) {

                            case TESTCMD_STOP: // deactivate test mode
                                stoptestmode();
                                break;

                            case TESTCMD_CONFIRMED: // activate confirmations
                                testmode.confirmed = 1;
                                break;

                            case TESTCMD_UNCONFIRMED: // deactivate confirmations
                                testmode.confirmed = 0;
                                break;

                            case TESTCMD_LINKCHECK: // XXX undocumented?!?
                                // XXX
                                break;

                            case TESTCMD_JOIN: // trigger join request
                                stoptestmode(); // (activation command will be resent after join)
                                lwm_setmode(LWM_MODE_SHUTDOWN);
                                lwm_setmode(LWM_MODE_NORMAL);
                                break;

                            case TESTCMD_ECHO: // modify and echo frame
                                LMIC.pendTxData[0] = buf[0];
                                for (int i = 1; i < LMIC.dataLen; i++) {
                                    LMIC.pendTxData[i] = buf[i] + 1;
                                }
                                LMIC.pendTxLen = LMIC.dataLen;
                                break;

                            case TESTCMD_CW: // continous wave
                                // set timeout and parameters
                                os_setApproxTimedCallback(&testmode.timer, os_getTime() + sec2osticks((buf[1] << 8) | buf[2]), stopcw); // duration [s]
                                LMIC.freq = ((buf[3] << 16) | (buf[4] << 8) | buf[5]) * 100; // [Hz]
                                LMIC.txpow = buf[6]; // dBm
                                // start continuous wave
                                os_radio(RADIO_TXCW);
                                return; // no uplink now
                        }
                    }
                } else { // test mode not active
                    if (port == TESTMODE_PORT && LMIC.dataLen == 4 && os_rlsbf4(buf) == 0x01010101) {
                        // activate test mode
                        starttestmode();
                    }
                }
            } else { // no downlink
                if (testmode.active &&
                    (++testmode.upcnt > TESTMODE_MAXCNT ||
                     (os_getTime() - testmode.dntime) > sec2osticks(TESTMODE_TIMEOUT))) {
                    // test mode timed out
                    debug_printf("TEST MODE TIMEOUT\r\n");
                    stoptestmode();
                }
            }

            if (testmode.active) {
                // schedule next uplink
                os_setApproxTimedCallback(&testmode.timer, os_getTime() + sec2osticks(TESTMODE_INTERVAL), uplink);
            }
        }

        default: // ignore other events
            break;
    }
}
