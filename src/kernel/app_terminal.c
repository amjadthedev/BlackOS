/* =============================================================================
 *  Black OS - GUI application: Terminal (text console inside a window)
 *  Rendered from an owned text grid; commands run through the syscall ABI.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "syscall.h"
#include "pmm.h"
#include "string.h"

#define TERM_COLS   64
#define TERM_ROWS   30

struct term_state {
    char grid[TERM_ROWS][TERM_COLS + 1];
    char line[64];
    int  cur_row, cur_col;
    int  line_len;
};

static struct term_state *term_state(struct window *win)
{
    return (struct term_state *)win->priv;
}

static void term_scroll(struct term_state *t)
{
    for (int r = 1; r < TERM_ROWS; r++)
        memcpy(t->grid[r - 1], t->grid[r], TERM_COLS + 1);
    memset(t->grid[TERM_ROWS - 1], 0, TERM_COLS + 1);
    t->cur_row = TERM_ROWS - 1;
}

static void term_write(struct term_state *t, const char *s)
{
    for (; *s; s++) {
        char c = *s;
        if (c == '\n') {
            t->cur_col = 0;
            t->cur_row++;
            if (t->cur_row >= TERM_ROWS)
                term_scroll(t);
            continue;
        }
        if (c == '\b') {
            if (t->cur_col > 0)
                t->cur_col--;
            else if (t->cur_row > 0) {
                t->cur_row--;
                t->cur_col = TERM_COLS - 1;
            }
            if (t->cur_row >= 0)
                t->grid[t->cur_row][t->cur_col] = '\0';
            continue;
        }
        if (c < 0x20)
            continue;
        if (t->cur_col >= TERM_COLS) {
            t->cur_col = 0;
            t->cur_row++;
            if (t->cur_row >= TERM_ROWS)
                term_scroll(t);
        }
        t->grid[t->cur_row][t->cur_col] = c;
        t->cur_col++;
    }
}

static void term_prompt(struct term_state *t)
{
    term_write(t, "\n[black@blackos /]$ ");
}

/* binary number-to-text (the idiom used in this codebase) */
static void term_num(struct term_state *t, u64 v)
{
    char buf[20];
    int i = 0;
    if (v == 0)
        buf[i++] = '0';
    while (v > 0) {
        buf[i++] = (char)('0' + (int)(v % 10ULL));
        v /= 10ULL;
    }
    while (i > 0)
        term_write(t, &(char){ buf[--i] });
}

static void term_exec(struct window *win, char *line)
{
    struct term_state *t = term_state(win);

    if (strcmp(line, "help") == 0) {
        term_write(t, "\ncommands: help clear uptime mem tasks echo <x>");
    } else if (strcmp(line, "clear") == 0) {
        memset(t->grid, 0, sizeof(t->grid));
        t->cur_row = t->cur_col = 0;
        return;                         /* fresh screen, no prompt line */
    } else if (strcmp(line, "uptime") == 0) {
        term_write(t, "\nuptime ");
        term_num(t, sys_uptime_ms() / 1000);
        term_write(t, " s");
    } else if (strcmp(line, "mem") == 0) {
        term_write(t, "\nRAM free frames: ");
        term_num(t, sys_mem_frames_free());
        term_write(t, "  /  ");
        term_num(t, sys_mem_frames_total());
    } else if (strcmp(line, "tasks") == 0) {
        term_write(t, "\nopen tasks: ");
        term_num(t, sys_task_count());
        for (u32 i = 0; i < sys_task_count(); i++) {
            struct sys_task st;
            if (sys_task_get(i, &st) != 0)
                continue;
            term_write(t, "\n  pid ");
            term_num(t, st.pid);
            term_write(t, "  ");
            term_write(t, st.name);
        }
    } else if (strncmp(line, "echo ", 5) == 0) {
        term_write(t, "\n");
        term_write(t, line + 5);
    } else if (line[0] != '\0') {
        term_write(t, "\nunknown command try 'help'");
    }
    term_prompt(t);
}

static void term_open(struct window *win)
{
    win->priv = (struct term_state *)(usize)pmm_alloc_run(1);
    struct term_state *t = term_state(win);

    /* 30 * 65 + 64 + 3 * 4 == 2116 bytes: comfortably inside one frame.    */
    memset(t, 0, sizeof(*t));
    t->cur_row = t->cur_col = 0;

    term_write(t, "BLACK OS terminal - type and press ENTER.");
    term_prompt(t);
}

static void term_paint(struct window *win, u32 *buf, int w, int h)
{
    struct term_state *t = term_state(win);
    if (t == NULL)
        return;

    ui_fill(buf, w, h, 0, 0, w, h, UI_WINDOW_BG);

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int cidx = 0; cidx < TERM_COLS && t->grid[r][cidx]; cidx++) {
            char c = t->grid[r][cidx];
            int px = 8 + cidx * 8;
            int py = 4 + r * 10;
            if (px + 8 > w || py + 8 > h)
                continue;
            const u8 *g = font_glyph(c);
            for (int row = 0; row < 8; row++) {
                u8 bits = g[row];
                for (int col = 0; col < 8; col++)
                    if (bits & (0x80 >> col))
                        ui_put(buf, w, h, px + col, py + row, UI_TEXT_BRIGHT);
            }
        }
    }
    /* caret */
    if ((t->cur_row * 10 + 4) < h)
        ui_cursor(buf, w, h, 8 + t->cur_col * 8, 4 + t->cur_row * 10, 8, UI_ACCENT_CYAN);
}

static void term_key(struct window *win, char c)
{
    struct term_state *t = term_state(win);
    if (t == NULL)
        return;

    if (c == '\n') {
        char line[64];
        memset(line, 0, sizeof(line));
        memcpy(line, t->line, sizeof(line) - 1);
        t->line[0] = '\0';
        t->line_len = 0;
        term_exec(win, line);
        return;
    }

    if (c == 0x08) {                    /* backspace                        */
        if (t->line_len > 0) {
            t->line_len--;
            t->line[t->line_len] = '\0';
            term_write(t, "\b");
        }
        return;
    }

    if (c < 0x20 || t->line_len >= (int)sizeof(t->line) - 1)
        return;

    t->line[t->line_len++] = c;
    t->line[t->line_len] = '\0';
    term_write(t, &(char){ c });
}

const struct app app_terminal = {
    .name  = "terminal",
    .open  = term_open,
    .paint = term_paint,
    .key   = term_key,
    .close = NULL,
};
