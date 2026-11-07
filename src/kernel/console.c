/* =============================================================================
 *  Black OS - Phase 7 console abstraction: VGA text <-> framebuffer text
 *
 *  printk() always mirrors to COM1; the visible console is dispatchable:
 *    TEXT mode  -> vga.c (0xB8000, 80x25)
 *    FB   mode  -> font glyphs onto the linear framebuffer (fb.c)
 *  Kernel boot messages stay on TEXT until fb_init() succeeds and the caller
 *  decides to switch.  The GUI uses the framebuffer exclusively.
 * ========================================================================== */
#include "console.h"
#include "vga.h"
#include "fb.h"
#include "font.h"
#include "string.h"

static int mode = CONSOLE_MODE_TEXT;

/* FB text grid state (8x8 glyphs, hard slice of the 800x600 surface).      */
static int crow, ccol;
#define FB_MAX_VARS     80U                     /* fb cols (800 / 8)       */
#define FB_MAX_ROWS     75U                     /* fb rows (600 / 8)       */

void console_use_text(void) { mode = CONSOLE_MODE_TEXT; }
void console_use_fb(void)   { mode = CONSOLE_MODE_FB; }
int  console_mode(void)     { return mode; }

/*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ buffer glyph I/O ━━━━━━━━━━━━━━━━━━━━━━━━*/
static void fb_scroll(void)
{
    u32 stride = fb_bytes / 4;
    u32 rows   = FB_MAX_ROWS * 8;

    memmove(fb_base,
            fb_base + stride * 8,
            (stride * (rows - 8)) * 4);

    u32 *tail = fb_base + stride * (rows - 8);
    for (u32 i = 0; i < stride * 8; i++)
        tail[i] = 0;
}

static void fb_render_char(int ch, int cx, int cy)
{
    const u8 *glyph = font_glyph(ch);
    u32 stride = fb_bytes / 4;
    if ((u32)(cy + 8) > fb_height || (u32)(cx + 8) > fb_width)
        return;

    for (int row = 0; row < 8; row++) {
        u32 *line = fb_base + ((u32)cy + row) * stride + cx;
        u8 bits = glyph[row];
        for (int bit = 0; bit < 8; bit++)
            line[bit] = (bits & (0x80 >> bit)) ? 0x00B4D8 : 0;
    }
}

static void fb_newline(void)
{
    ccol = 0;
    crow++;
    if (crow >= (int)FB_MAX_ROWS) {
        fb_scroll();
        crow = (int)FB_MAX_ROWS - 1;
    }
}

void console_newline(void)
{
    if (mode == CONSOLE_MODE_TEXT)
        vga_putc('\n');
    else
        fb_newline();
}

void console_resync(void)
{
    crow = ccol = 0;
}

void console_putc(char c)
{
    if (mode == CONSOLE_MODE_TEXT) {
        vga_putc(c);
        return;
    }

    switch (c) {
    case '\n':
        fb_newline();
        return;
    case '\r':
        ccol = 0;
        return;
    case '\b':
        if (ccol > 0) {
            ccol--;
            fb_render_char(' ', ccol * 8, crow * 8);
        } else if (crow > 0) {
            crow--;
            ccol = (int)FB_MAX_VARS - 1;
            fb_render_char(' ', ccol * 8, crow * 8);
        }
        return;
    case '\t':
        ccol = (ccol + 4) & ~3;
        if (ccol >= (int)FB_MAX_VARS)
            fb_newline();
        return;
    default:
        fb_render_char(c, ccol * 8, crow * 8);
        ccol++;
        if (ccol >= (int)FB_MAX_VARS)
            fb_newline();
        return;
    }
}
