/* =============================================================================
 *  Black OS - themes.h / themes.c : acrylic / mica theme engine
 *
 *  Themes describe the desktop chrome colors.  apply_theme() installs the
 *  active theme into globals read by shell.c, comp.c and lockscreen.c so a
 *  theme change takes effect on the next repaint.
 * ========================================================================== */
#ifndef BLACKOS_THEMES_H
#define BLACKOS_THEMES_H

#include "types.h"

#define THEME_MAX 8

struct theme {
    char  name[32];
    u32   desktop_top;      /* gradient upper                         */
    u32   desktop_bot;      /* gradient lower                         */
    u32   panel_bg;         /* acrylic veil base                      */
    u32   panel_border;     /* rounded frame                          */
    u32   text_bright;      /* primary text                           */
    u32   text_dim;         /* secondary text                         */
    u32   accent;           /* accent (cyan/rose/etc.)                */
    u32   btn_hover;        /* button hover                           */
};

void   themes_init(void);                   /* load default theme set             */
int    theme_count(void);
const struct theme *theme_at(int index);
void   apply_theme(int index);              /* sets active theme globals          */
int    theme_active_index(void);
u32    theme_desktop_top(void);
u32    theme_desktop_bot(void);
u32    theme_panel_bg(void);
u32    theme_panel_border(void);
u32    theme_text_bright(void);
u32    theme_text_dim(void);
u32    theme_accent(void);
u32    theme_btn_hover(void);

/* active theme globals (read directly by shell / compositor / apps)          */
extern u32 g_desktop_top, g_desktop_bot;
extern u32 g_panel_bg, g_panel_border;
extern u32 g_text_bright, g_text_dim;
extern u32 g_accent, g_btn_hover;

#endif /* BLACKOS_THEMES_H */