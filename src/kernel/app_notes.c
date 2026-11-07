/* =============================================================================
 *  Black OS - app_notes.c : small text notepad
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "string.h"
#include "pmm.h"
#include "themes.h"
#include "keyboard.h"
#include "string.h"

struct notes_state {
    char buf[1024];
    char file[64];
    int  len;
};

static struct notes_state *ns_of(struct window *win)
{
    return (struct notes_state *)win->priv;
}

static void notes_paint(struct window *win, u32 *buf, int w, int h)
{
    struct notes_state *s = ns_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);
    ui_text(buf, w, h, 12, 8, "Notes", g_accent, 0, 0);
    ui_text(buf, w, h, w-110, 8, s->file, g_text_dim, 0, 0);

    int lx = 12, ly = 30, lw = w - 24, lh = h - 58;
    ui_fill(buf, w, h, lx, ly, lw, lh, 0x0A1015);
    ui_fill(buf, w, h, lx, ly, lw, 1, g_panel_border);
    ui_fill(buf, w, h, lx, ly+lh-1, lw, 1, g_panel_border);
    ui_fill(buf, w, h, lx, ly, 1, lh, g_panel_border);
    ui_fill(buf, w, h, lx+lw-1, ly, 1, lh, g_panel_border);

    ui_text(buf, w, h, lx+2, ly+2, s->buf, g_text_bright, 0, 0);
    ui_text(buf, w, h, 12, h-16, "arrows:scroll | Ctrl+S save (?) | ESC close", g_text_dim, 0, 0);
}

static void notes_key(struct window *win, char c)
{
    struct notes_state *s = ns_of(win);
    if (s == NULL) return;
    if (c == KBD_ESCAPE) return;
    if (c == '\n') {
        if (s->len < (int)sizeof(s->buf)-1) {
            s->buf[s->len++] = '\n';
            s->buf[s->len] = '\0';
        }
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (s->len > 0) s->len--;
        return;
    }
    if (c >= ' ' && s->len < (int)sizeof(s->buf)-1) {
        s->buf[s->len++] = c;
        s->buf[s->len] = '\0';
    }
}

static void notes_open(struct window *win)
{
    win->priv = (struct notes_state *)(usize)pmm_alloc_run(1);
    struct notes_state *s = (struct notes_state *)win->priv;
    memset(s, 0, sizeof(*s));
    strcpy(s->buf, "Type your notes here...");
    s->len = strlen(s->buf);
    strcpy(s->file, "untitled.txt");
}

const struct app app_notes = {
    .name  = "notes",
    .open  = notes_open,
    .paint = notes_paint,
    .key   = notes_key,
    .close = NULL,
};

/* app_notes already defined above */
