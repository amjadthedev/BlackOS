/* =============================================================================
 *  Black OS - app_terminal.c : in-shell terminal (expr/joke)
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "string.h"
#include "pmm.h"
#include "themes.h"
#include "keyboard.h"
#include "idt.h"

#define TERM_ROWS 24
#define TERM_COLS 80

struct term_state {
    char lines[TERM_ROWS][TERM_COLS+1];
    int  row, col;
    int  cursor;
};

static struct term_state *te_of(struct window *win){ return (struct term_state *)win->priv; }

static void te_printf(struct term_state *t, const char *s)
{
    for (; *s; s++) {
        char c = *s;
        if (c == '\n' || t->col >= TERM_COLS) {
            t->row++; t->col = 0;
            if (t->row >= TERM_ROWS) {
                for (int r = 1; r < TERM_ROWS; r++)
                    memcpy(t->lines[r-1], t->lines[r], TERM_COLS+1);
                memset(t->lines[TERM_ROWS-1], 0, TERM_COLS+1);
                t->row = TERM_ROWS-1;
            }
        }
        if (c == '\n') continue;
        t->lines[t->row][t->col] = c; t->col++;
    }
}

static void te_paint(struct window *win, u32 *buf, int w, int h)
{
    struct term_state *t = te_of(win);
    if (t == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x020408);
    ui_text(buf, w, h, 6, 4, win->title, g_accent, 0, 0);
    int ty = 24;
    for (int r = 0; r < TERM_ROWS; r++) {
        ui_text(buf, w, h, 4, ty + r*10, t->lines[r], g_text_bright, 0, 0);
    }
    ui_text(buf, w, h, 4, h-14, "type cmd: help/date/clear", g_text_dim, 0, 0);
}

static void te_exec(struct term_state *t)
{
    te_printf(t, "$ ");
    if (strcmp(t->lines[t->row], "help") == 0 ||
        strcmp(t->lines[t->row], "$ help") == 0) {
        te_printf(t, " tiny-term: help, clear, uptime\n");
    } else if (strcmp(t->lines[t->row], "date") == 0) {
        char d[64];
        int i=0; d[i++]='2';d[i++]='0';d[i++]='2';d[i++]='6'; d[i++]=0;
        te_printf(t, d);
    } else if (strcmp(t->lines[t->row], "clear") == 0) {
        for (int i=0;i<TERM_ROWS;i++) memset(t->lines[i],0,TERM_COLS+1);
        t->row=0; t->col=0;
    } else {
        te_printf(t, " unknown command\n");
    }
}

static void te_key(struct window *win, char c)
{
    struct term_state *t = te_of(win);
    if (t == NULL) return;
    if (c == '\n') { te_printf(t, "\n"); te_exec(t); return; }
    if (c == 0x08 || c == 0x7F) { if (t->col > 0) { t->col--; t->lines[t->row][t->col]=0; } return; }
    if (c >= 0x20 && t->col < TERM_COLS-1) {
        t->lines[t->row][t->col] = c; t->col++;
    }
}

static void te_open(struct window *win)
{
    win->priv = (struct term_state *)(usize)pmm_alloc_run(2);
    struct term_state *t = (struct term_state *)win->priv;
    memset(t, 0, sizeof(*t));
    strcpy(t->lines[0], "Black OS terminal ready.");
}

const struct app app_terminal = {
    .name  = "terminal",
    .open  = te_open,
    .paint = te_paint,
    .key   = te_key,
    .close = NULL,
};

/* app_terminal defined above */