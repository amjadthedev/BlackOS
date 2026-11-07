/* =============================================================================
 *  Black OS - Phase 12: live weather pipeline (DHCP -> DNS -> HTTP -> JSON)
 * ========================================================================== */
#ifndef BLACKOS_WEATHER_H
#define BLACKOS_WEATHER_H

#include "types.h"

/* ── widget view-model (what the UI renders) ─────────────────────────────  */
struct weather_data {
    int     valid;                       /* 1 once a fetch completed          */
    int     temperature;                 /* degrees Celsius                   */
    int     condition;                   /* WMO-ish icon code (see below)     */
    char    city[24];
    char    area[24];
};

/* condition codes (compact custom enum for icon mapping)                  */
#define W_CLEAR        0
#define W_CLOUDY       1
#define W_OVERCAST     2
#define W_FOG          3
#define W_DRIZZLE      4
#define W_RAIN         5
#define W_HEAVY_RAIN   6
#define W_SNOW         7
#define W_THUNDER      8
#define W_UNKNOWN      9

void weather_init(void);                 /* kick off pipeline in the bg       */
void weather_poll(void);                 /* pump via netstack (call freely)   */
void weather_get(struct weather_data *out);   /* snapshot copy                */
int  weather_fresh(void);
void weather_status(char *out, u32 cap); /* human summary, e.g. "+12C rain"  */

#ifndef BLACKOS_WEATHER_HOST
#define BLACKOS_WEATHER_HOST "ip-api.com"
#endif

#endif /* BLACKOS_WEATHER_H */
