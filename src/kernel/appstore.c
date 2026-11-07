/* =============================================================================
 *  Black OS - appstore.c : App Store + package manager
 *
 *  Shows a scrollable catalog of apps (name / category / size).  "Install"
 *  downloads a .pkg over HTTP into VFS (/apps/*.pkg).  Installed packages
 *  are read back from VFS and launched through the syscall ABI (ring 3).
 *  Includes a search bar to filter the catalog.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "netstack.h"
#include "package.h"
#include "vfs.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"
#include "idt.h"
#include "themes.h"
#include "fb.h"

#define STORE_MAX 12

struct store_item {
    char name[64];
    char category[32];
    char desc[128];
    u32  size;
    int  installed;
    char url[160];
};

struct store_state {
    struct store_item items[STORE_MAX];
    int count;
    int sel;
    int scroll;
    int view_rows;
    char search[64];
    int search_len;
    char msg[80];
    u32 msg_ms;
};

static struct store_state *st_of(struct window *win)
{
    return (struct store_state *)win->priv;
}

static void store_msg(struct store_state *s, const char *m)
{
    strncpy(s->msg, m, sizeof(s->msg) - 1);
    s->msg[sizeof(s->msg) - 1] = '\0';
    s->msg_ms = uptime_ms;
}

static int item_matches_search(const struct store_item *it, const char *search, int len)
{
    if (len == 0)
        return 1;
    /* simple substring search on name and category                       */
    for (const char *p = it->name; *p; p++) {
        int match = 1;
        for (int i = 0; i < len; i++) {
            if (!p[i] || p[i] != search[i]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 1;
    }
    for (const char *p = it->category; *p; p++) {
        int match = 1;
        for (int i = 0; i < len; i++) {
            if (!p[i] || p[i] != search[i]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 1;
    }
    return 0;
}

static int filtered_index(const struct store_state *s, int display_idx)
{
    /* Map display index to actual item index considering search filter     */
    int found = 0;
    for (int i = 0; i < s->count; i++) {
        if (item_matches_search(&s->items[i], s->search, s->search_len)) {
            if (found == display_idx)
                return i;
            found++;
        }
    }
    return -1;
}

static int filtered_count(const struct store_state *s)
{
    int n = 0;
    for (int i = 0; i < s->count; i++)
        if (item_matches_search(&s->items[i], s->search, s->search_len))
            n++;
    return n;
}

static void store_seed_catalog(struct store_state *s)
{
    s->count = 0;
    struct store_item *add = NULL;
#define ADD(name, cat, desc, sz, url)                                         \
    do {                                                                      \
        add = &s->items[s->count++];                                          \
        memset(add, 0, sizeof(*add));                                         \
        strncpy(add->name, name, sizeof(add->name) - 1);                      \
        strncpy(add->category, cat, sizeof(add->category) - 1);               \
        strncpy(add->desc, desc, sizeof(add->desc) - 1);                      \
        add->size = sz;                                                       \
        strncpy(add->url, url, sizeof(add->url) - 1);                         \
    } while (0)

    ADD("Snake", "Games", "Classic snake game", 4096, "http://10.0.2.2/pkg/snake.pkg");
    ADD("Pong", "Games", "Two-player pong", 4096, "http://10.0.2.2/pkg/pong.pkg");
    ADD("Notes", "Productivity", "Simple text notes", 2048, "http://10.0.2.2/pkg/notes.pkg");
    ADD("Calc", "Productivity", "Calculator", 2048, "http://10.0.2.2/pkg/calc.pkg");
    ADD("Weather", "Utilities", "Live weather widget", 1024, "http://10.0.2.2/pkg/weather.pkg");
    ADD("Music", "Media", "Audio player stub", 8192, "http://10.0.2.2/pkg/music.pkg");
#undef ADD

    /* mark locally built-in apps as installed                               */
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->items[i].name, "Snake") == 0 ||
            strcmp(s->items[i].name, "Pong") == 0)
            s->items[i].installed = 1;
    }
}

static void store_install(struct store_state *s, int idx)
{
    if (idx < 0 || idx >= s->count)
        return;
    struct store_item *it = &s->items[idx];
    if (it->installed) {
        store_msg(s, "already installed");
        return;
    }
    if (!net_dhcp_bound()) {
        store_msg(s, "network not up");
        return;
    }

    net_tcp_h c = net_http_get("10.0.2.2", it->url);
    if (c == NULL) {
        store_msg(s, "download failed");
        return;
    }
    static u8 pkg_buf[32 * 1024];
    u32 got = 0;
    u32 deadline = uptime_ms + 15000;
    while (got < sizeof(pkg_buf) - 1 && uptime_ms < deadline) {
        net_poll();
        int n = net_http_read(c, pkg_buf + got, sizeof(pkg_buf) - 1 - got);
        if (n > 0) got += (u32)n;
        else if (c->state == NET_TCP_CLOSED) break;
        for (volatile int k = 0; k < 2048; k++);
    }
    net_http_close(c);
    pkg_buf[got] = '\0';

    char dest[128];
    u32 dn = 0;
    const char *prefix = "/apps/";
    while (*prefix && dn < sizeof(dest) - 1)
        dest[dn++] = *prefix++;
    const char *np = it->name;
    while (*np && dn < sizeof(dest) - 5)
        dest[dn++] = *np++;
    dest[dn++] = '.'; dest[dn++] = 'p'; dest[dn++] = 'k'; dest[dn++] = 'g';
    dest[dn] = '\0';

    if (pkg_install(dest, pkg_buf, got) == 0) {
        it->installed = 1;
        store_msg(s, "installed");
    } else {
        store_msg(s, "install failed");
    }
}

static void store_launch_installed(struct store_state *s, int idx)
{
    if (idx < 0 || idx >= s->count)
        return;
    struct store_item *it = &s->items[idx];
    if (!it->installed)
        return;

    char dest[128];
    u32 dn = 0;
    const char *prefix = "/apps/";
    while (*prefix && dn < sizeof(dest) - 1)
        dest[dn++] = *prefix++;
    const char *np = it->name;
    while (*np && dn < sizeof(dest) - 5)
        dest[dn++] = *np++;
    dest[dn++] = '.'; dest[dn++] = 'p'; dest[dn++] = 'k'; dest[dn++] = 'g';
    dest[dn] = '\0';

    vfs_handle h = vfs_lookup(dest);
    if (h < 0) {
        store_msg(s, "pkg not found on disk");
        return;
    }
    u64 fsize = vfs_size(h);
    if (fsize == 0 || fsize > 64 * 1024) {
        store_msg(s, "pkg too large");
        return;
    }
    u8 *blob = (u8 *)(usize)pmm_alloc_run((fsize + 4095) / 4096);
    if (blob == NULL) {
        store_msg(s, "out of memory");
        return;
    }
    u64 got = vfs_read(h, blob, fsize, 0);

    struct pkg_package pkg;
    if (pkg_parse(blob, got, &pkg) != 0) {
        store_msg(s, "invalid pkg format");
        pmm_free_run((u64)(usize)blob, (fsize + 4095) / 4096);
        return;
    }

    u64 rc = pkg_launch(&pkg);
    pmm_free_run((u64)(usize)blob, (fsize + 4095) / 4096);

    char msgbuf[48];
    u32 n = 0;
    msgbuf[n++] = 'e'; msgbuf[n++] = 'x'; msgbuf[n++] = 'i'; msgbuf[n++] = 't';
    msgbuf[n++] = ':'; msgbuf[n++] = ' ';
    u32 v = (u32)rc;
    if (v == 0) msgbuf[n++] = '0';
    while (v > 0) { msgbuf[n++] = (char)('0' + v % 10); v /= 10; }
    for (u32 i = 0; i < n - 6; i++) {}
    /* reverse digits properly                                              */
    u32 start = 6;
    u32 end = n - 1;
    while (start < end) {
        char t = msgbuf[start];
        msgbuf[start] = msgbuf[end];
        msgbuf[end] = t;
        start++; end--;
    }
    msgbuf[n] = '\0';
    store_msg(s, msgbuf);
}

static void store_paint(struct window *win, u32 *buf, int w, int h)
{
    struct store_state *s = st_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);

    ui_text(buf, w, h, 12, 8, "BLACK OS App Store", g_accent, 1, 0);
    ui_text(buf, w, h, 12, 30, "Catalog", g_text_dim, 1, 0);

    /* Search bar                                                          */
    ui_fill(buf, w, h, 12, 46, w - 24, 20, 0x0E182A);
    ui_fill(buf, w, h, 12, 46, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 65, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 46, 1, 20, g_panel_border);
    ui_fill(buf, w, h, w - 13, 46, 1, 20, g_panel_border);
    ui_text(buf, w, h, 16, 52, "search:", g_text_dim, 1, 0);
    ui_text(buf, w, h, 50, 52, s->search, g_text_bright, 1, 0);
    if ((uptime_ms / 300) & 1)
        fb_pixel(50 + s->search_len * 8, 54, g_accent);

    int total = filtered_count(s);
    int visible_rows = (h - 90) / 34;
    if (visible_rows < 1) visible_rows = 1;
    s->view_rows = visible_rows;

    int y = 74;
    int shown = 0;
    for (int i = s->scroll; i < total && shown < visible_rows; i++, shown++) {
        int real_idx = filtered_index(s, i);
        if (real_idx < 0)
            break;
        struct store_item *it = &s->items[real_idx];
        int sel = (i == s->sel);
        int yy = y + shown * 34;
        if (yy + 30 > h) break;

        ui_fill(buf, w, h, 12, yy, w - 24, 28,
                sel ? g_btn_hover : 0x0F1B2E);
        ui_text(buf, w, h, 18, yy + 8, it->name, sel ? g_text_bright : g_text_bright, 1, 0);
        ui_text(buf, w, h, 180, yy + 8, it->category, g_text_dim, 1, 0);
        char sz[16];
        u32 nn = 0, vv = it->size;
        if (vv == 0) sz[nn++] = '0';
        while (vv > 0) { sz[nn++] = (char)('0' + vv % 10); vv /= 10; }
        for (u32 i2 = 0; i2 < nn / 2; i2++) { char t = sz[i2]; sz[i2] = sz[nn - 1 - i2]; sz[nn - 1 - i2] = t; }
        sz[nn] = '\0';
        ui_text(buf, w, h, 260, yy + 8, sz, g_text_dim, 1, 0);

        int bx = w - 110;
        ui_fill(buf, w, h, bx, yy + 4, 96, 20,
                it->installed ? 0x2A5A3A : g_btn_hover);
        ui_text(buf, w, h, bx + 12, yy + 10,
                it->installed ? "launch" : "install",
                g_text_bright, 1, 0);
    }

    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 24, s->msg, g_accent, 1, 0);
    ui_text(buf, w, h, 12, h - 10, "arrows: navigate   enter: install/launch   type: search", g_text_dim, 1, 0);
}

static void store_key(struct window *win, char c)
{
    struct store_state *s = st_of(win);
    if (s == NULL) return;

    int total = filtered_count(s);

    switch (c) {
    case KBD_UP:
        if (s->sel > 0) {
            s->sel--;
            if (s->sel < s->scroll) s->scroll = s->sel;
        }
        return;
    case KBD_DOWN:
        if (s->sel < total - 1) {
            s->sel++;
            if (s->sel >= s->scroll + s->view_rows)
                s->scroll = s->sel - s->view_rows + 1;
        }
        return;
    case '\n': {
        int idx = filtered_index(s, s->sel);
        if (idx < 0)
            return;
        if (s->items[idx].installed)
            store_launch_installed(s, idx);
        else
            store_install(s, idx);
        return;
    }
    case 0x08: case 0x7F:
        if (s->search_len > 0) {
            s->search_len--;
            s->search[s->search_len] = '\0';
            s->sel = 0;
            s->scroll = 0;
        }
        return;
    default:
        if (c >= 0x20 && s->search_len < (int)sizeof(s->search) - 1) {
            s->search[s->search_len++] = c;
            s->search[s->search_len] = '\0';
            s->sel = 0;
            s->scroll = 0;
        }
        return;
    }
}

static void store_open(struct window *win)
{
    win->priv = (struct store_state *)(usize)pmm_alloc_run(1);
    struct store_state *s = st_of(win);
    memset(s, 0, sizeof(*s));
    store_seed_catalog(s);
    s->sel = 0;
    s->scroll = 0;
}

static const struct app app_store = {
    .name  = "app store",
    .open  = store_open,
    .paint = store_paint,
    .key   = store_key,
    .close = NULL,
};
