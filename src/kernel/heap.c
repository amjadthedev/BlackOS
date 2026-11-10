/* =============================================================================
 *  Black OS - kernel heap implementation (first-fit, address-ordered list)
 *
 *  Block layout: [header][payload ...] with blocks chained in address order
 *  so adjacent free blocks can be coalesced in O(1).  The size field is kept
 *  16-byte aligned, which lets bit 0 double as the in-use flag.
 * ========================================================================== */
#include "heap.h"
#include "pmm.h"
#include "printk.h"
#include "string.h"

#define HEAP_ALIGN      16ULL
#define HEAP_MAGIC      0x48524145424C4B31ULL    /* "HRAEBLK1" tag           */

struct heap_block {
    u64                 size;        /* payload bytes, bit0 = in use         */
    u64                 magic;       /* HEAP_MAGIC once initialised          */
    struct heap_block  *next;        /* next block in address order          */
    struct heap_block  *prev;
};

#define HDR_SIZE        ((u64)sizeof(struct heap_block))   /* 32 bytes       */
#define ALIGNED(n)      (((n) + (HEAP_ALIGN - 1)) & ~(HEAP_ALIGN - 1))

static u8               *heap_base;
static u8               *heap_end;
static struct heap_block *heap_head;
static int               heap_live;

void heap_init(void)
{
    if (heap_live)
        return;

    u64 frames = (HEAP_ARENA_BYTES + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    u64 addr   = pmm_alloc_run(frames);
    if (addr == 0) {
        kprintf("[heap] WARNING: could not claim %llu MiB arena\n",
                (u64)(frames * PMM_PAGE_SIZE / 1024ULL / 1024ULL));
        return;
    }

    heap_base = (u8 *)(usize)addr;
    heap_end  = heap_base + frames * PMM_PAGE_SIZE;

    heap_head = (struct heap_block *)heap_base;
    heap_head->size = (u64)(heap_end - heap_base) - HDR_SIZE;
    heap_head->magic = HEAP_MAGIC;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;

    heap_live = 1;
}

int heap_ready(void) { return heap_live; }

void *malloc(usize size)
{
    if (!heap_live)
        return NULL;

    u64 need = ALIGNED(size ? (u64)size : HEAP_ALIGN);
    if (need < HEAP_ALIGN)
        need = HEAP_ALIGN;

    struct heap_block *b;
    for (b = heap_head; b != NULL; b = b->next) {
        if (b->magic != HEAP_MAGIC)
            return NULL;                    /* corrupt list: refuse to guess */
        if ((b->size & 1ULL) == 0 && (b->size & ~1ULL) >= need)
            break;
    }
    if (b == NULL)
        return NULL;

    u64 have = b->size & ~1ULL;

    /* Split off the tail when it can hold a header plus one payload. */
    if (have >= need + HDR_SIZE + HEAP_ALIGN) {
        struct heap_block *rest = (struct heap_block *)((u8 *)(b + 1) + need);
        rest->size  = have - need - HDR_SIZE;
        rest->magic = HEAP_MAGIC;
        rest->next  = b->next;
        rest->prev  = b;
        if (rest->next)
            rest->next->prev = rest;
        b->next = rest;
        b->size = need;
    }

    b->size |= 1ULL;                        /* mark in use                   */
    void *payload = (void *)(b + 1);
    memset(payload, 0, b->size & ~1ULL);    /* deterministic allocations      */
    return payload;
}

static void block_unlink(struct heap_block *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        heap_head = b->next;
    if (b->next)
        b->next->prev = b->prev;
}

void free(void *ptr)
{
    if (ptr == NULL || !heap_live)
        return;

    u8 *p = (u8 *)ptr;
    if (p < heap_base + HDR_SIZE || p >= heap_end) {
        kprintf("[heap] free() of foreign pointer %p ignored\n", ptr);
        return;
    }

    struct heap_block *b = ((struct heap_block *)ptr) - 1;
    if (b->magic != HEAP_MAGIC) {
        kprintf("[heap] free() of unowned pointer %p ignored\n", ptr);
        return;
    }
    if ((b->size & 1ULL) == 0)
        return;                             /* double free: ignore           */

    b->size &= ~1ULL;

    /* Coalesce with the next block, then with the previous one. */
    if (b->next != NULL && (b->next->magic == HEAP_MAGIC) &&
        (b->next->size & 1ULL) == 0) {
        b->size += HDR_SIZE + b->next->size;
        block_unlink(b->next);
    }
    if (b->prev != NULL && (b->prev->magic == HEAP_MAGIC) &&
        (b->prev->size & 1ULL) == 0) {
        b->prev->size += HDR_SIZE + b->size;
        block_unlink(b);
    }
}

void *realloc(void *ptr, usize size)
{
    if (ptr == NULL)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    if (!heap_live)
        return NULL;

    struct heap_block *b = ((struct heap_block *)ptr) - 1;
    if (b->magic != HEAP_MAGIC || (b->size & 1ULL) == 0)
        return NULL;

    u64 have = (b->size & ~1ULL);               /* payload bytes in place     */
    u64 need = ALIGNED(size);
    if (need <= have)
        return ptr;

    void *fresh = malloc(size);
    if (fresh == NULL)
        return NULL;
    memcpy(fresh, ptr, have);
    free(ptr);
    return fresh;
}

void heap_stats(void)
{
    if (!heap_live) {
        kprintf("[heap] not initialised\n");
        return;
    }

    u64 total = (u64)(heap_end - heap_base) - HDR_SIZE;
    u64 used = 0, freeb = 0, blocks = 0;

    for (struct heap_block *b = heap_head; b != NULL; b = b->next, blocks++) {
        u64 sz = b->size & ~1ULL;
        if (b->size & 1ULL)
            used += sz;
        else
            freeb += sz;
    }

    kprintf("[heap] arena %llu KiB: %llu KiB used, %llu KiB free, %llu blocks\n",
            total / 1024ULL, used / 1024ULL, freeb / 1024ULL, blocks);
}
