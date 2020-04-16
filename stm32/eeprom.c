// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

// write 32-bit word to EEPROM memory
void eeprom_write (void* dest, unsigned int val) {
    u4_t* addr = dest;
    // check previous value
    if( *addr != val ) {
        // unlock data eeprom memory and registers
        FLASH->PEKEYR = 0x89ABCDEF; // FLASH_PEKEY1
        FLASH->PEKEYR = 0x02030405; // FLASH_PEKEY2

        // only auto-erase if neccessary (when content is non-zero)
#if defined(STM32L0)
        FLASH->PECR &= ~FLASH_PECR_FIX; // clear FIX
#elif defined(STM32L1)
        FLASH->PECR &= ~FLASH_PECR_FTDW; // clear FTDW
#endif

        // write value
        *addr = val;

        // check for end of programming
        while( FLASH->SR & FLASH_SR_BSY ); // loop while busy

        // lock data eeprom memory and registers
        FLASH->PECR |= FLASH_PECR_PELOCK;

        // verify value
        ASSERT( *((volatile u4_t*) addr) == val );
    }
}

void eeprom_copy (void* dest, const void* src, int len) {
    ASSERT( (((u4_t) dest | (u4_t) src | len) & 3) == 0 );
    u4_t* d = (u4_t*) dest;
    u4_t* s = (u4_t*) src;
    len >>= 2;

    while( len-- ) {
        eeprom_write(d++, *s++);
    }
}
