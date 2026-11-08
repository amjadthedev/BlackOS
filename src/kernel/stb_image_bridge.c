/* =============================================================================
 *  Black OS - stb_image_bridge.c : decode hook for PNG/JPG (Phase 1)
 * ========================================================================== */
#include "fb.h"
#include "printk.h"

int png_decode_stub(const void *data, int len, int *w, int *h, int *chans)
{
    printk("[stb] decode stub called %d bytes\n", len);
    if (w) *w = 0; if (h) *h = 0; if (chans) *chans = 0;
    (void)data;
    return 0;
}