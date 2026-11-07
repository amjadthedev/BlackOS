/* =============================================================================
 *  Black OS - Phase 7: linear framebuffer driver (Multiboot2 RGB, 32 bpp)
 * ========================================================================== */
#ifndef BLACKOS_FB_H
#define BLACKOS_FB_H

#include "types.h"

/* Global framebuffer state (filled by fb_init from MB2 tag type 8).       */
extern u32  fb_width, fb_height, fb_bytes;   /* dimensions + bytes/line     */
extern u32 *fb_base;                          /* NULL until fb_init succeeds */
extern int  fb_ready;

/* Parse the MB2 framebuffer info tag, identity-map the LFB, load the font
 * and unlock the fb_* drawing primitives.  Returns 0 on success.          */
int  fb_init(u64 mb2_info_addr);              /* 0 = ok, -1 = no fb        */

/*                                                                       PRIM */
int  fb_clipped(int x, int y, int w, int h);
void fb_pixel(int x, int y, u32 rgb);
void fb_fill_rect(int x, int y, int w, int h, u32 rgb);
void fb_line(int x0, int y0, int x1, int y1, u32 rgb);
void fb_frame(int x, int y, int w, int h, u32 rgb);
void fb_clear(u32 rgb);
void fb_contrast(u32 rgb_c1, u32 rgb_c2);      /* vertical gradient        */

/* Blend an arbitrary surface buffer (PMM-allocated) onto the framebuffer */
void fb_blit(u32 *src, int sw, int sh, int dx, int dy);

/* ── Phase 11 compositing primitives (Win11 acrylic shell) ──────────────── */
void fb_blend(u32 *src, int sw, int sh, int dx, int dy);      /* per-pixel alpha  */
void fb_rect_rounded(int x, int y, int w, int h, int r, u32 rgb);
void fb_rounded_frame(int x, int y, int w, int h, int r, u32 rgb);
/* Box-blur ×3 ≈ Gaussian, for frosted-panel backdrops. Uses a static
 * scratch line buffer (<= 1.2 MiB); call after the backdrop is painted. */
void fb_blur_region(int x, int y, int w, int h, int radius);

/* Bytes -> u32 colour helper (0xRRGGBB theme values are passed as 0x00RRGGBB). */
static inline u32 fb_rgb(u8 r, u8 g, u8 b) { return ((u32)r << 16) | ((u32)g << 8) | (u32)b; }

/* Blend two colours: over = a over b, with alpha in the top byte. */
static inline u32 fb_mix(u32 dst, u32 src, u8 alpha)
{
    u32 a  = alpha;
    u32 ia = 255 - a;
    u32 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    u32 sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    return ((((dr * ia + sr * a) / 255) & 0xFF) << 16)
         | ((((dg * ia + sg * a) / 255) & 0xFF) << 8)
         |  ((db * ia + sb * a) / 255 & 0xFF);
}

#endif /* BLACKOS_FB_H */
