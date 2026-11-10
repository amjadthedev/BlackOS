/* =============================================================================
 *  Black OS - port I/O, MSR access, control-register helpers
 * ========================================================================== */
#ifndef BLACKOS_IO_H
#define BLACKOS_IO_H

#include "types.h"

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outw(u16 port, u16 value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u16 inw(u16 port)
{
    u16 v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outl(u16 port, u32 value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void io_wait(void)            /* ~1 us probe-port delay */
{
    outb(0x80, 0);
}

static inline void cli(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile ("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile ("hlt"); }

static inline u64 read_cr0(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(u64 v)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"(v) : "memory");
}

static inline u64 read_cr4(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4(u64 v)
{
    __asm__ volatile ("mov %0, %%cr4" : : "r"(v) : "memory");
}

/* SSE/x87 runtime: kernel code is built with -mno-sse, but Duktape is not
 * (GCC refuses some of its float struct returns without SSE), so the CPU
 * feature must be turned on before any JS runs.  No kernel code emits SSE,
 * so leaving the unit idle outside JS evaluation is harmless. */
static inline void cpu_enable_sse(void)
{
    u32 mxcsr = 0x1F80;                     /* default: all exceptions masked */

    write_cr0(read_cr0() & ~((1ULL << 2) | (1ULL << 3)));  /* CR0.EM=0 TS=0  */
    write_cr4(read_cr4() | (1ULL << 9) | (1ULL << 10));    /* FXSR | OSXMMEXCPT */
    __asm__ volatile ("fninit");
    __asm__ volatile ("ldmxcsr %0" : : "m"(mxcsr));
}

static inline u64 read_cr3(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline u64 read_cr2(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline u64 rdmsr(u32 msr)
{
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void wrmsr(u32 msr, u64 value)
{
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(msr) : "memory");
}

#endif /* BLACKOS_IO_H */
