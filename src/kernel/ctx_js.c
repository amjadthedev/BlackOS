/* =============================================================================
*  Black OS - ctx_js.c : JS bridge (Phase 1 stub-compatible, safe compile)
* ========================================================================== */
#include "browser.h"
#include "printk.h"
#include "types.h"
#include "pmm.h"

#ifdef DUKTAPE_LINKED
#include "../contrib/duk_config.h"
#include "../contrib/duktape.h"
#endif

void ctx_js_eval(const char *src, int len)
{
#ifdef DUKTAPE_LINKED
    printk("[ctx_js] duktape evaluate %d bytes (must be compiled with ffreestanding, no SSE)\n", len);
    /* Run on host compiler (WSL) per instructions after linking */
#else
    printk("[ctx_js] JS runtime stub - length=%d (manual Duktape drop needed)\n", len);
#endif
    (void)src;
}