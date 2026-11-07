/* =============================================================================
 *  Black OS - Phase 5: paging introspection + dynamic 2 MiB mapping helper
 * ========================================================================== */
#ifndef BLACKOS_PAGING_H
#define BLACKOS_PAGING_H

#include "types.h"

/* CR3 / control-register audit, printed at boot and by the shell `info`. */
void paging_report(void);

/*
 * Identity-map one extra 2 MiB page covering `phys` (i.e. phys maps to the
 * same virtual address).  Middle page tables are created on demand using
 * pmm_alloc(); safe to call with interrupts enabled.  Returns 0 when a new
 * entry was created, 1 when the page was already mapped, negative on error.
 */
int paging_map_2mb(u64 phys);

/* ── ring-3 user window (0x100000000+, PML4[1] shared PDPT) ──────────────  */
int  paging_user_init(void);
int  paging_user_map_2mb(u64 phys, u64 n);    /* n-th 2 MiB user page       */
u64  paging_user_va(u64 n);                   /* VA of the n-th user page   */

#endif /* BLACKOS_PAGING_H */
