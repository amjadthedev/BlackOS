/* =============================================================================
 *  Black OS - Windows 11-inspired shell (centered taskbar, start menu,
 *             quick-settings flyout, tray with live weather + RTC clock)
 * ========================================================================== */
#include "shell.h"
#include "fb.h"
#include "ui.h"
#include "font.h"
#include "win.h"
#include "app.h"
#include "rtc.h"
#include "weather.h"
#include "netstack.h"
#include "string.h"
#include "printk.h"
#include "io.h"
#include "keyboard.h"
#include "themes.h"
#include "idt.h"
#include "acpi.h"

#define SHELL_TB_H          36          /* taskbar height, px                */
#define SHELL_TB_ICONS      6           /* pinned app buttons                */
#define SHELL_BTN_W         34
#define SHELL_START_W       44
#define SHELL_MENU_W        380
#define SHELL_MENU_H        330
#define SHELL_QS_W          250
#define SHELL_QS_H          190
#define SHELL_SEARCH_MAX    20

static int menu_open;
static int menu_search_focus;
static int qs_open;
static char menu_search[SHELL_SEARCH_MAX];
static int fw_inbound_allow;
static u8  snd_quiet;

/* key hit rectangles (taskbar coords, refreshed each draw)                 */
static struct shell_rect {
    int start_x, start_y;
    int pinned_x;
    int qs_x;
} r;

static void text_px(const char *s, int x, int y, u32 fg)
{
    for (int k = 0; s[k]; k++) {
        const u8 *g = font_glyph(s[k]);
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    fb_pixel(x + k * 8 + col, y + row, fg);
        }
    }
}

static int hit(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void shell_init(void)
{
    r.start_x = r.start_y = r.pinned_x = r.qs_x = 0;
    menu_open = 0;
    menu_search_focus = 0;
    qs_open = 0;
    menu_search[0] = '\0';
    fw_inbound_allow = 0;
    snd_quiet = 0;
}

/* helper: render net.gw as dotted quad                                     */
static int sprintf_gw(char *out, u32 cap)
{
    u32 n = 0;
    for (int i = 0; i < 4 && n + 4 < cap; i++) {
        u8 v = net.gw[i];
        if (v >= 100) out[n++] = (char)('0' + v / 100);
        if (v >= 10)  out[n++] = (char)('0' + (v / 10) % 10);
        out[n++] = (char)('0' + v % 10);
        if (i < 3)
            out[n++] = '.';
    }
    out[n] = '\0';
    return (int)n;
}

/*────────────────────────────── taskbar paint ────────────────────────────*/
static void draw_taskbar(void)
{
    int ty = (int)fb_height - SHELL_TB_H;
    int tw = (int)fb_width;

    /* frosted glass: blur the strip first, then a translucent veil        */
    fb_blur_region(0, ty, tw, SHELL_TB_H, 4);
    for (int y2 = ty; y2 < ty + SHELL_TB_H; y2++) {
        u32 *row = fb_base + (u32)y2 * (fb_bytes / 4);
        u8 alpha = (u8)(y2 == ty ? 0x20 : 0x55);
        for (int x2 = 0; x2 < tw; x2++)
            row[x2] = fb_mix(row[x2], 0x060A12, alpha);
    }
    fb_fill_rect(0, ty, tw, 1, UI_BEVEL_LIT);

    int y_btn = ty + (SHELL_TB_H - 26) / 2;

    /* centered app row: [start][pinned...]                                */
    int total = SHELL_START_W + SHELL_BTN_W * SHELL_TB_ICONS;
    int bx = (tw - total) / 2;

    r.start_x = bx;
    r.start_y = y_btn;

    /* start button: 4-pane flag glyph                                     */
    if (menu_open)
        fb_rect_rounded(bx, y_btn, SHELL_START_W, 26, 6, 0x1D2F4E);
    else
        fb_rect_rounded(bx, y_btn, SHELL_START_W, 26, 6, 0x141E2E);
    int ox = bx + (SHELL_START_W - 16) / 2;
    int oy =
        y_btn + 5;
    fb_fill_rect(ox, oy, 7, 7, UI_ACCENT_CYAN);
    fb_fill_rect(ox + 9, oy, 7, 7, UI_ACCENT_CYAN);
    fb_fill_rect(ox, oy + 9, 7, 7, UI_ACCENT_CYAN);
    fb_fill_rect(ox + 9, oy + 9, 7, 7, UI_ACCENT_CYAN);
    bx += SHELL_START_W;

    r.pinned_x = bx;
    for (int i = 0; i < SHELL_TB_ICONS && i < app_count; i++) {
        char glyph[2];
        char c0 = apps[i]->name[0];
        glyph[0] = (c0 >= 'a' && c0 <= 'z') ? (char)(c0 - 32) : c0;
        glyph[1] = '\0';

        struct window *live = NULL;
        for (int z = 1; z <= win_top_z(); z++) {
            struct window *w = win_by_z(z);
            if (w != NULL && w->app == apps[i]) {
                live = w;
                break;
            }
        }
        if (live != NULL)
            fb_rect_rounded(bx, y_btn, SHELL_BTN_W - 4, 26, 6, 0x1A2A44);
        text_px(glyph, bx + 12, y_btn + 9, UI_TEXT_BRIGHT);
        if (live != NULL && !live->minimized &&
            live == win_focus_top())
            fb_fill_rect(bx + 8, ty + SHELL_TB_H - 2, 14, 2, UI_ACCENT_CYAN);
        bx += SHELL_BTN_W;
    }

    /* ── tray, right-aligned: weather chip | qs | clock ──                 */
    struct rtc_time t;
    char clk[8];
    if (rtc_get(&t) == 0) {
        clk[0] = (char)('0' + t.hour / 10);
        clk[1] = (char)('0' + t.hour % 10);
        clk[2] = ':';
        clk[3] = (char)('0' + t.minute / 10);
        clk[4] = (char)('0' + t.minute % 10);
    } else {
        clk[0] = '-'; clk[1] = '-'; clk[2] = ':';
        clk[3] = '-'; clk[4] = '-';
    }
    clk[5] = '\0';

    int clock_x = tw - 8 - 5 * 8 - 6;      /* clock block                     */
    int qs_x    = clock_x - 30;            /* quick settings glyph            */
    int wx_x    = qs_x - 8;                /* weather chip, right-aligned     */

    text_px(clk, clock_x, ty + (SHELL_TB_H - 8) / 2 + 1, UI_TEXT_BRIGHT);

    r.qs_x = qs_x;
    fb_rect_rounded(qs_x, y_btn, 24, 26, 6, 0x121B2A);
    fb_fill_rect(qs_x + 9, y_btn + 5, 6, 4, UI_TEXT_BRIGHT);
    fb_fill_rect(qs_x + 9, y_btn + 11, 6, 4, UI_TEXT_DIM);
    fb_fill_rect(qs_x + 9, y_btn + 17, 6, 4, UI_TEXT_DIM);

    char wxs[48];
    weather_status(wxs, sizeof(wxs));
    u32 wl = strlen(wxs);
    if (wl * 8 > 190)
        wl = 23;                            /* clamp the chip width           */
    text_px(wxs, wx_x - (int)wl * 8, ty + (SHELL_TB_H - 8) / 2 + 1,
            UI_TEXT_DIM);
}

/*────────────────────────────── start menu ───────────────────────────────*/
static int pw_here(int mw);

static void draw_start_menu(void)
{
    int mw = SHELL_MENU_W;
    int mh = SHELL_MENU_H;
    int x = ((int)fb_width - mw) / 2;
    int y = (int)fb_height - SHELL_TB_H - mh - 10;

    fb_blur_region(x, y, mw, mh, 5);
    fb_rect_rounded(x, y, mw, mh, 12, 0x0B1322);
    /* re-blur keeps the panel acrylic: mix veil over the blurred strip    */
    for (int yy = 0; yy < mh; yy++) {
        u32 *row = fb_base + (u32)(y + yy) * (fb_bytes / 4);
        for (int xx = 0; xx < mw; xx++)
            row[x + xx] = fb_mix(row[x + xx], 0x0A1020, 0x40);
    }
    fb_rounded_frame(x, y, mw, mh, 12, 0x2A4468);

    /* header                                                              */
    text_px("BLACK OS", x + 20, y + 14, UI_ACCENT_CYAN);
    text_px("start", x + 96, y + 15, UI_TEXT_MUTED);

    /* search box                                                          */
    int sx = x + 16, sy = y + 34, sw = mw - 32;
    fb_rect_rounded(sx, sy, sw, 22, 5, 0x0E182A);
    if (menu_search_focus)
        fb_rounded_frame(sx, sy, sw, 22, 5, 0x2E6FB0);
    else
        fb_rounded_frame(sx, sy, sw, 22, 5, 0x1C2B44);
    text_px("search:", sx + 8, sy + 7, UI_TEXT_DIM);
    text_px(menu_search, sx + 64, sy + 7, UI_TEXT_BRIGHT);
    if (menu_search_focus && ((uptime_ms / 400) & 1)) {
        int cl = (int)strlen(menu_search);
        text_px("|", sx + 64 + cl * 8, sy + 7, UI_ACCENT_CYAN);
    }

    /* pinned grid, filtered by search                                    */
    int gx = x + 16;
    int gy = sy + 34;
    const int cols = 4;
    const int cw = (mw - 32) / cols;

    int shown = 0;
    for (int i = 0; i < app_count && shown < 16; i++) {
        const struct app *p = apps[i];
        if (menu_search[0] != '\0' && strstr(p->name, menu_search) == NULL)
            continue;
        int cx = gx + (shown % cols) * cw;
        int cy = gy + (shown / cols) * 44;
        fb_rect_rounded(cx + 4, cy, cw - 12, 38, 8, 0x0F1B2E);
        text_px(p->name, cx + 14, cy + 15, UI_TEXT_DIM);
        shown++;
    }

    /* power row                                                           */
    fb_rect_rounded(x + 16, y + mh - 34, pw_here(mw), 22, 6, 0x131F33);
    text_px("power", x + 28, y + mh - 27, UI_ACCENT_RED);
    fb_rect_rounded(x + 16 + 72, y + mh - 34, pw_here(mw) - 72, 22, 6, 0x131F33);
    text_px("reboot", x + 16 + 82, y + mh - 27, UI_TEXT_DIM);
}

static int pw_here(int mw)
{
    return mw - 32 > 0 ? mw - 32 : 1;
}

/*──────────────────────────── quick settings ─────────────────────────────*/
static void draw_quick_settings(void)
{
    int w = SHELL_QS_W;
    int h = SHELL_QS_H;
    int x = (int)fb_width - w - 8;
    int y = (int)fb_height - SHELL_TB_H - h - 8;

    fb_blur_region(x, y, w, h, 5);
    fb_rect_rounded(x, y, w, h, 10, 0x0B1424);
    fb_rounded_frame(x, y, w, h, 10, 0x2A4468);

    int ty = y + 14;
    int net_up = net_dhcp_bound();

    const char *t1 = net_up ? "NET   up" : "NET   down";
    text_px(t1, x + 24, ty + 10, net_up ? UI_ACCENT_GREEN : UI_TEXT_DIM);

    char t2[48];
    u32 n = 0;
    const char *pref = "GW    ";
    while (*pref)
        t2[n++] = *pref++;
    if (net_up)
        sprintf_gw(t2 + n, sizeof(t2) - n);
    else
        t2[n] = '\0';
    text_px(t2, x + 24, ty + 64, net_up ? UI_TEXT_BRIGHT : UI_TEXT_DIM);

    const char *t3 = fw_inbound_allow ? "FW    allow in" : "FW    deny in";
    text_px(t3, x + 24, ty + 118,
            fw_inbound_allow ? UI_ACCENT_CYAN : UI_TEXT_DIM);

    const char *t4 = snd_quiet ? "SND   muted" : "SND   on";
    text_px(t4, x + 24, ty + 162, UI_TEXT_DIM);
}

/*────────────────────────────── app launch ───────────────────────────────*/
void shell_launch_app(const struct app *a)
{
    struct window *w = win_open(520, 300, a->name, a);
    if (w == NULL) {
        printk("[gui ] out of RAM for app surface\n");
        return;
    }
    if (a->open)
        a->open(w);
    win_raise(w);
}

static void shell_power_off(void)
{
    acpi_poweroff();
}

/*──────────────────────────── keyboard intake ────────────────────────────*/
int shell_key(char c)
{
    if (!menu_open)
        return 0;

    if (c == KBD_ESCAPE) {
        menu_open = 0;
        menu_search[0] = '\0';
        return 1;
    }
    if (c == KBD_ENTER) {
        /* enter with no filter launches the first matching app            */
        for (int i = 0; i < app_count; i++) {
            if (menu_search[0] == '\0' ||
                strstr(apps[i]->name, menu_search) != NULL) {
                shell_launch_app(apps[i]);
                menu_open = 0;
                menu_search[0] = '\0';
                return 1;
            }
        }
        menu_open = 0;
        return 1;
    }
    if (c == 8 || c == 0x7F) {
        u32 l = strlen(menu_search);
        if (l > 0)
            menu_search[l - 1] = '\0';
        return 1;
    }
    if (c < 0x20)
        return 0;

    u32 l = strlen(menu_search);
    if (l < SHELL_SEARCH_MAX - 1) {
        menu_search[l] = c;
        menu_search[l + 1] = '\0';
    }
    return 1;
}

static void qs_toggle_firewall(void)
{
    fw_inbound_allow = !fw_inbound_allow;
    u8 any[4] = { 0, 0, 0, 0 };
    if (fw_inbound_allow)
        net_firewall_add(FW_DIR_IN, FW_ALLOW, 0, any, 0, 0);
    else
        net_firewall_add(FW_DIR_IN, FW_DENY, 0, any, 0, 0);
}

/*──────────────────────────── mouse intake ───────────────────────────────*/
int shell_input(struct shell_input *ev)
{
    if (ev == NULL)
        return 0;

    if (!ev->click)
        return (menu_open || qs_open);  /* idle motion never consumes        */

    /* click-away dismisses floating panels                                */
    if (menu_open) {
        int mx = SHELL_MENU_W, mh = SHELL_MENU_H;
        int x = ((int)fb_width - mx) / 2;
        int y = (int)fb_height - SHELL_TB_H - mh - 10;
        if (!hit(ev->mx, ev->my, x, y, mx, mh) &&
            !hit(ev->mx, ev->my, r.start_x, r.start_y, SHELL_START_W, 26)) {
            menu_open = 0;
            menu_search[0] = '\0';
            return 1;
        }
    }
    if (qs_open) {
        if (!hit(ev->mx, ev->my, (int)fb_width - SHELL_QS_W - 8,
                 (int)fb_height - SHELL_TB_H - SHELL_QS_H - 8,
                 SHELL_QS_W, SHELL_QS_H) &&
            !hit(ev->mx, ev->my, r.qs_x, r.start_y, 24, 26)) {
            qs_open = 0;
            return 1;
        }
    }

    /* start button                                                        */
    if (hit(ev->mx, ev->my, r.start_x, r.start_y, SHELL_START_W, 26)) {
        menu_open = !menu_open;
        if (menu_open) {
            menu_search[0] = '\0';
            menu_search_focus = 1;
            qs_open = 0;
        }
        return 1;
    }

    /* pinned app buttons                                                  */
    for (int i = 0; i < SHELL_TB_ICONS && i < app_count; i++) {
        int px = r.pinned_x + i * SHELL_BTN_W;
        if (hit(ev->mx, ev->my, px, r.start_y, SHELL_BTN_W - 4, 26)) {
            struct window *live = NULL;
            for (int z = 1; z <= win_top_z(); z++) {
                struct window *w = win_by_z(z);
                if (w != NULL && w->app == apps[i]) {
                    live = w;
                    break;
                }
            }
            if (live != NULL) {
                if (live->minimized) {
                    win_restore(live);
                } else if (live == win_focus_top()) {
                    win_minimize(live);   /* Win11: click focused = minimize */
                } else {
                    win_raise(live);
                }
            } else {
                shell_launch_app(apps[i]);
            }
            return 1;
        }
    }

    /* quick settings button (and its toggles while open)                  */
    if (hit(ev->mx, ev->my, r.qs_x, r.start_y, 24, 26)) {
        qs_open = !qs_open;
        if (qs_open)
            menu_open = 0;
        return 1;
    }
    if (qs_open) {
        int x = (int)fb_width - SHELL_QS_W - 8;
        int y = (int)fb_height - SHELL_TB_H - SHELL_QS_H - 8;
        if (hit(ev->mx, ev->my, x + 14, y + 66, SHELL_QS_W - 28, 40)) {
            qs_toggle_firewall();
            return 1;
        }
    }

    /* start menu internals: search + tiles + power                        */
    if (menu_open) {
        int x = ((int)fb_width - SHELL_MENU_W) / 2;
        int y = (int)fb_height - SHELL_TB_H - SHELL_MENU_H - 10;

        if (hit(ev->mx, ev->my, x + 16, y + SHELL_MENU_H - 34,
                SHELL_MENU_W - 32, 22)) {
            shell_power_off();
            return 1;
        }
        if (hit(ev->mx, ev->my, x + 16 + 72, y + SHELL_MENU_H - 34,
                SHELL_MENU_W - 32 - 72, 22)) {
            /* reboot via 8042 pulse                                       */
            outb(0x64, 0xFE);
            return 1;
        }

        int shown = 0;
        for (int ai = 0; ai < app_count && shown < 16; ai++) {
            if (menu_search[0] != '\0' &&
                strstr(apps[ai]->name, menu_search) == NULL)
                continue;
            int cw = (SHELL_MENU_W - 32) / 4;
            int cx = x + 16 + (shown % 4) * cw;
            int cy = y + 34 + 34 + (shown / 4) * 44;
            if (hit(ev->mx, ev->my, cx + 4, cy, cw - 12, 38)) {
                shell_launch_app(apps[ai]);
                menu_open = 0;
                menu_search[0] = '\0';
                return 1;
            }
            shown++;
        }
        if (hit(ev->mx, ev->my, x + 16, y + 34, SHELL_MENU_W - 32, 22))
            menu_search_focus = 1;
        return 1;                       /* menu absorbs all clicks           */
    }

    return 0;
}

void shell_draw(void)
{
    draw_taskbar();
    if (menu_open)
        draw_start_menu();
    if (qs_open)
        draw_quick_settings();
}
