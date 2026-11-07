/* =============================================================================
 *  Black OS - app_snake.c : Snake game in a compositor window
 *
 *  Grid-based, arrow-key controlled.  Rendered into the window surface each
 *  frame via the compositor paint callback.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "pmm.h"
#include "string.h"
#include "idt.h"

#define SNAKE_W 20
#define SNAKE_H 15
#define SNAKE_CELL 14

struct snake_state {
    u8  grid[SNAKE_H][SNAKE_W];      /* 0 empty, 1 snake, 2 food           */
    s8  sx[256], sy[256];            /* snake body                         */
    int len, dir;                    /* 0 up,1 right,2 down,3 left         */
    int fx, fy;                      /* food                               */
    int score, over;
    u32 last_move;
};

static struct snake_state *sn_of(struct window *win)
{
    return (struct snake_state *)win->priv;
}

static void snake_reset(struct snake_state *s)
{
    memset(s->grid, 0, sizeof(s->grid));
    s->len = 3;
    s->dir = 1;
    s->sx[0] = SNAKE_W / 2; s->sy[0] = SNAKE_H / 2;
    s->sx[1] = SNAKE_W / 2 - 1; s->sy[1] = SNAKE_H / 2;
    s->sx[2] = SNAKE_W / 2 - 2; s->sy[2] = SNAKE_H / 2;
    for (int i = 0; i < s->len; i++)
        s->grid[s->sy[i]][s->sx[i]] = 1;
    s->score = 0;
    s->over = 0;
    s->last_move = uptime_ms;
    snake_place_food(s);
}

static void snake_place_food(struct snake_state *s)
{
    for (;;) {
        s->fx = (u32)(uptime_ms * 7919) % SNAKE_W;
        s->fy = (u32)(uptime_ms * 131) % SNAKE_H;
        if (s->grid[s->fy][s->fx] == 0)
            break;
    }
    s->grid[s->fy][s->fx] = 2;
}

static void snake_paint(struct window *win, u32 *buf, int w, int h)
{
    struct snake_state *s = sn_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x05050A);

    int ox = (w - SNAKE_W * SNAKE_CELL) / 2;
    int oy = (h - SNAKE_H * SNAKE_CELL) / 2 - 10;

    for (int y = 0; y < SNAKE_H; y++)
        for (int x = 0; x < SNAKE_W; x++) {
            u8 c = s->grid[y][x];
            if (c == 0) continue;
            u32 col = c == 1 ? g_accent : 0xFF0000;
            ui_fill(buf, w, h, ox + x * SNAKE_CELL + 1, oy + y * SNAKE_CELL + 1,
                    SNAKE_CELL - 2, SNAKE_CELL - 2, col);
        }

    char sc[16];
    u32 n = 0, v = s->score;
    if (v == 0) sc[n++] = '0';
    while (v > 0) { sc[n++] = (char)('0' + v % 10); v /= 10; }
    for (u32 i = 0; i < n / 2; i++) { char t = sc[i]; sc[i] = sc[n - 1 - i]; sc[n - 1 - i] = t; }
    sc[n] = '\0';
    ui_text(buf, w, h, 12, 8, "SNAKE  score: ", g_text_dim, 1, 0);
    ui_text(buf, w, h, 120, 8, sc, g_text_bright, 1, 0);

    if (s->over) {
        ui_fill(buf, w, h, ox, oy + SNAKE_H * SNAKE_CELL + 8,
                SNAKE_W * SNAKE_CELL, 20, 0x1A2A44);
        ui_text(buf, w, h, ox + 10, oy + SNAKE_H * SNAKE_CELL + 14,
                "GAME OVER - press R to restart", g_text_bright, 1, 0);
    }
    ui_text(buf, w, h, 12, h - 20, "arrows: move   R: restart", g_text_dim, 1, 0);
}

static void snake_key(struct window *win, char c)
{
    struct snake_state *s = sn_of(win);
    if (s == NULL || s->over) {
        if (c == 'r' || c == 'R') snake_reset(s);
        return;
    }
    switch (c) {
    case KBD_UP:    if (s->dir != 2) s->dir = 0; break;
    case KBD_DOWN:  if (s->dir != 0) s->dir = 2; break;
    case KBD_LEFT:  if (s->dir != 1) s->dir = 3; break;
    case KBD_RIGHT: if (s->dir != 3) s->dir = 1; break;
    case 'r': case 'R': snake_reset(s); break;
    default: break;
    }
}

static void snake_open(struct window *win)
{
    win->priv = (struct snake_state *)(usize)pmm_alloc_run(1);
    struct snake_state *s = sn_of(win);
    memset(s, 0, sizeof(*s));
    snake_reset(s);
}

static const struct app app_snake = {
    .name  = "snake",
    .open  = snake_open,
    .paint = snake_paint,
    .key   = snake_key,
    .close = NULL,
};
