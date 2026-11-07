/* =============================================================================
 *  Black OS - Phase 3: freestanding kprintf (subset printf) + dual console.
 *  Every byte goes to the VGA text screen and to COM1 so boots can be
 *  audited from a script (scripts/verify.py).
 * ========================================================================== */
#ifndef BLACKOS_PRINTK_H
#define BLACKOS_PRINTK_H

#include "types.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)

void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);
void kputc(char c);
void kputs(const char *s);

/* Panic (used by exception handlers): print, mask interrupts, halt forever. */
void __attribute__((noreturn, format(printf, 1, 2)))
panic(const char *fmt, ...);

#endif /* BLACKOS_PRINTK_H */
