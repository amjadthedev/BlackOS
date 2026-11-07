/* =============================================================================
 *  Black OS - Phase 8: widget library (Win32-flavoured bevel aesthetic)
 *  Every function takes the surface geometry (bw x bh) so all pixel writes
 *  stay in bounds.
 * ========================================================================== */
#ifndef BLACKOS_UI_H
#define BLACKOS_UI_H

#include "types.h"

/* ─── theme palette (pitch-black surfaces, neon cyan/crimson accents) ──── */
#define UI_DESKTOP_TOP   0x060A12      /* gradient upper sky               */
#define UI_DESKTOP_BOT   0x010309      /* gradient lower floor             */
#define UI_FB_BG         0x000000
#define UI_TASKBAR_BG    0x04070D
#define UI_TASKBAR_H     28             /* taskbar height, px               */
#define UI_TASKBAR_HI    0x0C1828
#define UI_WINDOW_BG     0x080D16
#define UI_WINDOW_TITLE  0x0A1220
#define UI_WINDOW_BORDER 0x16283D
#define UI_BEVEL_LIT     0x24384F
#define UI_BEVEL_DK      0x010208
#define UI_TEXT_BRIGHT   0xE8F4FF
#define UI_TEXT_DIM      0x7089A6
#define UI_TEXT_MUTED    0x42566B
#define UI_ACCENT_CYAN   0x00C0E8
#define UI_ACCENT_RED    0xD42222
#define UI_ACCENT_GREEN  0x2ED573
#define UI_MENU_BG       0x050810

/* Simple primitives on a caller-owned u32 surface. */
void ui_put(u32 *buf, int bw, int bh, int x, int y, u32 c);
void ui_fill(u32 *buf, int bw, int bh, int x, int y, int w, int h, u32 c);
void ui_text(u32 *buf, int bw, int bh, int x, int y, const char *s,
             u32 fg, int transparent, u32 bg);
void ui_text_box(u32 *buf, int bw, int bh, int x, int y, int w,
                 const char *s, u32 fg, u32 bg);

/* Styled composites. */
void ui_panel(u32 *buf, int bw, int bh, int x, int y, int w, int h, u32 fill);
void ui_bevel_raised(u32 *buf, int bw, int bh, int x, int y, int w, int h,
                     u32 fill);
void ui_bevel_sunken(u32 *buf, int bw, int bh, int x, int y, int w, int h,
                     u32 fill);
void ui_button(u32 *buf, int bw, int bh, int x, int y, int w, int h,
               const char *text, int pressed, u32 accent);
void ui_progress(u32 *buf, int bw, int bh, int x, int y, int w, int h,
                 int pct, u32 accent);
void ui_cursor(u32 *buf, int bw, int bh, int x, int y, int h, u32 fg);

#endif /* BLACKOS_UI_H */
