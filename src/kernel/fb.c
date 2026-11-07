/* =============================================================================
 *  Black OS - Phase 7: linear framebuffer driver (Multiboot2 RGB, 32 bpp)
 *
 *  Boots with a Multiboot2 framebuffer tag (type 5, console_type = 1) so
 *  GRUB hands us a linear 800x600 RGB surface.  The info (tag type 8) is
 *  parsed here; the LFB is identity-mapped beyond 1 GiB with 2 MiB pages
 *  generated on demand from the PMM (see paging_map_2mb()).
 * ========================================================================== */
#include "fb.h"
#include "multiboot2.h"
#include "paging.h"
#include "pmm.h"
#include "font.h"
#include "printk.h"
#include "string.h"
#include "io.h"

u32  fb_width, fb_height, fb_bytes;
u32 *fb_base;
int  fb_ready = 0;

int fb_init(u64 mb2_info_addr)
{
    if (fb_ready == 1)
        return 0;

    font_load();

    /* ------------------------------------------------- locate type-8 tag -- */
    const u8 *base = (const u8 *)(usize)mb2_info_addr;
    const u8 *tag_addr = base + 8;
    const u8 *end_addr = base + ((const struct mb2_info *)base)->total_size;

    u64 addr = 0;
    u32 pitch = 0, width = 0, height = 0;

    while (tag_addr + sizeof(struct mb2_tag) <= end_addr) {
        const struct mb2_tag *t = (const struct mb2_tag *)tag_addr;
        if (t->type == MB2_TAG_END)
            break;
        if (t->type == 8) {                    /* MB2_TAG_FRAMEBUFFER     */
            /* addr u64 @ +8, pitch @ +16, width @ +20, height @ +24     */
            memcpy(&addr,   tag_addr + 8,  8);
            memcpy(&pitch,  tag_addr + 16, 4);
            memcpy(&width,  tag_addr + 20, 4);
            memcpy(&height, tag_addr + 24, 4);
            u32 bpp = 0;
            memcpy(&bpp,    tag_addr + 28, 1);
            if (bpp != 32) {
                printk("[fb  ] unsupported depth %u bpp\n", bpp);
                return -1;
            }
        }
        tag_addr += mb2_tag_step(t->size);
    }

    if (addr == 0 || width == 0 || height == 0 || pitch == 0) {
        printk("[fb  ] no linear framebuffer in boot info\n");
        return -1;
    }

    /* ---------------------------------------------------- map the memory -- */
    u64 map_first = addr & ~0x1FFFFFULL;
    u64 map_end   = (addr + (u64)pitch * height + 0x1FFFFFULL) & ~0x1FFFFFULL;
    for (u64 p = map_first; p < map_end; p += 0x200000) {
        if (paging_map_2mb(p) < 0) {
            printk("[fb  ] failed to map LFB page %llx\n", p);
            return -1;
        }
    }

    fb_width  = width;
    fb_height = height;
    fb_bytes  = pitch;
    fb_base   = (u32 *)(usize)addr;
    fb_ready  = 1;

    printk("[fb  ] linear framebuffer %ux%u @ %llx (pitch %u)\n",
            width, height, addr, pitch);
    return 0;
}

int fb_clipped(int x, int y, int w, int h)
{
    return (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            (u32)(x + w) > fb_width || (u32)(y + h) > fb_height);
}

void fb_pixel(int x, int y, u32 rgb)
{
    if (x < 0 || y < 0 || (u32)x >= fb_width || (u32)y >= fb_height)
        return;
    fb_base[y * (fb_bytes / 4) + x] = rgb;
}

void fb_fill_rect(int x, int y, int w, int h, u32 rgb)
{
    if (fb_clipped(x, y, w, h))
        return;
    const u32 stride = fb_bytes / 4;
    for (int row = y; row < y + h; row++) {
        u32 *line = fb_base + row * stride + x;
        for (int col = 0; col < w; col++)
            line[col] = rgb;
    }
}

void fb_line(int x0, int y0, int x1, int y1, u32 rgb)
{
    int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    int dy = (y1 > y0) ? y1 - y0 : y0 - y1;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        fb_pixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void fb_frame(int x, int y, int w, int h, u32 rgb)
{
    fb_line(x, y, x + w - 1, y, rgb);
    fb_line(x, y + h - 1, x + w - 1, y + h - 1, rgb);
    fb_line(x, y, x, y + h - 1, rgb);
    fb_line(x + w - 1, y, x + w - 1, y + h - 1, rgb);
}

void fb_clear(u32 rgb)
{
    fb_fill_rect(0, 0, (int)fb_width, (int)fb_height, rgb);
}

void fb_contrast(u32 top, u32 bottom)
{
    u32 h = fb_height;
    u32 rows = fb_height - 1;
    for (u32 y = 0; y < h; y++) {
        u32 r = fb_rgb(((top >> 16) & 0xFF) * (rows - y) / rows +
                        ((bottom >> 16) & 0xFF) * y / rows,
                        ((top >> 8) & 0xFF) * (rows - y) / rows +
                        ((bottom >> 8) & 0xFF) * y / rows,
                        (top & 0xFF) * (rows - y) / rows +
                        (bottom & 0xFF) * y / rows);
        for (u32 x = 0; x < fb_width; x++)
            fb_pixel((int)x, (int)y, r);
    }
}

void fb_blit(u32 *src, int sw, int sh, int dx, int dy)
{
    if (dx < 0 || dy < 0)            /* paranoid: windows are on-screen */
        return;
    int can_w = ((int)fb_width  - dx < sw) ? (int)fb_width - dx : sw;
    int can_h = ((int)fb_height - dy < sh) ? (int)fb_height - dy : sh;
    if (can_w <= 0 || can_h <= 0)
        return;

    const u32 screen_stride = fb_bytes / 4;
    for (int row = 0; row < can_h; row++) {
        u32 *dst = fb_base + (dy + row) * screen_stride + dx;
        u32 *s   = src + row * sw;
        for (int col = 0; col < can_w; col++)
            dst[col] = s[col];
    }
}

/*━━━━━━━━━━━━━━━━━━━━━ Phase 11 compositing primitives ━━━━━━━━━━━━━━━━━━━*/
void fb_blend(u32 *src, int sw, int sh, int dx, int dy)
{
    if (dx < 0 || dy < 0)
        return;
    int can_w = ((int)fb_width  - dx < sw) ? (int)fb_width - dx : sw;
    int can_h = ((int)fb_height - dy < sh) ? (int)fb_height - dy : sh;
    if (can_w <= 0 || can_h <= 0)
        return;

    const u32 screen_stride = fb_bytes / 4;
    for (int row = 0; row < can_h; row++) {
        u32 *dst = fb_base + (dy + row) * screen_stride + dx;
        u32 *s   = src + row * sw;
        for (int col = 0; col < can_w; col++) {
            /* src pixels are AARRGGBB: alpha in the high byte             */
            u32 pix = *s++;
            if ((pix & 0xFF000000u) == 0xFF000000u) {      /* opaque       */
                dst[col] = pix & 0x00FFFFFFu;
                continue;
            }
            if ((pix & 0xFF000000u) == 0) {                /* keep screen  */
                continue;
            }
            /* ramp alpha: 0x80..0xFF => strong blend factor               */
            u32 a = (pix >> 24) & 0xFF;
            dst[col] = fb_mix(dst[col], pix, (u8)a);
        }
    }
}

/* Horizontal + vertical rounded corners on a filled rect (r <= min(w,h)/2).
 * Corner rows use integer circle drawing: inset = r - sqrt(r^2 - d^2).    */
static u32 isqrt32(u32 v)
{
    u32 res = 0, bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res  = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

void fb_rect_rounded(int x, int y, int w, int h, int r, u32 rgb)
{
    if (r < 0)
        r = 0;
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;

    if (r == 0) {
        fb_fill_rect(x, y, w, h, rgb);
        return;
    }

    /* central body between the corner bands */
    fb_fill_rect(x + r, y, w - 2 * r, h, rgb);

    for (int i = 0; i < r; i++) {
        /* corner rows: inset = r - sqrt(r^2 - d^2), d = r-1-i            */
        u32 rem = (u32)r * (u32)r - (u32)((r - 1 - i) * (r - 1 - i));
        u32 root = isqrt32(rem);
        int inset = (int)r - 1 - (int)(root != 0 ? root : 1);
        if (inset < 0)
            inset = 0;

        fb_fill_rect(x + inset, y + i, w - 2 * inset, 1, rgb);
        fb_fill_rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, rgb);
    }
}

void fb_rounded_frame(int x, int y, int w, int h, int r, u32 rgb)
{
    /* top and bottom arcs, drawn as 1px-inset strokes */
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    fb_line(x + r, y,         x + w - 1 - r, y,           rgb);   /* top     */
    fb_line(x + r, y + h - 1, x + w - 1 - r, y + h - 1,     rgb);   /* bottom  */
    fb_line(x, y + r,         x,         y + h - 1 - r,     rgb);   /* left    */
    fb_line(x + w - 1, y + r, x + w - 1, y + h - 1 - r,     rgb);   /* right   */

    for (int i = 0; i < r; i++) {
        u32 rem = (u32)r * (u32)r - (u32)((r - 1 - i) * (r - 1 - i));
        u32 root = isqrt32(rem);
        int inset = (int)r - 1 - (int)(root != 0 ? root : 1);
        if (inset < 0)
            inset = 0;

        /* corner arcs: one dot per line ends                            */
        fb_pixel(x + inset,     y + i,             rgb);            /* UL */
        fb_pixel(x + w - 1 - inset, y + i,         rgb);            /* UR */
        fb_pixel(x + inset,     y + h - 1 - i,     rgb);            /* LL */
        fb_pixel(x + w - 1 - inset, y + h - 1 - i, rgb);            /* LR */
    }
}

/*──────────────────────────────── acrylic blur ───────────────────────────*/
/* Separable box blur applied three times converges to a Gaussian.  The
 * scratch buffer holds one full region copy (up to 800x600 px, ~1.9 MiB
 * BSS) plus one row accumulator, all static.                              */
static u32 blur_scratch[480000];      /* up to 800x600 px                   */
static u32 blur_row[800];             /* one row accumulator                */

void fb_blur_region(int x, int y, int w, int h, int radius)
{
    if (fb_ready == 0)
        return;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || radius <= 0)
        return;

    /* clamp to screen and scratch capacity */
    if ((u32)(x + w) > fb_width)
        w = (int)fb_width - x;
    if ((u32)(y + h) > fb_height)
        h = (int)fb_height - y;
    if ((u32)(x + w) > fb_width || (u32)(y + h) > fb_height)
        return;
    if ((u64)w * (u64)h > sizeof(blur_scratch) / sizeof(u32))
        return;                              /* region too big: skip blur  */

    const u32 stride = fb_bytes / 4;
    u32 *region = blur_scratch;

    /* copy J: region <- fb */
    for (int row = 0; row < h; row++) {
        u32 *src = fb_base + (u32)(y + row) * stride + (u32)x;
        memcpy(region + row * w, src, (usize)w * 4);
    }

    /* box blur radius clipping (1..8 keeps it cheap)                       */
    if (radius > 8)
        radius = 8;

    for (int pass = 0; pass < 3; pass++) {
        /* horizontal: region -> accumulator rows -> region                */
        for (int row = 0; row < h; row++) {
            u32 *line = region + row * w;
            u32 sum_r = 0, sum_g = 0, sum_b = 0;

            for (int i = 0; i <= radius && i < w; i++) {
                u32 px = line[i];
                sum_r += (px >> 16) & 0xFF;
                sum_g += (px >> 8)  & 0xFF;
                sum_b +=  px        & 0xFF;
            }

            for (int col = 0; col < w; col++) {
                blur_row[col] = ((sum_r / (u32)(2 * radius + 1)) << 16)
                              | ((sum_g / (u32)(2 * radius + 1)) << 8)
                              |  (sum_b / (u32)(2 * radius + 1));

                int addc = col + radius + 1;
                int delc = col - radius;
                if (addc < w) {
                    u32 p = line[addc];
                    sum_r += (p >> 16) & 0xFF;
                    sum_g += (p >> 8)  & 0xFF;
                    sum_b +=  p        & 0xFF;
                }
                if (delc >= 0) {
                    u32 p = line[delc];
                    sum_r -= (p >> 16) & 0xFF;
                    sum_g -= (p >> 8)  & 0xFF;
                    sum_b -=  p        & 0xFF;
                }
            }
            memcpy(line, blur_row, (usize)w * 4);
        }

        /* vertical: column walk with the same sliding window              */
        for (int col = 0; col < w; col++) {
            u32 sr = 0, sg = 0, sb = 0;
            for (int i = 0; i <= radius && i < h; i++) {
                u32 px = region[(u32)i * (u32)w + (u32)col];
                sr += (px >> 16) & 0xFF;
                sg += (px >> 8)  & 0xFF;
                sb +=  px        & 0xFF;
            }
            for (int row = 0; row < h; row++) {
                u32 avg = ((sr / (u32)(2 * radius + 1)) << 16)
                        | ((sg / (u32)(2 * radius + 1)) << 8)
                        |  (sb / (u32)(2 * radius + 1));
                region[(u32)row * (u32)w + (u32)col] = avg;

                int addr = row + radius + 1;
                int delr = row - radius;
                if (addr < h) {
                    u32 p = region[(u32)addr * (u32)w + (u32)col];
                    sr += (p >> 16) & 0xFF;
                    sg += (p >> 8)  & 0xFF;
                    sb +=  p        & 0xFF;
                }
                if (delr >= 0) {
                    u32 p = region[(u32)delr * (u32)w + (u32)col];
                    sr -= (p >> 16) & 0xFF;
                    sg -= (p >> 8)  & 0xFF;
                    sb -=  p        & 0xFF;
                }
            }
        }
    }

    /* copy back: fb <- region (blended 50 % so the desktop ghosts through) */
    for (int row = 0; row < h; row++) {
        u32 *dst = fb_base + (u32)(y + row) * stride + (u32)x;
        u32 *src = region + row * w;
        for (int col = 0; col < w; col++)
            dst[col] = fb_mix(dst[col], src[col], 0x99);   /* 60 % frosted  */
    }
}

