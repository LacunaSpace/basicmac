// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"

#ifdef CFG_DEBUG

#include <stdarg.h>

void debug_led (int val) {
    hal_debug_led(val);
}

void debug_str (const char* str) {
    hal_debug_str(str);
}

static void btoa (char* buf, unsigned char b) {
    buf[0] = "0123456789ABCDEF"[b>>4];
    buf[1] = "0123456789ABCDEF"[b&0xF];
}

static int itoa (char* buf, u4_t val, int base, int mindigits, int exp, int prec, char sign) {
    char num[33], *p = num, *b = buf;
    if(sign) {
	if((s4_t)val < 0) {
	    val = -val;
	    *b++ = '-';
	} else if(sign != '-') {
	    *b++ = sign; // space or plus
	}
    }
    if(mindigits > 32) {
	mindigits = 32;
    }
    do {
	int m = val % base;
        *p++ = (m <= 9) ? m+'0' : m-10+'A';
	if(p - num == exp) *p++ = '.';
    } while( (val /= base) || p - num < mindigits );
    do {
	*b++ = *--p;
    } while( p > num + exp - prec);
    *b = 0;
    return b - buf;
}

static int strpad (char *buf, int size, const char *str, int len, int width, int leftalign, char pad) {
    if(len > width) {
	width = len;
    }
    if(width > size) {
	width = size;
    }
    for(int i=0, npad=width-len; i<width; i++) {
	buf[i] = (leftalign) ? ((i<len) ? str[i] : pad) : ((i<npad) ? pad : str[i-npad]);
    }
    return width;
}

static const char* const evnames[] = {
    [EV_SCAN_TIMEOUT]   = "SCAN_TIMEOUT",
    [EV_BEACON_FOUND]   = "BEACON_FOUND",
    [EV_BEACON_MISSED]  = "BEACON_MISSED",
    [EV_BEACON_TRACKED] = "BEACON_TRACKED",
    [EV_JOINING]        = "JOINING",
    [EV_JOINED]         = "JOINED",
    [EV_RFU1]           = "RFU1",
    [EV_JOIN_FAILED]    = "JOIN_FAILED",
    [EV_REJOIN_FAILED]  = "REJOIN_FAILED",
    [EV_TXCOMPLETE]     = "TXCOMPLETE",
    [EV_LOST_TSYNC]     = "LOST_TSYNC",
    [EV_RESET]          = "RESET",
    [EV_RXCOMPLETE]     = "RXCOMPLETE",
    [EV_ADR_BACKOFF]    = "ADR_BACKOFF",
    [EV_LINK_DEAD]      = "LINK_DEAD",
    [EV_LINK_ALIVE]     = "LINK_ALIVE",
    [EV_SCAN_FOUND]     = "SCAN_FOUND",
    [EV_TXSTART]        = "TXSTART",
    [EV_TXDONE]         = "TXDONE",
    [EV_DATARATE]       = "DATARATE",
    [EV_START_SCAN]     = "START_SCAN",
};

static int debug_vsnprintf(char *str, int size, const char *format, va_list arg) {
    char c, *dst = str, *end = str + size - 1;
    int width, left, base, zero, space, plus, prec, sign, longint;

    while( (c = *format++) && dst < end ) {
	if(c != '%') {
	    *dst++ = c;
	} else {
	    // flags
	    width = prec = left = zero = sign = space = plus = longint = 0;
	    while( (c = *format++) ) {
		if(c == '-') left = 1;
		else if(c == ' ') space = 1;
		else if(c == '+') plus = 1;
		else if(c == '0') zero = 1;
		else break;
	    }
	    // width
	    if(c == '*') {
		width = va_arg(arg, int);
		c = *format++;
	    } else {
		while(c >= '0' && c <= '9') {
		    width = width*10 + c - '0';
		    c = *format++;
		}
	    }
	    // precision
	    if(c == '.') {
		c = *format++;
		if(c == '*') {
		    prec = va_arg(arg, int);
		    c = *format++;
		} else {
		    while(c >= '0' && c <= '9') {
			prec = prec*10 + c - '0';
			c = *format++;
		    }
		}
	    }
	    // length
	    if(c == 'l') {
		c = *format++;
		longint = 1;
	    }
	    // conversion specifiers
	    switch(c) {
		case 'c': // character
		    c = va_arg(arg, int);
		    // fallthrough
		case '%': // percent literal
		    *dst++ = c;
		    break;
		case 's': { // nul-terminated string
		    char *s = va_arg(arg, char *);
		    int l = strlen(s);
		    if(prec && l > prec) {
			l = prec;
		    }
		    dst += strpad(dst, end - dst, s, l, width, left, ' ');
		    break;
		}
		case 'd': // signed integer as decimal
		    sign = (plus) ? '+' : (space) ? ' ' : '-';
		    // fallthrough
		case 'u': // unsigned integer as decimal
		    base = 10;
		    goto numeric;
		case 'x':
		case 'X': // unsigned integer as hex
		    base = 16;
		    goto numeric;
		case 'b': // unsigned integer as binary
		    base = 2;
		numeric: {
			char num[33], pad = ' ';
			if(zero && left==0 && prec==0) {
			    prec = width - 1; // have itoa() do the leading zero-padding for correct placement of sign
			    pad = '0';
			}
			u4_t val = longint ? va_arg(arg, long) : va_arg(arg, int);
			int len = itoa(num, val, base, prec, 0, 0, sign);
			dst += strpad(dst, end - dst, num, len, width, left, pad);
			break;
		    }
		case 'F': { // signed integer and exponent as fixed-point decimal
		    char num[33], pad = (zero && left==0) ? '0' : ' ';
		    u4_t val = va_arg(arg, u4_t);
		    int exp = va_arg(arg, int);
		    int len = itoa(num, val, 10, exp+2, exp, (prec) ? prec : exp, (plus) ? '+' : (space) ? ' ' : '-');
		    dst += strpad(dst, end - dst, num, len, width, left, pad);
		    break;
		}
		case 'e': { // LMIC event name
		    unsigned ev = va_arg(arg, unsigned);
		    const char *evn = (ev < sizeof(evnames)/sizeof(evnames[0]) && evnames[ev]) ? evnames[ev] : "UNKNOWN";
		    dst += strpad(dst, end - dst, evn, strlen(evn), width, left, ' ');
		    break;
		}
		case 'E': // EUI64, lsbf (no field padding)
		    if(end - dst >= 23) {
			unsigned char *eui = va_arg(arg, unsigned char *);
			for(int i=7; i>=0; i--) {
			    btoa(dst, eui[i]);
			    dst += 2;
			    if(i) *dst++ = '-';
			}
		    }
		    break;
		case 't': // ostime_t  (hh:mm:ss.mmm, no field padding)
		case 'T': // osxtime_t (ddd.hh:mm:ss, no field padding)
		#if defined(CFG_DEBUG_RAW_TIMESTAMPS)
		    if (c == 't') {
			longint = 1;
			base = 10;
			goto numeric;
		    }
		#endif

		    if (end - dst >= 12) {
			uint64_t t = ((c == 'T') ? va_arg(arg, uint64_t) : va_arg(arg, uint32_t)) * 1000 / OSTICKS_PER_SEC;
			int ms = t % 1000;
			t /= 1000;
			int sec = t % 60;
			t /= 60;
			int min = t % 60;
			t /= 60;
			int hr = t % 24;
			t /= 24;
			int day = t;
			if (c == 'T') {
			    dst += itoa(dst, day, 10, 3, 0, 0, 0);
			    *dst++ = '.';
			}
			dst += itoa(dst, hr, 10, 2, 0, 0, 0);
			*dst++ = ':';
			dst += itoa(dst, min, 10, 2, 0, 0, 0);
			*dst++ = ':';
			dst += itoa(dst, sec, 10, 2, 0, 0, 0);
			if (c == 't') {
			    *dst++ = '.';
			    dst += itoa(dst, ms, 10, 3, 0, 0, 0);
			}
		    }
		    break;
		case 'h': { // buffer with length as hex dump (no field padding)
		    unsigned char *buf = va_arg(arg, unsigned char *);
		    int len = va_arg(arg, int);
		    while(len-- && end - dst > 2+space) {
			btoa(dst, *buf++);
			dst += 2;
			if(space && len) *dst++ = ' ';
		    }
		    break;
		}
		default: // (also catch '\0')
		    goto stop;
	    }
	}
    }
 stop:
    *dst++ = 0;
    return dst - str - 1;
}

int debug_snprintf (char *str, int size, const char *format, ...) {
    va_list arg;
    int length;

    va_start(arg, format);
    length = debug_vsnprintf(str, size, format, arg);
    va_end(arg);
    return length;
}

void debug_printf_real (char const *format, ...) {
    char buf[256];
    va_list arg;

    va_start(arg, format);
    debug_vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);
    debug_str(buf);
}

#endif
