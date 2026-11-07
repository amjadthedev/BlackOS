/* =============================================================================
 *  Black OS - lockscreen.c : full-screen lock with password + live clock/weather
 *
 *  Drawn entirely onto the linear framebuffer.  Loop:
 *      - RTC clock (seconds resolution) + date
 *      - weather card from weather_poll() / weather_get()
 *      - password field (masked with •), backspace, Enter to submit
 *      - wrong password: red shake + flash
 *      - right password: fade out and return 0 (caller resumes the shell)
 * ========================================================================== */
#include "lockscreen.h"
#include "auth.h"
#include "fb.h"
#include "font.h"
#include "ui.h"
#include "rtc.h"
#include "weather.h"
#include "keyboard.h"
#include "ps2mouse.h"
#include "netstack.h"
#include "string.h"
#include "idt.h"

/*───────────────────────────── drawing helpers ───────────────────────────────*/
static void px_text(const char *s, int x, int y, int scale, u32 fg)
{
    for (int k = 0; s[k]; k++) {
        const u8 *g = font_glyph(s[k]);
        for (int row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col)))
                    continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_pixel(x + k * 8 * scale + col * scale + sx,
                                 y + row * scale + sy, fg);
            }
        }
    }
}

static int text_w(const char *s) { return (int)strlen(s) * 8; }

static void draw_avatar(int cx, int cy, int r, const char *initial, u32 fg)
{
    /* circle */
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                fb_pixel(cx + dx, cy + dy, fg);
    /* inner darker ring */
    for (int dy = -r + 2; dy <= r - 2; dy++)
        for (int dx = -r + 2; dx <= r - 2; dx++)
            if (dx * dx + dy * dy <= (r - 2) * (r - 2))
                fb_pixel(cx + dx, cy + dy, 0x0A1020);
    /* initial */
    px_text(initial, cx - 4, cy - 4, 2, fg);
}

static void draw_weather_card(int x, int y, const struct weather_data *w)
{
    fb_rect_rounded(x, y, 230, 86, 10, 0x0E182A);
    fb_rounded_frame(x, y, 230, 86, 10, 0x2A4468);

    if (!w->valid) {
        px_text("weather: updating…", x + 16, y + 36, 1, UI_TEXT_DIM);
        return;
    }

    char temp[10];
    u32 ti = 0;
    temp[ti++] = (w->temperature < 0) ? '-' : '+';
    int t = (w->temperature < 0) ? -w->temperature : w->temperature;
    if (t >= 10)
        temp[ti++] = (char)('0' + t / 10);
    temp[ti++] = (char)('0' + t % 10);
    temp[ti++] = 'C';
    temp[ti] = '\0';
    px_text(temp, x + 18, y + 26, 3, UI_TEXT_BRIGHT);

    static const char *const icons[10] = {
        "O", "C", "C", "F", "D", "R", "R", "S", "T", "?"
    };
    const char *icon = (w->condition < 10) ? icons[w->condition] : icons[9];
    int icol;
    switch (w->condition) {
    case W_CLEAR:       icol = 0xE0C030; break;
    case W_CLOUDY:      icol = 0x9BADCB; break;
    case W_OVERCAST:    icol = 0x6B7E96; break;
    case W_FOG:         icol = 0xEAD8C4; break;
    case W_DRIZZLE:     icol = 0x7FA8D0; break;
    case W_RAIN:        icol = 0x4E9DD8; break;
    case W_HEAVY_RAIN:  icol = 0x3C7FB0; break;
    case W_SNOW:        icol = 0xEFF6FF; break;
    case W_THUNDER:     icol = 0xE0D040; break;
    default:            icol = UI_TEXT_DIM; break;
    }
    px_text(icon, x + 172, y + 18, 4, icol);

    char loc[40];
    u32 li = 0;
    for (const char *p = w->city; *p && li + 1 < sizeof(loc); p++)
        loc[li++] = *p;
    if (w->area[0] && li + 2 < sizeof(loc) - 1) {
        loc[li++] = ','; loc[li++] = ' ';
        for (const char *p = w->area; *p && li + 1 < sizeof(loc) - 1; p++)
            loc[li++] = *p;
    }
    loc[li] = '\0';
    px_text(loc, x + 18, y + 60, 1, UI_TEXT_DIM);
}

/*───────────────────────────── lock screen loop ──────────────────────────────*/
void lockscreen_run(void)
{
    int mx = (int)fb_width / 2, my = (int)fb_height / 2;
    char pass[64];
    int plen = 0;
    int wrong = 0;
    u32 wrong_ms = 0;
    u32 last_sec = 0xFFFFFFFFu;
    int unlock = 0;

    weather_init();

    for (;;) {
        u32 now = uptime_ms;

        /**************** backdrop + weather poll ****************/
        fb_contrast(0x0A1428, 0x02040A);
        fb_blur_region(40, 40, (int)fb_width - 80, (int)fb_height - 120, 6);

        weather_poll();
        net_poll();

        /**************** live clock *****************/
        if (now / 1000 != last_sec) {
            last_sec = now / 1000;
        }
        struct rtc_time t;
        char clock_line[16];
        if (rtc_get(&t) == 0) {
            clock_line[0]  = (char)('0' + t.hour / 10);
            clock_line[1]  = (char)('0' + t.hour % 10);
            clock_line[2]  = ':';
            clock_line[3]  = (char)('0' + t.minute / 10);
            clock_line[4]  = (char)('0' + t.minute % 10);
            clock_line[5]  = ':';
            clock_line[6]  = (char)('0' + t.sec / 10);
            clock_line[7]  = (char)('0' + t.sec % 10);
            clock_line[8]  = '\0';
        } else {
            clock_line[0] = '-'; clock_line[1] = '-'; clock_line[2] = ':';
            clock_line[3] = '-'; clock_line[4] = '-'; clock_line[5] = ':';
            clock_line[6] = '-'; clock_line[7] = '-'; clock_line[8] = '\0';
        }

        static const char *const wdays[7] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
        };
        static const char *const months[13] = {
            "", "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };
        char date_line[44];
        const char *wday = (t.wday >= 1 && t.wday <= 7) ? wdays[t.wday - 1] : wdays[0];
        u32 di = 0;
        for (const char *p = wday; *p && di < 3; p++)
            date_line[di++] = *p;
        date_line[di++] = ' ';
        for (const char *p = months[t.month]; *p && di < 20; p++)
            date_line[di++] = *p;
        date_line[di++] = ' ';
        if (t.day >= 10)
            date_line[di++] = (char)('0' + t.day / 10);
        date_line[di++] = (char)('0' + t.day % 10);
        date_line[di++] = ',';
        date_line[di++] = ' ';
        u16 y = t.year; char ybuf[6]; int yi = 0;
        while (y > 0 && yi < 5) { ybuf[yi++] = (char)('0' + y % 10); y /= 10; }
        while (yi > 0 && di < 38) date_line[di++] = ybuf[--yi];
        date_line[di] = '\0';

        /**************** avatar + greeting ****************/
        int cx = (int)fb_width / 2;
        int cy = 150;
        int shake_x = 0;
        if (wrong && now - wrong_ms < 600) {
            shake_x = (((now - wrong_ms) / 50) % 3) - 1;
            shake_x *= 6;
        }
        draw_avatar(cx + shake_x, cy, 34, auth_current_user(), UI_ACCENT_CYAN);
        px_text("Welcome back,", cx - text_w("Welcome back,") / 2 + shake_x,
                cy + 46, 1, UI_TEXT_DIM);
        px_text(auth_current_user(), cx - text_w(auth_current_user()) / 2 + shake_x,
                cy + 58, 1, UI_TEXT_BRIGHT);

        /**************** clock *****************/
        int cw = text_w(clock_line) * 4;
        px_text(clock_line, (cx - cw / 2) + shake_x, 210, 4, UI_TEXT_BRIGHT);
        int dw = text_w(date_line);
        px_text(date_line, cx - dw / 2 + shake_x, 270, 1, UI_TEXT_DIM);

        /**************** weather *****************/
        struct weather_data w;
        weather_get(&w);
        draw_weather_card((int)fb_width / 2 - 115, 300, &w);

        /**************** password field *****************/
        int py = 430;
        char masked[68];
        for (int i = 0; i < plen; i++)
            masked[i] = '•';
        masked[plen] = '\0';
        int pw = text_w(masked);
        int shake_pw = wrong && now - wrong_ms < 600 ? shake_x : 0;

        /* field bg */
        fb_rect_rounded(cx - pw / 2 - 18, py - 6, pw + 36, 34, 8, 0x0E182A);
        fb_rounded_frame(cx - pw / 2 - 18, py - 6, pw + 36, 34, 8,
                         wrong ? 0xD42222 : 0x2A4468);
        px_text(masked, cx - pw / 2 + shake_pw, py, 1, UI_TEXT_BRIGHT);
        /* cursor blink */
        if ((now / 300) & 1)
            fb_fill_rect(cx - pw / 2 + plen + shake_pw, py + 2, 2, 8, UI_ACCENT_CYAN);

        px_text("password", cx - pw / 2 - 14 + shake_pw, py - 20, 1, UI_TEXT_MUTED);

        if (wrong && now - wrong_ms > 0 && now - wrong_ms < 600) {
            px_text("wrong password", cx - text_w("wrong password") / 2 + shake_x,
                    py + 42, 1, UI_ACCENT_RED);
        }

        const char *hint = wrong ? "try again" : "press enter to unlock";
        px_text(hint, cx - text_w(hint) / 2 + shake_x, py + 62, 1, UI_TEXT_MUTED);

        /**************** mouse cursor *****************/
        struct mouse_event mev;
        while (mouse_pop(&mev)) {
            mx += mev.dx; my += mev.dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if ((u32)mx > fb_width - 4) mx = (int)fb_width - 4;
            if ((u32)my > fb_height - 4) my = (int)fb_height - 4;
        }
        fb_rect_rounded(mx, my, 2, 12, 1, UI_TEXT_BRIGHT);
        fb_rect_rounded(mx - 6, my + 12, 12, 2, 1, UI_TEXT_BRIGHT);

        /**************** keyboard *****************/
        char c;
        while (keyboard_pop(&c)) {
            if (c == '\n' || c == '\r') {
                if (auth_login(auth_current_user(), pass)) {
                    unlock = 1;
                    break;
                }
                wrong = 1;
                wrong_ms = now;
                plen = 0;
                pass[0] = '\0';
            } else if (c == 0x08 || c == 0x7F) {   /* backspace */
                if (plen > 0) {
                    plen--;
                    pass[plen] = '\0';
                }
            } else if (c >= 0x20 && plen < (int)sizeof(pass) - 1) {
                pass[plen++] = c;
                pass[plen] = '\0';
            }
        }

        if (unlock)
            return;

        hlt();
    }
}
