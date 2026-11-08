/* =============================================================================
 *  Black OS - app_clock.c : world clock + calendar panel
 * ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "string.h"
#include "pmm.h"
#include "themes.h"
#include "idt.h"
#include "rtc.h"

struct clock_state {
    int tick;
};

static struct clock_state *cl_of(struct window *win){ return (struct clock_state *)win->priv; }

static void dd (int c) { (void)c; }

static int dts(char *buf, int val)
{
    int n = 0;
    if (val >= 100) buf[n++] = '0' + val/100;
    if (val >= 10)  buf[n++] = '0' + (val/10)%10;
    buf[n++] = '0' + val%10;
    buf[n] = 0;
    return n;
}

static void clk_paint(struct window *win,u32*buf,int w,int h)
{
    struct clock_state *s = cl_of(win);
    if(!s) return;
    struct rtc_time t;
    ui_fill(buf,w,h,0,0,w,h,g_panel_bg);
    ui_text(buf,w,h,12,8,"Clock",g_accent,0,0);
    if(rtc_get(&t)==0){
        char tt[16];
        int n=0;
        /* hour */
        tt[n] = '0' + t.hour/10; n++;
        tt[n] = '0' + t.hour%10; n++;
        tt[n] = ':'; n++;
        tt[n] = '0' + t.minute/10; n++;
        tt[n] = '0' + t.minute%10; n++;
        tt[n] = ':'; n++;
        tt[n] = '0' + t.sec/10; n++;
        tt[n] = '0' + t.sec%10; n++;
        tt[n]=0;
        /* big centered */
        ui_text(buf,w,h, w/2 - 40, h/2 - 30, tt, g_text_bright,0,0);
        ui_text(buf,w,h,12,44,"Date",g_text_dim,0,0);
        char dd[32];
        int dn=0;
        dd[dn]='0'+(t.day/10)%10; dn++;
        dd[dn]='0'+t.day%10; dn++;
        dd[dn]='/'; dn++;
        dd[dn]='0'+(t.month/10)%10; dn++;
        dd[dn]='0'+t.month%10; dn++;
        dd[dn]='/'; dn++;
        dd[dn]='2'; dn++; dd[dn]='0'; dn++; dd[dn]=(char)('0'+(t.year/10)%10); dn++; dd[dn]=(char)('0'+(t.year%10)); dn++; dd[dn]=0;
        ui_text(buf,w,h,70,44,dd,g_text_bright,0,0);
    } else {
        ui_text(buf,w,h,12,40,"RTC unavailable", g_text_dim,0,0);
    }
    ui_text(buf,w,h,12,h-16,"tick="+ (s->tick%10)%10, g_text_dim,0,0);
    s->tick++;
}

static void clk_key(struct window *win, char c)
{
    (void)win;(void)c;
}

static void clk_open(struct window *win)
{
    win->priv = (struct clock_state *)(usize)pmm_alloc_run(1);
    struct clock_state *s = (struct clock_state*)win->priv;
    memset(s, 0, sizeof(*s));
}

const struct app app_clock = {
    .name="clock",
    .open = clk_open,
    .paint = clk_paint,
    .key = clk_key,
    .close=NULL,
};

/* app_clock defined above */