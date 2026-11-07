/* =============================================================================
 *  Black OS - app_browser.c : Browser app fallback implementation
 *
 *  This is a simple fallback browser implementation that uses the text-based
 *  approach.  The main implementation is in browser.c (NetSurf integration).
 *  This file provides a fallback for basic browsing functionality.
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
#include "fb.h"
#include "themes.h"
#include "browser.h"

#define BROWSE_BUF   4096

struct browser_state {
    char url[160];
    char resp[BROWSE_BUF + 1];
    u32  resp_len;
    int  fetched;
    int  err;
    int  url_len;
    int  cursor;
    int  scroll;
};

static struct browser_state *br_of(struct window *win)
{
    return (struct browser_state *)win->priv;
}

static const char *default_url = "http://example.com";

/* case-insensitive strstr without libc                                      */
static const char *strstr_bs(const char *haystack, const char *needle)
{
    if (!*needle)
        return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n) {
            char ca = *h, cb = *n;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb)
                break;
            h++;
            n++;
        }
        if (!*n)
            return haystack;
    }
    return (const char *)0;
}

/* strchr without libc                                                       */
static char *strchr_bs(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (char *)0;
}

/* strcpy without libc                                                       */
static void strcpy_bs(char *dst, const char *src)
{
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

/* strncmp without libc                                                      */
static int strncmp_bs(const char *a, const char *b, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}

/* case-insensitive compare without libc                                     */
static int strncasecmp_bs(const char *a, const char *b, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        if (ca == '\0')
            return 0;
    }
    return 0;
}

/* Strip HTML tags for readable text output, preserving basic structure      */
static u32 strip_tags(const char *html, u32 hlen, char *out, u32 cap)
{
    u32 o = 0;
    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;
    char tag_buf[64];
    u32 tag_len = 0;

    for (u32 i = 0; i < hlen && o + 1 < cap; i++) {
        char c = html[i];

        if (!in_tag && c == '<') {
            in_tag = 1;
            tag_len = 0;
            if (strncasecmp_bs(html + i, "<script", 7) == 0) in_script = 1;
            if (strncasecmp_bs(html + i, "<style", 6) == 0) in_style = 1;
            continue;
        }
        if (in_tag) {
            if (c == '>') {
                in_tag = 0;
                tag_buf[tag_len] = '\0';

                /* Closing tags that add structure                           */
                if (tag_len > 1 && tag_buf[0] == '/') {
                    if (strncasecmp_bs(tag_buf + 1, "p", 1) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "div", 3) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "h1", 2) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "h2", 2) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "h3", 2) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "h4", 2) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "h5", 2) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "h6", 2) == 0 ||
                        strncasecmp_bs(tag_buf + 1, "li", 2) == 0) {
                        if (o > 0 && out[o - 1] != '\n')
                            out[o++] = '\n';
                    }
                    if (strncasecmp_bs(tag_buf + 1, "script", 6) == 0) in_script = 0;
                    if (strncasecmp_bs(tag_buf + 1, "style", 5) == 0) in_style = 0;
                } else {
                    /* Opening tags                                         */
                    if (strncasecmp_bs(tag_buf, "br", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "p ", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "p>", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "div", 3) == 0) {
                        if (o > 0 && out[o - 1] != '\n')
                            out[o++] = '\n';
                    }
                    if (strncasecmp_bs(tag_buf, "h1", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "h2", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "h3", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "h4", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "h5", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "h6", 2) == 0) {
                        if (o > 0 && out[o - 1] != '\n')
                            out[o++] = '\n';
                        out[o++] = '*'; out[o++] = ' ';
                    }
                    /* Extract href from <a> tags                          */
                    if (strncasecmp_bs(tag_buf, "a ", 2) == 0 ||
                        strncasecmp_bs(tag_buf, "a>", 2) == 0) {
                        const char *href = strstr_bs(html + i, "href=");
                        if (href) {
                            href += 5;
                            if (*href == '"') href++;
                            u32 hl = 0;
                            while (*href && *href != '"' && *href != ' ' &&
                                   *href != '>' && hl < 96) {
                                tag_buf[hl++] = *href++;
                            }
                            tag_buf[hl] = '\0';
                            if (hl > 0) {
                                out[o++] = '[';
                                u32 j = 0;
                                while (tag_buf[j] && o + 1 < cap)
                                    out[o++] = tag_buf[j++];
                                out[o++] = ']';
                                out[o++] = '(';
                                j = 0;
                                while (tag_buf[j] && o + 1 < cap)
                                    out[o++] = tag_buf[j++];
                                out[o++] = ')';
                            }
                        }
                    }
                    /* Extract src from  tags                         */
                    if (strncasecmp_bs(tag_buf, "img", 3) == 0) {
                        const char *src = strstr_bs(html + i, "src=");
                        if (src) {
                            src += 4;
                            if (*src == '"') src++;
                            u32 sl = 0;
                            while (*src && *src != '"' && *src != ' ' &&
                                   *src != '>' && sl < 96) {
                                tag_buf[sl++] = *src++;
                            }
                            tag_buf[sl] = '\0';
                            if (sl > 0) {
                                out[o++] = '{'; out[o++] = 'I';
                                u32 j = 0;
                                while (tag_buf[j] && o + 1 < cap)
                                    out[o++] = tag_buf[j++];
                                out[o++] = '}';
                            }
                        }
                    }
                }
                continue;
            }
            if (tag_len < sizeof(tag_buf) - 1)
                tag_buf[tag_len++] = c;
            continue;
        }
        if (in_script || in_style)
            continue;

        if (c == '\n' || c == '\r') {
            if (o == 0 || out[o - 1] != '\n')
                out[o++] = '\n';
        } else if (c == ' ' || c == '\t') {
            if (o == 0 || out[o - 1] != ' ')
                out[o++] = ' ';
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return o;
}

static void browse_fetch(struct browser_state *s)
{
    if (!net_dhcp_bound()) {
        s->err = 1;
        strncpy(s->resp, "network not up", sizeof(s->resp) - 1);
        s->resp_len = strlen(s->resp);
        s->fetched = 1;
        return;
    }

    char host[128];
    char path[128];
    const char *u = s->url[0] ? s->url : default_url;

    if (strncmp_bs(u, "http://", 7) == 0)
        u += 7;
    else if (strncmp_bs(u, "https://", 8) == 0)
        u += 8;

    const char *slash = strchr_bs(u, '/');
    if (slash) {
        u32 hl = (u32)(slash - u);
        if (hl > 127) hl = 127;
        memcpy(host, u, hl);
        host[hl] = '\0';
        strncpy(path, slash, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        strncpy(host, u, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        strcpy_bs(path, "/");
    }

    net_tcp_h c = net_http_get(host, path);
    if (c == NULL) {
        s->err = 1;
        strncpy(s->resp, "fetch failed", sizeof(s->resp) - 1);
        s->resp_len = strlen(s->resp);
        s->fetched = 1;
        return;
    }

    u32 got = 0;
    u32 deadline = uptime_ms + 15000;
    while (got < BROWSE_BUF && uptime_ms < deadline) {
        net_poll();
        int n = net_http_read(c, s->resp + got, BROWSE_BUF - got);
        if (n > 0) {
            got += (u32)n;
        } else if (c->state == NET_TCP_CLOSED) {
            break;
        }
        for (volatile int k = 0; k < 2048; k++);
    }
    net_http_close(c);
    s->resp[got] = '\0';
    s->resp_len = got;

    char *body = strstr_bs(s->resp, "\r\n\r\n");
    if (body) {
        body += 4;
        u32 blen = (u32)strlen(body);
        char stripped[BROWSE_BUF + 1];
        u32 slen = strip_tags(body, blen, stripped, sizeof(stripped) - 1);
        memcpy(s->resp, stripped, slen);
        s->resp[slen] = '\0';
        s->resp_len = slen;
    }
    s->fetched = 1;
    s->err = 0;
}

static void browser_paint(struct window *win, u32 *buf, int w, int h)
{
    struct browser_state *s = br_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x090A0F);

    ui_text(buf, w, h, 12, 8, "Black OS Browser", g_accent, 1, 0);
    ui_text(buf, w, h, 120, 8, "real internet over e1000+HTTP", g_text_dim, 1, 0);

    ui_fill(buf, w, h, 12, 30, w - 24, 20, 0x0E182A);
    ui_fill(buf, w, h, 12, 30, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 49, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 30, 1, 20, g_panel_border);
    ui_fill(buf, w, h, w - 13, 30, 1, 20, g_panel_border);
    ui_text(buf, w, h, 16, 36, s->url, g_text_bright, 1, 0);
    if ((uptime_ms / 300) & 1)
        fb_pixel(16 + s->cursor * 8, 38, g_accent);

    ui_fill(buf, w, h, w - 96, 58, 84, 22, g_btn_hover);
    ui_fill(buf, w, h, w - 96, 58, 84, 1, g_panel_border);
    ui_fill(buf, w, h, w - 96, 79, 84, 1, g_panel_border);
    ui_fill(buf, w, h, w - 96, 58, 1, 22, g_panel_border);
    ui_fill(buf, w, h, w - 13, 58, 1, 22, g_panel_border);
    ui_text(buf, w, h, w - 76, 64, "go", g_text_bright, 1, 0);

    int vx = 12, vy = 90, vw = w - 24, vh = h - 130;
    ui_fill(buf, w, h, vx, vy, vw, vh, 0x05050A);
    ui_fill(buf, w, h, vx, vy, vw, 1, g_panel_border);
    ui_fill(buf, w, h, vx, vy + vh - 1, vw, 1, g_panel_border);
    ui_fill(buf, w, h, vx, vy, 1, vh, g_panel_border);
    ui_fill(buf, w, h, vx + vw - 1, vy, 1, vh, g_panel_border);

    int ty = vy + 8;
    int lines = (vh - 16) / 10;
    int start = s->scroll;
    const char *p = s->resp;
    int line_idx = 0;

    while (*p && line_idx < start) {
        if (*p == '\n') line_idx++;
        p++;
    }
    int shown = 0;
    while (*p && shown < lines && ty + 8 < vy + vh) {
        int linelen = 0;
        while (p[linelen] && p[linelen] != '\n')
            linelen++;
        if (linelen > vw / 8 - 2)
            linelen = vw / 8 - 2;
        char line[120];
        memcpy(line, p, linelen);
        line[linelen] = '\0';
        ui_text(buf, w, h, vx + 8, ty, line, g_text_bright, 1, 0);
        ty += 10;
        shown++;
        if (p[linelen] == '\n') p++;
        line_idx++;
    }

    if (!s->fetched) {
        ui_text(buf, w, h, vx + 8, vy + 8, "type a URL and press GO", g_text_dim, 1, 0);
    } else if (s->err) {
        ui_text(buf, w, h, vx + 8, vy + 8, "error loading page", 0xD42222, 1, 0);
    }

    ui_text(buf, w, h, 12, h - 20,
            "arrows: scroll   enter: go   ESC: back", g_text_dim, 1, 0);
}

static void browser_key(struct window *win, char c)
{
    struct browser_state *s = br_of(win);
    if (s == NULL) return;

    if (c == '\n') {
        browse_fetch(s);
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
        s->url[s->url_len++] = c;
        s->url[s->url_len] = '\0';
        s->cursor = s->url_len;
        return;
    }
    if (c == KBD_UP) {
        if (s->scroll > 0) s->scroll--;
        return;
    }
    if (c == KBD_DOWN) {
        s->scroll++;
        return;
    }
}

static void browser_open(struct window *win)
{
    win->priv = (struct browser_state *)(usize)pmm_alloc_run(1);
    struct browser_state *s = br_of(win);
    memset(s, 0, sizeof(*s));
    s->url_len = (int)strlen(default_url);
    memcpy(s->url, default_url, s->url_len);
    s->cursor = s->url_len;
}

/* Fallback browser implementation - now integrated with NetSurf */
static void browser_fallback_open(struct window *win)
{
    /* This is the old text-based browser implementation */
    win->priv = (struct browser_state *)(usize)pmm_alloc_run(1);
    struct browser_state *s = br_of(win);
    memset(s, 0, sizeof(*s));
    s->url_len = (int)strlen(default_url);
    memcpy(s->url, default_url, s->url_len);
    s->cursor = s->url_len;
}

static void browser_fallback_paint(struct window *win, u32 *buf, int w, int h)
{
    /* Fallback implementation */
    struct browser_state *s = br_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x090A0F);
    ui_text(buf, w, h, 12, 8, "Black OS Browser (Fallback)", g_accent, 1, 0);
    ui_text(buf, w, h, 12, 36, s->url, g_text_bright, 1, 0);
}

static void browser_fallback_key(struct window *win, char c)
{
    struct browser_state *s = br_of(win);
    if (s == NULL) return;
    if (c == '\n') {
        browse_fetch(s);
        return;
    }
    if (c >= 0x20 && s->url_len < (int)sizeof(s->url) - 1) {
        s->url[s->url_len++] = c;
        s->url[s->url_len] = '\0';
        s->cursor = s->url_len;
    }
}

static const struct app app_browser_fallback = {
    .name  = "browser-fallback",
    .open  = browser_fallback_open,
    .paint = browser_fallback_paint,
    .key   = browser_fallback_key,
    .close = NULL,
};
