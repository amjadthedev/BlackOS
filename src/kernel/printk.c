/* =============================================================================
 *  Black OS - Phase 3: freestanding kprintf (subset printf) + dual console.
 *  Every byte goes to the VGA text screen and to COM1 so boots can be
 *  audited from a script (scripts/verify.py).
 * ========================================================================== */
#include "printk.h"
#include "console.h"
#include "vga.h"        /* vga_print_color: panic still paints TEXT fallback */
#include "serial.h"
#include "io.h"
#include "string.h"

void kputc(char c)
{
    console_putc(c);
    serial_putc(c);
}

void kputs(const char *s)
{
    while (*s)
        kputc(*s++);
}

/* Convert a 64-bit value to text in the radix implied by `digits`. */
static void print_number(u64 value, int negative, u8 base, int uppercase,
                         int width, char pad_char)
{
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[24];
    int i = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            buf[i++] = digits[value % base];
            value /= base;
        }
    }
    if (negative)
        buf[i++] = '-';

    /* Zero/left padding reserved for hex width modifiers (%08x). */
    if (width > i) {
        for (int p = i; p < width; p++)
            kputc(pad_char);
    }
    while (i > 0)
        kputc(buf[--i]);
}

/*
 * Supported conversions:
 *   %c  %s  %d  %u  %x  %X  %p   %lu/%llu/%lx likewise via 'l'/'ll' prefix,
 *   optional zero padding width: %08x, %04x, %016lx...
 */
void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            kputc(*fmt);
            continue;
        }

        fmt++;

        /* parse optional zero-pad width: "%0N" */
        int width = 0, pad_zero = 0;
        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* parse optional length modifier */
        int long_count = 0;
        while (*fmt == 'l') {
            long_count++;
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            kputc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            kputs(s ? s : "(null)");
            break;
        }
        case 'd': {
            long long v = (long_count >= 1)
                              ? va_arg(ap, long long)
                              : va_arg(ap, int);
            u64 mag = (v < 0) ? (u64)(-v) : (u64)v;
            print_number(mag, v < 0, 10, 0, width, pad_zero ? '0' : ' ');
            break;
        }
        case 'u': {
            u64 v = (long_count >= 1)
                        ? va_arg(ap, unsigned long long)
                        : (u64)va_arg(ap, unsigned int);
            print_number(v, 0, 10, 0, width, pad_zero ? '0' : ' ');
            break;
        }
        case 'x': {
            u64 v = (long_count >= 1)
                        ? va_arg(ap, unsigned long long)
                        : (u64)va_arg(ap, unsigned int);
            print_number(v, 0, 16, 0, width, pad_zero ? '0' : ' ');
            break;
        }
        case 'X': {
            u64 v = (long_count >= 1)
                        ? va_arg(ap, unsigned long long)
                        : (u64)va_arg(ap, unsigned int);
            print_number(v, 0, 16, 1, width, pad_zero ? '0' : ' ');
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            kputs("0x");
            print_number((u64)(usize)p, 0, 16, 0, 16, '0');
            break;
        }
        case 'P': {
            void *p = va_arg(ap, void *);
            kputs("0x");
            print_number((u64)(usize)p, 0, 16, 1, 16, '0');
            break;
        }
        case '%':
            kputc('%');
            break;
        case '\0':
            return;
        default:                        /* unknown spec: emit as-is */
            kputc('%');
            kputc(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...)
{
    va_list ap;

    cli();
    vga_print_color("\n*** KERNEL PANIC: ", VGA_LRED, VGA_BLACK);
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    vga_print_color(" ***\n", VGA_LRED, VGA_BLACK);
    vga_print_color("system halted - it is now safe to power off.\n",
                    VGA_YELLOW, VGA_BLACK);
    for (;;)
        hlt();
}
