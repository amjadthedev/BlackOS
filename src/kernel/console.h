/* =============================================================================
 *  Black OS - Phase 7 console abstraction: VGA text <-> framebuffer text
 * ========================================================================== */
#ifndef BLACKOS_CONSOLE_H
#define BLACKOS_CONSOLE_H

#include "types.h"

#define CONSOLE_MODE_TEXT   0
#define CONSOLE_MODE_FB     1

void console_use_text(void);              /* classic 0xB8000 path           */
void console_use_fb(void);                /* draw glyphs to the linear LFB  */
int  console_mode(void);
void console_putc(char c);                /* printk -> here                 */
void console_newline(void);
void console_resync(void);                /* reposition after fb switch     */

#endif /* BLACKOS_CONSOLE_H */
