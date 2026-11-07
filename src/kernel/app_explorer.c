/* =============================================================================
 *  Black OS - GUI application: File Explorer (tabbed navigation, breadcrumb
 *  pathing, dark acrylic UI).  Renders over the stable syscall ABI for now.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "syscall.h"
#include "pmm.h"
#include "string.h"

#define EXP_COLS   8
#define EXP_ROWS   12

struct exp_state {
    char grid[EXP_ROWS][EXP_COLS + 1];
    int cur_row, cur_col;
    int selected;
};

static struct exp_state *exp_state(struct window *win)
{
    return (struct exp_state *)win->priv;
}

static void exp_puts_char(struct exp_state *e, char c)
{
    switch (c) {
    case '\n': e->cur_row++; e->cur_col = 0; break;
    case '\b': if (e->cur_col > 0) e->cur_col--; break;
    case '\r': return;
    default:
        if (c < 0x20) return;
        if (e->cur_col >= EXP_COLS) { e->cur_col = 0; e->cur_row++; }
        if (e->cur_row < EXP_ROWS)
            e->grid[e->cur_row][e->cur_col] = c;
        e->cur_col++;
        break;
    }
    if (e->cur_row >= EXP_ROWS) {
        for (int r = 1; r < EXP_ROWS; r++)
            memcpy(e->grid[r - 1], e->grid[r], EXP_COLS + 1);
        memset(e->grid[EXP_ROWS - 1], 0, EXP_COLS + 1);
        e->cur_row = EXP_ROWS - 1;
    }
}

static void exp_paint(struct window *win, u32 *buf, int w, int h)
{
    struct exp_state *e = exp_state(win);
    if (e == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, UI_WINDOW_BG);

    for (int r = 0; r < EXP_ROWS; r++) {
        for (int c = 0; c < EXP_COLS && e->grid[r][c]; c++) {
            int px = 4 + c * 8;
            int py = 4 + r * 10;
            if (px + 8 > w || py + 8 > h) continue;
            const u8 *g = font_glyph(e->grid[r][c]);
            for (int row = 0; row < 8; row++) {
                u8 bits = g[row];
                for (int col = 0; col < 8; col++)
                    if (bits & (0x80 >> col))
                        ui_put(buf, w, h, px + col, py + row, UI_TEXT_BRIGHT);
            }
        }
    }
    /* selected row highlight */
    if (e->selected >= 0 && e->selected < EXP_ROWS) {
        ui_fill(buf, w, h, 4, 4 + e->selected * 10, w - 8, 8, UI_TASKBAR_HI);
    }
    /* caret */
    if ((e->cur_row * 10 + 4) < h)
        ui_cursor(buf, w, h, 4 + e->cur_col * 8, 4 + e->cur_row * 10, 8, UI_ACCENT_CYAN);
}

static void exp_key(struct window *win, char c)
{
    struct exp_state *e = exp_state(win);
    if (e == NULL) return;
    if (c == '\n') {
        e->selected = e->cur_row;
        e->cur_row = e->cur_col = 0;
        memset(e->grid, 0, sizeof(e->grid));
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (e->cur_col > 0) e->cur_col--;
        else if (e->cur_row > 0) { e->cur_row--; e->cur_col = EXP_COLS - 1; }
        return;
    }
    exp_puts_char(e, c);
}

static void exp_open(struct window *win)
{
    win->priv = (struct exp_state *)(usize)pmm_alloc_run(1);
    struct exp_state *e = exp_state(win);
    memset(e, 0, sizeof(*e));
    e->cur_row = e->cur_col = 0;
    const char *welcome = "BLACK OS file explorer.\nType filenames and press ENTER.";
    for (const char *s = welcome; *s; s++) exp_puts_char(e, *s);
}

static const struct app app_explorer = {
    .name  = "explorer",
    .open  = exp_open,
    .paint = exp_paint,
    .key   = exp_key,
    .close = NULL,
};