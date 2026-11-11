/* =============================================================================
 *  Black OS - settings.c : Windows 11-inspired Settings app
 *
 *  Tabs: Display (theme / wallpaper), Network (DHCP + firewall),
 *        System (about / reboot / shutdown), Audio, Account, About
 *        (third-party credits).
 *  Renders into its compositor window buffer; driven by keyboard arrows
 *  and Enter.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "themes.h"
#include "wallpaper.h"
#include "netstack.h"
#include "auth.h"
#include "rtc.h"
#include "syscall.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"
#include "idt.h"
#include "keyboard.h"
#include "acpi.h"

#define SET_TABS 6
#define SET_COLS 56

struct settings_state {
    int tab;                 /* 0 Display, 1 Network, 2 System, 3 Audio, 4 Account, 5 About */
    int volume;
    char new_pass[AUTH_PASS_MAX];
    int pass_len;
    int pass_field;          /* 0 = old, 1 = new, 2 = confirm             */
    char msg[64];
    u32 msg_ms;
};

static struct settings_state *st_of(struct window *win)
{
    return (struct settings_state *)win->priv;
}

static const char *tab_names[SET_TABS] = {
    "Display", "Network", "System", "Audio", "Account", "About"
};

static void set_msg(struct settings_state *s, const char *m)
{
    strncpy(s->msg, m, sizeof(s->msg) - 1);
    s->msg[sizeof(s->msg) - 1] = '\0';
    s->msg_ms = uptime_ms;
}

static void paint_tab_bar(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 10;
    for (int i = 0; i < SET_TABS; i++) {
        int x = 12 + i * 82;
        int sel = (i == s->tab);
        u32 bg = sel ? g_btn_hover : 0x0F1B2E;
        u32 fg = sel ? g_text_bright : g_text_dim;
        ui_fill(buf, w, h, x, y, 76, 26, bg);
        ui_text(buf, w, h, x + 8, y + 9, tab_names[i], fg, 1, 0);
    }
    /* underline active tab */
    ui_fill(buf, w, h, 12 + s->tab * 82, y + 26, 76, 2, g_accent);
}

static void paint_display(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 70;
    ui_text(buf, w, h, 12, y, "Theme", g_text_dim, 1, 0);
    y += 24;
    for (int i = 0; i < theme_count(); i++) {
        const struct theme *t = theme_at(i);
        int x = 12 + i * 150;
        int sel = (i == theme_active_index());
        ui_fill(buf, w, h, x, y, 140, 44, sel ? t->accent : 0x0F1B2E);
        ui_text(buf, w, h, x + 8, y + 10, t->name, sel ? 0 : g_text_bright, 1, 0);
        ui_text(buf, w, h, x + 8, y + 30, sel ? "active" : "", g_text_dim, 1, 0);
    }
    y += 56;
    ui_text(buf, w, h, 12, y, "Wallpaper", g_text_dim, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, wallpaper_mode() == WALL_IMAGE ? wallpaper_path()
             : (wallpaper_mode() == WALL_GRADIENT ? "gradient" : "solid"),
             g_text_bright, 1, 0);
    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 40, s->msg, g_accent, 1, 0);
}

static void paint_network(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 70;
    ui_text(buf, w, h, 12, y, "Network", g_text_dim, 1, 0);
    y += 24;
    char gw[32];
    u32 n = 0;
    for (int i = 0; i < 4 && n + 4 < sizeof(gw); i++) {
        u8 v = net.gw[i];
        if (v >= 100) gw[n++] = (char)('0' + v / 100);
        if (v >= 10)  gw[n++] = (char)('0' + (v / 10) % 10);
        gw[n++] = (char)('0' + v % 10);
        if (i < 3) gw[n++] = '.';
    }
    gw[n] = '\0';
    ui_text(buf, w, h, 12, y, "Gateway:", g_text_dim, 1, 0);
    ui_text(buf, w, h, 70, y, net_dhcp_bound() ? gw : "offline", g_text_bright, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "DHCP:", g_text_dim, 1, 0);
    ui_text(buf, w, h, 50, y, net_dhcp_bound() ? "bound" : "discovering", g_text_bright, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "Firewall inbound:", g_text_dim, 1, 0);
    /* we can't read the current firewall default from netstack easily; show toggle hint */
    ui_text(buf, w, h, 110, y, "press F to toggle allow/deny", g_text_dim, 1, 0);
    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 40, s->msg, g_accent, 1, 0);
}

static void paint_system(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 70;
    ui_text(buf, w, h, 12, y, "System", g_text_dim, 1, 0);
    y += 24;
    char tmp[32];
    u32 n = 0, v = uptime_ms / 1000;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (u32 i = 0; i < n / 2; i++) { char t = tmp[i]; tmp[i] = tmp[n - 1 - i]; tmp[n - 1 - i] = t; }
    tmp[n] = '\0';
    ui_text(buf, w, h, 12, y, "Uptime (s):", g_text_dim, 1, 0);
    ui_text(buf, w, h, 90, y, tmp, g_text_bright, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "Total RAM frames:", g_text_dim, 1, 0);
    n = 0; v = (u32)sys_mem_frames_total();
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (u32 i = 0; i < n / 2; i++) { char t = tmp[i]; tmp[i] = tmp[n - 1 - i]; tmp[n - 1 - i] = t; }
    tmp[n] = '\0';
    ui_text(buf, w, h, 120, y, tmp, g_text_bright, 1, 0);
    y += 24;
    ui_fill(buf, w, h, 12, y, 100, 24, g_btn_hover);
    ui_text(buf, w, h, 22, y + 8, "Reboot", g_text_bright, 1, 0);
    ui_fill(buf, w, h, 120, y, 100, 24, 0x30131A);
    ui_text(buf, w, h, 130, y + 8, "Shutdown", UI_ACCENT_RED, 1, 0);
    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 40, s->msg, g_accent, 1, 0);
}

static void paint_about(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 70;
    ui_text(buf, w, h, 12, y, "About", g_text_dim, 1, 0);
    y += 24;

    ui_text(buf, w, h, 12, y, "Black OS v0.1 (original work)", g_text_bright, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "Third-party components:", g_text_dim, 1, 0);
    y += 20;

    ui_text(buf, w, h, 12, y, "Duktape 2.7.0 (MIT)", g_accent, 1, 0);
    y += 16;
    ui_text(buf, w, h, 24, y, "github.com/svaarala/duktape", g_text_dim, 1, 0);
    y += 20;

    ui_text(buf, w, h, 12, y, "stb_image v2.30 (public domain)", g_accent, 1, 0);
    y += 16;
    ui_text(buf, w, h, 24, y, "github.com/nothings/stb", g_text_dim, 1, 0);
    y += 20;

    ui_text(buf, w, h, 12, y, "GRUB 2.x (GPLv2+) - ISO loader", g_accent, 1, 0);
    y += 16;
    ui_text(buf, w, h, 24, y, "gnu.org/software/grub/", g_text_dim, 1, 0);
    y += 20;

    ui_text(buf, w, h, 12, y, "ACPI spec 6.5 + ACPICA (reference)", g_accent, 1, 0);
    y += 16;
    ui_text(buf, w, h, 24, y, "uefi.org/specs/ACPI/ acpica.org/", g_text_dim, 1, 0);
    y += 20;

    ui_text(buf, w, h, 12, y, "QEMU + OVMF (test only, not shipped)", g_accent, 1, 0);
    y += 16;
    ui_text(buf, w, h, 24, y, "qemu.org/ tianocore/edk2", g_text_dim, 1, 0);
    y += 24;

    ui_text(buf, w, h, 12, y, "Full license texts: THIRD_PARTY_NOTICES.md",
            g_accent, 1, 0);
    y += 24;

    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 40, s->msg, g_accent, 1, 0);
}

static void paint_account(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 70;
    ui_text(buf, w, h, 12, y, "Account", g_text_dim, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "User:", g_text_dim, 1, 0);
    ui_text(buf, w, h, 44, y, auth_current_user(), g_text_bright, 1, 0);
    y += 24;
    const char *labels[3] = { "Old password:", "New password:", "Confirm:" };
    ui_text(buf, w, h, 12, y, labels[s->pass_field], g_text_dim, 1, 0);
    char masked[64];
    for (int i = 0; i < s->pass_len; i++)
        masked[i] = '•';
    masked[s->pass_len] = '\0';
    ui_text(buf, w, h, 100, y, masked, g_text_bright, 1, 0);
    y += 24;
    ui_fill(buf, w, h, 12, y, 100, 24, g_btn_hover);
    ui_text(buf, w, h, 22, y + 8, "Set password", g_text_bright, 1, 0);
    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 40, s->msg, g_accent, 1, 0);
}

static void paint_audio(struct settings_state *s, u32 *buf, int w, int h)
{
    int y = 70;
    ui_text(buf, w, h, 12, y, "Audio", g_text_dim, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "Device:", g_text_dim, 1, 0);
    ui_text(buf, w, h, 60, y, audio_present() ? "present" : "none",
            audio_present() ? g_text_bright : g_text_dim, 1, 0);
    y += 24;
    ui_text(buf, w, h, 12, y, "Volume:", g_text_dim, 1, 0);
    {
        char vol[8];
        u32 n = 0, v = s->volume;
        if (v == 0) vol[n++] = '0';
        while (v > 0) { vol[n++] = (char)('0' + v % 10); v /= 10; }
        for (u32 i = 0; i < n / 2; i++) { char t = vol[i]; vol[i] = vol[n-1-i]; vol[n-1-i] = t; }
        vol[n] = '\0';
        ui_text(buf, w, h, 60, y, vol, g_text_bright, 1, 0);
        ui_text(buf, w, h, 80, y, "%", g_text_bright, 1, 0);
    }
    /* volume slider bar                                                   */
    int bar_w = (s->volume * (w - 140)) / 100;
    ui_fill(buf, w, h, 60, y + 16, bar_w > 0 ? bar_w : 1, 10, g_accent);
    ui_fill(buf, w, h, 60 + bar_w, y + 16, (w - 140) - bar_w, 10, 0x1A2A44);

    ui_text(buf, w, h, 12, y + 34, "Up/down arrows adjust volume (stub - wire to AC'97)",
            g_text_dim, 1, 0);
    if (s->msg[0] && uptime_ms - s->msg_ms < 3000)
        ui_text(buf, w, h, 12, h - 40, s->msg, g_accent, 1, 0);
}

static void settings_paint(struct window *win, u32 *buf, int w, int h)
{
    struct settings_state *s = st_of(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);

    ui_text(buf, w, h, 12, 6, "BLACK OS Settings", g_accent, 1, 0);
    paint_tab_bar(s, buf, w, h);

    switch (s->tab) {
    case 0: paint_display(s, buf, w, h); break;
    case 1: paint_network(s, buf, w, h); break;
    case 2: paint_system(s, buf, w, h); break;
    case 3: paint_audio(s, buf, w, h); break;
    case 4: paint_account(s, buf, w, h); break;
    case 5: paint_about(s, buf, w, h); break;
    }
}

static void settings_key(struct window *win, char c)
{
    struct settings_state *s = st_of(win);
    if (s == NULL) return;

    if (c >= '1' && c <= '5') {
        s->tab = c - '1';
        return;
    }
    switch (c) {
    case KBD_LEFT:
        if (s->tab > 0) s->tab--;
        return;
    case KBD_RIGHT:
        if (s->tab < SET_TABS - 1) s->tab++;
        return;
    case KBD_UP:
        if (s->tab == 3) {
            if (s->volume < 100) {
                s->volume += 5;
                audio_set_volume(s->volume);
            }
            set_msg(s, "volume up");
        } else if (s->tab > 0) {
            s->tab--;
        }
        return;
    case KBD_DOWN:
        if (s->tab == 3) {
            if (s->volume > 0) {
                s->volume -= 5;
                audio_set_volume(s->volume);
            }
            set_msg(s, "volume down");
        } else if (s->tab < SET_TABS - 1) {
            s->tab++;
        }
        return;
    case '\n': {
        if (s->tab == 2) {
            /* clicked reboot/shutdown area approximated by Enter on tab 2 */
            acpi_poweroff();
            return;
        }
        if (s->tab == 3) {
            auth_set_password(auth_current_user(), s->new_pass);
            set_msg(s, "password set");
            s->pass_len = 0;
            s->new_pass[0] = '\0';
            return;
        }
        return;
    }
    case 0x08: case 0x7F:
        if (s->pass_len > 0) {
            s->pass_len--;
            s->new_pass[s->pass_len] = '\0';
        }
        return;
    default:
        if (s->tab == 3 && c >= 0x20 && s->pass_len < AUTH_PASS_MAX - 1) {
            s->new_pass[s->pass_len++] = c;
            s->new_pass[s->pass_len] = '\0';
        }
        return;
    }
}

static void settings_open(struct window *win)
{
    win->priv = (struct settings_state *)(usize)pmm_alloc_run(1);
    struct settings_state *s = st_of(win);
    memset(s, 0, sizeof(*s));
    s->tab = 0;
    s->volume = 100;
    audio_set_volume(100);
}

const struct app app_settings = {
    .name  = "settings",
    .open  = settings_open,
    .paint = settings_paint,
    .key   = settings_key,
    .close = NULL,
};
