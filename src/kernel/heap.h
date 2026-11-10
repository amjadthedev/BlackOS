/* =============================================================================
 *  Black OS - kernel heap: malloc/free/realloc on a PMM-backed arena
 *
 *  The kernel links no libc, but Duktape and other libraries expect the
 *  classic allocator entry points.  heap_init() carves a contiguous arena
 *  out of the physical memory manager once at boot; the allocator is a
 *  first-fit free list with block splitting and forward/back coalescing.
 * ========================================================================== */
#ifndef BLACKOS_HEAP_H
#define BLACKOS_HEAP_H

#include "types.h"

/* Arena size handed out by the PMM (8 MiB -> Duktape's heap + page buffers). */
#define HEAP_ARENA_BYTES   (8ULL * 1024 * 1024)

void  heap_init(void);                 /* claim the arena (idempotent)        */
int   heap_ready(void);                /* 1 once the arena is live            */
void  heap_stats(void);                /* print used/free/total               */

void *malloc(usize size);
void *realloc(void *ptr, usize size);
void  free(void *ptr);

#endif /* BLACKOS_HEAP_H */
