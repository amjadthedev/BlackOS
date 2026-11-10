/* =============================================================================
 *  Black OS - Phase 5: bitmap Physical Memory Manager (PMM)
 * ========================================================================== */
#ifndef BLACKOS_PMM_H
#define BLACKOS_PMM_H

#include "types.h"

#define PMM_PAGE_SIZE       4096ULL
#define PMM_ADDR(fname)     ((fname) * PMM_PAGE_SIZE)

void pmm_init(u64 info_addr, u64 magic);

/* Returns the physical address of a claimed frame, or 0 when out of memory. */
u64  pmm_alloc(void);
void pmm_free(u64 addr);
void pmm_reserve_range(u64 addr, u64 len);

/* Contiguous-run allocators (PMM_PAGE_LEN-multiples): used by the graphical
 * compositor for window surface buffers and by the VFS for file payloads.
 * Returns 0 on failure. */
u64  pmm_alloc_run(u64 frames);
void pmm_free_run(u64 addr, u64 frames);

/* Telemetry used by the shell `mem` / `info` commands. */
void pmm_stats(void);
void pmm_selftest(void);

u64  pmm_frames_total(void);
u64  pmm_frames_free(void);

#endif /* BLACKOS_PMM_H */
