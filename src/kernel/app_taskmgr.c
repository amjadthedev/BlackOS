/* =============================================================================
 *  Black OS - GUI application: Task Manager (Phase 9 kernel ABI consumer)
 *
 *  Renders live kernel telemetry over the stable ABI: task table (name/pid/
 *  state), RAM pressure, window count. Keyboard selection via arrows.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "keyboard.h"
#include "syscall.h"
#include "pmm.h"
#include "string.h"

struct tmgr_state {
    int selected;
};

static struct tmgr_state *tmgr_state(struct window *win)
{
    return (struct tmgr_state *)win->priv;
}

static void tmgr_open(struct window *win)
{
    struct tmgr_state *self = (struct tmgr_state *)(usize)pmm_alloc_run(1);
    memset(self, 0, sizeof(*self));
    win->priv = self;
}

static void num_str(u64 v, char out[24])
{
    int i = 0;
    if (v == 0)
        out[i++] = '0';
    while (v > 0 && i < 22) {
        out[i++] = (char)('0' + (int)(v % 10ULL));
        v /= 10ULL;
    }
    for (int l = 0, r = i - 1; l < r; l++, r--) {
        char t = out[l];
        out[l] = out[r];
        out[r] = t;
    }
    out[i] = '\0';
}

static void tmgr_paint(struct window *win, u32 *buf, int w, int h)
{
    struct tmgr_state *self = tmgr_state(win);
    if (self == NULL)
        return;

    ui_fill(buf, w, h, 0, 0, w, h, UI_WINDOW_BG);

    int y = 6;
    ui_text(buf, w, h, 10, y, "BLACK OS task manager", UI_TEXT_BRIGHT, 1, 0);
    y += 14;

    char tmp[24];
    int bar_w = (w - 170 > 0) ? w - 170 : 1;

    /* RAM panel */
    u64 total_b = sys_mem_total_bytes();
    u64 used_b  = sys_mem_used_bytes();
    int pct     = total_b ? (int)(used_b * 100ULL / total_b) : 0;
    ui_text(buf, w, h, 10, y + 1, "RAM", UI_TEXT_DIM, 1, 0);
    ui_progress(buf, w, h, 50, y, bar_w, 12, pct, UI_ACCENT_CYAN);
    num_str((u64)pct, tmp);
    {
        int tl = (int)strlen(tmp);
        tmp[tl] = '%';
        tmp[tl + 1] = '\0';
    }
    ui_text(buf, w, h, 56 + bar_w, y + 2, tmp, UI_TEXT_BRIGHT, 1, 0);
    y += 18;

    /* window panel */
    u32 wins = sys_window_count();
    ui_text(buf, w, h, 10, y + 1, "GUI", UI_TEXT_DIM, 1, 0);
    ui_progress(buf, w, h, 50, y, bar_w, 12, wins * 100 / WIN_MAX,
                UI_ACCENT_GREEN);
    num_str(wins, tmp);
    ui_text(buf, w, h, 56 + bar_w, y + 2, tmp, UI_TEXT_BRIGHT, 1, 0);
    y += 22;

    /* task table */
    ui_text(buf, w, h, 10, y, "pid", UI_TEXT_MUTED, 1, 0);
    ui_text(buf, w, h, 50, y, "task", UI_TEXT_MUTED, 1, 0);
    ui_text(buf, w, h, w - 90, y, "state", UI_TEXT_MUTED, 1, 0);
    y += 14;

    u32 n = sys_task_count();
    for (u32 i = 0; i < n && y + 10 < h - 8; i++) {
        struct sys_task st;
        if (sys_task_get(i, &st) != 0)
            continue;
        int sel = ((int)i == self->selected);
        if (sel)
            ui_fill(buf, w, h, 3, y - 2, w - 6, 12, UI_TASKBAR_HI);

        num_str(st.pid, tmp);
        ui_text(buf, w, h, 10, y, tmp, sel ? UI_TEXT_BRIGHT : UI_TEXT_DIM, 1, 0);
        ui_text(buf, w, h, 50, y, st.name,
                sel ? UI_TEXT_BRIGHT : UI_TEXT_DIM, 1, 0);
        ui_text(buf, w, h, w - 90, y, st.state ? "backgrnd" : "running",
                sel ? UI_ACCENT_CYAN : UI_TEXT_MUTED, 1, 0);
        y += 12;
    }
}

static void tmgr_key(struct window *win, char c)
{
    struct tmgr_state *self = tmgr_state(win);
    if (self == NULL)
        return;

    u32 n = sys_task_count();
    switch (c) {
    case KBD_UP:
        if (self->selected > 0)
            self->selected--;
        break;
    case KBD_DOWN:
        if (self->selected < (int)n - 1)
            self->selected++;
        break;
    default:
        break;
    }
}

const struct app app_taskmgr = {
    .name  = "task manager",
    .open  = tmgr_open,
    .paint = tmgr_paint,
    .key   = tmgr_key,
    .close = NULL,
};
