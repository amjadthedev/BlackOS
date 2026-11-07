/* =============================================================================
 *  Black OS - Phase 8: widget library (Win32-flavoured bevel aesthetic)
 * ========================================================================== */
#include "ui.h"
#include "font.h"
#include "string.h"

void ui_put(u32 *buf, int bw, int bh, int x, int y, u32 c)
{
    if (x < 0 || y < 0 || x >= bw || y >= bh)
        return;
    buf[y * bw + x] = c;
}

void ui_fill(u32 *buf, int bw, int bh, int x, int y, int w, int h, u32 c)
{
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            ui_put(buf, bw, bh, x + col, y + row, c);
}

static void hline(u32 *buf, int bw, int bh, int x, int y, int w, u32 c)
{
    ui_fill(buf, bw, bh, x, y, w, 1, c);
}

static void vline(u32 *buf, int bw, int bh, int x, int y, int h, u32 c)
{
    ui_fill(buf, bw, bh, x, y, 1, h, c);
}

void ui_text(u32 *buf, int bw, int bh, int x, int y, const char *s,
             u32 fg, int transparent, u32 bg)
{
    for (; *s; s++) {
        const u8 *g = font_glyph(*s);
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col))
                    ui_put(buf, bw, bh, x + col, y + row, fg);
                else if (!transparent)
                    ui_put(buf, bw, bh, x + col, y + row, bg);
            }
        }
        x += 8;
    }
}

/* Text clipped inside a box of `w` pixels ("label" widgets). */
void ui_text_box(u32 *buf, int bw, int bh, int x, int y, int w,
                 const char *s, u32 fg, u32 bg)
{
    int i = 0;
    while (s[i] && (i + 1) * 8 <= w) {
        const u8 *g = font_glyph(s[i]);
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col))
                    ui_put(buf, bw, bh, x + i * 8 + col, y + row, fg);
                else if (bg)
                    ui_put(buf, bw, bh, x + i * 8 + col, y + row, bg);
            }
        }
        i++;
    }
}

void ui_panel(u32 *buf, int bw, int bh, int x, int y, int w, int h, u32 fill)
{
    ui_fill(buf, bw, bh, x, y, w, h, fill);
}

void ui_bevel_raised(u32 *buf, int bw, int bh, int x, int y, int w, int h,
                     u32 fill)
{
    ui_fill(buf, bw, bh, x, y, w, h, fill);
    hline(buf, bw, bh, x, y, w, UI_BEVEL_LIT);
    vline(buf, bw, bh, x, y + 1, h - 1, UI_BEVEL_LIT);
    hline(buf, bw, bh, x, y + h - 1, w, UI_BEVEL_DK);
    vline(buf, bw, bh, x + w - 1, y, h - 1, UI_BEVEL_DK);
}

void ui_bevel_sunken(u32 *buf, int bw, int bh, int x, int y, int w, int h,
                     u32 fill)
{
    ui_fill(buf, bw, bh, x, y, w, h, fill);
    hline(buf, bw, bh, x, y, w, UI_BEVEL_DK);
    vline(buf, bw, bh, x, y + 1, h - 1, UI_BEVEL_DK);
    hline(buf, bw, bh, x, y + h - 1, w, UI_BEVEL_LIT);
    vline(buf, bw, bh, x + w - 1, y, h - 1, UI_BEVEL_LIT);
}

void ui_button(u32 *buf, int bw, int bh, int x, int y, int w, int h,
               const char *label, int pressed, u32 accent)
{
    u32 lit = UI_BEVEL_LIT, dk = UI_BEVEL_DK;

    if (pressed) {
        lit = UI_BEVEL_DK;
        dk = UI_BEVEL_LIT;
    }
    ui_fill(buf, bw, bh, x, y, w, h, UI_WINDOW_BG);
    hline(buf, bw, bh, x, y, w, lit);
    vline(buf, bw, bh, x, y + 1, h - 1, lit);
    hline(buf, bw, bh, x, y + h - 1, w, dk);
    vline(buf, bw, bh, x + w - 1, y, h - 1, dk);
    hline(buf, bw, bh, x + 2, y + h - 3, w - 4, accent);

    if (label && w > 16) {
        int tw = (int)strlen(label) * 8;
        int tx = x + (w - tw) / 2;
        int ty = y + (h - 8) / 2 + (pressed ? 1 : 0);
        ui_text(buf, bw, bh, tx, ty, label, UI_TEXT_BRIGHT, 1, 0);
    }
}

void ui_progress(u32 *buf, int bw, int bh, int x, int y, int w, int h,
                 int pct, u32 accent)
{
    ui_bevel_sunken(buf, bw, bh, x, y, w, h, UI_WINDOW_BG);
    int filled = pct * (w - 2) / 100;
    if (filled < 0)
        filled = 0;
    if (filled > w - 2)
        filled = w - 2;
    if (filled > 0 && h >= 2)
        ui_fill(buf, bw, bh, x + 1, y + 1, filled, h - 2, accent);
}

void ui_cursor(u32 *buf, int bw, int bh, int x, int y, int h, u32 fg)
{
    ui_fill(buf, bw, bh, x, y, 1, h, fg);
}
