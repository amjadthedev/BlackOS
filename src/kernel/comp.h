/* =============================================================================
 *  Black OS - Phase 8: the GUI compositor + taskbar + start menu
 * ========================================================================== */
#ifndef BLACKOS_COMP_H
#define BLACKOS_COMP_H

#include "types.h"

void comp_init(void);
void comp_run(void) __attribute__((noreturn));   /* GUI main loop           */

#endif /* BLACKOS_COMP_H */
