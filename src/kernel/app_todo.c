/* =============================================================================
 *  Black OS - app_todo.c : clean todo list with checkbox styling
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "string.h"
#include "pmm.h"
#include "themes.h"
#include "keyboard.h"

struct todo_state {
    char items[32][64];
    int  done[32];
    int  count;
    int  sel;
    int  scroll;
    int  editing;
    char draft[64];
    int  draft_len;
};

static struct todo_state *td_of(struct window *win) { return (struct todo_state *)win->priv; }

static void td_ensure(struct todo_state *s)
{
    if (s == NULL) return;
    if (s->count == 0) {
        const char **items = (const char*[]) { "Walk the dog", "Buy milk", "Review PR", "Call mom", 0 };
        /* Static strings always include a 0 terminators; build N slack */
        const char *def_items[] = { "Walk the dog", "Buy milk", "Review PR", "Call mom", NULL };
        int i = 0;
        while (i < 4) {
            int k = 0; while (def_items[i] && def_items[i][k] && k < 63) { s->items[i][k] = def_items[i][k]; k++; }
            s->items[i][k] = '\0';
            s->done[i] = 0;
            i++;
            s->count = i;
        }
    }
}

static void td_paint(struct window *win, u32 *buf, int w, int h)
{
    struct todo_state *s = td_of(win);
    if (s == NULL) return;
    td_ensure(s);
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);
    ui_text(buf, w, h, 12, 8, "Todo", g_accent, 0, 0);
    ui_text(buf, w, h, 80, 8, "items:", g_text_dim, 0, 0);
    char nbuf[8];
    int nn = 0, v = s->count;
    if (v == 0) nbuf[nn++] = '0';
    while (v > 0) { nbuf[nn++] = '0' + v % 10; v /= 10; }
    for (int i = 0; i < nn / 2; i++) { char t = nbuf[i]; nbuf[i] = nbuf[nn-1-i]; nbuf[nn-1-i] = t; }
    nbuf[nn] = 0;
    ui_text(buf, w, h, 140, 8, nbuf, g_text_bright, 0, 0);

    /* list */
    int y = 30;
    for (int i = s->scroll; i < s->count && y + 16 < h - 40; i++, y += 16) {
        u32 bg = (i == s->sel) ? g_btn_hover : 0x0a0c12;
        ui_fill(buf, w, h, 12, y, w - 24, 14, bg);
        /* checkbox */
        ui_fill(buf, w, h, 18, y + 2, 10, 10, s->done[i] ? g_accent : 0x1a1c1c);
        ui_fill(buf, w, h, 19, y + 3, 8, 8, g_panel_bg);
        if (s->done[i]) ui_text(buf, w, h, 21, y + 2, "x", g_text_bright, 0, 0);
        ui_text(buf, w, h, 34, y + 2, s->items[i], s->done[i] ? g_text_dim : g_text_bright, 0, 0);
    }

    /* draft input */
    int by = h - 32;
    ui_fill(buf, w, h, 12, by, w - 24, 22, 0x0E182A);
    ui_fill(buf, w, h, 12, by, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, by + 21, w - 24, 1, g_panel_border);
    ui_text(buf, w, h, 18, by + 7, s->editing ? ": " : "+ new task ", g_text_dim, 0, 0);
    ui_text(buf, w, h, 18 + 80, by + 7, s->draft, g_text_bright, 0, 0);
    ui_text(buf, w, h, 12, h - 14, "Enter add | Space toggle | Del delete", g_text_dim, 0, 0);
}

static void td_key(struct window *win, char c)
{
    struct todo_state *s = td_of(win);
    if (s == NULL) return;
    if (s->editing) {
        if (c == '\n' || c == '\r') {
            if (s->draft_len > 0 && s->count < 32) {
                int k = 0; while (s->draft[k] && k < 63) { s->items[s->count][k] = s->draft[k]; k++; }
                s->items[s->count][k] = '\0';
                s->done[s->count] = 0;
                s->count++;
                s->draft_len = 0;
                s->draft[0] = '\0';
            }
            s->editing = 0;
            return;
        }
        if (c == 0x08 || c == 0x7F) { if (s->draft_len > 0) s->draft_len--; s->draft[s->draft_len] = '\0'; return; }
        if (c >= ' ' && s->draft_len < 63) {
            s->draft[s->draft_len++] = c; s->draft[s->draft_len] = '\0';
        }
        return;
    }
    if (c == 'n' || c == 'N') { s->editing = 1; return; }
    if (c == ' ') { if (s->sel >= 0 && s->sel < s->count) s->done[s->sel] = !s->done[s->sel]; }
    if (c == KBD_UP) { if (s->sel > 0) s->sel--; }
    if (c == KBD_DOWN) { if (s->sel < s->count - 1) s->sel++; }
    if (c == 0x7F || c == 0x08) {
        if (s->sel >= 0 && s->sel < s->count) {
            int i = s->sel;
            for (; i < s->count - 1; i++) {
                memcpy(s->items[i], s->items[i+1], 64);
                s->done[i] = s->done[i+1];
            }
            s->count--; if (s->sel >= s->count) s->sel = (s->count ? s->count - 1 : 0);
        }
    }
}

static void td_open(struct window *win)
{
    win->priv = (struct todo_state *)(usize)pmm_alloc_run(1);
    struct todo_state *s = (struct todo_state *)win->priv;
    memset(s, 0, sizeof(*s));
    s->sel = 0;
    td_ensure(s);
}

const struct app app_todo = {
    .name  = "todo",
    .open  = td_open,
    .paint = td_paint,
    .key   = td_key,
    .close = NULL,
};
