/* =============================================================================
 *  Black OS - Phase 5: bitmap Physical Memory Manager (PMM)
 *
 *  Tracks 4 KiB frames with one bit each, seeded from the multiboot2 memory
 *  map (tag type 6).  Inventory covers the first 1 GiB - matching the
 *  identity map built in boot.s; anything above is counted as outside PMM
 *  management (the temporary identity mapping makes it kernel-hostile anyway
 *  until dynamic page table growth is done - see paging.c).
 * ========================================================================== */
#include "pmm.h"
#include "multiboot2.h"
#include "string.h"
#include "printk.h"

/* 1 GiB / 4 KiB = 262144 frames = 32768 bytes of bitmap. */
#define BITMAP_FRAMES   (1ULL << 18)
#define BITMAP_BYTES    (BITMAP_FRAMES / 8)

static u8 bitmap[BITMAP_BYTES] __attribute__((aligned(PMM_PAGE_SIZE)));
static u64 frames_total;    /* frames inside the managed window            */
static u64 frames_free;     /* frames marked available in the bitmap       */
static u64 frames_used;     /* frames marked reserved                      */
static u64 ram_reported;    /* raw RAM bytes advertised by firmware loader */

extern u8 __kernel_start[], __kernel_end[];

static inline int bit_test(u64 frame)
{
    return (bitmap[frame >> 3] >> (frame & 7)) & 1;
}

static inline void bit_set_mark_used(u64 frame)
{
    if (frame >= BITMAP_FRAMES || bit_test(frame))
        return;
    bitmap[frame >> 3] |= (u8)(1u << (frame & 7));
    frames_used++;
    frames_free--;
}

static inline void bit_clear_mark_free(u64 frame)
{
    if (frame >= BITMAP_FRAMES || !bit_test(frame))
        return;
    bitmap[frame >> 3] &= (u8)~(1u << (frame & 7));
    frames_used--;
    frames_free++;
}

void pmm_reserve_range(u64 addr, u64 len)
{
    u64 first = addr / PMM_PAGE_SIZE;
    u64 last  = (addr + len + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    for (u64 f = first; f < last; f++)
        bit_set_mark_used(f);
}

static void pmm_free_range(u64 addr, u64 len)
{
    /* Round up the start and round down the end: only whole frames free. */
    u64 first = (addr + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    u64 last  = (addr + len) / PMM_PAGE_SIZE;
    for (u64 f = first; f < last; f++)
        bit_clear_mark_free(f);
}

void pmm_init(u64 mb2_info_addr)
{
    frames_total = frames_free = frames_used = 0;

    /* 1) Mark the entire managed window reserved, then turn regions loose. */
    memset(bitmap, 0xFF, sizeof(bitmap));
    frames_used = BITMAP_FRAMES;
    frames_free = 0;
    frames_total = BITMAP_FRAMES;

    /* 2) Walk the multiboot2 tag list. */
    const u8 *base = (const u8 *)(usize)mb2_info_addr;
    struct mb2_info *mb = (struct mb2_info *)base;
    const u8 *tag_addr = base + sizeof(struct mb2_info);
    const u8 *end_addr = base + mb->total_size;

    while (tag_addr + sizeof(struct mb2_tag) <= end_addr) {
        const struct mb2_tag *tag = (const struct mb2_tag *)tag_addr;

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->type == MB2_TAG_BASIC_MEMINFO) {
            const struct mb2_basemem *bm =
                (const struct mb2_basemem *)tag_addr;
            ram_reported = (u64)bm->mem_lower * 1024ULL +
                           (u64)bm->mem_upper * 1024ULL;
        }

        if (tag->type == MB2_TAG_MMAP) {
            const struct mb2_mmap *mm = (const struct mb2_mmap *)tag_addr;
            const u8 *entry_addr =
                tag_addr + sizeof(struct mb2_mmap);
            const u8 *entry_end = tag_addr + tag->size;

            while (entry_addr + sizeof(struct mb2_mmap_entry) <= entry_end) {
                const struct mb2_mmap_entry *e =
                    (const struct mb2_mmap_entry *)entry_addr;

                if (e->type == MB2_MMAP_AVAILABLE) {
                    if (e->addr >= (1ULL << 30)) {
                        /* above the managed window: counted, not managed */
                        ram_reported += e->len;
                    } else {
                        pmm_free_range(e->addr, e->len);
                    }
                }
                entry_addr += mm->entry_size;
            }
        }

        tag_addr += mb2_tag_step(tag->size);
    }

    /* 3) Carve the permanent reservations back out of the pool:
     *    - all RAM below 1 MiB (IVT, BDA, EBDA, VGA buffers, boot stack)
     *    - the kernel image itself
     *    - the bitmap (it lives in .bss inside [__kernel_start, __kernel_end,
     *      but assertion-friendly code re-reserves it anyway)              */
    pmm_reserve_range(0, 0x100000);
    u64 kstart = (u64)(usize)__kernel_start;
    u64 kend   = (u64)(usize)__kernel_end;
    pmm_reserve_range(kstart, kend - kstart);
    pmm_reserve_range((u64)(usize)bitmap, sizeof(bitmap));
}

/* Lowest-address-first allocator: a linear scan keeps dual purpose -
 * pages handed out early stay inside the identity-mapped 1 GiB window,
 * which is exactly what paging.c needs for freshly created page tables. */
u64 pmm_alloc(void)
{
    for (u64 f = 0; f < BITMAP_FRAMES; f++) {
        u64 byte = f >> 3;
        if (bitmap[byte] == 0xFF)
            continue;
        for (u64 b = 0; b < 8; b++) {
            u64 frame = (byte << 3) | b;
            if (!bit_test(frame)) {
                bit_set_mark_used(frame);
                return PMM_ADDR(frame);
            }
        }
    }
    return 0;
}

void pmm_free(u64 addr)
{
    if (addr == 0 ||
        addr >= BITMAP_FRAMES * PMM_PAGE_SIZE ||
        (addr & (PMM_PAGE_SIZE - 1)) != 0)
        return;
    bit_clear_mark_free(addr / PMM_PAGE_SIZE);
}

void pmm_stats(void)
{
    kprintf("[mem ] bitmap-managed window : %llu MiB (0x0 - 0x%llx)\n",
            (u64)(BITMAP_FRAMES * PMM_PAGE_SIZE / 1024ULL / 1024ULL),
            (u64)(BITMAP_FRAMES * PMM_PAGE_SIZE - 1));
    kprintf("[mem ] frames total / free / used : %llu / %llu / %llu\n",
            frames_total, frames_free, frames_used);
    kprintf("[mem ] usable RAM in pool: %llu KiB\n", frames_free * 4ULL);
    if (ram_reported)
        kprintf("[mem ] firmware-reported RAM: %llu MiB\n",
                ram_reported / 1024ULL / 1024ULL);
    kprintf("[mem ] kernel image: [0x%llx - 0x%llx] (%llu KiB)\n",
            (u64)(usize)__kernel_start, (u64)(usize)__kernel_end,
            ((u64)(usize)__kernel_end - (u64)(usize)__kernel_start) / 1024ULL);
}

/* Allocation smoke test: take a dozen frames, verify they are distinct and
 * aligned, hand them back, and confirm the free counter returns home. */
void pmm_selftest(void)
{
    enum { N = 12 };
    u64 got[N];

    kprintf("[pmm ] running allocator selftest: %u frames\n", (u32)N);

    u64 free_before = frames_free;
    for (int i = 0; i < N; i++) {
        got[i] = pmm_alloc();
        if (got[i] == 0 || (got[i] & (PMM_PAGE_SIZE - 1)) != 0) {
            kprintf("[pmm ] FAIL frame %d: alloc %llx\n", i, got[i]);
            return;
        }
    }

    /* another sweep must not hand out any duplicate */
    for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
            if (got[i] == got[j]) {
                kprintf("[pmm ] FAIL duplicate frame %llx\n", got[i]);
                return;
            }
        }
    }

    for (int i = 0; i < N; i++)
        pmm_free(got[i]);

    if (frames_free != free_before) {
        kprintf("[pmm ] FAIL free-count drift: %llu -> %llu\n",
                free_before, frames_free);
        return;
    }

    kprintf("[pmm ] selftest OK - alloc/free accounting consistent\n");
}

u64 pmm_frames_total(void) { return frames_total; }
u64 pmm_frames_free(void)  { return frames_free; }

/* ---------------------------------------------------------------- cont-run --- */
u64 pmm_alloc_run(u64 n)
{
    u64 run_start = 0, run_len = 0;

    for (u64 f = 0; f < BITMAP_FRAMES; f++) {
        u64 byte = f >> 3;
        if (bitmap[byte] == 0xFF) {         /* fast path: full byte */
            f = ((f | 7) < BITMAP_FRAMES) ? (f | 7) : BITMAP_FRAMES - 1;
            if (f + 1 >= BITMAP_FRAMES && run_len < n)
                break;
            run_len = 0;
            continue;
        }
        if (!bit_test(f)) {
            if (run_len == 0)
                run_start = f;
            run_len++;
            if (run_len == n) {
                for (u64 g = run_start; g <= f; g++)
                    bit_set_mark_used(g);
                memset ((void *)(usize)PMM_ADDR(run_start), 0, n * PMM_PAGE_SIZE);
                return PMM_ADDR(run_start);
            }
        } else {
            run_len = 0;
        }
    }
    return 0;
}

void pmm_free_run(u64 addr, u64 n)
{
    if (addr == 0 || (addr & (PMM_PAGE_SIZE - 1)) != 0)
        return;
    u64 first = addr / PMM_PAGE_SIZE;
    for (u64 f = first; f < first + n && f < BITMAP_FRAMES; f++)
        bit_clear_mark_free(f);
}
