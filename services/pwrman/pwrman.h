// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _pwrman_h_
#define _pwrman_h_

enum {
    PWRMAN_C_RUN,       // MCU run
    PWRMAN_C_SLEEP,     // MCU sleep
    PWRMAN_C_RX,        // radio RX
    PWRMAN_C_TX,        // radio TX
    PWRMAN_C_APP1,      // application-specific 1
    PWRMAN_C_APP2,      // application-specific 2
    PWRMAN_C_APP3,      // application-specific 3
    PWRMAN_C_APP4,      // application-specific 4

    PWRMAN_C_MAX
};

void pwrman_consume (int ctype, uint32_t ticks, uint32_t ua);
void pwrman_commit (void);
void pwrman_reset (void);

uint32_t pwrman_accu_uah (void);

#endif
