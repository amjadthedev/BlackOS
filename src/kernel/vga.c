/* =============================================================================
 *  Black OS - Phase 3: VGA text-mode driver (80x25, buffer at 0xB8000)
 *
 *  Color scheme: pitch-black background (#000000) with neon-cyan (#0B) and
 *  crimson (#0C) foreground accents.
 * ========================================================================== */
#include "vga.h"
#include "io.h"
#include "string.h"

#define VGA_MEM   ((volatile u16 *)0xB8000)

/* CRTC ports for the hardware cursor. */
#define CRTC_INDEX      0x3D4
#define CRTC_DATA       0x3D5

static volatile u16 *const vptr = VGA_MEM;
static u8 attrib = (VGA_BLACK << 4) | VGA_LCYAN;   /* black bg, neon cyan fg */
static usize crow, ccol;

/* Cursor position update: CRT index 0x0F = cursor low, 0x0E = cursor high. */
static void vga_update_hw_cursor(void)
{
    u16 pos = (u16)(crow * VGA_WIDTH + ccol);
    outb(CRTC_INDEX, 0x0F);
    outb(CRTC_DATA, (u8)(pos & 0xFF));
    outb(CRTC_INDEX, 0x0E);
    outb(CRTC_DATA, (u8)(pos >> 8));
}

void vga_init(void)
{
    crow = 0;
    ccol = 0;
    attrib = (VGA_BLACK << 4) | VGA_LCYAN;
    vga_clear();
}

void vga_set_color(u8 fg, u8 bg)
{
    attrib = (u8)((bg << 4) | (fg & 0x0F));
}

u8 vga_get_color(void)
{
    return attrib;
}

void vga_clear(void)
{
    u16 blank = (u16)(attrib << 8) | (u16)' ';
    for (usize i = 0; i < (usize)(VGA_WIDTH * VGA_HEIGHT); i++)
        vptr[i] = blank;
    crow = 0;
    ccol = 0;
    vga_update_hw_cursor();
}

static void vga_scroll(void)
{
    u16 blank = (u16)(attrib << 8) | (u16)' ';

    /* Move every line up one row. */
    for (usize i = 0; i < (usize)(VGA_WIDTH * (VGA_HEIGHT - 1)); i++)
        vptr[i] = vptr[i + VGA_WIDTH];

    /* Blank the final row. */
    for (usize i = (usize)(VGA_WIDTH * (VGA_HEIGHT - 1));
         i < (usize)(VGA_WIDTH * VGA_HEIGHT); i++)
        vptr[i] = blank;

    crow = VGA_HEIGHT - 1;
}

static void vga_newline(void)
{
    ccol = 0;
    crow++;
    if (crow >= VGA_HEIGHT)
        vga_scroll();
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        vga_newline();
        break;
    case '\r':
        ccol = 0;
        break;
    case '\b':
        if (ccol > 0) {
            ccol--;
            vptr[crow * VGA_WIDTH + ccol] =
                (u16)(attrib << 8) | (u16)' ';
        }
        break;
    case '\t':
        ccol = (ccol + 8) & ~(usize)7;
        if (ccol >= VGA_WIDTH) {
            ccol = 0;
            vga_newline();
        }
        break;
    default: {
        if (c < 0x20)           /* unprintable control chars: drop them */
            break;
        vptr[crow * VGA_WIDTH + ccol] =
            (u16)(attrib << 8) | (u16)(unsigned char)c;
        ccol++;
        if (ccol >= VGA_WIDTH)
            vga_newline();
        break;
    }
    }
    vga_update_hw_cursor();
}

void vga_print(const char *s)
{
    while (*s)
        vga_putc(*s++);
}

void vga_print_color(const char *s, u8 fg, u8 bg)
{
    u8 saved = attrib;
    vga_set_color(fg, bg);
    vga_print(s);
    attrib = saved;
}

usize vga_row(void) { return crow; }
usize vga_col(void) { return ccol; }

/* --------------------------------------------------------------------------
 * The BLACK OS splash.  Cyan host art, crimson accents, white details.
 * ------------------------------------------------------------------------ */
static const char *const banner_art[] = {
    "     ____  _        ____   _____  ",
    "    |  __ \\| |      / __ \\ / ____| ",
    "    | |__) | |     | |  | | (___   ",
    "    |  _  /| |     | |  | |\\___ \\  ",
    "    | | \\ \\| |____ | |__| |____) | ",
    "    |_|  \\_\\______| \\____/|_____/  ",
};

void vga_printf_banner(void)
{
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    vga_putc('\n');
    for (usize i = 0; i < sizeof(banner_art) / sizeof(banner_art[0]); i++)
        vga_print(banner_art[i]), vga_putc('\n');

    vga_set_color(VGA_LRED, VGA_BLACK);
    vga_print("  ======================================\n");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    vga_print("   BLACK OS ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("v1.0.0");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    vga_print("  ::  x86_64 long-mode kernel\n");
    vga_print("   built from scratch :: no linux :: no windows\n");
    vga_set_color(VGA_LRED, VGA_BLACK);
    vga_print("  ======================================\n");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    vga_print("  [system] boot chain: GRUB2 -> multiboot2 -> 64-bit\n");
    vga_print("     |_____ ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("pitch-black console :: neon cyan telemetry");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    vga_putc('\n');
    vga_set_color(VGA_BLACK, VGA_BLACK);
    vga_print("\n");
}

void vga_banner(void)
{
    vga_printf_banner();
}
