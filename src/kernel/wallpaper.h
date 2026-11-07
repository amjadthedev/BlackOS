/* =============================================================================
 *  Black OS - wallpaper.h / wallpaper.c : desktop backgrounds
 *
 *  Modes: SOLID, GRADIENT, IMAGE (24-bit BMP loaded from VFS).
 *  draw_wallpaper() is called by the compositor each repaint.
 * ========================================================================== */
#ifndef BLACKOS_WALLPAPER_H
#define BLACKOS_WALLPAPER_H

#include "types.h"

#define WALL_SOLID    0
#define WALL_GRADIENT 1
#define WALL_IMAGE    2

void   wallpaper_init(void);                      /* default solid/gradient   */
void   wallpaper_set_solid(u32 color);
void   wallpaper_set_gradient(u32 top, u32 bot);
int    wallpaper_set_image(const char *vfs_path); /* 24-bit BMP, stretched    */
void   draw_wallpaper(void);                      /* paint full desktop       */
int    wallpaper_mode(void);
const char *wallpaper_path(void);

#endif /* BLACKOS_WALLPAPER_H */