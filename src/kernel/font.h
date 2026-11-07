/* =============================================================================
 *  Black OS - 8x8 bitmap font (Phase 7 foundation for the GUI)
 *
 *  Primary source: the classic ROM character generator, 256 glyphs x 8 bytes
 *  at physical F000:FA6E (linear 0x000FFA6E) - present in every IBM-PC
 *  compatible boot, including QEMU's SeaBIOS.  It is copied into kernel BSS
 *  before anything can clobber the low RAM window.
 *
 *  Fallback: a compact embedded atlas (uppercase USD digits, punctuation).
 *  Lowercase renders from the uppercase bitmaps.  This is only used if the
 *  ROM copy fails integrity validation.
 * ========================================================================== */
#ifndef BLACKOS_FONT_H
#define BLACKOS_FONT_H

#include "types.h"

#define FONT_WIDTH   8
#define FONT_HEIGHT  8
#define FONT_BYTES   8                  /* one byte per glyph row */

void font_load(void);
const u8 *font_glyph(int c);            /* never NULL: '?' on miss          */

#endif /* BLACKOS_FONT_H */
