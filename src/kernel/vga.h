/* =============================================================================
 *  Black OS - Phase 3: VGA text-mode driver (80x25, buffer at 0xB8000)
 *
 *  Colour scheme: pitch-black background (#000000) with neon-cyan (#0B) and
 *  crimson (#0C) foreground accents.
 * ========================================================================== */
#ifndef BLACKOS_VGA_H
#define BLACKOS_VGA_H

#include "types.h"

/* Standard VGA 4-bit attribute colours. */
#define VGA_BLACK        0x0
#define VGA_BLUE         0x1
#define VGA_GREEN        0x2
#define VGA_CYAN         0x3
#define VGA_RED          0x4
#define VGA_MAGENTA      0x5
#define VGA_BROWN        0x6
#define VGA_LIGHTGRAY    0x7
#define VGA_DARKGRAY     0x8
#define VGA_LBLUE        0x9
#define VGA_LGREEN       0xA
#define VGA_LCYAN        0xB   /* neon cyan */
#define VGA_LRED         0xC   /* neon crimson */
#define VGA_LPINK        0xD
#define VGA_YELLOW       0xE
#define VGA_WHITE        0xF

#define VGA_WIDTH        80
#define VGA_HEIGHT       25

void  vga_init(void);
void  vga_clear(void);
void  vga_set_color(u8 fg, u8 bg);
u8    vga_get_color(void);
void  vga_putc(char c);
void  vga_print(const char *s);
void  vga_print_color(const char *s, u8 fg, u8 bg);
void  vga_printf_banner(void);            /* the BLACK OS splash */
void  vga_banner(void);                   /* alias kept for external callers */
usize vga_row(void);
usize vga_col(void);

#endif /* BLACKOS_VGA_H */
