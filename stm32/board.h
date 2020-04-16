// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _board_h_
#define _board_h_

// GPIO definitions
// 33222222 22221111 11111100 00000000
// 10987654 32109876 54321098 76543210
// ________ _____fff ccccaaaa PPPPpppp

#define BRD_GPIO(port,pin)              (((port) << 4) | (pin))
#define BRD_GPIO_EX(port,pin,ex)        (((port) << 4) | (pin) | (ex))
#define BRD_GPIO_AF(port,pin,af)        (((af) << 8) | ((port) << 4) | (pin))
#define BRD_GPIO_AF_EX(port,pin,af,ex)  (((af) << 8) | ((port) << 4) | (pin) | (ex))
#define BRD_PIN(gpio)                   ((gpio) & 0x0f)
#define BRD_PORT(gpio)                  (((gpio) >> 4) & 0x0f)
#define BRD_AF(gpio)                    (((gpio) >> 8) & 0x0f)

// alternate function configuratons (c)
#define BRD_GPIO_CHAN(ch)               ((ch) << 12)
#define BRD_GPIO_GET_CHAN(gpio)         (((gpio) >> 12) & 0x07)

// flags (f)
#define BRD_GPIO_EXT_PULLUP             (1 << 16)
#define BRD_GPIO_EXT_PULLDN             (1 << 17)
#define BRD_GPIO_ACTIVE_LOW             (1 << 18)

// special values for low-power UART
#define BRD_LPUART(x)                   ((x) | (1 << 8))

#define PORT_A  0
#define PORT_B  1
#define PORT_C  2


#ifdef BRD_IMPL_INC
#include BRD_IMPL_INC
#else
#error "Missing board implementation include file"
#endif

#endif
