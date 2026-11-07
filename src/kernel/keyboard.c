/* =============================================================================
 *  Black OS - Phase 4: PS/2 keyboard driver (scancode set 1 translation)
 *
 *  IRQ1 delivers scancodes; this file translates them to ASCII and pushes
 *  complete characters into a small ring buffer.  interrupt_handler() (the
 *  dispatcher in idt.c) drains one character per IRQ via keyboard_pop() and
 *  forwards it into the shell hook.
 * ========================================================================== */
#include "keyboard.h"
#include "idt.h"
#include "io.h"

#define KB_DATA     0x60
#define KB_STATUS   0x64

#define KB_BUF_LEN  64

static char kb_buf[KB_BUF_LEN];
static volatile usize kb_head, kb_tail;

static u8 left_shift, right_shift, escaped;

static keyboard_hook_t hook;

void keyboard_set_hook(keyboard_hook_t fn)
{
    hook = fn;
}

/* Black OS arrow-key control codes (extended set-1 scancodes)            */
#define KBD_UP      0x91
#define KBD_DOWN    0x92
#define KBD_LEFT    0x93
#define KBD_RIGHT   0x94

/* Scancode set 1 -> ASCII. Index = make code. */
static const char map_normal[0x60] = {
    0,   0x1B, '1', '2', '3', '4', '5', '6',      /* 0x01 ESC ... */
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0, 'a', 's',       /* 0x1C enter, 0x1D ctrl */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',      /* 0x2A lshift */
    'b', 'n', 'm', ',', '.', '/', 0, '*',        /* 0x36 rshift, kp */
    0, ' ', 0,                                    /* 0x39 space */
};

static const char map_shift[0x60] = {
    0, 0x1B, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0,
};

static void push_char(char c)
{
    usize next = (kb_head + 1) % KB_BUF_LEN;
    if (next == kb_tail)          /* full: drop the keystroke */
        return;
    kb_buf[kb_head] = c;
    kb_head = next;
}

int keyboard_pop(char *out)
{
    if (kb_tail == kb_head)
        return 0;
    *out = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_LEN;
    return 1;
}

static void translate(u8 sc)
{
static void translate(u8 sc)
{
    /* Handle prefix escapes (0xE0 for extended keys: arrows, etc.). */
    if (sc == 0xE0) {
        escaped = 1;
        return;
    }
    if (escaped) {
        escaped = 0;
        switch (sc) {
        case 0x48: push_char(KBD_UP);    return;
        case 0x50: push_char(KBD_DOWN);  return;
        case 0x4B: push_char(KBD_LEFT);  return;
        case 0x4D: push_char(KBD_RIGHT); return;
        default:   return;               /* extensions not consumed yet */
        }
    }

    switch (sc) {
    case 0x2A:  left_shift  = 1; return;
    case 0x36:  right_shift = 1; return;
    case 0xAA:  left_shift  = 0; return;
    case 0xB6:  right_shift = 0; return;
    default:
        break;
    }

    if (sc & 0x80)                  /* key release: nothing to consume */
        return;
    if (sc >= sizeof(map_normal))
        return;

    u8 shift = (u8)(left_shift || right_shift);
    char c = shift ? map_shift[sc] : map_normal[sc];
    if (c && hook)
        hook(c);
    else if (c)
        push_char(c);
}

void keyboard_irq_handler(void)
{
    u8 sc = inb(KB_DATA);
    translate(sc);

    /* Deliver buffered chars to the shell hook. */
    if (hook) {
        char c;
        while (keyboard_pop(&c))
            hook(c);
    }
}

char keyboard_read_char(void)
{
    char c;
    while (!keyboard_pop(&c))
        hlt();
    return c;
}

void keyboard_init(void)
{
    kb_head = kb_tail = 0;
    left_shift = right_shift = 0;
    escaped = 0;

    /* Flush the output buffer if the boot loader left bytes behind. */
    u64 spins = 0;
    while ((inb(KB_STATUS) & 0x01) && spins < 64)
        (void)inb(KB_DATA), spins++;
}
