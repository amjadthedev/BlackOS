/* =============================================================================
 *  Black OS - Phase 8: window manager (surface allocation + z-order)
 * ========================================================================== */
#ifndef BLACKOS_WIN_H
#define BLACKOS_WIN_H

#include "types.h"
#include "pmm.h"

struct app;                             /* defined in app.h */

#define WIN_MAX             8
#define WIN_TITLEBAR        20U                 /* chrome height, px       */
#define WIN_TITLE_BYTES     24

struct window {
    char        title[WIN_TITLE_BYTES];
    int         w, h;               /* content area, px                     */
    int         x, y;               /* top-left of the *full chrome*, px    */
    u32        *buf;                /* content surface (PMM run, identity)  */
    u64         frames;             /* run size held by pmm_alloc_run()     */
    int         z;                  /* stacking order, 0 = bottom           */
    int         alive;
    u8          minimized;
    u8          maximized;
    int         saved_x, saved_y;   /* pre-maximize geometry                */
    int         saved_w, saved_h;
    u64         opened_ms;          /* sys_uptime() stamp for Task Manager  */
    const struct app *app;          /* app callbacks (app.h)                */
    void       *priv;               /* per-instance app state               */
};

void            win_init(void);
struct window  *win_open(int w, int h, const char *title, const struct app *app);
void            win_close(struct window *w);
struct window  *win_focus_top(void);          /* currently focused window   */
struct window  *win_by_z(int z);              /* 1..top_z, NULL if empty    */
struct window  *win_hit(int px, int py);      /* topmost window under pt    */
int             win_count(void);
int             win_top_z(void);
void            win_raise(struct window *w);  /* move to top of the stack   */
void            win_minimize(struct window *w);
void            win_maximize(struct window *w);
void            win_restore(struct window *w);
void            win_resize(struct window *w, int w_px, int h_px);
u32            *win_alloc_surface(int w, int h, u64 *frames_out);

#endif /* BLACKOS_WIN_H */
