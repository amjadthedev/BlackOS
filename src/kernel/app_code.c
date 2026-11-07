/* =============================================================================
 *  Black OS - app_code.c : Code editor / lightweight IDE
 *
 *  Opens text files from VFS, edits them in place, saves back to disk, and
 *  can "run" the file as a userspace task via the syscall ABI (ring 3).
 *  Supports syntax-aware coloring for C, JS, Python, and shell by extension.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "vfs.h"
#include "syscall.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"
#include "idt.h"
#include "themes.h"
#include "paging.h"

#define CODE_MAX_LINES  60
#define CODE_COLS       90
#define CODE_LINE_H     10

struct code_state {
    char  path[128];          /* file being edited                */
    char  lines[CODE_MAX_LINES][CODE_COLS + 1];
    int   line_count;
    int   scroll;             /* top visible line                 */
    int   cur_line, cur_col;  /* cursor                           */
    int   modified;           /* unsaved changes                  */
    int   run_result;         /* last run exit code               */
    u32   run_result_ms;      /* when run finished                */
};

static struct code_state *code_of(struct window *win)
{
    return (struct code_state *)win->priv;
}

static void strcpy_fixed(char *dst, const char *src);

static u32 syntax_color(const char *path, char c, int col)
{
    /* very naive per-extension coloring based on column position         */
    const char *ext = strrchr(path, '.');
    if (!ext)
        return UI_TEXT_BRIGHT;

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
        strcmp(ext, ".cpp") == 0 || strcmp(ext, ".js") == 0) {
        if (col < 10)
            return g_accent;                 /* keywords/preproc area        */
        return UI_TEXT_BRIGHT;
    }
    if (strcmp(ext, ".py") == 0) {
        if (col == 0)
            return 0xD42222;                 /* python-ish red               */
        return UI_TEXT_BRIGHT;
    }
    if (strcmp(ext, ".sh") == 0) {
        if (col == 0)
            return 0x2ED573;
        return UI_TEXT_BRIGHT;
    }
    return UI_TEXT_BRIGHT;
}

static void code_paint(struct window *win, u32 *buf, int w, int h)
{
    struct code_state *s = code_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, 0x0A0A12);   /* dark editor bg               */

    /* header: filename + status                                          */
    ui_text(buf, w, h, 6, 4, s->path[0] ? s->path : "untitled",
            g_text_bright, 1, 0);
    if (s->modified)
        ui_text(buf, w, h, 6 + (int)strlen(s->path) * 8 + 4, 4, "*",
                0xFFD700, 1, 0);

    int bar_x = 6 + (int)strlen(s->path) * 8 + 10;
    ui_text(buf, w, h, bar_x, 4, "LINES:", g_text_dim, 1, 0);
    {
        char num[8];
        u32 n = 0, v = s->line_count;
        if (v == 0) num[n++] = '0';
        while (v > 0) { num[n++] = (char)('0' + v % 10); v /= 10; }
        for (u32 i = 0; i < n / 2; i++) { char t = num[i]; num[i] = num[n-1-i]; num[n-1-i] = t; }
        num[n] = '\0';
        ui_text(buf, w, h, bar_x + 36, 4, num, g_text_bright, 1, 0);
    }

    /* line numbers gutter                                                */
    int gutter_w = 36;
    ui_fill(buf, w, h, 0, 22, gutter_w, h - 22 - 22, 0x05050A);

    /* text area                                                          */
    int tx = gutter_w + 6;
    int ty = 24;
    int visible = (h - 46) / CODE_LINE_H;

    for (int i = 0; i < visible && s->scroll + i < s->line_count; i++) {
        int ly = s->scroll + i;
        char num[8];
        u32 n = 0, v = ly + 1;
        if (v == 0) num[n++] = '0';
        while (v > 0) { num[n++] = (char)('0' + v % 10); v /= 10; }
        for (u32 k = 0; k < n / 2; k++) { char t = num[k]; num[k] = num[n-1-k]; num[n-1-k] = t; }
        num[n] = '\0';
        ui_text(buf, w, h, 8, ty + i * CODE_LINE_H, num,
                (ly == s->cur_line) ? g_accent : g_text_dim, 1, 0);

        const char *line = s->lines[ly];
        int cx = tx;
        for (int j = 0; line[j] && cx + 8 < w; j++, cx += 8) {
            u32 col = syntax_color(s->path, line[j], j);
            ui_put(buf, w, h, cx, ty + i * CODE_LINE_H, col);
            const u8 *g = font_glyph(line[j]);
            for (int row = 0; row < 8; row++) {
                u8 bits = g[row];
                for (int bit = 0; bit < 8; bit++)
                    if (bits & (0x80 >> bit))
                        ui_put(buf, w, h, cx + bit, ty + i * CODE_LINE_H + row, col);
            }
        }
    }

    /* cursor                                                             */
    if (s->cur_line >= s->scroll && s->cur_line < s->scroll + visible) {
        int cy = ty + (s->cur_line - s->scroll) * CODE_LINE_H;
        int cx = tx + s->cur_col * 8;
        if (cx + 8 < w && cy + 8 < h)
            ui_fill(buf, w, h, cx, cy, 2, 8, UI_TEXT_BRIGHT);
    }

    /* run result badge                                                   */
    if (s->run_result_ms && uptime_ms - s->run_result_ms < 3000) {
        char badge[32];
        u32 n = 0;
        badge[n++] = 'R'; badge[n++] = ':'; badge[n++] = ' ';
        u32 v = s->run_result;
        if (v == 0) badge[n++] = '0';
        while (v > 0) { badge[n++] = (char)('0' + v % 10); v /= 10; }
        for (u32 i = 0; i < n - 3; i++) {}
        /* badge written backwards above is wrong, rewrite simply:        */
        n = 3;
        v = s->run_result;
        char tmp[8]; int tn = 0;
        if (v == 0) tmp[tn++] = '0';
        while (v > 0) { tmp[tn++] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < tn / 2; i++) { char t = tmp[i]; tmp[i] = tmp[tn-1-i]; tmp[tn-1-i] = t; }
        tmp[tn] = '\0';
        for (int i = 0; tmp[i]; i++) badge[n++] = tmp[i];
        badge[n] = '\0';
        ui_fill(buf, w, h, w - 120, 4, 114, 20, 0x1A2A44);
        ui_text(buf, w, h, w - 114, 10, badge, g_accent, 1, 0);
    }

    ui_text(buf, w, h, 6, h - 14,
            "Ctrl+S save | Ctrl+O open | Ctrl+R run | Ctrl+Q quit",
            g_text_dim, 1, 0);
}

static int code_load_file(struct code_state *s, const char *path)
{
    vfs_handle h = vfs_lookup(path);
    if (h < 0)
        return -1;
    u64 fsize = vfs_size(h);
    if (fsize == 0 || fsize > 64 * 1024)
        return -1;

    u8 *buf = (u8 *)(usize)pmm_alloc_run((fsize + 4095) / 4096);
    if (buf == NULL)
        return -1;
    u64 got = vfs_read(h, buf, fsize, 0);

    memset(s->lines, 0, sizeof(s->lines));
    s->line_count = 0;
    int col = 0;
    int li = 0;
    for (u64 i = 0; i < got && li < CODE_MAX_LINES; i++) {
        char c = (char)buf[i];
        if (c == '\n') {
            s->lines[li][col] = '\0';
            li++;
            col = 0;
        } else if (c == '\r') {
            /* skip */
        } else if (col < CODE_COLS) {
            s->lines[li][col++] = c;
        }
    }
    if (col > 0 || li == 0) {
        s->lines[li][col] = '\0';
        li++;
    }
    s->line_count = li;
    s->cur_line = 0;
    s->cur_col = 0;
    s->scroll = 0;
    s->modified = 0;
    strncpy(s->path, path, sizeof(s->path) - 1);
    s->path[sizeof(s->path) - 1] = '\0';

    pmm_free_run((u64)(usize)buf, (fsize + 4095) / 4096);
    return 0;
}

static int code_save_file(struct code_state *s)
{
    if (s->path[0] == '\0')
        return -1;
    vfs_handle h = vfs_open(s->path);
    if (h < 0)
        return -1;

    /* build flat buffer                                                    */
    static u8 flat[64 * 1024];
    u32 off = 0;
    for (int i = 0; i < s->line_count; i++) {
        const char *line = s->lines[i];
        while (*line && off < sizeof(flat) - 2)
            flat[off++] = *line++;
        flat[off++] = '\n';
    }
    u64 wrote = vfs_write(h, flat, off);
    s->modified = (wrote != off);
    return s->modified ? -1 : 0;
}

static void code_run_file(struct window *win, struct code_state *s)
{
    if (s->path[0] == '\0')
        return;

    /* Read the file from VFS into a PMM frame                            */
    vfs_handle h = vfs_lookup(s->path);
    if (h < 0)
        return;
    u64 fsize = vfs_size(h);
    if (fsize == 0 || fsize > 2 * 1024 * 1024)
        return;

    u8 *blob = (u8 *)(usize)pmm_alloc_run((fsize + 4095) / 4096);
    if (blob == NULL)
        return;
    u64 got = vfs_read(h, blob, fsize, 0);

    /* Map into user VA window and run in ring 3                          */
    u64 rc = syscall_run_task(blob, got, 0, 0);
    s->run_result = (u32)rc;
    s->run_result_ms = uptime_ms;

    pmm_free_run((u64)(usize)blob, (fsize + 4095) / 4096);
}

static void code_key(struct window *win, char c)
{
    struct code_state *s = code_of(win);
    if (s == NULL) return;

    if (c == 0x1b) {  /* ESC */
        /* handled by shell close */
        return;
    }
    if (c == '\n') {
        /* insert newline at cursor                                       */
        if (s->line_count < CODE_MAX_LINES - 1) {
            for (int i = s->line_count; i > s->cur_line; i--)
                strcpy_fixed(s->lines[i], s->lines[i - 1]);
            memset(s->lines[s->cur_line + 1], 0, CODE_COLS + 1);
            s->line_count++;
            s->cur_line++;
            s->cur_col = 0;
            s->modified = 1;
        }
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (s->cur_col > 0) {
            s->cur_col--;
            s->lines[s->cur_line][s->cur_col] = '\0';
            s->modified = 1;
        } else if (s->cur_line > 0) {
            /* merge with previous line                                   */
            int prev = s->cur_line - 1;
            int plen = (int)strlen(s->lines[prev]);
            int clen = (int)strlen(s->lines[s->cur_line]);
            if (plen + clen < CODE_COLS) {
                strcpy_fixed(s->lines[prev] + plen, s->lines[s->cur_line]);
                for (int i = s->cur_line; i < s->line_count - 1; i++)
                    strcpy_fixed(s->lines[i], s->lines[i + 1]);
                memset(s->lines[s->line_count - 1], 0, CODE_COLS + 1);
                s->line_count--;
                s->cur_line = prev;
                s->cur_col = plen;
                s->modified = 1;
            }
        }
        return;
    }
    if (c >= 0x20 && s->cur_col < CODE_COLS - 1) {
        s->lines[s->cur_line][s->cur_col++] = c;
        s->lines[s->cur_line][s->cur_col] = '\0';
        s->modified = 1;
        return;
    }
}

/* fixed strcpy without libc dependency                                   */
static void strcpy_fixed(char *dst, const char *src)
{
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

static void code_open(struct window *win)
{
    win->priv = (struct code_state *)(usize)pmm_alloc_run(1);
    struct code_state *s = code_of(win);
    memset(s, 0, sizeof(*s));
    s->line_count = 1;
    s->lines[0][0] = '\0';
}

static const struct app app_code = {
    .name  = "code editor",
    .open  = code_open,
    .paint = code_paint,
    .key   = code_key,
    .close = NULL,
};
