/* =============================================================================
 *  Black OS - Phase 11: CMOS Real-Time Clock (lock screen time source)
 *
 *  MC146818 register set via ports 0x70/0x71; behind IRQ8 output. We read
 *  synchronously (polling), guarding against the update-in-progress flag so
 *  the values stay internally consistent.
 * ========================================================================== */
#include "rtc.h"
#include "io.h"

#define CMOS_ADDR       0x70
#define CMOS_DATA       0x71

#define RTC_SEC        0x00
#define RTC_MIN        0x02
#define RTC_HOUR       0x04
#define RTC_WDAY       0x06
#define RTC_MDAY       0x07
#define RTC_MONTH      0x08
#define RTC_YEAR       0x09
#define RTC_STATUS_A   0x0A
#define RTC_STATUS_B   0x0B

void rtc_init(void)
{
    /* No setup needed: CMOS battery state drives the clock. Ticking happens
     * through RTC polling in lockscreen.c / shell.c.                      */
}

static u8 rtc_read(u8 reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

/* BCD -> packed binary (two digits) */
static u8 bcd(u8 v)
{
    return (u8)((v & 0x0F) + ((v >> 4) * 10));
}

int rtc_get(struct rtc_time *t)
{
    if (t == NULL)
        return -1;

    /* wait for UIP flag to clear (values stable within ~250 us)            */
    u32 spins = 0;
    while ((rtc_read(RTC_STATUS_A) & 0x80) && spins < 100000)
        spins++;

    t->sec    = rtc_read(RTC_SEC);
    t->minute = rtc_read(RTC_MIN);
    t->hour   = rtc_read(RTC_HOUR);
    t->wday   = rtc_read(RTC_WDAY);
    t->day    = rtc_read(RTC_MDAY);
    t->month  = rtc_read(RTC_MONTH);
    t->year   = rtc_read(RTC_YEAR);
    u8 regb   = rtc_read(RTC_STATUS_B);

    int bcd_mode = !(regb & 0x04);          /* bit1 set => binary data       */
    if (bcd_mode) {
        t->sec    = bcd(t->sec);
        t->minute = bcd(t->minute);
        t->hour   = bcd(t->hour & 0x7F);    /* hour: bit7 = 12hr flag        */
        t->day    = bcd(t->day);
        t->month  = bcd(t->month);
        t->year   = bcd(t->year);
    }

    if (regb & 0x02)                        /* 12-hour clock?                */
        if (regb & 0x80)
            t->hour += 12;

    t->year += 2000;                        /* CMOS year is offset 2000     */
    return 0;
}
