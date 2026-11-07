/* =============================================================================
 *  Black OS - Phase 4: IDT / exception / IRQ plumbing
 * ========================================================================== */
#ifndef BLACKOS_IDT_H
#define BLACKOS_IDT_H

#include "types.h"

#define IDT_ENTRIES_MAX   48              /* 0-31 CPU exceptions, 32-47 IRQ0-15 */

/* Register snapshot laid out by src/kernel/isr.s at handler entry. */
struct isr_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no;      /* vector number pushed by the stub */
    u64 err;         /* CPU error code (fake 0 for exceptions without one) */
    u64 rip, cs, rflags, rsp, ss;
};

void idt_init(void);
void interrupt_handler(struct isr_frame *frame);

/* Millisecond uptime ticked by the IRQ0 (PIT) handler. */
extern volatile u64 uptime_ms;

#endif /* BLACKOS_IDT_H */
