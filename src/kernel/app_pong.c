/* =============================================================================
 *  Black OS - app_pong.c : Pong game in a compositor window
 *
 *  Two-player: left paddle = arrows, right paddle = W / S.  Ball bounces;
 *  first to 7 wins.  Rendered into the window surface each paint frame.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "themes.h"
#include "keyboard.h"
#include "font.h"
#include "pmm.h"
#include "string.h"
#include "idt.h"

struct pong_state {
    int px, py;           /* ball pos (subpixel * 16)             */
    int vx, vy;           /* ball vel                             */
    int ly, ry;           /* paddle Y centers                     */
    int lscore, rscore;
    int over;
    u32 last_move;
};

static struct pong_state *pn_of(struct window *win)
{
    return (struct pong_state *)win->priv;
}

static void pong_serve(struct pong_state *p)
{
    p->px = 0; p->py = 0;
    p->vx = (p->lscore > p->rscore) ? -200 : 200;
    p->vy = 100;
}

static void pong_reset(struct pong_state *p)
{
    p->px = 0; p->py = 0;
    p->vx = 200; p->vy = 100;
    p->ly = p->ry = 0;
    p->lscore = p->rscore = 0;
    p->over = 0;
    p->last_move = uptime_ms;
    pong_serve(p);
}

static void pong_paint(struct window *win, u32 *buf, int w, int h)
{
    struct pong_state *p = pn_of(win);
    if (p == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x020408);

    int cx = w / 2;
    int cy = h / 2;

    /* center dashed line */
    for (int y = 0; y < h; y += 20)
        ui_fill(buf, w, h, cx - 1, y, 2, 8, 0x2A3F4F);

    /* paddles */
    int pw = 6, ph = 40;
    ui_fill(buf, w, h, 20, p->ly, pw, ph, g_text_bright);
    ui_fill(buf, w, h, w - 20 - pw, p->ry, pw, ph, g_text_bright);

    /* ball */
    int bx = cx + p->px / 16;
    int by = cy + p->py / 16;
    ui_fill(buf, w, h, bx - 2, by - 2, 4, 4, g_accent);

    /* scores */
    char sc[8];
    sc[0] = (char)('0' + p->lscore); sc[1] = ' '; sc[2] = ' '; sc[3] = '|';
    sc[4] = ' '; sc[5] = ' '; sc[6] = (char)('0' + p->rscore); sc[7] = '\0';
    ui_text(buf, w, h, cx - 20, 20, sc, g_text_bright, 1, 0);

    if (p->over) {
        ui_fill(buf, w, h, cx - 90, cy - 10, 180, 24, 0x1A2A44);
        ui_text(buf, w, h, cx - 70, cy - 6, p->lscore >= 7 ? "LEFT WINS" : "RIGHT WINS",
                g_text_bright, 1, 0);
        ui_text(buf, w, h, cx - 60, cy + 14, "press R to restart", g_text_dim, 1, 0);
    }
    ui_text(buf, w, h, 12, h - 20, "left: arrows   right: W/S   R: restart", g_text_dim, 1, 0);
}

static void pong_key(struct window *win, char c)
{
    struct pong_state *p = pn_of(win);
    if (p == NULL) return;
    if (p->over) { if (c == 'r' || c == 'R') pong_reset(p); return; }

    int step = 16;
    switch (c) {
    case KBD_UP:    if (p->ly > step) p->ly -= step; break;
    case KBD_DOWN:  if (p->ly < 400 - step - 40) p->ly += step; break;
    case 'w': case 'W': if (p->ry > step) p->ry -= step; break;
    case 's': case 'S': if (p->ry < 400 - step - 40) p->ry += step; break;
    case 'r': case 'R': pong_reset(p); break;
    default: break;
    }
}

static void pong_open(struct window *win)
{
    win->priv = (struct pong_state *)(usize)pmm_alloc_run(1);
    struct pong_state *p = pn_of(win);
    memset(p, 0, sizeof(*p));
    pong_reset(p);
}

const struct app app_pong = {
    .name  = "pong",
    .open  = pong_open,
    .paint = pong_paint,
    .key   = pong_key,
    .close = NULL,
};
