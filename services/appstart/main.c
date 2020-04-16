// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"

#include "svcdefs.h"

#ifdef SVC_backtrace
#include "backtrace/backtrace.h"
#endif

static void initfunc (osjob_t* job) {
#if defined(CFG_DEBUG) && CFG_DEBUG != 0
    unsigned char eui[8];
    hal_fwi fwi;

    os_getDevEui(eui);
    hal_fwinfo(&fwi);

    debug_printf("id: %E | sn: %.16s | hw: 0x%03x | flash: %dK\r\n",
            eui, hal_serial(), hal_hwid(), fwi.flashsz >> 10);
    debug_printf("bl: v%d | fw: %s %s 0x%08x 0x%08x | boot: %s\r\n",
            fwi.blversion,
            PROJECT, VARIANT, fwi.version, fwi.crc,
#if 0
            (BOOT_DEVINFO->bootmode == TABS_BOOT_UFT) ? "uft" :
            (BOOT_DEVINFO->bootmode == TABS_BOOT_SHIPPING) ? "ship" :
            (BOOT_DEVINFO->bootmode == TABS_BOOT_FLIGHT) ? "flight" :
#endif
            "normal");
#endif

#ifdef SVC_backtrace
    bt_print();
#endif

    // Application start hook
    SVCHOOK_appstart(job);
}

int main (void* bootarg) {
    osjob_t initjob;

    os_init(bootarg);
    os_setCallback(&initjob, initfunc);
    os_runloop();

    // (not reached)
    return 0;
}
