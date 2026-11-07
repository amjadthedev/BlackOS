/* =============================================================================
 *  Black OS - Phase 11: CMOS Real-Time Clock (lock screen time source)
 * ========================================================================== */
#ifndef BLACKOS_RTC_H
#define BLACKOS_RTC_H

#include "types.h"

struct rtc_time {
    u8  sec, minute, hour;              /* 24-hour format                    */
    u8  wday, day;
    u8  month;                          /* 1..12                             */
    u16 year;                           /* full year, e.g. 2026              */
};

void    rtc_init(void);                  /* clear update status               */
int     rtc_get(struct rtc_time *out);   /* -1 on read race (retry next pass) */

#endif /* BLACKOS_RTC_H */
