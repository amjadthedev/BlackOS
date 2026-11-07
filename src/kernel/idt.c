/* =============================================================================
 *  Black OS - Phase 4: IDT, exception dispatch, IRQ dispatch
 *
 *  * Vectors 0..31     : CPU exceptions (#DE divide, #PF page fault, #GP ...).
 *  * Vectors 32..47    : hardware lines after PIC remap (IRQ0 -> 0x20).
 *  * IRQ0              : PIT tick -> uptime_ms.
 *  * IRQ1              : keyboard -> hook into the shell.
 *
 *  The gate stubs live in src/kernel/isr.s; this file owns the table and
 *  the C-language dispatch.
 * ========================================================================== */
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "ps2mouse.h"
#include "printk.h"
#include "io.h"
#include "string.h"

/* ------------------------------------------------------------------ IDT --- */
#pragma pack(push, 1)
struct idt_gate {
    u16    offset_low;     /* bits 15:0  of handler address */
    u16    selector;       /* GDT code segment (0x08)       */
    u8     ist;            /* interrupt stack table slot    */
    u8     type_attr;      /* 0x8E = present, DPL0, 64-bit interrupt gate */
    u16    offset_mid;     /* bits 31:16 */
    u32    offset_high;    /* bits 63:32 */
    u32    reserved;
};                          /* 16 bytes */

struct idt_pointer {
    u16    limit;
    u64    base;
};                          /* 10 bytes */
#pragma pack(pop)

static struct idt_gate idt[IDT_ENTRIES_MAX] __attribute__((aligned(4096)));
static struct idt_pointer idtr;

/* Assembly-generated array of 48 stub addresses (isr0 .. isr47). */
extern void *isr_stub_table[IDT_ENTRIES_MAX];

volatile u64 uptime_ms = 0;

static void set_gate(u16 vector, void *handler)
{
    u64 addr = (u64)(usize)handler;
    struct idt_gate *g = &idt[vector];
    g->offset_low  = (u16)(addr & 0xFFFF);
    g->selector    = 0x08;              /* flat 64-bit kernel code segment */
    g->ist         = 0;
    g->type_attr   = 0x8E;              /* P=1, DPL=0, interrupt gate 64-bit */
    g->offset_mid  = (u16)((addr >> 16) & 0xFFFF);
    g->offset_high = (u32)(addr >> 32);
    g->reserved    = 0;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));

    for (u16 v = 0; v < IDT_ENTRIES_MAX; v++)
        set_gate(v, isr_stub_table[v]);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (u64)(usize)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    sti();                              /* gates are live: enable hardware */
}

/* ------------------------------------------------- exception diagnostics --- */
static const char *const exception_names[32] = {
    "#DE Divide Error",              "#DB Debug Exception",
    "#NMI Non-maskable Interrupt",   "#BP Breakpoint",
    "#OF Overflow",                  "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",            "#NM Device Not Available",
    "#DF Double Fault",              "#COP Co-processor Overrun",
    "#TS Invalid TSS",               "#NP Segment Not Present",
    "#SS Stack Segment Fault",       "#GP General Protection Fault",
    "#PF Page Fault",                "Reserved 15",
    "#MF x87 FP Exception",          "#AC Alignment Check",
    "#MC Machine Check",             "#XM/#XF SIMD FP Exception",
    "#VE Virtualisation Exception",  "#CP Control Protection",
    "Reserved 22",                   "Reserved 23",
    "Reserved 24",                   "Reserved 25",
    "Reserved 26",                   "Reserved 27",
    "#HV Hypervisor Injection",      "#VC VMM Communication",
    "#SX Security Exception",        "Reserved 31",
};

static void dump_frame(const struct isr_frame *f, const char *kind)
{
    kprintf("[fault] %s (vector %llu)\n", kind, f->int_no);
    kprintf("[fault] error code  = %llx\n", f->err);
    kprintf("[fault] RIP = %llX   CS = %llX   EFLAGS = %llX\n",
            f->rip, f->cs, f->rflags);
    kprintf("[fault] RSP = %llX   SS = %llX\n", f->rsp, f->ss);
    kprintf("[fault] RAX=%llX RBX=%llX RCX=%llX RDX=%llX\n",
            f->rax, f->rbx, f->rcx, f->rdx);
    kprintf("[fault] RSI=%llX RDI=%llX RBP=%llX\n", f->rsi, f->rdi, f->rbp);
    if (f->int_no == 14)    /* page fault: append CR2 (faulting address) */
        kprintf("[fault] CR2 (faulting address) = %llX\n", read_cr2());
}

static void handle_exception(struct isr_frame *f)
{
    dump_frame(f, (f->int_no < 32) ? exception_names[f->int_no] : "???");
    kprintf("This is an unrecoverable fault inside kernel mode: the CPU\n"
            "state above is preserved and the machine is halted by design\n"
            "(no userspace exists yet that could catch this).\n");
    panic("unhandled exception");
}

/* ------------------------------------------------------------ IRQ routing --- */
static void handle_irq(struct isr_frame *f)
{
    u8 irq = (u8)(f->int_no - 32);

    /* Timer: about 1 tick per millisecond (pit_init(1000)). */
    if (irq == 0) {
        uptime_ms++;
        pic_eoi(0);
        return;
    }

    /* Keyboard: read the scancode, translate, feed the shell. */
    if (irq == 1) {
        keyboard_irq_handler();
        pic_eoi(1);
        return;
    }

    /* PS/2 auxiliary (mouse): 3-byte packets decoded by the driver. */
    if (irq == 12) {
        mouse_irq_handler();
        pic_eoi(12);
        return;
    }

    /* Spurious IRQ detection (classic PIC only raises these on lines 7/15). */
    if (pic_is_spurious(irq))
        return;                         /* spurious: EOI handled internally */

    /* Any device we have not attached a driver to: just make sure the chip
     * keeps signalling by acknowledging the interrupt. */
    pic_eoi(irq);
}

/* ---------------------------------------------------------------- dispatch --- */
void interrupt_handler(struct isr_frame *frame)
{
    if (frame->int_no >= 32)
        handle_irq(frame);
    else
        handle_exception(frame);   /* unreachable in practice: noreturn */
}
