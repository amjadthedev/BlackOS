/* =============================================================================
 *  Black OS - Phase 4: PS/2 keyboard driver (scancode set 1 translation)
 * ========================================================================== */
#ifndef BLACKOS_KEYBOARD_H
#define BLACKOS_KEYBOARD_H

#include "types.h"

/* Black OS arrow-key + control codes (extended scancode translations). */
#define KBD_UP      0x91
#define KBD_DOWN    0x92
#define KBD_LEFT    0x93
#define KBD_RIGHT   0x94

/* ASCII control names shared by the shell / compositor apps. */
#define KBD_ESCAPE  27
#define KBD_ENTER   '\n'

/* Function called for every translated ASCII character.  kernel_main.c
 * registers the shell feeder through keyboard_set_hook(). */
typedef void (*keyboard_hook_t)(char c);

void keyboard_set_hook(keyboard_hook_t hook_fn);

/* Called by the IRQ1 handler in idt.c once full bytes are available. */
void keyboard_init(void);
void keyboard_irq_handler(void);

/* Non-blocking key dequeue: 1 when a translated char landed in *out. */
int keyboard_pop(char *out);

/* Legacy polling path (kept for completeness). */
char keyboard_read_char(void);

#endif /* BLACKOS_KEYBOARD_H */
