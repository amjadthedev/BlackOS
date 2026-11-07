/* =============================================================================
 *  Black OS - Phase 4: i8259 PIC remap + interrupt gate management
 * ========================================================================== */
#include "pic.h"
#include "io.h"

void pic_init(void)
{
    /* ICW1: cascade + ICW4 needed. */
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    /* ICW2: remap vectors. Master IRQn -> 32+n, slave -> 40+n,
     * leaving 0x00-0x1F for CPU exceptions. */
    outb(PIC1_DATA, IRQ_OFFSET);
    io_wait();
    outb(PIC2_DATA, IRQ_OFFSET + 8);
    io_wait();

    /* ICW3 identity: master has slave on IRQ2; slave answers as IRQ2. */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: 8086 mode, normal EOI. */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* OCW1 masks: on the master, unmask only the devices Black OS uses at
     * this stage (timer IRQ0 + keyboard IRQ1). The slave stays fully masked
     * until device drivers come online. */
    outb(PIC1_DATA, (u8)~((1 << 0) | (1 << 1)));   /* 0xFC */
    outb(PIC2_DATA, 0xFF);
}

void pic_eoi(u8 irq)
{
    outb(PIC1_COMMAND, PIC_EOI);
    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
}

int pic_is_spurious(u8 irq)
{
    /* A spurious IRQ is one raised without the ISR bit set: only possible
     * on line 7 of the master or line 15 of the slave. */
    if (irq != 7 && irq != 15)
        return 0;

    u16 port = (irq == 7) ? PIC1_COMMAND : PIC2_COMMAND;
    outb(port, PIC_READ_ISR);           /* OCW3 read in-service register */
    u8 in_service = inb(port);

    if (in_service & (1 << (irq % 8)))
        return 0;                       /* real interrupt */

    /* Spurious: masters need no EOI for the fake line, but a spurious IRQ15
     * still requires an EOI sent to the master for the cascade line. */
    if (irq == 15)
        pic_eoi(2);
    return 1;
}

void pic_set_mask(u8 irq)
{
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 bit = (u8)(1 << (irq % 8));
    outb(port, (u8)(inb(port) | bit));
}

void pic_clear_mask(u8 irq)
{
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 bit = (u8)(1 << (irq % 8));
    outb(port, (u8)(inb(port) & (u8)~bit));
}

/* --------------------------------------------------------------- PIT tick --- */
#define PIT_CH0_DATA    0x40
#define PIT_CMD         0x43
#define PIT_BASE_FREQ   1193182uL

void pit_init(u32 hz)
{
    u32 divisor = PIT_BASE_FREQ / hz;
    outb(PIT_CMD, 0x36);                /* chan 0, lo/hi byte, square wave */
    outb(PIT_CH0_DATA, (u8)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (u8)((divisor >> 8) & 0xFF));
}
