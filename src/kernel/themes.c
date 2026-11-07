/* =============================================================================
 *  Black OS - themes.c : acrylic / mica theme engine
 * ========================================================================== */
#include "themes.h"
#include "string.h"
#include "printk.h"
#include "idt.h"
#include "wallpaper.h"

static struct theme themes[THEME_MAX];
static int theme_n = 0;
static int active = 0;

/* exported globals read by shell.c / comp.c / lockscreen.c                   */
u32 g_desktop_top  = 0x060A12;
u32 g_desktop_bot  = 0x010309;
u32 g_panel_bg     = 0x0A1020;
u32 g_panel_border = 0x2A4468;
u32 g_text_bright  = 0xE8F4FF;
u32 g_text_dim     = 0x7089A6;
u32 g_accent       = 0x00C0E8;
u32 g_btn_hover    = 0x1A2A44;

static void add_theme(const char *name,
                      u32 top, u32 bot, u32 panel, u32 border,
                      u32 bright, u32 dim, u32 accent, u32 hover)
{
    if (theme_n >= THEME_MAX)
        return;
    struct theme *t = &themes[theme_n++];
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    t->desktop_top  = top;
    t->desktop_bot  = bot;
    t->panel_bg     = panel;
    t->panel_border = border;
    t->text_bright  = bright;
    t->text_dim     = dim;
    t->accent       = accent;
    t->btn_hover    = hover;
}

void themes_init(void)
{
    theme_n = 0;
    active = 0;

    add_theme("Black Acrylic",
        0x060A12, 0x010309, 0x0A1020, 0x2A4468,
        0xE8F4FF, 0x7089A6, 0x00C0E8, 0x1A2A44);
    add_theme("Mica Blue",
        0x0A1428, 0x02040A, 0x0E182A, 0x3A5A8C,
        0xE8F4FF, 0x9BADCB, 0x4E9DD8, 0x1E3A5A);
    add_theme("Neon Cyan",
        0x00111A, 0x00050A, 0x05121A, 0x00C0E8,
        0xFFFFFF, 0x7FD4FF, 0x00F0FF, 0x0A2A3A);
    add_theme("Dark Rose",
        0x1A0810, 0x0A0205, 0x160A12, 0x5A2A44,
        0xFFE8F0, 0xC48FA0, 0xD42222, 0x2A101A);
    add_theme("Forest",
        0x08100A, 0x020502, 0x0A120A, 0x2A5A3A,
        0xE8FFE8, 0x9AC49A, 0x2ED573, 0x0A2A10);
    add_theme("Midnight",
        0x05050A, 0x000003, 0x0A0A14, 0x3A3A5A,
        0xD8D8FF, 0x8A8AAA, 0x7B68EE, 0x1A1A3A);

    apply_theme(0);
    printk("[theme] %d themes loaded\n", theme_n);
}

int theme_count(void) { return theme_n; }

const struct theme *theme_at(int index)
{
    if (index < 0 || index >= theme_n)
        return NULL;
    return &themes[index];
}

void apply_theme(int index)
{
    if (index < 0 || index >= theme_n)
        return;
    active = index;
    const struct theme *t = &themes[active];

    g_desktop_top  = t->desktop_top;
    g_desktop_bot  = t->desktop_bot;
    g_panel_bg     = t->panel_bg;
    g_panel_border = t->panel_border;
    g_text_bright  = t->text_bright;
    g_text_dim     = t->text_dim;
    g_accent       = t->accent;
    g_btn_hover    = t->btn_hover;

    wallpaper_set_gradient(t->desktop_top, t->desktop_bot);
}

int theme_active_index(void) { return active; }

u32 theme_desktop_top(void)  { return g_desktop_top; }
u32 theme_desktop_bot(void)  { return g_desktop_bot; }
u32 theme_panel_bg(void)     { return g_panel_bg; }
u32 theme_panel_border(void) { return g_panel_border; }
u32 theme_text_bright(void)  { return g_text_bright; }
u32 theme_text_dim(void)     { return g_text_dim; }
u32 theme_accent(void)       { return g_accent; }
u32 theme_btn_hover(void)    { return g_btn_hover; }
