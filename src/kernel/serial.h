/* =============================================================================
 *  Black OS - COM1 serial driver (used for automated verificaiton, see
 *  scripts/verify.py: the boot log is captured from COM1 as a file).
 * ========================================================================== */
#ifndef BLACKOS_SERIAL_H
#define BLACKOS_SERIAL_H

#include "types.h"

#define SERIAL_BASE      0x3F8      /* COM1 */

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

#endif /* BLACKOS_SERIAL_H */
