// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _debug_h_
#define _debug_h_

#ifndef CFG_DEBUG

#define debug_snprintf(s,n,f,...)       do { } while (0)
#define debug_printf(f,...)             do { } while (0)
#define debug_printf_continue(f,...)             do { } while (0)
#define debug_str(s)                    do { } while (0)
#define debug_led(val)                  do { } while (0)
#define debug_verbose_printf(f,...)             do { } while (0)

#ifdef CFG_DEBUG_VERBOSE
#error CFG_DEBUG_VERBOSE requires CFG_DEBUG
#endif

#else

#include <limits.h>

// When printing 16-bit values, the code uses %d and friends (which
// accept an `int`-sized argument). This works, because these arguments
// are integer-promoted automatically (provided int is at least 16-bits,
// but the C language requires this
// When printing 32-bit values, the code uses %ld (which accepts a
// `long`-sized argument). This only works when `long` is actually
// 32-bits. This is the case on at least ARM and AVR, but to be sure,
// check at compiletime. Since there is no portable LONG_WIDTH, we use
// LONG_MAX instead.
#if LONG_MAX != ((1 << 31) - 1)
#error "long is not exactly 32 bits, printing will fail"
#endif

// write formatted string to buffer
int debug_snprintf (char *str, int size, const char *format, ...);

// write formatted string to USART
void debug_printf_real (char const *format, ...);
#define debug_printf(format, ...) debug_printf_real("%10t: " format, os_getTime(), ## __VA_ARGS__)
// To continue a line, omit the timestamp
#define debug_printf_continue(format, ...) debug_printf_real(format, ## __VA_ARGS__)

// write nul-terminated string to USART
void debug_str (const char* str);

// set LED state
void debug_led (int val);

#ifndef CFG_DEBUG_VERBOSE
#define debug_verbose_printf(f,...)             do { } while (0)
#define debug_verbose_printf_continue(f,...)             do { } while (0)
#else
#define debug_verbose_printf debug_printf
#define debug_verbose_printf_continue debug_printf_continue
#endif

#endif

#endif
