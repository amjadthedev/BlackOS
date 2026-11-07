/* =============================================================================
 *  Black OS - GUI application: Editor (simple text editor with save/clear).
 *
 *  Uses a single PMM frame for the text grid.  Keyboard input goes into a
 *  line buffer; ENTER confirms the "command" line, ESC cancels.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "syscall.h"
#include "pmm.h"
#include "string.h"

#define ED_COLS   70
#define ED_ROWS   20

struct ed_state {
    char grid[ED_ROWS][ED_COLS + 1];
    char line[64];
    int cur_row, cur_col;
    int line_len;
};

static struct ed_state *ed_state(struct window *win)
{
    return (struct ed_state *)win->priv;
}

static void ed_puts_char(struct ed_state *e, char c)
{
    if (c == '\n') {
        e->cur_row++; e->cur_col = 0;
        if (e->cur_row >= ED_ROWS) {
            for (int r = 1; r < ED_ROWS; r++)
                memcpy(e->grid[r - 1], e->grid[r], ED_COLS + 1);
            memset(e->grid[ED_ROWS - 1], 0, ED_COLS + 1);
            e->cur_row = ED_ROWS - 1;
        }
        return;
    }
    if (c == '\b') {
        if (e->cur_col > 0) e->cur_col--;
        else if (e->cur_row > 0) { e->cur_row--; e->cur_col = ED_COLS - 1; }
        return;
    }
    if (c < 0x20) return;
    if (e->cur_col >= ED_COLS) { e->cur_col = 0; e->cur_row++; }
    if (e->cur_row < ED_ROWS)
        e->grid[e->cur_row][e->cur_col] = c;
    e->cur_col++;
    if (e->cur_row >= ED_ROWS) {
        for (int r = 1; r < ED_ROWS; r++)
            memcpy(e->grid[r - 1], e->grid[r], ED_COLS + 1);
        memset(e->grid[ED_ROWS - 1], 0, ED_COLS + 1);
        e->cur_row = ED_ROWS - 1;
    }
}

static void ed_paint(struct window *win, u32 *buf, int w, int h)
{
    struct ed_state *e = ed_state(win);
    if (e == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, UI_WINDOW_BG);

    for (int r = 0; r < ED_ROWS; r++) {
        for (int c = 0; c < ED_COLS && e->grid[r][c]; c++) {
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
    /* prompt line */
    ui_text(buf, w, h, 4, ED_ROWS * 10 + 2, "EDIT>", UI_TEXT_DIM, 1, 0);
    ui_text(buf, w, h, 4 + 6 * 8, ED_ROWS * 10 + 2, e->line, UI_TEXT_BRIGHT, 1, 0);
    /* caret */
    if ((e->cur_row * 10 + 12) < h)
        ui_cursor(buf, w, h, 4 + e->cur_col * 8, ED_ROWS * 10 + 2, 8, UI_ACCENT_CYAN);
}

static void ed_key(struct window *win, char c)
{
    struct ed_state *e = ed_state(win);
    if (e == NULL) return;
    if (c == '\n') {
        /* "save": pretend we wrote it, then clear */
        e->cur_row = e->cur_col = 0;
        memset(e->grid, 0, sizeof(e->grid));
        memset(e->line, 0, sizeof(e->line));
        return;
    }
    if (c == 0x1b) { ESC:           /* ESC cancels */
        e->cur_row = e->cur_col = 0;
        memset(e->grid, 0, sizeof(e->grid));
        memset(e->line, 0, sizeof(e->line));
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (e->line_len > 0) {
            e->line_len--;
            e->line[e->line_len] = '\0';
            ed_puts_char(e, '\b');
        }
        return;
    }
    if (c < 0x20) return;
    if (e->line_len >= (int)sizeof(e->line) - 1) return;
    e->line[e->line_len++] = c;
    e->line[e->line_len] = '\0';
    ed_puts_char(e, c);
}

static void ed_open(struct window *win)
{
    win->priv = (struct ed_state *)(usize)pmm_alloc_run(1);
    struct ed_state *e = ed_state(win);
    memset(e, 0, sizeof(*e));
    e->cur_row = e->cur_col = 0;
}

const struct app app_editor = {
    .name  = "editor",
    .open  = ed_open,
    .paint = ed_paint,
    .key   = ed_key,
    .close = NULL,
};