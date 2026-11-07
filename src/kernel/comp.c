/* =============================================================================
 *  Black OS - Phase 12: GUI compositor + Win11 shell wiring
 *
 *  Screen budget (800x600):
 *      desktop       vertical gradient + BLACK OS wordmark
 *      windows       chrome: 20px titlebar (drag), min/max/close buttons,
 *                    z-ordered, FancyZones-style snap previews
 *      shell         centered taskbar + start menu + quick settings (shell.c)
 *
 *  Input:
 *      keyboard      apps receive ASCII; TAB cycles focus; ESC closes
 *      mouse         titlebar drag; edge drag resizes; snap via top/left/
 *                    right edges; titlebar buttons min/max/close; taskbar
 *                    via shell
 * ========================================================================== */
#include "comp.h"
#include "fb.h"
#include "ui.h"
#include "win.h"
#include "app.h"
#include "shell.h"
#include "weather.h"
#include "auth.h"
#include "keyboard.h"
#include "ps2mouse.h"
#include "lockscreen.h"
#include "console.h"
#include "printk.h"
#include "string.h"
#include "idt.h"
#include "io.h"
#include "themes.h"
#include "wallpaper.h"

enum {
    L_TITLEBAR   = 20,                   /* chrome height                    */
    L_BTN_W      = 26,                   /* per titlebar button              */
};

/* cursor + interaction state */
static int cur_x, cur_y;
static u8  cur_buttons, last_buttons;

enum { DRAG_NONE = 0, DRAG_MOVE, DRAG_RESIZE };
static int drag_mode;
static struct window *drag_win;
static int drag_off_x, drag_off_y;
static int drag_start_w, drag_start_h;

/* snap preview */
static int snap_show;
static int snap_x, snap_y, snap_w, snap_h;

/*──────────────────────────── chrome drawing ─────────────────────────────*/
static void draw_desktop(void)
{
    draw_wallpaper();

    const char *brand = "BLACK OS";
    int bw2 = (int)strlen(brand) * 16;
    int bx = ((int)fb_width - bw2) / 2;
    int by = (int)fb_height / 2 - 40;

    for (int k = 0; brand[k]; k++) {
        const u8 *g = font_glyph(brand[k]);
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col)))
                    continue;
                for (int s = 0; s < 2; s++) {
                    fb_pixel(bx + k * 16 + col * 2 + s,     by + row * 2,     UI_ACCENT_CYAN);
                    fb_pixel(bx + k * 16 + col * 2 + s,     by + row * 2 + 1, UI_ACCENT_CYAN);
                }
            }
        }
    }
    const char *sub = ":: shell online ::";
    int sx = ((int)fb_width - (int)strlen(sub) * 8) / 2;
    for (int k = 0; sub[k]; k++) {
        const u8 *g = font_glyph(sub[k]);
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    fb_pixel(sx + k * 8 + col, by + 22 + row, UI_TEXT_MUTED);
        }
    }
}

/* titlebar control buttons: [ _ ][ ][X ] right-aligned                     */

static void titlebar_buttons(struct window *w)
{
    int bw = w->w + 2;
    int y0 = w->y - 1;
    int cls_x = w->x + bw - L_BTN_W;
    int max_x = w->x + bw - 2 * L_BTN_W;
    int min_x = w->x + bw - 3 * L_BTN_W;

    fb_fill_rect(w->x, y0, bw, (int)L_TITLEBAR, UI_WINDOW_TITLE);

    /* close: red accent */
    fb_fill_rect(cls_x, y0, L_BTN_W, (int)L_TITLEBAR, 0x30131A);
    const u8 *gx = font_glyph('X');
    int gy = y0 + ((int)L_TITLEBAR - 8) / 2;
    for (int row = 0; row < 8; row++) {
        u8 bits = gx[row];
        for (int col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                fb_pixel(cls_x + (L_BTN_W - 8) / 2 + col, gy + row,
                         UI_ACCENT_RED);
    }

    /* maximize/restore: hollow square / double square */
    const u8 *gm = font_glyph(w->maximized ? 'R' : 'O');
    for (int row = 0; row < 8; row++) {
        u8 bits = gm[row];
        for (int col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                fb_pixel(max_x + (L_BTN_W - 8) / 2 + col, gy + row,
                         UI_TEXT_DIM);
    }

    /* minimize: underscore                                                */
    fb_fill_rect(min_x + 8, gy + 6, 10, 2, UI_TEXT_DIM);
}

static void draw_window_chrome(struct window *w)
{
    int cx = w->x - 1, cy = w->y - 1;
    int cw = w->w + 2, chh = w->h + (int)L_TITLEBAR + 2;

    /* drop shadow */
    for (int i = 1; i <= 3; i++)
        fb_frame(cx + i, cy + i, cw, chh, 0x020409);

    /* rounded window body                                                 */
    fb_rect_rounded(cx, cy, cw, chh, 8, UI_WINDOW_BG);
    fb_rounded_frame(cx, cy, cw, chh, 8, UI_WINDOW_BORDER);

    /* titlebar controls (covers the top band cleanly)                     */
    titlebar_buttons(w);

    /* title text, clipped before the buttons                              */
    int tx = cx + 8;
    int max_chars = (w->w - 4 * L_BTN_W) / 8;
    if (max_chars > WIN_TITLE_BYTES - 1)
        max_chars = WIN_TITLE_BYTES - 1;
    for (int i = 0; i < max_chars && w->title[i]; i++) {
        const u8 *g = font_glyph(w->title[i]);
        int ty = cy + ((int)L_TITLEBAR - 8) / 2;
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    fb_pixel(tx + i * 8 + col, ty + row, UI_TEXT_BRIGHT);
        }
    }

    /* content surface */
    fb_blit(w->buf, w->w, w->h, w->x, w->y + (int)L_TITLEBAR);

    /* resize grip (bottom-right corner)                                   */
    for (int i = 0; i < 6; i++) {
        fb_pixel(cx + cw - 2 - i, cy + chh - 3 - i, UI_BEVEL_LIT);
        fb_pixel(cx + cw - 3 - i, cy + chh - 2 - i, UI_BEVEL_LIT);
    }
}

static void draw_snap_preview(void)
{
    if (!snap_show)
        return;
    for (int i = 0; i < 2; i++)
        fb_rounded_frame(snap_x + i, snap_y + i, snap_w - 2 * i,
                          snap_h - 2 * i, 8, UI_ACCENT_CYAN);
    /* 30% cyan wash                                                       */
    for (int yy = snap_y; yy < snap_y + snap_h; yy++) {
        u32 *row = fb_base + (u32)yy * (fb_bytes / 4);
        for (int xx = snap_x; xx < snap_x + snap_w; xx++)
            row[xx] = fb_mix(row[xx], UI_ACCENT_CYAN, 0x30);
    }
}

static void draw_cursor(void)
{
    /* arrow-ish cursor: vertical bar + wings                              */
    fb_pixel(cur_x, cur_y, 0xFFFFFF);
    fb_fill_rect(cur_x, cur_y + 1, 2, 11, 0xFFFFFF);
    fb_fill_rect(cur_x + 2, cur_y + 10, 2, 2, 0xFFFFFF);
    fb_fill_rect(cur_x + 4, cur_y + 12, 2, 2, 0xFFFFFF);
    for (int i = 0; i < 11; i++)
        fb_pixel(cur_x + 1, cur_y + 1 + i, 0x202020);
}

/*──────────────────────────── app lifecycle ──────────────────────────────*/
static void app_close_window(struct window *w)
{
    if (w == NULL)
        return;
    if (w->app->close)
        w->app->close(w);
    win_close(w);
}

/*──────────────────────────── mouse hit-testing ──────────────────────────*/
static int in_titlebar(struct window *w, int px, int py)
{
    return px >= w->x && px < w->x + w->w + 2 &&
           py >= w->y - 1 && py < w->y + (int)L_TITLEBAR;
}

static int in_resize_grip(struct window *w, int px, int py)
{
    int bx = w->x + w->w - 14;
    int by = w->y + (int)L_TITLEBAR + w->h - 14;
    return !w->maximized && px >= bx && px <= w->x + w->w + 1 &&
           py >= by && py <= w->y + (int)L_TITLEBAR + w->h + 1;
}

static void titlebar_button_hit(struct window *w, int px, int py, int *hit_min,
                                int *hit_max, int *hit_close)
{
    int bw = w->w + 2;
    *hit_close = in_titlebar(w, px, py) &&
                 px >= w->x + bw - L_BTN_W && px < w->x + bw;
    *hit_max   = in_titlebar(w, px, py) &&
                 px >= w->x + bw - 2 * L_BTN_W && px < w->x + bw - L_BTN_W;
    *hit_min   = in_titlebar(w, px, py) &&
                 px >= w->x + bw - 3 * L_BTN_W && px < w->x + bw - 2 * L_BTN_W;
}

enum {
    SNAP_NONE = 0,
    SNAP_MAX,
    SNAP_LEFT,
    SNAP_RIGHT
};

static void snap_zone_of(int px, int py, int *kind)
{
    int edges = 12;
    *kind = SNAP_NONE;
    if (py <= edges)
        *kind = SNAP_MAX;
    else if (px <= edges)
        *kind = SNAP_LEFT;
    else if ((int)fb_width - px <= edges)
        *kind = SNAP_RIGHT;
}

static void apply_snap(struct window *w, int kind)
{
    switch (kind) {
    case SNAP_MAX:
        if (w->maximized)
            win_restore(w);
        else
            win_maximize(w);
        break;
    case SNAP_LEFT:
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
        w->x = 0;
        w->y = (int)L_TITLEBAR - 1;
        w->w = (int)fb_width / 2;
        w->h = (int)fb_height - (int)L_TITLEBAR - UI_TASKBAR_H;
        win_resize(w, w->w, w->h);
        break;
    case SNAP_RIGHT:
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
        w->x = (int)fb_width / 2 + 1;
        w->w = (int)fb_width / 2 - 2;
        w->h = (int)fb_height - (int)L_TITLEBAR - UI_TASKBAR_H;
        win_resize(w, w->w, w->h);
        break;
    }
}

static void mouse_press(int px, int py)
{
    struct window *w = win_hit(px, py);
    if (w == NULL)
        return;

    win_raise(w);

    int hmin, hmax, hclose;
    titlebar_button_hit(w, px, py, &hmin, &hmax, &hclose);

    if (hclose) {
        app_close_window(w);
        return;
    }
    if (hmax) {
        if (w->maximized)
            win_restore(w);
        else
            win_maximize(w);
        return;
    }
    if (hmin) {
        win_minimize(w);
        return;
    }

    if (in_resize_grip(w, px, py)) {
        drag_mode   = DRAG_RESIZE;
        drag_win    = w;
        drag_start_w = w->w;
        drag_start_h = w->h;
        return;
    }

    if (in_titlebar(w, px, py)) {
        drag_mode   = DRAG_MOVE;
        drag_win    = w;
        drag_off_x  = px - w->x;
        drag_off_y  = py - w->y;
        if (w->maximized)
            win_restore(w);             /* drag off a maximized window       */
        drag_off_x  = px - w->x;
    }
}

static void mouse_drag(int px, int py)
{
    if (drag_mode == DRAG_NONE || drag_win == NULL || !drag_win->alive) {
        drag_mode = DRAG_NONE;
        snap_show = 0;
        return;
    }

    if (drag_mode == DRAG_MOVE) {
        int nx = px - drag_off_x;
        int ny = py - drag_off_y;
        if (nx < -(int)drag_win->w + 60)
            nx = -(int)drag_win->w + 60;
        if (ny < (int)L_TITLEBAR - 1)
            ny = (int)L_TITLEBAR - 1;
        if ((u32)(ny + (int)L_TITLEBAR + drag_win->h) >
            fb_height - UI_TASKBAR_H + 12)
            ny = (int)fb_height - UI_TASKBAR_H - drag_win->h - (int)L_TITLEBAR + 12;
        drag_win->x = nx;
        drag_win->y = ny;

        int kind;
        snap_zone_of(px, py, &kind);
        snap_show = 0;
        if (kind != SNAP_NONE) {
            snap_show = 1;
            switch (kind) {
            case SNAP_MAX:
                snap_x = 0;
                snap_y = (int)L_TITLEBAR - 1;
                snap_w = (int)fb_width;
                snap_h = (int)fb_height - (int)L_TITLEBAR - UI_TASKBAR_H;
                break;
            case SNAP_LEFT:
                snap_x = 0;
                snap_y = (int)L_TITLEBAR - 1;
                snap_w = (int)fb_width / 2;
                snap_h = (int)fb_height - (int)L_TITLEBAR - UI_TASKBAR_H;
                break;
            case SNAP_RIGHT:
                snap_x = (int)fb_width / 2;
                snap_y = (int)L_TITLEBAR - 1;
                snap_w = (int)fb_width / 2;
                snap_h = (int)fb_height - (int)L_TITLEBAR - UI_TASKBAR_H;
                break;
            }
        }
        drag_win->maximized = 0;
        return;
    }

    /* resize                                                              */
    if (drag_mode == DRAG_RESIZE) {
        int nw = px - drag_win->x - 1;
        int nh = py - drag_win->y - (int)L_TITLEBAR - 1;
        if (nw < drag_start_w / 3)
            nw = drag_start_w / 3;
        if (nh < drag_start_h / 3)
            nh = drag_start_h / 3;
        win_resize(drag_win, nw, nh);
    }
}

static void mouse_release(void)
{
    if (drag_mode == DRAG_MOVE && snap_show && drag_win != NULL) {
        int kind;
        snap_zone_of(cur_x, cur_y, &kind);
        if (kind != SNAP_NONE)
            apply_snap(drag_win, kind);
    }
    drag_mode = DRAG_NONE;
    drag_win = NULL;
    snap_show = 0;
}

/*──────────────────────────── keyboard dispatch ──────────────────────────*/
static void key_to_focused_app(char c)
{
    struct window *focus = win_focus_top();
    if (focus == NULL || focus->minimized)
        return;
    if (focus->app->key)
        focus->app->key(focus, c);
}

static void dispatch_key(char c)
{
    struct window *focus = win_focus_top();

    /* floating panels eat keys first (start-menu search, ESC to close)    */
    if (shell_key(c))
        return;

    if (c == KBD_ESCAPE) {
        if (focus != NULL)
            app_close_window(focus);
        return;
    }
    if (c == '\t') {
        int n = win_count();
        if (n > 1) {
            int z = win_top_z() - 1;
            while (z >= 1) {
                struct window *w = win_by_z(z);
                if (w != NULL) {
                    win_raise(w);
                    break;
                }
                z--;
            }
        }
        return;
    }

    if (shell_key(c))
        return;

    if (focus != NULL) {
        switch (c) {
        case KBD_UP:
            if (focus->y > (int)L_TITLEBAR + 1)
                focus->y -= 4;
            return;
        case KBD_DOWN:
            if ((u32)(focus->y + focus->h + (int)L_TITLEBAR) <
                fb_height - UI_TASKBAR_H)
                focus->y += 4;
            return;
        case KBD_LEFT:
            if (focus->x > 0)
                focus->x -= 4;
            return;
        case KBD_RIGHT:
            if ((u32)(focus->x + focus->w) < fb_width)
                focus->x += 4;
            return;
        default:
            key_to_focused_app(c);
            return;
        }
    }

    if (c == 'b' || c == 'B')
        shell_key(c);                   /* convenience: open the menu        */
}

/*──────────────────────────────── paint ──────────────────────────────────*/
static void paint_windows(void)
{
    for (int z = 1; z <= win_top_z(); z++) {
        struct window *w = win_by_z(z);
        if (w == NULL || w->minimized)
            continue;
        if (w->app->paint)
            w->app->paint(w, w->buf, w->w, w->h);
        draw_window_chrome(w);
    }
}

static void repaint(void)
{
    draw_desktop();
    paint_windows();
    draw_snap_preview();

    shell_draw();

    draw_cursor();
}

/*────────────────────────────── main loop ────────────────────────────────*/
static void pump_input(void)
{
    char c;
    while (keyboard_pop(&c))
        dispatch_key(c);

    struct mouse_event mev;
    while (mouse_pop(&mev)) {
        cur_x += mev.dx;
        cur_y += mev.dy;
        if (cur_x < 0)      cur_x = 0;
        if (cur_y < 0)      cur_y = 0;
        if ((u32)cur_x > fb_width - 4)  cur_x = (int)fb_width - 4;
        if ((u32)cur_y > fb_height - 4) cur_y = (int)fb_height - 4;
        cur_buttons = mev.buttons;

        int click = (mev.buttons & 0x01) && !(last_buttons & 0x01);
        int release_evt = !(mev.buttons & 0x01) && (last_buttons & 0x01);

        if (release_evt) {
            mouse_release();
            last_buttons = mev.buttons;
            continue;
        }

        if (click) {
            struct shell_input sin = {
                .mx = cur_x, .my = cur_y,
                .buttons = mev.buttons, .click = 1
            };
            if (shell_input(&sin)) {
                last_buttons = mev.buttons;
                continue;
            }
            mouse_press(cur_x, cur_y);
        } else if (mev.buttons & 0x01) {
            mouse_drag(cur_x, cur_y);
        }

        last_buttons = mev.buttons;
    }
}

void comp_init(void)
{
    win_init();
    shell_init();
    console_use_fb();
    console_resync();

    cur_x = (int)fb_width / 2;
    cur_y = (int)fb_height / 2;
    cur_buttons = last_buttons = 0;
    drag_mode = DRAG_NONE;
    snap_show = 0;
}

void comp_run(void)
{
    static int dirty;
    u64 last_sec = (uptime_ms / 1000) + 1;
    u64 last_pulse = 0;

    repaint();

    for (;;) {
        /* lock screen: blocks until the user enters the correct password     */
        if (auth_is_locked()) {
            lockscreen_run();
            repaint();        /* refresh desktop after unlock               */
            last_sec = (uptime_ms / 1000) + 1;
        }

        pump_input();

        /* weather keeps streaming while the shell is up                   */
        if (uptime_ms - last_pulse > 200) {
            weather_poll();
            last_pulse = uptime_ms;
        }

        if (uptime_ms / 1000 != last_sec) {
            last_sec = uptime_ms / 1000;
            dirty = 1;
        }
        if (dirty) {
            dirty = 0;
            repaint();
        }
        hlt();
    }
}
