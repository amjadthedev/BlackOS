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
#include "themes.h"
#include "keyboard.h"

#define SNAKE_W 20
#define SNAKE_H 15
#define SNAKE_CELL 14

struct snake_state {
    u8  grid[SNAKE_H][SNAKE_W];      /* 0 empty, 1 snake, 2 food           */
    signed char  sx[256], sy[256];            /* snake body                         */
    int len, dir;                    /* 0 up,1 right,2 down,3 left         */
    int fx, fy;                      /* food                               */
    int score, over;
    u32 last_move;
};

static struct snake_state *sn_of(struct window *win)
{
    return (struct snake_state *)win->priv;
}

static void snake_place_food(struct snake_state *s);

static void snake_reset(struct snake_state *s)
{
    for (int y = 0; y < SNAKE_H; y++)
        for (int x = 0; x < SNAKE_W; x++)
            s->grid[y][x] = 0;
    s->len = 3;
    s->sx[0] = 10; s->sy[0] = 7;
    s->sx[1] = 9; s->sy[1] = 7;
    s->sx[2] = 8; s->sy[2] = 7;
    s->dir = 1;
    s->score = 0;
    s->over = 0;
    s->last_move = uptime_ms;
    snake_place_food(s);
}

static void snake_place_food(struct snake_state *s)
{
    int x, y;
    do {
        x = uptime_ms % SNAKE_W;
        y = uptime_ms % SNAKE_H;
        if ((uptime_ms / 3) % 2 == 0) { x++; y -= 2; if (x >= SNAKE_W) x -= SNAKE_W; if (y < 0) y += SNAKE_H; }
    } while (s->grid[y][x] == 1);
    s->fx = x; s->fy = y;
    s->grid[y][x] = 2;
}

static void snake_paint(struct window *win, u32 *buf, int w, int h)
{
    struct snake_state *s = sn_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);
    ui_text(buf, w, h, 12, 8, "SNAKE  score: ", g_text_dim, 0, 0);
    char sc[12];
    int n = 0;
    int v = s->score;
    if (v == 0) sc[n++] = '0';
    else while (v > 0) { sc[n++] = (char)('0' + (v % 10)); v /= 10; }
    int i;
    for (i = 0; i < n / 2; i++) { char t = sc[i]; sc[i] = sc[n-1-i]; sc[n-1-i] = t; }
    sc[n] = '\0';
    ui_text(buf, w, h, 120, 8, sc, g_text_bright, 0, 0);

    int cellW = 14, cellH = 14;
    int offX = (w - SNAKE_W * cellW) / 2;
    int offY = (h - SNAKE_H * cellH) / 2 + 12;
    for (int y = 0; y < SNAKE_H; y++) {
        for (int x = 0; x < SNAKE_W; x++) {
            int cx = offX + x * cellW;
            int cy = offY + y * cellH;
            u32 col = 0x0A1010;
            if (s->grid[y][x] == 1) col = g_accent;
            else if (s->grid[y][x] == 2) col = 0xCD3030;
            ui_fill(buf, w, h, cx, cy, cellW-1, cellH-1, col);
            ui_fill(buf, w, h, cx, cy, cellW, 1, g_panel_border);
        }
    }
    if (s->over)
        ui_text(buf, w, h, w/2-80, h/2-64+28, "GAME OVER", 0xCD3030, 0, 0);
}

static void snake_key(struct window *win, char c)
{
    struct snake_state *s = sn_of(win);
    if (s == NULL) return;
    if (c == KBD_UP)    { if (s->dir != 2) s->dir = 0; }
    else if (c == KBD_DOWN) { if (s->dir != 0) s->dir = 2; }
    else if (c == KBD_LEFT)  { if (s->dir != 1) s->dir = 3; }
    else if (c == KBD_RIGHT) { if (s->dir != 3) s->dir = 1; }
    else if (c == 'r' || c == 'R') snake_reset(s);
}

static void snake_open(struct window *win)
{
    win->priv = (struct snake_state *)(usize)pmm_alloc_run(1);
    struct snake_state *s = (struct snake_state *)win->priv;
    memset(s, 0, sizeof(*s));
    snake_reset(s);
}

const struct app app_snake = {
    .name  = "snake",
    .open  = snake_open,
    .paint = snake_paint,
    .key   = snake_key,
    .close = NULL,
};

extern const struct app app_snake;