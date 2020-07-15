// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "bootloader.h"
#include "boottab.h"

#if defined(CFG_perso)
#include "perso.h"
#endif

#if defined(SVC_eefs)
#include "eefs/eefs.h"
#endif

#if defined(SVC_pwrman)
#include "pwrman/pwrman.h"
#endif

// HAL state
static struct {
    s4_t irqlevel;
    u4_t ticks;
    int watchcount;
    u4_t reset;
#ifdef CFG_rtstats
    struct {
        uint32_t run;                   // ticks running
        uint32_t sleep[HAL_SLEEP_CNT];  // ticks sleeping
    } rtstats;
#endif
    u1_t maxsleep[HAL_SLEEP_CNT-1]; // deep sleep restrictions
    u1_t battlevel;
    boot_boottab* boottab;
} HAL;


// -----------------------------------------------------------------------------
// Panic

// don't change these values, so we know what they are in the field...
enum {
    PANIC_HAL_FAILED    = 0,
    PANIC_LSE_NOSTART   = 1,
    PANIC_CAL_FAILED    = 2,
    PANIC_STS_FAILED    = 3,
    PANIC_SR_BUSY       = 4,
};

#ifdef CFG_panic911
__attribute__((noinline)) // ensure function has a frame, since called from naked
static void call911 (uint32_t reason, uint32_t addr) {
    struct {
        uint32_t magic;
        unsigned char deveui[8];
        uint32_t reason;
        uint32_t addr;
    } info;
    info.magic = 0x504c4548; // HELP
    memcpy(info.deveui, BOOT_DEVINFO->deveui, 8);
    info.reason = reason;
    info.addr = addr;
    memcpy(LMIC.frame, &info, LMIC.dataLen = sizeof(info));

#if defined(CFG_eu868)
    LMIC.freq = 868300000;
    LMIC.rps = updr2rps(DR_SF12);
#elif defined(CFG_us915)
    LMIC.freq = 903700000;
    LMIC.rps = updr2rps(DR_SF10);
#else
#error "Unsupported region"
#endif
    LMIC.txpow = 14;
    os_radio(RADIO_TX);
}
#endif

__attribute__((noreturn))
static void panic (uint32_t reason, uint32_t addr) {
    // disable interrupts
    __disable_irq();

#ifdef CFG_panic911
    // yelp for help
    call911(reason, addr);
#endif

    // call bootloader's panic function
    HAL.boottab->panic(reason, addr);
    // not reached
}

__attribute__((noreturn, naked))
void hal_failed () {
    // get return address
    uint32_t addr;
    __asm__("mov %[addr], lr" : [addr]"=r" (addr) : : );
    // in thumb mode the linked address is the address of the calling instruction plus 4 bytes
    addr -= 4;

#ifdef SVC_backtrace
    // log address of assertion
    bt_addr(__LINE__, addr);
    // save trace to EEPROM
    bt_save();
#endif

    // call panic function
    panic(PANIC_HAL_FAILED, addr);
    // not reached
}


// -----------------------------------------------------------------------------
// timer rollover based watchdog

void hal_watchcount (int cnt) {
    HAL.watchcount = cnt;
}

// -----------------------------------------------------------------------------
// SPI

#if BRD_RADIO_SPI == 1
#define SPIx                    SPI1
#define SPIx_enable()           do { RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; } while (0)
#define SPIx_disable()          do { RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN; } while (0)
#define RCC_APB2ENR_SPIxEN      RCC_APB2ENR_SPI1EN
#elif BRD_RADIO_SPI == 2
#define SPIx                    SPI2
#define SPIx_enable()           do { RCC->APB1ENR |= RCC_APB1ENR_SPI2EN; } while (0)
#define SPIx_disable()          do { RCC->APB1ENR &= ~RCC_APB1ENR_SPI2EN; } while (0)
#else
#error "Unsupported value for BRD_RADIO_SPI"
#endif


static void hal_spi_init () {
    // enable clock for SPI interface 1
    SPIx_enable();

    // configure and activate the SPI (master, internal slave select, software slave mgmt)
    // (use default mode: 8-bit, 2-wire, no crc, MSBF, CPOL0 [clock is low when idle], CPHA0, PCLK32/4=8MHz)
    SPIx->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_SPE | SPI_CR1_BR_0;

    // configure I/O lines and disable clock
    hal_spi_select(0);
}

void hal_spi_select (int on) {
    if (on) {
        // enable clock for SPI interface 1
        SPIx_enable();
        // configure pins for alternate function SPIx (SCK, MISO, MOSI)
        CFG_PIN_AF(GPIO_SCK, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PDN);
        CFG_PIN_AF(GPIO_MISO, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PDN);
        CFG_PIN_AF(GPIO_MOSI, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PDN);
        // drive chip select low
        SET_PIN(GPIO_NSS, 0);
        CFG_PIN(GPIO_NSS, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    } else {
        // stop driving chip select, activate pull-up
        CFG_PIN(GPIO_NSS, GPIOCFG_MODE_INP | GPIOCFG_PUPD_PUP);
        // put SCK, MISO, MOSI back to analog input (HiZ) mode
#if defined(BRD_sck_mosi_pulldown)
        CFG_PIN(GPIO_SCK, GPIOCFG_MODE_INP | GPIOCFG_PUPD_PDN);
        CFG_PIN(GPIO_MOSI, GPIOCFG_MODE_INP | GPIOCFG_PUPD_PDN);
#elif defined(BRD_sck_mosi_drivelow)
        SET_PIN(GPIO_SCK, 0);
        CFG_PIN(GPIO_SCK, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
        SET_PIN(GPIO_MOSI, 0);
        CFG_PIN(GPIO_MOSI, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
#else
        CFG_PIN_DEFAULT(GPIO_SCK);
        CFG_PIN_DEFAULT(GPIO_MOSI);
#endif
        CFG_PIN_DEFAULT(GPIO_MISO);
        // disable clock for SPI interface
        SPIx_disable();
    }
}

// perform SPI transaction with radio
u1_t hal_spi (u1_t out) {
    SPIx->DR = out;
    while( (SPIx->SR & SPI_SR_RXNE ) == 0);
    return SPIx->DR; // in
}


// -----------------------------------------------------------------------------
// Clock and Time
//
// Notes:
// - 32.768kHz LSE must be available
// - LPTIM1 using LSE as clock source to keep time (always on)
// - LPTIM1 is 16bit, so it will roll-over every 2 seconds, causing a wake-up
//
// - TIM22 also uses the LSE as clock source
// - TIM22 can be correlated to LPTIM1
// - TIM22 is used as the on-time wake-up source for S0/S1
// - TIM22 can be used to timestamp external events, e.g. DIO0 from the radio
//
//
//    R0 ──────┬────────┐
//    ╷        │        │
//    │        ▾        ▾
//    │        R1       R2
//    │        ╷        ╷
//    │        │        │
//    ▾        ▾        ▾
//    S0       S1       S2
//
//
// R0 is the main run mode. R1-2 are only used on the way to and from their
// respective sleep modes. Also, timer roll-over is handled in the R mode
// corresponding to the current sleep mode to avoid waking up completely.
// Otherwise, a wake-up out of any sleep mode will return from the
// corresponding R mode back to R0.
//
// S0 is a low-latency sleep mode, i.e. it can be entered and exited in less
// than 1 tick.
//
// Run modes:
// - R0: Run       32MHz HSI  ~ 7.15    mA
// - R1: Run        4MHz MSI  ~ 0.700   mA
// - R2: LP Run    65kHz MSI  ~ 0.00945 mA     - flash off (must run from RAM)
//
// Sleep modes:
// - S0: Sleep     32MHz HSI  ~ 1.750   mA     - e.g. when high-speed-clock-dependent peripherals are active
// - S1: Sleep      4MHz MSI  ~ 0.150   mA     - e.g. when the time is near
// - S2: Stop            n/a  ~ 0.00043 mA     - all other cases
//
// Peripherals and applications can use the hal_setMaxSleep() and
// hal_clearMaxSleep() APIs in matching pairs to restrict the HAL from entering
// the deeper sleep modes.
//
// Wake-up times to R0 (empirical)
// - from S0         < 1 tick
// - from S1         ~ 4 ticks
// - from S2       ~ 177 ticks
//
// Observations:
// - The events of the LPTIM peripheral fire a tick "late":
//   - After CMPM, CNT reads CMP+1
//   - After ARRM, CNT reads 0
// - When switching clocks or clock frequencies, the TIM22 peripheral misses
//   some clock edges.
//
// Things to consider:
// - Option to set Run speed (16Mhz could be enough and save power)


#define PWR_CR_VOS_VRANGE1      (               PWR_CR_VOS_0)   // 1.8 V
#define PWR_CR_VOS_VRANGE2      (PWR_CR_VOS_1               )   // 1.5 V
#define PWR_CR_VOS_VRANGE3      (PWR_CR_VOS_1 | PWR_CR_VOS_0)   // 1.2 V

// Busy wait on condition with timeout (about 10s)
#define SAFE_while(reason, expr) do { \
    uint32_t __timeout = (1 << 25); \
    while( expr ) { \
        if( __timeout-- == 0 ) { \
            panic(reason, hal_getpc()); \
        } \
    } \
} while (0)

// initialize the LSE and the LPTIM1 peripheral
static void time_init (void) {
    // check if LSE is still on
    if( (RCC->CSR & RCC_CSR_LSEON) == 0) {
        // enable power manager peripheral
        RCC->APB1ENR |= RCC_APB1ENR_PWREN;
        // disable RTC write protection (LSE bits in RCC->CSR are in RTC domain)
        PWR->CR |= PWR_CR_DBP;

        // Reset RTC
        RCC->CSR |= RCC_CSR_RTCRST;
        RCC->CSR &= ~RCC_CSR_RTCRST;

#ifdef BRD_clock_lsedrv
        // set the drive strength for the LSE (specify in board file if needed)
        RCC->CSR = (RCC->CSR & ~RCC_CSR_LSEDRV) | BRD_clock_lsedrv;
#endif

        // turn on LSE
        RCC->CSR |= RCC_CSR_LSEON;

        // re-enable RTC write protection
        PWR->CR &= ~PWR_CR_DBP;
        // disable power manager peripheral
        RCC->APB1ENR &= ~RCC_APB1ENR_PWREN;
    }

    // wait until LSE is ready
    SAFE_while(PANIC_LSE_NOSTART, (RCC->CSR & RCC_CSR_LSERDY) == 0);

    // select LSE as LPTIM1 clock source
    RCC->CCIPR |= (RCC_CCIPR_LPTIM1SEL_0 | RCC_CCIPR_LPTIM1SEL_1);

    // enable clock to LPTIM1 peripheral
    RCC->APB1ENR |= RCC_APB1ENR_LPTIM1EN;

    // reset LPTIM1 peripheral
    RCC->APB1RSTR |= RCC_APB1RSTR_LPTIM1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_LPTIM1RST;

    // enable LPTIM1 peripheral
    LPTIM1->CR |= LPTIM_CR_ENABLE;

    // initialize auto-reload register
    LPTIM1->ICR = LPTIM_ICR_ARROKCF;
    LPTIM1->ARR = 0xffff;
    while( (LPTIM1->ISR & LPTIM_ISR_ARROK) == 0 );

    // enable auto-reload interrupt
    NVIC_EnableIRQ(LPTIM1_IRQn);
    LPTIM1->IER = LPTIM_IER_ARRMIE;

    // start counting in continuous mode
    LPTIM1->CR |= LPTIM_CR_CNTSTRT;

    // enable clock to TIM22 peripheral
    RCC->APB2ENR |= RCC_APB2ENR_TIM22EN;

    // reset TIM22 peripheral
    RCC->APB2RSTR |= RCC_APB2RSTR_TIM22RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_TIM22RST;

    // set TIM22 clock source to LSE
    TIM22->SMCR = TIM_SMCR_ECE;
    TIM22->OR |= 3;

    // enable TIM22 interrupts in NVIC
    NVIC_EnableIRQ(TIM22_IRQn);

    // start counting
    TIM22->CR1 = TIM_CR1_CEN;

#if CFG_watchdog
    // configure watchdog
    IWDG->KR = 0xcccc;          // start
    IWDG->KR = 0x5555;          // enable write
    IWDG->PR = 4;               // prescaler: 64
    IWDG->RLR = (1 << 12) - 1;  // 37kHz⁻¹ * 64 * 2^12 = ~7s
    while( IWDG->SR );          // wait until ready
    IWDG->KR = 0xaaaa;          // refresh
#endif
}

__attribute__((noinline,used)) static void _time_irq (uint32_t addr) {
    // overflow
    if( (LPTIM1->ISR & LPTIM_ISR_ARRM) != 0 ) {
        LPTIM1->ICR = LPTIM_ICR_ARRMCF;
        HAL.ticks += 1;
        if( HAL.watchcount && --HAL.watchcount == 0 ) {
            // log return address
            TRACE_ADDR(addr);
            // fail and reboot
            ASSERT(0);
        }
#if CFG_watchdog
        IWDG->KR = 0xaaaa; // refresh
#endif
    }
    if( (LPTIM1->ISR & LPTIM_ISR_CMPM) != 0 ) {
        LPTIM1->ICR = LPTIM_ICR_CMPMCF;
    }
}

__attribute__((naked)) void time_irq (void) {
    __asm__("    movs r0, #4\n"
            "    mov  r1, lr\n"
            "    tst  r0, r1\n"
            "    bne  1f\n"
            "    mrs  r0, msp\n"
            "    b    2f\n"
            "1:  mrs  r0, psp\n"
            "2:  ldr  r0, [r0, #0x18]\n"
            "    b    _time_irq");
}

// NOTE: only call if interrupts are disabled
// NOTE: only call if clock is fast enough
static uint32_t time_cnt_unsafe (void) {
    uint32_t t0, t1;
    t1 = LPTIM1->CNT;
    do {
        t0 = t1;
        t1 = LPTIM1->CNT;
    } while( t1 != t0 );
    return t1;
}

__attribute__((always_inline)) static inline void flash_off (void) {
    // unlock RUN_PD bit
    FLASH->PDKEYR = 0x04152637;
    FLASH->PDKEYR = 0xFAFBFCFD;
    // disable flash
    FLASH->ACR |= FLASH_ACR_RUN_PD;
    RCC->AHBENR &= ~RCC_AHBENR_MIFEN;
}

__attribute__((always_inline)) static inline void flash_on (void) {
    // enable flash (this also relocks the RUN_PD bit)
    RCC->AHBENR |= RCC_AHBENR_MIFEN;
    (void) FLASH->ACR; // dummy read (see STM32L0xx errata)
    FLASH->ACR &= ~FLASH_ACR_RUN_PD;
}

// Sleep until hticks reaches htt (or other interrupt occurs)
// - implemented in sleep.S
// - fastcode, i.e. can be called with Flash off
extern u4_t sleep_htt (u4_t hticks, u4_t htt);

// Sleep until hticks reaches htt and lticks reaches ltt (or other interrupt occurs)
// Note: The secondary sleep step uses the TIM22 peripheral, correlation
//       to which only works at higher clock speeds.
static u4_t sleep_htt_ltt (u4_t hticks, u4_t htt, u4_t ltt) {
    while( hticks < htt ) {
        // zzzz....
        __WFI();

        // read and check ISR
        uint32_t isr = LPTIM1->ISR & LPTIM_ISR_ARRM;
        if( isr == 0 ) {
            // other interrupt, g'day
            goto done;
        }

        // clear ISR bits and NVIC
        LPTIM1->ICR = isr;
        while( (LPTIM1->ISR & isr) != 0 );
        NVIC->ICPR[0] = (1 << LPTIM1_IRQn);

        // increment hticks
        hticks += 1;
#if CFG_watchdog
        IWDG->KR = 0xaaaa; // refresh
#endif
    }

    // correlate LPTIM1 and TIM22
    u4_t t0, t1, tx;
    t1 = LPTIM1->CNT;
    do {
        t0 = t1;
        tx = TIM22->CNT;
        t1 = LPTIM1->CNT;
    } while( t1 != t0);

    // calculate number of ticks for secondary sleep phase
    s4_t d = (s4_t) ltt - (s4_t) t0;
    if( d > 0 ) {
        TIM22->CCR2 = (tx + d) & 0xffff;
        TIM22->SR = ~TIM_SR_CC2IF;
        TIM22->DIER |= TIM_DIER_CC2IE;
        TIM22->CCER |= TIM_CCER_CC2E;

        // zzzz....
        __WFI();

        TIM22->CCER &= ~TIM_CCER_CC2E;
        TIM22->DIER &= ~TIM_DIER_CC2IE;
        TIM22->SR = ~TIM_SR_CC2IF;
        NVIC->ICPR[0] = (1 << TIM22_IRQn);
    }
done:
    return hticks;
}

// used by both wake-up and init
static void clock_run (void) {
    // use 1 flash wait state (required for 32+MHz in Vrange 1 and 16+MHz in Vrange 2)
    FLASH->ACR |= FLASH_ACR_LATENCY;
    while( (FLASH->ACR & FLASH_ACR_LATENCY) == 0 );
    // enable flash prefetch buffer
    FLASH->ACR |= FLASH_ACR_PRFTEN;

    // disable LP sleep, select Vrange 1
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR &= ~PWR_CR_LPRUN; // must be cleared seperately
    PWR->CR = PWR_CR_VOS_VRANGE1;
    while( (PWR->CSR & PWR_CSR_VOSF) != 0 );
    RCC->APB1ENR &= ~RCC_APB1ENR_PWREN;

    // clear deep sleep (stop mode)
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    // enable HSI
    RCC->CR |= RCC_CR_HSION;
    while( (RCC->CR & RCC_CR_HSIRDY) == 0 );

    // enable PLL
    RCC->CR |= RCC_CR_PLLON;
    while( (RCC->CR & RCC_CR_PLLRDY) == 0 );

    // switch clock source to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while( (RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL );

    // disable MSI
    RCC->CR &= ~RCC_CR_MSION;
}

static void clock_sleep (int stype) {
    // startup MSI
    if( stype == HAL_SLEEP_S1 ) {
        RCC->ICSCR = (RCC->ICSCR & ~RCC_ICSCR_MSIRANGE) | RCC_ICSCR_MSIRANGE_6; // 4MHz
    } else {
        RCC->ICSCR = (RCC->ICSCR & ~RCC_ICSCR_MSIRANGE) | RCC_ICSCR_MSIRANGE_0; // 65kHz
    }
    RCC->CR |= RCC_CR_MSION;
    while( (RCC->CR & RCC_CR_MSIRDY) == 0 );

    // switch clock source to MSI
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_MSI;
    while( (RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_MSI );

    // disable flash prefetch buffer
    FLASH->ACR &= ~FLASH_ACR_PRFTEN;
    // disable flash wait states
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    while( (FLASH->ACR & FLASH_ACR_LATENCY) != 0 );

    // disable PLL and HSI
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSION);

    // select Vrange and LP options (LP modes require Vrange 2!)
    uint32_t pwr_cr;
    if( stype == HAL_SLEEP_S2 ) {
        pwr_cr = PWR_CR_VOS_VRANGE2
            | PWR_CR_LPSDSR      // LP sleep
            | PWR_CR_ULP         // Ultra LP sleep (no Vref, thus no BOR, PVD, temp sensor)
          /*| PWR_CR_DSEEKOFF */ // Do not power up NVM on wake up   FIXME doesn't work as advertized ?!?
            ;
        // set sleep mode to deep (maps to Stop mode)
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    } else {
        pwr_cr = PWR_CR_VOS_VRANGE3;
    }
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = pwr_cr;
    while( (PWR->CSR & PWR_CSR_VOSF) != 0 );
    if( stype != HAL_SLEEP_S1 ) {
        PWR->CR |= PWR_CR_LPRUN; // LP run
    }
    RCC->APB1ENR &= ~RCC_APB1ENR_PWREN;
}

__fastcode static u4_t sleep2 (u4_t hticks, u4_t htt, u4_t ltt) {
    clock_sleep(HAL_SLEEP_S2);
    flash_off();
    hticks = sleep_htt(HAL.ticks, htt);
    flash_on();
    clock_run();
    return hticks;
}

static u4_t sleep1 (u4_t hticks, u4_t htt, u4_t ltt) {
    clock_sleep(HAL_SLEEP_S1);
    hticks = sleep_htt_ltt(HAL.ticks, htt, ltt);
    clock_run();
    return hticks;
}

static u4_t sleep0 (u4_t hticks, u4_t htt, u4_t ltt) {
    hticks = sleep_htt_ltt(HAL.ticks, htt, ltt);
    return hticks;
}

static void sleep (int stype, u4_t htt, u4_t ltt) {
    static u4_t(* const sleepfuncs[])(u4_t,u4_t,u4_t) = {
        sleep0,
        sleep1,
        sleep2,
    };
    HAL.ticks = sleepfuncs[stype](HAL.ticks, htt, ltt);
}

static void clock_init() {
    // System is clocked by MSI @2.1MHz at startup
    // We want to go to PLL(HSI16) @32MHz

    // configure PLL: source=HSI16, multiplier=4, divider=2
    RCC->CFGR |= (RCC_CFGR_PLLSRC_HSI | RCC_CFGR_PLLMUL4 | RCC_CFGR_PLLDIV2);

    // startup HSI/PLL fast clock
    clock_run();

    // sleep mode settings
    FLASH->ACR |= FLASH_ACR_SLEEP_PD; // enable flash power-off during sleep
}

// NOTE: only call if interrupts are disabled
#define hal_ticks_unsafe() ((u4_t) hal_xticks_unsafe())
static u8_t hal_xticks_unsafe (void) {
    u8_t xt = HAL.ticks;
    u4_t cnt = time_cnt_unsafe();
    if( (LPTIM1->ISR & LPTIM_ISR_ARRM) != 0 ) {
        // include pending overflow in evaluation
        cnt = time_cnt_unsafe();
        xt++;
    }
    return (xt << 16) | cnt;
}

u8_t hal_xticks () {
    hal_disableIRQs();
    u8_t xt = hal_xticks_unsafe();
    hal_enableIRQs();
    return xt;
}

u4_t hal_ticks () {
    return hal_xticks();
}

// NOTE: interrupts are already be disabled when this HAL function is called!
u1_t hal_sleep (u1_t type, u4_t targettime) {
    static const u8_t S_TH[] = {
        0, 6, 190
    };

    u8_t xnow = hal_xticks_unsafe();
    s4_t dt;
    if( type == HAL_SLEEP_FOREVER ) {
        dt = sec2osticks(12*60*60); // 12 h
    } else {
        dt = (s4_t) targettime - (s4_t) xnow;
    }
    if( dt <= 0 ) {
        return 0; // it's time now
    }

    // select sleep type
    int stype;
    for( stype = 0; stype < (HAL_SLEEP_CNT-1); stype++ ) {
        if( dt < S_TH[stype + 1] || HAL.maxsleep[stype] ) {
            break;
        }
    }

    // only use S2 if htt would be strictly larger
    if( stype == HAL_SLEEP_S2 ) {
        u8_t xtt = xnow + dt - S_TH[HAL_SLEEP_S2];
        if( (xtt >> 16) <= (xnow >> 16) ) {
            stype -= 1;
        }
    }

#ifdef CFG_rtstats
    static ostime_t wakeup;
    ostime_t t1 = xnow;
    ASSERT((t1 - wakeup) >= 0);
    HAL.rtstats.run += (t1 - wakeup);
#endif

    xnow += (dt - S_TH[stype]);
    sleep(stype, xnow >> 16, xnow & 0xffff);

#ifdef CFG_rtstats
    ostime_t t2 = hal_ticks_unsafe();
    ASSERT((t2 - t1) >= 0);
    HAL.rtstats.sleep[stype] += (t2 - t1);
    wakeup = t2;
#endif

    return 1; // we slept
}

// short-term busy wait
// shouldn't be used for extended periods of time
// cannot possibly wait for more than 2 sec when interrupts are disabled because of timer overrun
void hal_waitUntil (u4_t time) {
    // assure waiting period is in intended range of up to 1 sec (and hasn't expired too long ago)
    ostime_t diff = time - hal_ticks();
    ASSERT(diff > -sec2osticks(1) && diff < sec2osticks(1));
    // busy wait until timestamp is reached
    while( ((s4_t) time - (s4_t) hal_ticks()) > 0 );
}

void hal_setMaxSleep (unsigned int level) {
    hal_disableIRQs();
    ASSERT(level < HAL_SLEEP_CNT-1);
    HAL.maxsleep[level] += 1;
    hal_enableIRQs();
}

void hal_clearMaxSleep (unsigned int level) {
    hal_disableIRQs();
    ASSERT(level < HAL_SLEEP_CNT-1);
    ASSERT(HAL.maxsleep[level]);
    HAL.maxsleep[level] -= 1;
    hal_enableIRQs();
}


// -----------------------------------------------------------------------------
// I/O

static void hal_io_init () {
#ifndef CFG_noradio
#if defined(GPIO_RX)
    SET_PIN_ONOFF(GPIO_RX, 0);
    CFG_PIN(GPIO_RX, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
#endif
#if defined(GPIO_TX)
    SET_PIN_ONOFF(GPIO_TX, 0);
    CFG_PIN(GPIO_TX, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
#endif
#if defined(GPIO_TX2)
    SET_PIN_ONOFF(GPIO_TX2, 0);
    CFG_PIN(GPIO_TX2, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
#endif
#ifdef GPIO_TXRX_EN
    SET_PIN_ONOFF(GPIO_TXRX_EN, 0);
    CFG_PIN(GPIO_TXRX_EN, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
#endif
#ifdef GPIO_DIO0
    IRQ_PIN(GPIO_DIO0, GPIO_IRQ_RISING);
#endif
#ifdef GPIO_DIO1
    IRQ_PIN(GPIO_DIO1, GPIO_IRQ_RISING);
#endif
#ifdef GPIO_DIO2
    IRQ_PIN(GPIO_DIO2, GPIO_IRQ_RISING);
#endif
#ifdef GPIO_DIO3
    IRQ_PIN(GPIO_DIO3, GPIO_IRQ_RISING);
#endif
#endif
}

#if defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)
bool hal_pin_tcxo (u1_t val) {
#if defined(GPIO_TCXO_PWR)
    if (val != 0) {
        SET_PIN(GPIO_TCXO_PWR, 1);
        CFG_PIN(GPIO_TCXO_PWR, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    } else {
        SET_PIN(GPIO_TCXO_PWR, 0);
        CFG_PIN_DEFAULT(GPIO_TCXO_PWR);
    }
    return true;
#else
    return false;
#endif
}
#endif // defined(BRD_sx1272_radio) || defined(BRD_sx1276_radio)

void hal_ant_switch (u1_t val) {
#ifdef SVC_pwrman
    static ostime_t t1;
    static int ctype;
    static uint32_t radio_ua;
    ostime_t now = hal_ticks();
    if( radio_ua ) {
        pwrman_consume(ctype, now - t1, radio_ua);
        radio_ua = 0;
    }
#endif
    if (val == HAL_ANTSW_OFF) {
#ifdef GPIO_TXRX_EN
        SET_PIN_ONOFF(GPIO_TXRX_EN, 0);
#endif
    } else {
#ifdef SVC_pwrman
        t1 = now;
        ctype = (val == HAL_ANTSW_RX) ? PWRMAN_C_RX : PWRMAN_C_TX;
        radio_ua = LMIC.radioPwr_ua;
#endif
#ifdef GPIO_TXRX_EN
        SET_PIN_ONOFF(GPIO_TXRX_EN, 1);
#endif
    }
#ifdef GPIO_RX
    SET_PIN_ONOFF(GPIO_RX, (val == HAL_ANTSW_RX));
#endif
#ifdef GPIO_TX
    SET_PIN_ONOFF(GPIO_TX, (val == HAL_ANTSW_TX));
#endif
#ifdef GPIO_TX2
    SET_PIN_ONOFF(GPIO_TX2, (val == HAL_ANTSW_TX2));
#endif
}

// set radio RST pin to given value (or keep floating!)
bool hal_pin_rst (u1_t val) {
    #ifdef GPIO_RST
    if(val == 0 || val == 1) { // drive pin
        SET_PIN(GPIO_RST, val);
        CFG_PIN(GPIO_RST, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    } else { // keep pin floating
        CFG_PIN(GPIO_RST, GPIOCFG_MODE_ANA | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_NONE);
    }
    return true;
    #else
    return false;
    #endif
}

void hal_pin_busy_wait (void) {
#ifdef GPIO_BUSY
    CFG_PIN(GPIO_BUSY, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_40MHz);

    while (GET_PIN(GPIO_BUSY) != 0);

    CFG_PIN_DEFAULT(GPIO_BUSY);
#endif
}

#if defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)
bool hal_dio3_controls_tcxo (void) {
    #if defined(LMIC_DIO3_CONTROLS_TCXO)
    return true;
    #else
    return false;
    #endif
}

bool hal_dio2_controls_rxtx (void) {
    #if defined(LMIC_DIO2_CONTROLS_RXTX)
    return true;
    #else
    return false;
    #endif
}
#endif // defined(BRD_sx1261_radio) || defined(BRD_sx1262_radio)

#define DIO_UPDATE(dio,mask,time) do { \
    if( (EXTI->PR & (1 << BRD_PIN(GPIO_DIO ## dio))) ) { \
        EXTI->PR = (1 << BRD_PIN(GPIO_DIO ## dio)); \
        *(mask) |= (1 << dio); \
        /* if( BRD_GPIO_GET_CHAN(GPIO_DIO ## dio) ) { \
            unsigned int ch = BRD_GPIO_GET_CHAN(GPIO_DIO ## dio) - 1; \
            ASSERT(TIMx->SR & (TIM_SR_CC1IF << ch)); \
            s2_t ct = (&(TIMx->CCR1))[ch]; \
            *(time) += (ct - (s2_t) *(time)); \
        } */ \
    } \
} while( 0 )

// generic EXTI IRQ handler for all channels
static void EXTI_IRQHandler () {
    u4_t now = hal_ticks_unsafe();
    u1_t diomask = 0;
#ifdef GPIO_DIO0
    // DIO 0
    DIO_UPDATE(0, &diomask, &now);
#endif
#ifdef GPIO_DIO1
    // DIO 1
    DIO_UPDATE(1, &diomask, &now);
#endif
#ifdef GPIO_DIO2
    // DIO 2
    DIO_UPDATE(2, &diomask, &now);
#endif
#ifdef GPIO_DIO3
    // DIO 3
    DIO_UPDATE(3, &diomask, &now);
#endif

    if(diomask) {
        // invoke radio handler (on IRQ)
        radio_irq_handler(diomask, now);
    }

#ifdef CFG_EXTI_IRQ_HANDLER
    // invoke user-defined interrupt handler
    {
        extern void CFG_EXTI_IRQ_HANDLER(void);
        CFG_EXTI_IRQ_HANDLER();
    }
#endif // CFG_EXTI_IRQ_HANDLER
}

static void dio_config (int mask, int pin, int gpio) {
    if( mask & pin ) {
#if 0
        if( BRD_GPIO_GET_CHAN(gpio) ) {
            unsigned int ch = BRD_GPIO_GET_CHAN(gpio) - 1;
            TIMx->CCMR1 |= (1 << (ch << 3));
            TIMx->SR = ~(TIM_SR_CC1IF << ch);
            TIMx->CCER |= (TIM_CCER_CC1E << (ch << 2));
            CFG_PIN_AF(gpio, 0);
        } else
#endif
        {
            CFG_PIN(gpio, GPIOCFG_MODE_INP);
        }
        IRQ_PIN_SET(gpio, 1);
    } else {
#if 0
        if( BRD_GPIO_GET_CHAN(gpio) ) {
            unsigned int ch = BRD_GPIO_GET_CHAN(gpio) - 1;
            TIMx->CCER &= ~(TIM_CCER_CC1E << (ch << 2));
        }
#endif
        IRQ_PIN_SET(gpio, 0);
        CFG_PIN_DEFAULT(gpio);
        EXTI->PR = (1 << BRD_PIN(gpio)); // clear irq
    }
}

void hal_irqmask_set (int mask) {
    static int prevmask = 0;

#ifdef GPIO_DIO0
    dio_config(mask, HAL_IRQMASK_DIO0, GPIO_DIO0);
#endif
#ifdef GPIO_DIO1
    dio_config(mask, HAL_IRQMASK_DIO1, GPIO_DIO1);
#endif
#ifdef GPIO_DIO2
    dio_config(mask, HAL_IRQMASK_DIO2, GPIO_DIO2);
#endif
#ifdef GPIO_DIO3
    dio_config(mask, HAL_IRQMASK_DIO3, GPIO_DIO3);
#endif

    mask = (mask != 0);
    if (prevmask != mask) {
        // prevent sleep if we are waiting for radio interrupts
        // TODO - evaluate if that is the correct thing to do
        if (mask) {
            hal_setMaxSleep(HAL_SLEEP_S0);
        } else {
            hal_clearMaxSleep(HAL_SLEEP_S0);
        }
        prevmask = mask;
    }
}


// -----------------------------------------------------------------------------
// IRQ

void hal_disableIRQs () {
    __disable_irq();
    HAL.irqlevel++;
}

void hal_enableIRQs () {
    if(--HAL.irqlevel == 0) {
        __enable_irq();
    }
}

#ifdef CFG_rtstats
void hal_rtstats_collect (hal_rtstats* stats) {
    stats->run_ticks = HAL.rtstats.run;
    HAL.rtstats.run = 0;
    for( int i = 0; i < HAL_SLEEP_CNT; i++ ) {
        stats->sleep_ticks[i] = HAL.rtstats.sleep[i];
        HAL.rtstats.sleep[i] = 0;
    }
}
#endif


#ifdef BRD_borlevel
// -----------------------------------------------------------------------------
// Brown-out reset

__fastcode static void write_optbyte (volatile uint32_t* pdest, uint32_t data) {
    // unlock data eeprom memory and registers
    FLASH->PEKEYR = 0x89ABCDEF; // FLASH_PEKEY1
    FLASH->PEKEYR = 0x02030405; // FLASH_PEKEY2

    // unlock option bytes area
    FLASH->OPTKEYR = 0xFBEAD9C8; // FLASH_OPTKEY1
    FLASH->OPTKEYR = 0x24252627; // FLASH_OPTKEY2

    // write option byte with complement
    *pdest = data | (~data << 16);

    // wait for programming to complete
    SAFE_while(PANIC_SR_BUSY, FLASH->SR & FLASH_SR_BSY);

    // check status
    ASSERT(FLASH->SR & FLASH_SR_EOP);
    FLASH->SR = FLASH_SR_EOP;

    // reload option bytes (causes reset)
    FLASH->PECR |= FLASH_PECR_OBL_LAUNCH;
}

static void setbrownout (int level) {
    unsigned int user = OB->USER & 0xffff;
    if( (user & 0xf) != level ) {
        write_optbyte(&OB->USER, (user & ~0xf) | level);
        // not reached
        ASSERT(0);
    }
}

#endif


// -----------------------------------------------------------------------------

#ifdef CFG_DEBUG
static void debug_init (void); // fwd decl
#endif

void hal_init (void* bootarg) {
    memset(&HAL, 0x00, sizeof(HAL));
    HAL.boottab = bootarg;
    HAL.battlevel = MCMD_DEVS_BATT_NOINFO;

    HAL.reset = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;

    ASSERT(HAL.boottab->version >= 0x105); // require bootloader v261

#ifdef BRD_borlevel
    setbrownout(BRD_borlevel);
#endif

    hal_disableIRQs();

    clock_init();

    pd_init();

#if 1
    // disable single-wire debug (SWD) when running
    CFG_PIN_DEFAULT(BRD_GPIO(PORT_A, 13));
    CFG_PIN_DEFAULT(BRD_GPIO(PORT_A, 14));
#endif

#if defined(CFG_wailmer_board) || defined(CFG_wailord_board) || defined(CFG_coxproto_board) || defined(CFG_itrack_board)
    // ensure GNSS is turned off (there is no external pull-down on load switch control line)
    CFG_PIN(GPIO_GNSS_EN, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    SET_PIN(GPIO_GNSS_EN, 0);
#endif

    // configure radio I/O and interrupt handler
    hal_io_init();
    // configure radio SPI
    hal_spi_init();
    // configure timer and interrupt handler
    time_init();

    hal_enableIRQs();

    leds_init();
#ifdef BRD_USART
    usart_init();
#endif
#ifdef BRD_VIBE_TIM
    vibe_init();
#endif

#if defined(CFG_perso)
    perso_enter();
#endif

#ifdef CFG_DEBUG
    debug_init();
#endif

#if defined(SVC_eefs)
    eefs_init((void*) APPDATA_BASE, APPDATA_SZ);
#endif
}

const irqdef HAL_irqdefs[] = {
    { EXTI0_1_IRQn, EXTI_IRQHandler },
    { EXTI2_3_IRQn, EXTI_IRQHandler },
    { EXTI4_15_IRQn, EXTI_IRQHandler },

    { LPTIM1_IRQn, time_irq },

#if defined(BRD_I2C)
#if BRD_I2C == 1
    { I2C1_IRQn, i2c_irq },
#endif
#endif

#if defined(BRD_USART)
#if BRD_USART == 1
    { USART1_IRQn, usart_irq },
#elif BRD_USART == BRD_LPUART(1)
    { LPUART1_IRQn, usart_irq },
#endif
#endif

#if defined(BRD_PWM_TIM)
#if BRD_PWM_TIM == 3
    { TIM3_IRQn, pwm_irq },
#endif
#endif

#if defined(BRD_PIR_TIM)
#if BRD_PIR_TIM == 3
    { TIM3_IRQn, pir_tim_irq },
#endif
#endif

#if defined(BRD_LED_TIM)
#if BRD_LED_TIM == 2
    { TIM2_IRQn, leds_pwm_irq },
#endif
#endif

#if defined(BRD_IR_TIM)
    { TIM2_IRQn, ir_tim_irq },
#endif

    { ~0, NULL } // end of list
};

unsigned int crc32 (void* ptr, int nwords) {
    return HAL.boottab->crc32(ptr, nwords);
}

u1_t hal_getBattLevel (void) {
    return HAL.battlevel;
}

void hal_setBattLevel (u1_t level) {
    HAL.battlevel = level;
}


#ifdef CFG_DEBUG

#if BRD_DBG_UART == 1
#define DBG_USART USART1
#define DBG_USART_enable()    do { RCC->APB2ENR |= RCC_APB2ENR_USART1EN; } while (0)
#define DBG_USART_disable()   do { RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN; } while (0)
#elif BRD_DBG_UART == 2
#define DBG_USART USART2
#define DBG_USART_enable()    do { RCC->APB1ENR |= RCC_APB1ENR_USART2EN; } while (0)
#define DBG_USART_disable()   do { RCC->APB1ENR &= ~RCC_APB1ENR_USART2EN; } while (0)
#elif BRD_DBG_UART == 4
#define DBG_USART USART4
#define DBG_USART_enable()    do { RCC->APB1ENR |= RCC_APB1ENR_USART4EN; } while (0)
#define DBG_USART_disable()   do { RCC->APB1ENR &= ~RCC_APB1ENR_USART4EN; } while (0)
#endif
#define DBG_USART_WRITE(c)    do { DBG_USART->TDR = (c); } while (0)
#define DBG_USART_BUSY()      ((DBG_USART->ISR & USART_ISR_TXE) == 0)
#define DBG_USART_TXING()     ((DBG_USART->ISR & USART_ISR_TC) == 0)

static void debug_init (void) {
    // configure USART (115200/8N1, tx-only)
    DBG_USART_enable();
    DBG_USART->BRR = 16; // 2000000 baud (APB1 clock @32MHz)
    DBG_USART->CR1 = USART_CR1_UE | USART_CR1_TE; // usart + transmitter enable
    DBG_USART_disable();
#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
}

void hal_debug_str (const char* str) {
    DBG_USART_enable();
    CFG_PIN_AF(GPIO_DBG_TX, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    char c;
    while( (c = *str++) ) {
        while( DBG_USART_BUSY() );
        DBG_USART_WRITE(c);
    }
    while( DBG_USART_TXING() );
    CFG_PIN(GPIO_DBG_TX, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    DBG_USART_disable();
}

void hal_debug_led (int val) {
#if defined(GPIO_DBG_LED)
    leds_set(GPIO_DBG_LED, val);
#endif
}

#endif


void hal_fwinfo (hal_fwi* fwi) {
    fwi->blversion = HAL.boottab->version;

    extern volatile hal_fwhdr fwhdr;
    fwi->version = fwhdr.version;
    fwi->crc = fwhdr.boot.crc;
    fwi->flashsz = FLASH_SZ;
}

u4_t hal_unique (void) {
    uint32_t unique[3] = {
        *((uint32_t*) UNIQUE_ID0),
        *((uint32_t*) UNIQUE_ID1),
        *((uint32_t*) UNIQUE_ID2)
    };
    return crc32(&unique, 3);
}

void hal_reboot (void) {
    NVIC_SystemReset();
    // not reached
    hal_failed();
}

// persistent storage of stack data
typedef struct {
    uint32_t    dnonce[4];      // dev nonce history
    uint32_t    jnonce[4];      // join nonce history
} pdata;

// Note: The next nonce is stored, and the writes are spread over 4 fields. At
// 100k write cycles, this will allow 400k join requests. The DevNonce counter
// is only 16 bits, so it will roll-over much earlier, but the counter can be
// restarted at 0 if/when the Join EUI changes.

u4_t hal_dnonce_next (void) {
    pdata* p = (pdata*) STACKDATA_BASE;
    int x = 0;
    while( x < 3 && p->dnonce[x] < p->dnonce[x + 1] ) {
        x += 1;
    }
    u4_t dn = p->dnonce[x];
    eeprom_write(p->dnonce + ((x + 1) & 3), dn + 1);
    return dn;
}

void hal_dnonce_clear (void) {
    pdata* p = (pdata*) STACKDATA_BASE;
    for( int i = 0; i < 4; i++) {
        eeprom_write(p->dnonce + i, 0);
    }
}

void sha256 (uint32_t* hash, const uint8_t* msg, uint32_t len) {
    HAL.boottab->sha256(hash, msg, len);
}

bool hal_set_update (void* ptr) {
    return HAL.boottab->update(ptr, NULL) == BOOT_OK;
}

void flash_write (void* dst, const void* src, unsigned int nwords, bool erase) {
    hal_disableIRQs();
    HAL.boottab->wr_flash(dst, src, nwords, erase);
    hal_enableIRQs();
}

void hal_logEv (uint8_t evcat, uint8_t evid, uint32_t evparam) {
    // XXX:TBD
}
