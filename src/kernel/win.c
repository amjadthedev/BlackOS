/* =============================================================================
 *  Black OS - Phase 8: window manager (surface allocation + z-order)
 * ========================================================================== */
#include "win.h"
#include "app.h"
#include "fb.h"
#include "ui.h"
#include "string.h"
#include "idt.h"                /* uptime_ms */

struct window windows[WIN_MAX];
static int top_z = 0;

void win_init(void)
{
    memset(windows, 0, sizeof(windows));
    top_z = 0;
}

static u64 surface_frames(int w, int h)
{
    u64 bytes = (u64)w * (u64)h * 4ULL;
    return (bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
}

u32 *win_alloc_surface(int w, int h, u64 *frames_out)
{
    u64 frames = surface_frames(w, h);
    u32 *buf = (u32 *)(usize)pmm_alloc_run(frames);
    if (buf == NULL)
        return NULL;
    if (frames_out)
        *frames_out = frames;
    return buf;
}

struct window *win_open(int w, int h, const char *title, const struct app *app)
{
    int max_w = (int)fb_width  - 16;
    int max_h = (int)fb_height - (int)UI_TASKBAR_H - WIN_TITLEBAR - 16;
    if (w > max_w)
        w = max_w;
    if (h > max_h)
        h = max_h;

    for (int i = 0; i < WIN_MAX; i++) {
        struct window *win = &windows[i];
        if (win->alive)
            continue;

        u32 *buf = win_alloc_surface(w, h, &win->frames);
        if (buf == NULL)
            return NULL;               /* no RAM left for this surface     */

        memset(win->title, 0, sizeof(win->title));
        for (int k = 0; k < WIN_TITLE_BYTES - 1 && title[k]; k++)
            win->title[k] = title[k];

        win->w      = w;
        win->h      = h;
        win->x      = ((int)fb_width  - w) / 2;
        win->y      = (WIN_TITLEBAR + ((int)fb_height - (int)UI_TASKBAR_H - h - WIN_TITLEBAR) / 2);
        win->buf    = buf;
        win->z      = ++top_z;
        win->alive  = 1;
        win->opened_ms = uptime_ms;
        win->app    = app;
        win->priv   = NULL;
        return win;
    }
    return NULL;                       /* window table full                */
}

void win_close(struct window *win)
{
    if (win == NULL || !win->alive)
        return;

    win->alive = 0;
    if (win->priv)
        win->priv = NULL;
    pmm_free_run((u64)(usize)win->buf, win->frames);
    memset(win, 0, sizeof(*win));

    /* Compact the z ladder so it stays within 1..win_count(). */
    int z = 1;
    for (;;) {
        struct window *min = NULL;
        for (int i = 0; i < WIN_MAX; i++) {
            struct window *other = &windows[i];
            if (!other->alive)
                continue;
            if (min == NULL || other->z < min->z)
                min = other;
        }
        if (min == NULL)
            break;
        min->z = z++;
    }
    top_z = z > 1 ? z - 1 : 0;
}

void win_raise(struct window *win)
{
    if (win == NULL || !win->alive)
        return;
    if (win->z == top_z)
        return;
    for (int i = 0; i < WIN_MAX; i++) {
        struct window *other = &windows[i];
        if (!other->alive || other == win)
            continue;
        if (other->z > win->z)
            other->z--;
    }
    win->z = top_z;
}

struct window *win_focus_top(void)
{
    return win_by_z(top_z);
}

int win_top_z(void)
{
    return top_z;
}

struct window *win_hit(int px, int py)
{
    /* scan from the top of the stack down; only non-minimized windows hit */
    for (int z = top_z; z >= 1; z--) {
        struct window *w = win_by_z(z);
        if (w == NULL || w->minimized)
            continue;
        int cw = w->w + 2;
        int ch = w->h + (int)WIN_TITLEBAR + 2;
        if (px >= w->x - 1 && px < w->x - 1 + cw &&
            py >= w->y - 1 && py < w->y - 1 + ch)
            return w;
    }
    return NULL;
}

void win_minimize(struct window *w)
{
    if (w == NULL || !w->alive)
        return;
    w->minimized = 1;
}

void win_maximize(struct window *w)
{
    if (w == NULL || !w->alive || w->maximized)
        return;

    if (!w->maximized) {
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
    }
    w->x = -1;
    w->y = (int)WIN_TITLEBAR - 1;
    w->w = (int)fb_width - 2;
    w->h = (int)fb_height - (int)WIN_TITLEBAR - (int)UI_TASKBAR_H;
    w->maximized = 1;

    /* resurface the buffer for the larger geometry                        */
    pmm_free_run((u64)(usize)w->buf, w->frames);
    u32 *buf = win_alloc_surface(w->w, w->h, &w->frames);
    if (buf != NULL)
        w->buf = buf;
}

void win_restore(struct window *w)
{
    if (w == NULL || !w->alive)
        return;

    if (w->minimized) {
        w->minimized = 0;
        win_raise(w);
        return;
    }
    if (!w->maximized)
        return;

    w->maximized = 0;
    pmm_free_run((u64)(usize)w->buf, w->frames);
    u32 *buf = win_alloc_surface(w->saved_w, w->saved_h, &w->frames);
    if (buf == NULL)
        return;
    w->buf = buf;
    w->w = w->saved_w;
    w->h = w->saved_h;
    w->x = w->saved_x;
    w->y = w->saved_y;
}

void win_resize(struct window *w, int w_px, int h_px)
{
    if (w == NULL || !w->alive)
        return;
    if (w_px < 120)
        w_px = 120;
    if (h_px < 80)
        h_px = 80;
    if (w->x + w_px > (int)fb_width)
        w_px = (int)fb_width - w->x;
    if (w->y + (int)WIN_TITLEBAR + h_px >
        (int)fb_height - (int)UI_TASKBAR_H)
        h_px = (int)fb_height - (int)UI_TASKBAR_H - w->y - (int)WIN_TITLEBAR;
    if (w_px < 40 || h_px < 40)
        return;

    pmm_free_run((u64)(usize)w->buf, w->frames);
    u32 *buf = win_alloc_surface(w_px, h_px, &w->frames);
    if (buf == NULL)
        return;
    w->buf = buf;
    w->w = w_px;
    w->h = h_px;
}

struct window *win_by_z(int z)
{
    for (int i = 0; i < WIN_MAX; i++)
        if (windows[i].alive && windows[i].z == z)
            return &windows[i];
    return NULL;
}

int win_count(void)
{
    int n = 0;
    for (int i = 0; i < WIN_MAX; i++)
        if (windows[i].alive)
            n++;
    return n;
}
