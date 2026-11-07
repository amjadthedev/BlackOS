/* =============================================================================
 *  Black OS - app_calc.c : in-window calculator
 *
 *  Small desktop calculator: digits, + - * /, '=' to compute, 'C' clear.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "string.h"
#include "pmm.h"
#include "themes.h"

struct calc_state {
    char  buf[64];
    int   buf_len;
    int   acc;
    char  op;
    int   result_ready;
};

static struct calc_state *calc_of(struct window *win)
{
    return (struct calc_state *)win->priv;
}

static int parse_buf(struct calc_state *s)
{
    int sign = 1, v = 0, i = 0;
    if (!s->buf[0]) return 0;
    if (s->buf[0] == '-') { sign = -1; i = 1; }
    for (; s->buf[i] && s->buf[i] >= '0' && s->buf[i] <= '9'; i++)
        v = v * 10 + (s->buf[i] - '0');
    return sign * v;
}

static void apply_pending(struct calc_state *s, char next_op)
{
    int v = parse_buf(s);
    switch (s->op) {
        case '+': s->acc += v; break;
        case '-': s->acc -= v; break;
        case '*': s->acc *= v; break;
        case '/': if (v != 0) s->acc /= v; break;
        default:  s->acc = v; break;
    }
    s->op = next_op;
    s->buf_len = 0;
    s->buf[0] = '\0';
    s->result_ready = 1;
}

static void calc_paint(struct window *win, u32 *buf, int w, int h)
{
    struct calc_state *s = calc_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);
    ui_text(buf, w, h, 12, 8, "Calculator", g_accent, 0, 0);

    ui_fill(buf, w, h, 12, 30, w - 24, 28, 0x0E182A);
    ui_fill(buf, w, h, 12, 30, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 30 + 28 - 1, w - 24, 1, g_panel_border);
    ui_fill(buf, w, h, 12, 30, 1, 28, g_panel_border);
    ui_fill(buf, w, h, w - 13, 30, 1, 28, g_panel_border);

    char str[48];
    int si = 0;
    if (s->result_ready) {
        int av = s->acc < 0 ? -s->acc : s->acc;
        char rev[32]; int rn = 0;
        if (!av) rev[rn++] = '0';
        while (av > 0) { rev[rn++] = (char) ('0' + av % 10); av /= 10; }
        for (int i = 0; i < rn / 2; i++) { char t = rev[i]; rev[i] = rev[rn - 1 - i]; rev[rn - 1 - i] = t; }
        rev[rn] = '\0';
        ui_text(buf, w, h, 18, 36, rev, g_text_bright, 0, 0);
    } else {
        if (s->op) str[si++] = s->op;
        int k = 0;
        while (s->buf[k] && si < 47) str[si++] = s->buf[k++];
        str[si] = '\0';
        if (!str[0]) str[0] = '0';
        ui_text(buf, w, h, 18, 36, str, g_text_bright, 0, 0);
    }

    int gx = 12, gy = 62, gw = w - 24, gh = h - 80;
    int cellW = gw / 4, cellH = gh / 5;
    const char *labels[16] = {
        "7","8","9","/",
        "4","5","6","*",
        "1","2","3","-",
        "0",".","=","+",
    };
    for (int i = 0; i < 16; i++) {
        const char *lbl = labels[i];
        int bx = gx + (i % 4) * cellW;
        int by = gy + (i / 4) * cellH;
        ui_fill(buf, w, h, bx, by, cellW - 2, cellH - 2, g_btn_hover);
        ui_text(buf, w, h, bx + 4, by + cellH / 2 - 4, lbl, g_text_bright, 0, 0);
    }
    ui_text(buf, w, h, 12, h - 18, "digits=0..9 C=clear +/- */= compute", g_text_dim, 0, 0);
}

static void calc_key(struct window *win, char c)
{
    struct calc_state *s = calc_of(win);
    if (c >= '0' && c <= '9') {
        if (s->buf_len < 63) {
            s->buf[s->buf_len++] = c;
            s->buf[s->buf_len] = '\0';
            s->result_ready = 0;
        }
        return;
    }
    if (c == '+' || c == '-' || c == '*' || c == '/') {
        apply_pending(s, c);
        return;
    }
    if (c == '=' || c == '\n' || c == '\r') {
        apply_pending(s, 0);
        s->op = 0;
        return;
    }
    if (c == 'c' || c == 'C') {
        s->acc = 0;
        s->buf_len = 0;
        s->op = 0;
        s->result_ready = 0;
        s->buf[0] = '\0';
    }
}

static void calc_open(struct window *win)
{
    win->priv = (struct calc_state *)(usize)pmm_alloc_run(1);
    struct calc_state *s = (struct calc_state *)win->priv;
    memset(s, 0, sizeof(*s));
}

const struct app app_calc = {
    .name  = "calculator",
    .open  = calc_open,
    .paint = calc_paint,
    .key   = calc_key,
    .close = NULL,
};

/* app_calc defined above as a normal extern app */
