/* =============================================================================
 *  Black OS - Phase 12: live weather pipeline (DHCP -> DNS -> HTTP -> JSON)
 *
 *  Flow (asynchronous state machine driven by weather_poll from the GUI):
 *    1. wait for DHCP
 *    2. HTTP GET http://ip-api.com/json  -> city, regionName
 *    3. HTTP GET http://wttr.in/?format=j2 -> current_conditions temp/desc
 *    4. decode -> struct weather_data icon code + city/area
 *
 *  All network calls ride netstack's poll pump, so the desktop stays
 *  responsive; the pipeline only advances when its socket has data.
 * ========================================================================== */
#include "weather.h"
#include "netstack.h"
#include "string.h"
#include "printk.h"
#include "idt.h"                        /* uptime_ms                          */

#define WEATHER_ST_IDLE        0
#define WEATHER_ST_WAIT_DHCP   1
#define WEATHER_ST_GEO_GET     2
#define WEATHER_ST_GEO_READ    3
#define WEATHER_ST_WX_GET      4
#define WEATHER_ST_WX_READ     5
#define WEATHER_ST_DONE        6
#define WEATHER_ST_FAILED      7
#define WEATHER_STAGE_TIMEOUT  15000   /* ms; generous on QEMU slirp          */

static u8    wx_state;
static u32   wx_stage_ms;
static net_tcp_h wx_conn;

static char geo_buf[1400];              /* ip-api.com/json response           */
static u32  geo_len;
static u8   geo_done;

static char wx_buf[1400];               /* wttr.in payload                    */
static u32  wx_len;

static struct weather_data wx;

/* Map wttr.in / ip-api descriptors to the compact icon code.               */
static int weather_code_of(const char *desc)
{
    if (desc == NULL || *desc == '\0')
        return W_UNKNOWN;
    if (strstr(desc, "thunder") || strstr(desc, "Storm"))
        return W_THUNDER;
    if (strstr(desc, "snow") || strstr(desc, "Snow"))
        return W_SNOW;
    if (strstr(desc, "Heavy"))
        return W_HEAVY_RAIN;
    if (strstr(desc, "rain") || strstr(desc, "Rain") || strstr(desc, "Drizzle"))
        return W_RAIN;
    if (strstr(desc, "Overcast"))
        return W_OVERCAST;
    if (strstr(desc, "cloud") || strstr(desc, "Cloud"))
        return W_CLOUDY;
    if (strstr(desc, "fog") || strstr(desc, "mist") || strstr(desc, "Fog"))
        return W_FOG;
    if (strstr(desc, "Clear") || strstr(desc, "Sunny"))
        return W_CLEAR;
    return W_UNKNOWN;
}

/* Tiny decimal parser ("12", "-3", "+21" -> int).                          */
static int dec_parse(const char *s, int *out)
{
    if (s == NULL)
        return -1;
    int sign = 1, value = 0, digits = 0;
    if (*s == '-')
        { sign = -1; s++; }
    else if (*s == '+')
        s++;
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (digits == 0)
        return -1;
    if (out)
        *out = sign * value;
    return 0;
}

static void stage(u8 next)
{
    wx_state = next;
    wx_stage_ms = (u32)uptime_ms;
}

static int stage_expired(void)
{
    return (u32)(uptime_ms - wx_stage_ms) > WEATHER_STAGE_TIMEOUT;
}

void weather_init(void)
{
    memset(&wx, 0, sizeof(wx));
    wx.condition = W_UNKNOWN;
    memset(geo_buf, 0, sizeof(geo_buf));
    memset(wx_buf, 0, sizeof(wx_buf));
    geo_len = wx_len = 0;
    geo_done = 0;
    wx_stage_ms = (u32)uptime_ms;
    stage(WEATHER_ST_WAIT_DHCP);
}

void weather_get(struct weather_data *out) { *out = wx; }

int weather_fresh(void) { return wx.valid ? 1 : 0; }

void weather_status(char *out, u32 cap)
{
    if (out == NULL || cap < 4)
        return;
    if (!wx.valid) {
        strncpy(out, "offline", cap - 1);
        out[cap - 1] = '\0';
        return;
    }

    u32 n = 0;
    out[n++] = (wx.temperature < 0) ? '-' : '+';
    int t = (wx.temperature < 0) ? -wx.temperature : wx.temperature;
    if (t >= 10)
        out[n++] = (char)('0' + t / 10);
    out[n++] = (char)('0' + t % 10);
    out[n++] = 'C';
    out[n++] = ' ';

    static const char *const names[10] = {
        "clear", "cloudy", "overcast", "fog", "drizzle",
        "rain", "heavy rain", "snow", "thunder", "?"
    };
    const char *cond = (wx.condition < 10) ? names[wx.condition] : names[9];
    while (*cond && n < cap - 1)
        out[n++] = *cond++;
    out[n] = '\0';
}

/* Pump the HTTP stream into a bounded scratch buffer.                      */
static u32 drain_http(net_tcp_h c, char *buf, u32 cap, u32 already)
{
    u8 chunk[384];
    for (;;) {
        int n = net_http_read(c, chunk, sizeof(chunk));
        if (n <= 0)
            return already;
        u32 space = (cap - 1 > already) ? cap - 1 - already : 0;
        u32 take = (u32)n;
        if (take > space)
            take = space;
        if (take == 0)
            return already;
        memcpy(buf + already, chunk, take);
        already += take;
        buf[already] = '\0';
    }
}

void weather_poll(void)
{
    switch (wx_state) {
    case WEATHER_ST_IDLE:
    case WEATHER_ST_DONE:
    case WEATHER_ST_FAILED:
        return;

    case WEATHER_ST_WAIT_DHCP:
        if (net_dhcp_bound())
            stage(WEATHER_ST_GEO_GET);
        else {
            net_poll();
            if (stage_expired())
                stage(WEATHER_ST_FAILED);
        }
        return;

    case WEATHER_ST_GEO_GET:
        wx_conn = net_http_get(BLACKOS_WEATHER_HOST, "/json");
        if (wx_conn == NULL) {
            /* geolocation is optional: the weather fetch still proceeds   */
            geo_done = 0;
            stage(WEATHER_ST_WX_GET);
            return;
        }
        geo_len = 0;
        geo_buf[0] = '\0';
        stage(WEATHER_ST_GEO_READ);
        break;

    case WEATHER_ST_GEO_READ:
        net_poll();
        geo_len = drain_http(wx_conn, geo_buf, sizeof(geo_buf), geo_len);
        if (geo_len == 0 && stage_expired()) {
            net_http_close(wx_conn);
            stage(WEATHER_ST_WX_GET);
            return;
        }
        /* the ip-api response is short: one read round is enough          */
        if (strstr(geo_buf, "status") != NULL || geo_len > 32) {
            net_http_close(wx_conn);
            if (net_json_field_str(geo_buf, "city", wx.city, sizeof(wx.city)))
                geo_done = 1;
            if (net_json_field_str(geo_buf, "regionName", wx.area, sizeof(wx.area)))
                geo_done = 1;
            stage(WEATHER_ST_WX_GET);
        } else if (stage_expired()) {
            net_http_close(wx_conn);
            stage(WEATHER_ST_WX_GET);
        }
        break;

    case WEATHER_ST_WX_GET:
        wx_conn = net_http_get("wttr.in", "?format=j2");
        if (wx_conn == NULL) {
            stage(WEATHER_ST_FAILED);
            return;
        }
        wx_len = 0;
        wx_buf[0] = '\0';
        stage(WEATHER_ST_WX_READ);
        break;

    case WEATHER_ST_WX_READ:
        net_poll();
        wx_len = drain_http(wx_conn, wx_buf, sizeof(wx_buf), wx_len);
        {
            const char *needle = "current_condition";
            if (strstr(wx_buf, needle) != NULL) {
                net_http_close(wx_conn);

                char tbuf[8], cbuf[48];
                int temp = 0;
                if (net_json_field_str(wx_buf, "temp_C", tbuf, sizeof(tbuf)) &&
                    dec_parse(tbuf, &temp) == 0)
                    wx.temperature = temp;
                if (net_json_field_str(wx_buf, "weatherDesc", cbuf, sizeof(cbuf)))
                    wx.condition = weather_code_of(cbuf);

                if (geo_done && geo_len > 0) {
                    net_json_field_str(geo_buf, "city", wx.city, sizeof(wx.city));
                    net_json_field_str(geo_buf, "regionName", wx.area,
                                       sizeof(wx.area));
                }
                if (wx.city[0] == '\0')
                    strncpy(wx.city, "local", sizeof(wx.city) - 1);
                if (wx.city[0] == '\0')
                    wx.city[0] = 'l';

                wx.valid = 1;
                stage(WEATHER_ST_DONE);
                printk("[wx  ] %dC code=%d city=%s\n",
                        wx.temperature, wx.condition, wx.city);
                return;
            }
        }
        if (stage_expired()) {
            net_http_close(wx_conn);
            stage(WEATHER_ST_FAILED);
        }
        break;
    }
}
