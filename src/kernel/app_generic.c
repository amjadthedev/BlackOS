/* =============================================================================
 *  Black OS - app_generic.c : universal app/game surface wrapper
 *
 *  Provides a lightweight shell for any app that hasn't been materialized
 *  as a dedicated app_*.c module.  Paints a color-coded surface, echo keys,
 *  and reports runtime identity.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "themes.h"
#include "string.h"
#include "pmm.h"
#include "keyboard.h"
#include "string.h"
struct generic_state {
    char   label[64];
    char   buffer[256];
    int    len;
    u32    accent_seed;
    int    cursor;
    int    score;
};

static struct generic_state *gen_of(struct window *win)
{
    return (struct generic_state *)win->priv;
}

static void generic_paint(struct window *win, u32 *buf, int w, int h)
{
    struct generic_state *s = gen_of(win);
    if (s == NULL) return;
    u32 bg = g_panel_bg;
    if (s->accent_seed == 0) s->accent_seed = g_accent;
    ui_fill(buf, w, h, 0, 0, w, h, bg);
    ui_text(buf, w, h, 12, 8, win->title[0] ? win->title : "Generic app", s->accent_seed, 0, 0);
    /* center card */
    int cx = 14, cy = 32, cw = w - 28, ch = h - 64;
    ui_fill(buf, w, h, cx, cy, cw, ch, 0x0a0c12);
    ui_fill(buf, w, h, cx, cy, cw, 1, g_panel_border);
    ui_fill(buf, w, h, cx, cy+ch-1, cw, 1, g_panel_border);
    ui_fill(buf, w, h, cx, cy+1, 1, ch-2, g_panel_border);
    ui_fill(buf, w, h, cx+cw-1, cy+1, 1, ch-2, g_panel_border);

    char status[64];
    int i = 0;
    const char *core = "Typed: ";
    for (int j=0; core[j] && i<63; j++) status[i++] = core[j];
    if (s->len > 0) {
        int max = 40; int l = s->len;
        if (l > max) l = max;
        int off = s->len - l;
        for (int m=0; m<l && i<63; m++) status[i++] = s->buffer[off+m];
    } else {
        const char *empty = "press any key";
        for (int j=0; empty[j] && i<63; j++) status[i++] = empty[j];
    }
    status[i] = '\0';
    ui_text(buf, w, h, cx+8, cy+10, status, g_text_bright, 0, 0);

    /* score / hint */
    char score[64];
    int sn=0; score[sn]='s'; score[sn+1]='c'; score[sn+2]='o'; score[sn+3]='r'; score[sn+4]='e'; score[sn+5]=':';
    sn=6;
    for (int j=0; j<4; j++) score[sn+j] = (char)('0' + (s->score>>(j*4)&15));
    score[sn+4]=0;
    ui_text(buf, w, h, cx+8, cy+30, score, g_text_dim, 0, 0);
    ui_text(buf, w, h, 12, h-16, "arrows move, letters echo, Enter clears", g_text_dim, 0, 0);
}

static void generic_key(struct window *win, char c)
{
    struct generic_state *s = gen_of(win);
    if (s == NULL) return;
    if (c == '\n' || c == '\r') {
        s->len = 0;
        s->buffer[0] = '\0';
        return;
    }
    if (c == 0x08 || c == 0x7F) { if (s->len > 0) s->len--; return; }
    if (c >= 'A' && c <= 'Z') s->accent_seed = (s->accent_seed*31 + (u32)c) & 0x00FFFFFF;
    if (c >= 'a' && c <= 'z') s->accent_seed = (s->accent_seed*31 + (u32)c) & 0x00FFFFFF;
    if (c >= 0x20 && s->len < 255) {
        s->buffer[s->len++] = c;
        s->buffer[s->len] = '\0';
        return;
    }
    if (c == KBD_UP) {
        s->cursor++;
        s->score++;
    } else if (c == KBD_DOWN) {
        if (s->cursor > 0) s->cursor--;
    }
}

static void generic_open(struct window *win)
{
    win->priv = (struct generic_state *)(usize)pmm_alloc_run(1);
    struct generic_state *s = (struct generic_state *)win->priv;
    memset(s, 0, sizeof(*s));
    s->accent_seed = g_accent;
    if (win->title[0]) {
        int i = 0;
        while (i < 63 && win->title[i]) { s->label[i] = win->title[i]; i++; }
        s->label[i] = '\0';
    } else {
        strcpy(s->label, "generic");
        s->label[7] = '\0';
    }
}

static void generic_close(struct window *win)
{
    if (win->priv != NULL) {
        /* win already frees at win_close; just detach */
        win->priv = NULL;
    }
}

const struct app app_generic = {
    .name  = "generic app",
    .open  = generic_open,
    .paint = generic_paint,
    .key   = generic_key,
    .close = generic_close,
};

/* app_generic defined above */
