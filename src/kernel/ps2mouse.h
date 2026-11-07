/* =============================================================================
 *  Black OS - Phase 11: PS/2 mouse driver (needed for dragging / snapping)
 * ========================================================================== */
#ifndef BLACKOS_PS2MOUSE_H
#define BLACKOS_PS2MOUSE_H

#include "types.h"

struct mouse_event {
    i32 dx, dy;                 /* delta since last packet, px              */
    u8  buttons;                /* bit0 left, bit1 right, bit2 middle       */
};

void mouse_init(void);                       /* 120 Hz sample, ints armed      */
void mouse_irq_handler(void);                /* wired into IRQ12 in idt.c      */
/* Non-blocking drain of queued events (dequeues one; 0 when empty).        */
int  mouse_pop(struct mouse_event *out);

#endif /* BLACKOS_PS2MOUSE_H */
