/* =============================================================================
 *  Black OS - COM1 serial driver (used for automated verificaiton, see
 *  scripts/verify.py: the boot log is captured from COM1 as a file).
 * ========================================================================== */
#include "serial.h"
#include "io.h"

#define DATA(port)          ((port) + 0)
#define IER(port)           ((port) + 1)
#define FCR(port)           ((port) + 2)
#define LCR(port)           ((port) + 3)
#define MCR(port)           ((port) + 4)
#define LSR(port)           ((port) + 5)

#define LSR_THR_EMPTY       0x20

void serial_init(void)
{
    outb(IER(SERIAL_BASE), 0x00);       /* no interrupts, polling only */
    outb(LCR(SERIAL_BASE), 0x80);       /* DLAB on, set divisor */
    outb(DATA(SERIAL_BASE), 0x03);      /* divisor 3 -> 38400 baud */
    outb(IER(SERIAL_BASE), 0x00);
    outb(LCR(SERIAL_BASE), 0x03);       /* 8 bits, no parity, 1 stop */
    outb(FCR(SERIAL_BASE), 0xC7);       /* 16550 FIFO, clear, 14-byte trig */
    outb(MCR(SERIAL_BASE), 0x0B);       /* DTR + RTS + OUT2 */
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');              /* CRLF for terminal hosts */

    u64 spins = 0;
    while (!(inb(LSR(SERIAL_BASE)) & LSR_THR_EMPTY)) {
        if (++spins > 100000000ULL)     /* belt + braces under VM baud loss */
            return;
    }
    outb(DATA(SERIAL_BASE), (u8)c);
}

void serial_write(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
