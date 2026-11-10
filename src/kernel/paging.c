/* =============================================================================
 *  Black OS - Phase 5: paging introspection + dynamic 2 MiB mapping helper
 *
 *  boot.s already established CR3 + long mode with a static identity map of
 *  the first 1 GiB (512 x 2 MiB pages).  This phase adds:
 *    1. a human-readable MMU audit for boot telemetry and the `info` command,
 *    2. dynamic mapping: paging_map_2mb() grows the page directory with
 *       2 MiB huge pages on demand, pulling fresh page tables out of the PMM.
 * ========================================================================== */
#include "paging.h"
#include "io.h"
#include "pmm.h"
#include "printk.h"
#include "string.h"

#define PRESENT   0x001ULL
#define WRITABLE  0x002ULL
#define HUGE_PAGE 0x080ULL
#define FLAGS_K   (PRESENT | WRITABLE)

#define PML4_INDEX(a)  (((a) >> 39) & 0x1FFULL)
#define PDPT_INDEX(a)  (((a) >> 30) & 0x1FFULL)
#define PD_INDEX(a)    (((a) >> 21) & 0x1FFULL)

/* The PML4 lives at a fixed identity address and pml4[0] is established by
 * boot.s; everything we map therefore routes through PML4[0] (VA = PA). */
static u64 *page_table_ptr(u64 entry)
{
    /* Physical addresses in the identity window double as pointers. */
    return (u64 *)(usize)(entry & 0x000FFFFFFFFFF000ULL);
}

void paging_report(void)
{
    u64 cr0 = read_cr0();
    u64 cr3 = read_cr3();
    u64 cr4 = read_cr4();
    u64 efer = rdmsr(0xC0000080);

    kprintf("[mmu ] CR0.PG = %llu   CR4.PAE = %llu\n",
            (cr0 >> 31) & 1, (cr4 >> 5) & 1);
    kprintf("[mmu ] EFER.LME = %llu   EFER.LMA = %llu   (IA-32e mode)\n",
            (efer >> 8) & 1, (efer >> 10) & 1);
    kprintf("[mmu ] CR3 root: %P\n", (void *)(usize)cr3);
    kprintf("[mmu ] static identity map: 0x0 - 0x3fffffff (512 x 2 MiB)\n");
}

int paging_map_2mb(u64 phys)
{
    if (phys & (2 * 1024 * 1024 - 1))
        return -1;                     /* must be 2 MiB aligned */

    /* PML4[0] must already exist (boot.s created it). */
    u64 *pml4 = page_table_ptr(read_cr3());
    if (!(pml4[0] & PRESENT))
        return -1;                     /* unexpected: boot guarantees it */

    u64 pdpt_idx = PDPT_INDEX(phys);
    u64 *pdpt = page_table_ptr(pml4[0]);
    if (!(pdpt[pdpt_idx] & PRESENT)) {
        u64 frame = pmm_alloc();       /* fresh level-2 table */
        if (frame == 0)
            return -1;                 /* PMM only yields identity-mapped RAM */
        memset((void *)(usize)frame, 0, PMM_PAGE_SIZE);
        pdpt[pdpt_idx] = frame | FLAGS_K;
    }

    u64 *pd = page_table_ptr(pdpt[pdpt_idx]);
    u64 pd_idx = PD_INDEX(phys);
    const u64 entry = (phys & 0xFFFFFFFFFFE00000ULL) | FLAGS_K | HUGE_PAGE;

    if (pd[pd_idx] & PRESENT)
        return 1;                      /* already mapped - nothing to do */

    pd[pd_idx] = entry;
    __asm__ volatile ("invlpg (%0)" :: "r"(phys) : "memory");
    return 0;
}

/*━━━━━━━━━━━━━━━━━━━━━ ring-3 user address window ━━━━━━━━━━━━━━━━━━━━━━━━*/
/* User VA window at 0x100000000+ (PML4[1]).  The kernel points PML4[1] at
 * a dedicated user PDPT so user pages remain reachable while CR3 holds the
 * kernel map (user buffers are touched by syscalls directly).             */

#define USER_VA_BASE       0x0000000100000000ULL

int paging_user_init(void)
{
    u64 *pml4 = page_table_ptr(read_cr3());
    if (pml4[1] & PRESENT)
        return 1;                       /* already wired                   */

    u64 frame = pmm_alloc();
    if (frame == 0)
        return -1;
    memset((void *)(usize)frame, 0, PMM_PAGE_SIZE);
    pml4[1] = frame | FLAGS_K | (1ULL << 2);   /* user-supervisor bit        */
    return 0;
}

/* Map one 2 MiB user page: VA = USER_VA_BASE + n * 2 MiB.  Returns 0 on a
 * fresh mapping, 1 if already present, negative on error.                 */
int paging_user_map_2mb(u64 phys, u64 n)
{
    u64 va = USER_VA_BASE + n * 0x200000ULL;
    u64 *pml4 = page_table_ptr(read_cr3());
    if (!(pml4[1] & PRESENT))
        return -1;

    u64 pdpt_idx = PDPT_INDEX(va);
    u64 *pdpt = page_table_ptr(pml4[1]);
    if (!(pdpt[pdpt_idx] & PRESENT)) {
        u64 frame = pmm_alloc();
        if (frame == 0)
            return -1;
        memset((void *)(usize)frame, 0, PMM_PAGE_SIZE);
        pdpt[pdpt_idx] = frame | FLAGS_K | (1ULL << 2);
    }

    u64 *pd  = page_table_ptr(pdpt[pdpt_idx]);
    u64 pidx = PD_INDEX(va);
    const u64 entry = (phys & 0xFFFFFFFFFFE00000ULL)
                    | FLAGS_K | HUGE_PAGE | (1ULL << 2);   /* user bit      */

    if (pd[pidx] & PRESENT)
        return 1;
    pd[pidx] = entry;
    __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory");
    return 0;
}

u64 paging_user_va(u64 n)
{
    return USER_VA_BASE + n * 0x200000ULL;
}
