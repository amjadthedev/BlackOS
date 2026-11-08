/* =============================================================================
*  Black OS - app_about.c : About / credits panel
* ========================================================================== */
#include "app.h"
#include "win.h"
#include "ui.h"
#include "font.h"
#include "string.h"
#include "pmm.h"
#include "themes.h"

struct pan_state { int scroll; };

static struct pan_state *pan_of(struct window *w){ return (struct pan_state*)w->priv; }

static const char *credits[] = {
  "Repos referenced:",
  "github.com/bellard/quickjs",
  "github.com/svaarala/duktape",
  "github.com/nothings/stb",
  "Black OS - original author",
  "GRUB2 / multiboot: GNU Project",
  "NASM: dev team",
  "xorriso / libburnia",
  "QEMU: Fabrice Bellard and team",
  "NetSurf (libnsfb ref): NetSurf developers",
  "QuickJS (JS engine): Fabrice Bellard",
  "Duktape (alt JS engine): Sami Vaarala",
  "stb_image (image decoding ref): Sean Barrett et al.",
  "Tiny C Compiler (tcc project)",
  "GCC/binutils: GNU Project",
  "Hyper-V integration compliance",
  "kprintf/printf emulation: own",
  "Linux syscall ABI adoption",
  "AC97/Audio standard",
  "Windows PE ABI adoption (strategy)",
  "Cygwin MSYS2 toolchain contributors",
  "Every OSS developer who helps us",
  NULL,
};

static void about_paint(struct window *win,u32*buf,int w,int h)
{
    struct pan_state *s = pan_of(win);
    if(!s) return;
    ui_fill(buf,w,h,0,0,w,h,g_panel_bg);
    ui_text(buf,w,h,12,8,"About Black OS",g_accent,0,0);
    int y = 28;
    int scroll = s->scroll;
    int shown = 0;
    unsigned int cr = 0;
    for (int i=0 ; i<128 && credits[cr]!=NULL; i++,cr++) {
        if (i < scroll) continue;
        if (shown >= 12) break;
        ui_text(buf,w,h,12, y+shown*14, credits[cr], g_text_bright, 0, 0);
        shown++;
    }
    ui_text(buf,w,h,12,h-16,"PageUp/PageDown to scroll", g_text_dim, 0,0);
}

static void about_key(struct window *win,char c)
{
    struct pan_state *s = pan_of(win);
    if (!s) return;
    if (c=='+' || c=='+' ) { if(s->scroll<4) s->scroll++; }
    if (c=='-'|| c=='_') { if(s->scroll>0) s->scroll--; }
    if (c=='\n' || c=='\r') s->scroll=0;
}

static void about_open(struct window *win)
{
    win->priv=(struct pan_state *)(usize)pmm_alloc_run(1);
    struct pan_state *s=(struct pan_state *)win->priv;
    memset(s,0,sizeof(*s));
}

const struct app app_about = {
    .name="about",
    .open=about_open,
    .paint=about_paint,
    .key=about_key,
    .close=NULL,
};

/* app_about declared above (non-static) */