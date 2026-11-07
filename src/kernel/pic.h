/* =============================================================================
 *  Black OS - Phase 4: i8259 PIC remap + interrupt gate management
 * ========================================================================== */
#ifndef BLACKOS_PIC_H
#define BLACKOS_PIC_H

#include "types.h"

#define PIC1_COMMAND        0x20
#define PIC1_DATA           0x21
#define PIC2_COMMAND        0xA0
#define PIC2_DATA           0xA1

#define PIC_EOI             0x20
#define PIC_READ_ISR        0x0B

#define IRQ_OFFSET          32          /* remapped base: IRQ0 -> vector 32  */

void pic_init(void);
void pic_eoi(u8 irq);
int  pic_is_spurious(u8 irq);           /* only lines 7 and 15 can be spurious */
void pic_set_mask(u8 irq);
void pic_clear_mask(u8 irq);

/* Phase 4 companion: program PIT channel 0 for a periodic tick. */
void pit_init(u32 hz);

#endif /* BLACKOS_PIC_H */
