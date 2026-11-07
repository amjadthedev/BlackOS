/* =============================================================================
 *  Black OS - Phase 11: PS/2 mouse driver (needed for dragging / snapping)
 *
 *  Device flow: enable aux port (0xA8), enable IRQ12 + clock line in the
 *  controller config byte, then send 0xF4 (enable data reporting).  IRQ12
 *  then delivers 3-byte packets:
 *      [0]: bit0 left   bit1 right  bit2 middle  bit3 always 1
 *           bit4 X-sign bit5 Y-sign  bit6/7 overflow
 *      [1]: signed X delta (9-bit: sign from [0] bit4)
 *      [2]: signed Y delta (9-bit: sign from [0] bit5, Y is inverted)
 *  Complete packets are queued in a small ring; mouse_pop() drains them
 *  without dropping button-only events.
 * ========================================================================== */
#include "ps2mouse.h"
#include "io.h"
#include "types.h"

#define PS2_CTRL        0x64
#define PS2_DATA        0x60

#define MOUSEQ_LEN      16

static struct mouse_event queue[MOUSEQ_LEN];
static volatile u8 q_head, q_tail;      /* head = producer, tail = consumer */

static volatile u8 pkt_state;           /* 0/1/2 bytes seen in packet       */
static volatile u8 pkt_btn;             /* decoded button mask              */
static volatile i32 pkt_dx, pkt_dy;     /* pending deltas                   */

void mouse_init(void)
{
    q_head = q_tail = 0;
    pkt_state = 0;

    outb(PS2_CTRL, 0xA8);               /* enable the auxiliary port        */

    outb(PS2_CTRL, 0x20);               /* read controller config           */
    u8 cfg = inb(PS2_DATA);
    cfg |= 0x02;                        /* IRQ12 enable                     */
    cfg &= (u8)~0x20;                   /* clear mouse clock inhibit        */
    outb(PS2_CTRL, 0x60);               /* next write = config              */
    outb(PS2_DATA, cfg);

    /* wait for the device input buffer to clear, then enable reporting    */
    u32 spins = 0;
    while ((inb(PS2_CTRL) & 0x02) && spins < 100000)
        spins++;
    outb(PS2_DATA, 0xF4);
}

static void queue_push(const struct mouse_event *ev)
{
    u8 next = (u8)((q_head + 1) % MOUSEQ_LEN);
    if (next == q_tail)
        return;                         /* full: drop the oldest overflow   */
    queue[q_head] = *ev;
    q_head = next;
}

void mouse_irq_handler(void)
{
    u8 b = inb(PS2_DATA);

    switch (pkt_state) {
    case 0:
        if (!(b & 0x08))
            return;                     /* resync: bit3 must be set         */
        pkt_btn = (u8)(b & 0x07);
        pkt_dx  = 0;
        pkt_dy  = 0;
        pkt_state = 1;
        return;
    case 1:
        pkt_dx = (b < 128) ? (i32)b : (i32)b - 256;   /* sign-extend       */
        pkt_state = 2;
        return;
    case 2: {
        i32 dy_raw = (b < 128) ? (i32)b : (i32)b - 256;
        pkt_dy = -dy_raw;               /* PS/2 Y grows downward on screen  */
        pkt_state = 0;

        struct mouse_event ev;
        ev.dx      = pkt_dx;
        ev.dy      = pkt_dy;
        ev.buttons = pkt_btn;
        queue_push(&ev);
        return;
    }
    }
}

int mouse_pop(struct mouse_event *out)
{
    if (out == NULL)
        return 0;
    if (q_tail == q_head)
        return 0;

    *out = queue[q_tail];
    q_tail = (u8)((q_tail + 1) % MOUSEQ_LEN);
    return 1;
}
