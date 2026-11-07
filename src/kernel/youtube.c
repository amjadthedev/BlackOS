/* =============================================================================
 *  Black OS - youtube.c : YouTube client over the real network stack
 *
 *  Fetch any youtube.com/watch?v=... URL with net_http_get, extract the HTML
 *  <title> tag, and render a video placeholder (color bars + title).  This
 *  proves end-to-end DHCP→DNS→TCP→HTTP on the e1000 NIC inside the OS.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "netstack.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"
#include "idt.h"
#include "themes.h"

#define YT_BUF 1024

struct yt_state {
    char url[160];
    char title[128];
    int  fetched;
    int  fetch_err;
    int  url_len;
    int  cursor;
};

static struct yt_state *yt_of(struct window *win)
{
    return (struct yt_state *)win->priv;
}

static const char *default_url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";

static void fetch_title(struct yt_state *s)
{
    if (!net_dhcp_bound()) {
        s->fetch_err = 1;
        strncpy(s->title, "network not up", sizeof(s->title) - 1);
        s->fetched = 1;
        return;
    }

    net_tcp_h c = net_http_get("www.youtube.com", s->url[0] ? s->url : default_url);
    if (c == NULL) {
        s->fetch_err = 1;
        strncpy(s->title, "fetch failed", sizeof(s->title) - 1);
        s->fetched = 1;
        return;
    }

    static u8 buf[YT_BUF + 1];
    u32 got = 0;
    u32 deadline = uptime_ms + 12000;
    while (got < YT_BUF && uptime_ms < deadline) {
        net_poll();
        int n = net_http_read(c, buf + got, YT_BUF - got);
        if (n > 0) {
            got += (u32)n;
        } else if (c->state == NET_TCP_CLOSED) {
            break;
        }
        for (volatile int k = 0; k < 2048; k++)
            ;
    }
    net_http_close(c);
    buf[got] = '\0';

    /* crude <title> extraction                                             */
    const char *tag = strstr((const char *)buf, "<title>");
    if (tag) {
        tag += 7;
        const char *end = strstr(tag, "</title>");
        u32 len = end ? (u32)(end - tag) : 0;
        if (len > 127) len = 127;
        memcpy(s->title, tag, len);
        s->title[len] = '\0';
        /* trim leading/trailing whitespace */
        u32 a = 0;
        while (s->title[a] == ' ' || s->title[a] == '\n' || s->title[a] == '\r' || s->title[a] == '\t')
            a++;
        memmove(s->title, s->title + a, len - a + 1);
        len -= a;
        while (len > 0 && (s->title[len - 1] == ' ' || s->title[len - 1] == '\n' ||
                           s->title[len - 1] == '\r' || s->title[len - 1] == '\t'))
            s->title[--len] = '\0';
        s->fetch_err = 0;
    } else {
        strncpy(s->title, "video page loaded (no title found)", sizeof(s->title) - 1);
        s->fetch_err = 1;
    }
    s->fetched = 1;
}

static void yt_paint(struct window *win, u32 *buf, int w, int h)
{
    struct yt_state *s = yt_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x090A0F);

    /* header */
    ui_text(buf, w, h, 12, 8, "YouTube", 0xFF0000, 1, 0);
    ui_text(buf, w, h, 64, 8, "BLACK OS client", g_text_dim, 1, 0);

    /* URL bar */
    ui_fill(buf, w, h, 12, 30, w - 24, 20, 0x0E182A);
    ui_fill(buf, w, h, 12, 30, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 49, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 30, 1, 20, g_panel_border);
    ui_fill(buf, w, h, w - 13, 30, 1, 20, g_panel_border);
    ui_text(buf, w, h, 16, 36, s->url, g_text_bright, 1, 0);
    if ((uptime_ms / 300) & 1)
        fb_pixel(16 + s->cursor * 8, 38, g_accent);

    /* fetch button */
    ui_fill(buf, w, h, w - 96, 58, 84, 22, g_btn_hover);
    ui_fill(buf, w, h, w - 96, 58, 84, 1, g_panel_border);
    ui_fill(buf, w, h, w - 96, 79, 84, 1, g_panel_border);
    ui_fill(buf, w, h, w - 96, 58, 1, 22, g_panel_border);
    ui_fill(buf, w, h, w - 13, 58, 1, 22, g_panel_border);
    ui_text(buf, w, h, w - 76, 64, "fetch", g_text_bright, 1, 0);

    /* video placeholder */
    int vx = 12, vy = 90, vw = w - 24, vh = h - 130;
    ui_fill(buf, w, h, vx, vy, vw, vh, 0x05050A);
    ui_fill(buf, w, h, vx, vy, vw, 1, g_panel_border);
    ui_fill(buf, w, h, vx, vy + vh - 1, vw, 1, g_panel_border);
    ui_fill(buf, w, h, vx, vy, 1, vh, g_panel_border);
    ui_fill(buf, w, h, vx + vw - 1, vy, 1, vh, g_panel_border);

    /* color bars */
    static const u32 bars[6] = { 0xFF0000, 0x00FF00, 0x0000FF,
                                 0xFFFF00, 0xFF00FF, 0x00FFFF };
    int bw = vw / 6;
    for (int i = 0; i < 6; i++)
        ui_fill(buf, w, h, vx + i * bw, vy + 10, bw, 40, bars[i]);

    /* title */
    if (s->fetched) {
        u32 col = s->fetch_err ? 0xD42222 : g_text_bright;
        ui_text(buf, w, h, vx, vy + 60, s->title, col, 1, 0);
    } else {
        ui_text(buf, w, h, vx, vy + 60, "press fetch to load video", g_text_dim, 1, 0);
    }

    ui_text(buf, w, h, 12, h - 20, "real internet: e1000 + DHCP + DNS + TCP + HTTP/1.1",
            g_text_dim, 1, 0);
}

static void yt_key(struct window *win, char c)
{
    struct yt_state *s = yt_of(win);
    if (s == NULL) return;

    if (c == '\n') {
        if (s->url_len > 0)
            fetch_title(s);
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (s->url_len > 0) {
            s->url_len--;
            s->url[s->url_len] = '\0';
            s->cursor = s->url_len;
        }
        return;
    }
    if (c >= 0x20 && s->url_len < (int)sizeof(s->url) - 1) {
        /* insert at cursor (append for simplicity) */
        s->url[s->url_len++] = c;
        s->url[s->url_len] = '\0';
        s->cursor = s->url_len;
        return;
    }
}

static void yt_open(struct window *win)
{
    win->priv = (struct yt_state *)(usize)pmm_alloc_run(1);
    struct yt_state *s = yt_of(win);
    memset(s, 0, sizeof(*s));
    s->url_len = (int)strlen(default_url);
    memcpy(s->url, default_url, s->url_len);
    s->cursor = s->url_len;
}

static const struct app app_youtube = {
    .name  = "youtube",
    .open  = yt_open,
    .paint = yt_paint,
    .key   = yt_key,
    .close = NULL,
};
