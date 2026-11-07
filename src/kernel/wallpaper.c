/* =============================================================================
 *  Black OS - wallpaper.c : desktop backgrounds (solid, gradient, BMP image)
 * ========================================================================== */
#include "wallpaper.h"
#include "fb.h"
#include "vfs.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"

#define BMP_MAGIC 0x4D42   /* 'BM'                                       */

static int wall_mode = WALL_SOLID;
static u32 wall_top = 0x060A12;
static u32 wall_bot = 0x010309;
static char wall_path[128] = "";

#pragma pack(push, 1)
struct bmp_file_header {
    u16 bf_type;
    u32 bf_size;
    u16 bf_reserved1;
    u16 bf_reserved2;
    u32 bf_off_bits;
};
struct bmp_info_header {
    u32 bi_size;
    u32 bi_width;
    u32 bi_height;
    u16 bi_planes;
    u16 bi_bit_count;
    u32 bi_compression;
    u32 bi_size_image;
    u32 bi_x_pels;
    u32 bi_y_pels;
    u32 bi_clr_used;
    u32 bi_clr_important;
};
#pragma pack(pop)

void wallpaper_init(void)
{
    wall_mode = WALL_SOLID;
    wall_top  = 0x060A12;
    wall_bot  = 0x010309;
    wall_path[0] = '\0';
}

void wallpaper_set_solid(u32 color)
{
    wall_mode = WALL_SOLID;
    wall_top = wall_bot = color;
}

void wallpaper_set_gradient(u32 top, u32 bot)
{
    wall_mode = WALL_GRADIENT;
    wall_top = top;
    wall_bot = bot;
}

static int bmp_load_and_draw(const char *path)
{
    vfs_handle h = vfs_lookup(path);
    if (h < 0)
        return -1;

    u64 fsize = vfs_size(h);
    if (fsize < 54 || fsize > 16 * 1024 * 1024)
        return -1;

    u8 *buf = (u8 *)(usize)pmm_alloc_run((fsize + 4095) / 4096);
    if (buf == NULL)
        return -1;
    u64 got = vfs_read(h, buf, fsize, 0);

    struct bmp_file_header *fh = (struct bmp_file_header *)buf;
    struct bmp_info_header *ih = (struct bmp_info_header *)(buf + 14);

    if (fh->bf_type != BMP_MAGIC ||
        ih->bi_bit_count != 24 ||
        ih->bi_compression != 0) {
        pmm_free_run((u64)(usize)buf, (fsize + 4095) / 4096);
        return -1;
    }

    u32 bw = ih->bi_width;
    u32 bh = ih->bi_height;
    u8 *pixels = buf + fh->bf_off_bits;

    /* BMP rows are bottom-up, 4-byte padded                                */
    u32 row_bytes = (bw * 3 + 3) & ~3;
    u8 *row = (u8 *)(usize)pmm_alloc_run(1);   /* 4 KiB row scratch        */
    if (row == NULL) {
        pmm_free_run((u64)(usize)buf, (fsize + 4095) / 4096);
        return -1;
    }

    u32 sw = fb_width, sh = fb_height;
    u32 x_scale_num = sw << 16;
    u32 y_scale_num = sh << 16;

    for (u32 sy = 0; sy < sh; sy++) {
        u32 src_y = ((sh - 1 - sy) * y_scale_num) >> 16;
        if (src_y >= bh)
            src_y = bh - 1;
        u8 *src = pixels + fh->bf_off_bits + src_y * row_bytes;
        memcpy(row, src, row_bytes > 4096 ? 4096 : row_bytes);
        for (u32 sx = 0; sx < sw; sx++) {
            u32 src_x = (sx * x_scale_num) >> 16;
            if (src_x >= bw)
                src_x = bw - 1;
            u8 *px = row + src_x * 3;
            /* BMP is BGR; our FB is 0x00RRGGBB                             */
            u32 c = ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
            fb_pixel((int)sx, (int)sy, c);
        }
    }

    pmm_free_run((u64)(usize)row, 1);
    pmm_free_run((u64)(usize)buf, (fsize + 4095) / 4096);
    return 0;
}

int wallpaper_set_image(const char *vfs_path)
{
    if (vfs_path == NULL || vfs_path[0] == '\0')
        return -1;
    /* probe: can we read it?                                               */
    vfs_handle h = vfs_lookup(vfs_path);
    if (h < 0)
        return -1;
    u64 sz = vfs_size(h);
    if (sz < 54)
        return -1;
    u8 head[54];
    if (vfs_read(h, head, 54, 0) != 54)
        return -1;
    if (((u16)head[0] | ((u16)head[1] << 8)) != BMP_MAGIC)
        return -1;

    strncpy(wall_path, vfs_path, sizeof(wall_path) - 1);
    wall_path[sizeof(wall_path) - 1] = '\0';
    wall_mode = WALL_IMAGE;
    return bmp_load_and_draw(vfs_path);
}

void draw_wallpaper(void)
{
    if (wall_mode == WALL_IMAGE) {
        if (bmp_load_and_draw(wall_path) != 0)
            wall_mode = WALL_GRADIENT;   /* fall back on error        */
    }

    if (wall_mode == WALL_GRADIENT) {
        u32 h = fb_height;
        u32 rows = fb_height - 1;
        for (u32 y = 0; y < h; y++) {
            u32 r = ((wall_top >> 16) & 0xFF) * (rows - y) / rows +
                    ((wall_bot >> 16) & 0xFF) * y / rows;
            u32 g = ((wall_top >> 8) & 0xFF) * (rows - y) / rows +
                    ((wall_bot >> 8) & 0xFF) * y / rows;
            u32 b = (wall_top & 0xFF) * (rows - y) / rows +
                    (wall_bot & 0xFF) * y / rows;
            u32 c = (r << 16) | (g << 8) | b;
            for (u32 x = 0; x < fb_width; x++)
                fb_pixel((int)x, (int)y, c);
        }
    } else {
        fb_clear(wall_top);
    }
}

int wallpaper_mode(void)   { return wall_mode; }
const char *wallpaper_path(void) { return wall_path; }
