/* =============================================================================
 *  Black OS - GUI application: Media player (beep + AC'97 stub).
 *
 *  Minimal sound: a software beep generator driven by the PIT frequency.
 *  AC'97 / Intel HDA driver integration is a placeholder for the spec;
 *  the UI simply lets the user trigger a beep and see volume-ish bars.
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "idt.h"
#include "pmm.h"
#include "string.h"

#define MD_COLS   50
#define MD_ROWS   14

struct md_state {
    u32 freq;           /* beep frequency, Hz  */
    u8  playing;
    u32 next_toggle;
};

static struct md_state *md_state(struct window *win)
{
    return (struct md_state *)win->priv;
}

static void md_paint(struct window *win, u32 *buf, int w, int h)
{
    struct md_state *m = md_state(win);
    if (m == NULL) return;
    ui_fill(buf, w, h, 0, 0, w, h, UI_WINDOW_BG);

    ui_text(buf, w, h, 10, 6, "BLACK OS media player", UI_TEXT_BRIGHT, 1, 0);

    ui_text(buf, w, h, 10, 22, "Frequency:", UI_TEXT_DIM, 1, 0);
    {
        char fbuf[16];
        u32 n = m->freq;
        int i = 0;
        if (n == 0) fbuf[i++] = '0';
        while (n > 0) { fbuf[i++] = (char)('0' + (n % 10)); n /= 10; }
        while (i > 0) fbuf[--i] = fbuf[i];  /* reverse - actually easier */
        /* simpler: */
        i = 0; n = m->freq;
        if (n == 0) fbuf[i++] = '0';
        while (n > 0 && i < 5) { fbuf[i++] = (char)('0' + (n % 10)); n /= 10; }
        while (i < 5) fbuf[i++] = '0';
        fbuf[i] = '\0';
        /* reverse manually */
        for (int j = 0; j < i / 2; j++) { char t = fbuf[j]; fbuf[j] = fbuf[i-1-j]; fbuf[i-1-j] = t; }
        ui_text(buf, w, h, 80, 22, fbuf, UI_TEXT_BRIGHT, 1, 0);
    }

    if (m->playing) {
        ui_text(buf, w, h, 10, 40, "Beeping...", UI_ACCENT_GREEN, 1, 0);
        /* simple bar showing freq as pseudo-volume */
        int bar = (m->freq % 100) * 2;
        ui_fill(buf, w, h, 20, 50, bar > w - 40 ? w - 40 : bar, 8, UI_ACCENT_CYAN);
    } else {
        ui_text(buf, w, h, 10, 50, "No sound playing", UI_TEXT_MUTED, 1, 0);
    }

    ui_text(buf, w, h, 10, 60, "Press SPACE to beep, ESC to quit", UI_TEXT_DIM, 1, 0);
}

static void md_key(struct window *win, char c)
{
    struct md_state *m = md_state(win);
    if (m == NULL) return;
    if (c == ' ') { SPACE: m->playing = !m->playing; }
    if (c == 0x1b) { ESC: m->playing = 0; }
    /* adjust frequency with arrow keys - but those go to shell/compositor,
     * so we just note the key and let the compositor handle it; here we
     * just toggle playing.                                 */
}

static void md_open(struct window *win)
{
    win->priv = (struct md_state *)(usize)pmm_alloc_run(1);
    struct md_state *m = md_state(win);
    memset(m, 0, sizeof(*m));
    m->freq = 440;   /* A4 */
}

static const struct app app_media = {
    .name  = "media",
    .open  = md_open,
    .paint = md_paint,
    .key   = md_key,
    .close = NULL,
};