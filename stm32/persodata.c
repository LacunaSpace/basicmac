// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#define PERSODATA_MAGIC_V1      0xb2dc4db2 /* openssl rand */

typedef struct {
    uint32_t    magic;          // 0x00 magic
    uint32_t    hwid;           // 0x04 hardware ID
    uint32_t    region;         // 0x08 region ID
    uint32_t    reserved;       // 0x0c (reserved, set to 0)
    uint8_t     serial[16];     // 0x10 production serial number
    uint8_t     deveui[8];      // 0x20 device EUI
    uint8_t     joineui[8];     // 0x28 join EUI
    uint8_t     nwkkey[16];     // 0x30 network key
    uint8_t     appkey[16];     // 0x40 application key
    uint32_t    hash[8];        // 0x50 hash
} persodata_v1;

_Static_assert(sizeof(persodata_v1) <= PERSODATA_SZ, "persodata struct too large");

persodata_v1 pd;

static persodata_v1* pd_check_v1 (void* ptr) {
    persodata_v1* ppd = ptr;
    if( ppd->magic == PERSODATA_MAGIC_V1 ) {
        uint32_t hash[8];
        sha256(hash, ptr, sizeof(persodata_v1) - 32);
        if( memcmp(hash, ppd->hash, 32) != 0 ) {
            return NULL;
        }
        return ppd;
    }
    return NULL;
}

void pd_init (void) {
    persodata_v1* ppd = pd_check_v1((void*) PERSODATA_BASE);
    if( ppd ) {
        pd = *ppd;
    } else { // TODO - check TrackNet legacy
        // fill defaults
        uint64_t eui;

        eui = 0xffffffaa00000000ULL | hal_unique();
        memcpy(pd.deveui, &eui, 8);
        eui = 0xffffffbb00000000ULL;
        memcpy(pd.joineui, &eui, 8);
        memcpy(pd.nwkkey, "@ABCDEFGHIJKLMNO", 16);
        memcpy(pd.appkey, "`abcdefghijklmno", 16);
    }
}

// private API to verify EEPROM data was valid
bool pd_verify (void) {
    persodata_v1* ppd = (void*) PERSODATA_BASE;
    if( ppd->magic == PERSODATA_MAGIC_V1 ) {
        uint32_t hash[8];
        sha256(hash, (uint8_t*) ppd, sizeof(persodata_v1) - 32);
        if( memcmp(hash, ppd->hash, 32) == 0 ) {
            return true;
        }
    }
    return false;
}

u1_t* hal_joineui (void) {
    return pd.joineui;
}

u1_t* hal_deveui (void) {
    return pd.deveui;
}

u1_t* hal_nwkkey (void) {
    return pd.nwkkey;
}

u1_t* hal_appkey (void) {
    return pd.appkey;
}

u1_t* hal_serial (void) {
    return pd.serial;
}

u4_t hal_region (void) {
    return pd.region;
}

u4_t hal_hwid (void) {
    return pd.hwid;
}

#ifdef CFG_eeprom_keys
// provide region code
u1_t os_getRegion (void) {
    return hal_region();
}

// provide device ID (8 bytes, LSBF)
void os_getDevEui (u1_t* buf) {
    memcpy(buf, hal_deveui(), 8);
}

// provide join ID (8 bytes, LSBF)
void os_getJoinEui (u1_t* buf) {
    memcpy(buf, hal_joineui(), 8);
}

// provide device network key (16 bytes)
void os_getNwkKey (u1_t* buf) {
    memcpy(buf, hal_nwkkey(), 16);
}

// provide device application key (16 bytes)
void os_getAppKey (u1_t* buf) {
    memcpy(buf, hal_appkey(), 16);
}
#endif
