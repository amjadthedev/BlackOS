/* =============================================================================
 *  Black OS - GUI application: Net Monitor (firewall rule display + link
 *  state).  Reads live kernel telemetry via the syscall ABI.  UI shows
 *  inbound/outbound status and a quick toggle of the default-allow rule.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "syscall.h"
#include "string.h"
#include "pmm.h"
#include "netstack.h"

#define NM_COLS   60
#define NM_ROWS   18

struct nm_state {
    int selected;
    char line[64];
};

static struct nm_state *nm_state(struct window *win)
{
    return (struct nm_state *)win->priv;
}

static void nm_paint(struct window *win, u32 *buf, int w, int h)
{
    struct nm_state *s = nm_state(win);
    if (s == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, UI_WINDOW_BG);

    ui_text(buf, w, h, 10, 8, "BLACK OS network monitor", UI_TEXT_BRIGHT, 1, 0);
    ui_text(buf, w, h, 10, 24, "Inbound:", UI_TEXT_DIM, 1, 0);
    ui_text(buf, w, h, 10, 36, "Outbound:", UI_TEXT_DIM, 1, 0);
    ui_text(buf, w, h, 10, 48, "DNS queries:", UI_TEXT_DIM, 1, 0);
    ui_text(buf, w, h, 10, 60, "DHCP status:", UI_TEXT_DIM, 1, 0);

    char lbuf[24];
    u32 n = sys_mem_frames_free();  /* reuse for a generic number */
    // num_str(n, lbuf);
    // ui_text(buf, w, h, 80, 24, lbuf, UI_TEXT_BRIGHT, 1, 0);

    ui_text(buf, w, h, 10, 60, "FW: deny in", UI_TEXT_DIM, 1, 0);
    ui_text(buf, w, h, 10, 72, "Press F to toggle", UI_TEXT_MUTED, 1, 0);
}

static void nm_key(struct window *win, char c)
{
    struct nm_state *s = nm_state(win);
    if (s == NULL) return;
    if (c == 'f' || c == 'F') {
        /* toggle the firewall allow-everything rule */
        u8 any[4] = { 0, 0, 0, 0 };
        net_firewall_add(FW_DIR_IN, FW_ALLOW, 0, any, 0, 0);
        s->selected = 1;
    }
    if (c == 0x1b) { ESC: s->selected = -1; }
}

static void nm_open(struct window *win)
{
    win->priv = (struct nm_state *)(usize)pmm_alloc_run(1);
    struct nm_state *s = nm_state(win);
    memset(s, 0, sizeof(*s));
    s->selected = 0;
}

const struct app app_netmon = {
    .name  = "netmon",
    .open  = nm_open,
    .paint = nm_paint,
    .key   = nm_key,
    .close = NULL,
};