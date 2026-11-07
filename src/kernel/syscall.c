/* =============================================================================
 *  Black OS - stable kernel ABI: Phase 9 telemetry + ring-3 SYSCALL surface
 *
 *  Ring 3: module 4 wires IA32_STAR / IA32_LSTAR / IA32_FMASK plus EFER.SCE;
 *  user code lives in the 0x100000000 window (shared PML4[1] PDPT) and calls
 *  the kernel through the `syscall` instruction.  Single cooperative task:
 *  sys_exit unwinds directly back into syscall_run_task's kernel frame.
 * ========================================================================== */
#include "syscall.h"
#include "win.h"
#include "idt.h"
#include "pmm.h"
#include "paging.h"
#include "fb.h"
#include "keyboard.h"
#include "netstack.h"
#include "vfs.h"
#include "string.h"
#include "io.h"
#include "printk.h"
#include "linux.h"

/* ──────────────────────────── Phase 9 telemetry ─────────────────────────── */
u64 sys_uptime_ms(void)          { return uptime_ms; }
u64 sys_mem_frames_total(void)   { return pmm_frames_total(); }
u64 sys_mem_frames_free(void)    { return pmm_frames_free(); }
u64 sys_mem_used_bytes(void)     { return (pmm_frames_total() - pmm_frames_free()) * PMM_PAGE_SIZE; }
u64 sys_mem_total_bytes(void)    { return pmm_frames_total() * PMM_PAGE_SIZE; }
u32 sys_window_count(void)       { return (u32)win_count(); }

u32 sys_task_count(void)
{
    return (u32)(win_count() + 2);              /* kernel + idle            */
}

int sys_task_get(u32 index, struct sys_task *out)
{
    if (out == NULL)
        return -1;

    memset(out, 0, sizeof(*out));
    out->pid   = index;
    out->state = SYS_TASK_STATE_RUNNING;

    if (index == 0) {
        strncpy(out->name, "kernel", sizeof(out->name) - 1);
        return 0;
    }
    if (index == 1) {
        strncpy(out->name, "idle", sizeof(out->name) - 1);
        out->state = SYS_TASK_STATE_BACKGROUND;
        return 0;
    }

    /* remaining indices map onto open windows in z order                   */
    u32 idx = index - 2;
    for (int z = win_top_z(); z >= 1; z--) {
        struct window *w = win_by_z((int)z);
        if (w == NULL)
            continue;
        if (idx == 0) {
            strncpy(out->name, w->title, sizeof(out->name) - 1);
            out->age_ms = uptime_ms - w->opened_ms;
            return 0;
        }
        idx--;
    }
    return -1;
}

/* ───────────────────────────── MSR programming ─────────────────────────── */
extern void syscall_entry(void);        /* src/kernel/syscall.s              */
void syscall_finish_user(u64 exit_code);/* src/kernel/syscall.s              */

#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_FMASK       0xC0000084
#define EFER_SCE        0x0001

void syscall_arch_init(void)
{
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    /* STAR:  [63:48] = 0x0020 -> SYSRET CS 0x23, SS 0x2B
     *        [47:32] = 0x0009 -> SYSCALL CS 0x08, SS 0x10                     */
    u64 star = ((u64)0x0020 << 48) | ((u64)0x0009 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (u64)(usize)&syscall_entry);
    wrmsr(MSR_FMASK, 0x000000000000FFDFULL);

    printk("[sysc] STAR/LSTAR/FMASK programmed (SCE enabled)\n");
}

/* ───────────────────────── user task management ────────────────────────── */
extern u64 syscall_kernel_rsp;          /* syscall.s BSS slot                */
void syscall_kernel_zero_slot(void)
{
    syscall_kernel_rsp = 0;
}

#define USER_STACK_TOP(r0)  ((r0) + 2 * 1024 * 1024 - 256)

/* Copy a flat user image into the user VA window and enter ring 3.         */
u64 syscall_run_task(const void *blob, u64 blob_len, u64 entry_off,
                     u64 stack_top_va)
{
    (void)stack_top_va;
    if (blob == NULL || blob_len == 0)
        return (u64)-1;

    paging_user_init();

    /* page 0 = stack, page 1 = image (phys identity RAM from the PMM)      */
    u64 stack_phys = pmm_alloc_run(1);
    u64 code_phys  = pmm_alloc_run(1);
    if (stack_phys == 0 || code_phys == 0)
        return (u64)-1;

    if (paging_user_map_2mb(stack_phys, 0) < 0)
        return (u64)-1;
    if (paging_user_map_2mb(code_phys, 1) < 0)
        return (u64)-1;

    u64 code_va = paging_user_va(1);
    u64 n = blob_len;
    if (n > 2 * 1024 * 1024)
        n = 2 * 1024 * 1024;
    memcpy((void *)(usize)code_va, blob, (usize)n);

    u64 user_rsp = paging_user_va(0) + (2 * 1024 * 1024) - 256;

    /* Linux ELF images: derive the entry offset from the program headers
     * (entry VA - load bias).  Flat Black OS binaries keep the raw entry_off. */
    u64 run_off = entry_off;
    struct linux_elf_info elf;
    if (linux_elf_parse(blob, blob_len, &elf) == 0) {
        u64 rel = elf.entry - elf.load_bias;
        if (rel < n)
            run_off = rel;
    }

    /* run to completion (cooperative: sys_exit rewinds here)               */
    u64 exit_code = asm_run_user(code_va + run_off, user_rsp);

    pmm_free_run(stack_phys, 1);
    pmm_free_run(code_phys, 1);
    return exit_code;
}

/* syscall.s helper */
extern u64 asm_run_user(u64 rip, u64 user_rsp);

/* ─────────────────────────── dispatcher table ──────────────────────────── */
#define USER_WINDOW_MAX 0x40

static u64 user_page_allocated(u64 slot)
{
    /* sys_allocate_user_page: phys allocated from PMM, mapped 1:1 windows  */
    u64 phys = pmm_alloc_run(1);
    if (phys == 0)
        return 0;
    u64 n = slot;
    if (n < 2 || n >= USER_WINDOW_MAX)
        n = 2;                            /* 0 stack, 1 image, 2.. heap     */
    if (paging_user_map_2mb(phys, n) < 0) {
        pmm_free_run(phys, 1);
        return 0;
    }
    return paging_user_va(n);
}

u64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
    switch (nr) {
    case SYS_EXIT:
        /* unwind straight into the syscall_run_task kernel frame         */
        syscall_finish_user(a1);
        break;                            /* not reached                    */

    case SYS_UPTIME:
        return sys_uptime_ms();

    case SYS_WRITE: {
        const char *s = (const char *)(usize)a1;
        u64 cap = a2;
        if (s == (const char *)0)
            return (u64)-1;
        for (u64 i = 0; i < cap && s[i]; i++)
            console_putc(s[i]);
        return a2;
    }

    case SYS_READ_FILE: {
        const char *path = (const char *)(usize)a1;
        u8 *buf = (u8 *)(usize)a2;
        u64 cap = a3;
        if (path == (const char *)0 || buf == (u8 *)0 || cap == 0)
            return (u64)-1;
        vfs_handle h = vfs_lookup(path);
        if (h < 0)
            return (u64)-1;
        u64 got = vfs_read(h, buf, cap, 0);
        return got;
    }

    case SYS_ALLOCATE_USER_PAGE:
        return user_page_allocated(a1);

    case SYS_DRAW_PIXEL_BUFFER: {
        u32 *buf = (u32 *)(usize)a1;
        u32 w = (u32)a2;
        u32 h = (u32)a3;
        u32 dx = (u32)a4;
        u32 dy = (u32)a5;
        if (buf == (u32 *)0 || w == 0 || h == 0 || fb_ready == 0)
            return (u64)-1;
        /* bounded blit with clipping                                     */
        u32 limit = w * h;
        if (limit > (4 * 1024 * 1024 / 4))
            limit = 1024 * 1024;
        for (u32 y = 0; y < h && dy + y < fb_height; y++)
            for (u32 x = 0; x < w && dx + x < fb_width; x++)
                fb_pixel((int)(dx + x), (int)(dy + y), buf[y * w + x]);
        return 0;
    }

    case SYS_GET_PROCESS_LIST: {
        struct sys_task *out = (struct sys_task *)(usize)a1;
        u64 cap = a2;
        if (out == (struct sys_task *)0)
            return (u64)-1;
        u64 n = 0;
        u32 total = sys_task_count();
        for (u32 i = 0; i < total && (u64)(i + 1) * sizeof(struct sys_task) <= cap; i++)
            if (sys_task_get(i, out + n) == 0)
                n++;
        return n;
    }

    case SYS_SOCKET: {
        /* blocking HTTP GET: host, path, buffer, cap                      */
        const char *host = (const char *)(usize)a1;
        const char *path = (const char *)(usize)a2;
        u8 *buf = (u8 *)(usize)a3;
        u64 cap = a4;
        if (host == (const char *)0 || path == (const char *)0 ||
            buf == (u8 *)0 || cap < 16)
            return (u64)-1;

        net_tcp_h c = net_http_get(host, path);
        if (c == NULL)
            return (u64)-1;
        u64 got = 0;
        u32 deadline = (u32)uptime_ms + 12000;
        while (got < cap - 1 && uptime_ms < deadline) {
            net_poll();
            int n = net_http_read(c, buf + got, (u32)(cap - 1 - got));
            if (n > 0)
                got += (u64)n;
            else if (c->state == NET_TCP_CLOSED)
                break;
            for (volatile int s = 0; s < 2048; s++)
                ;
        }
        net_http_close(c);
        buf[got] = 0;
        return got;
    }

    case SYS_KILL_PROCESS: {
        u32 pid = (u32)a1;
        if (pid < 2)
            return (u64)-1;
        u32 idx = pid - 2;
        for (int z = win_top_z(); z >= 1; z--) {
            struct window *w = win_by_z((int)z);
            if (w == NULL)
                continue;
            if (idx == 0) {
                if (w->app->close)
                    w->app->close(w);
                win_close(w);
                return 0;
            }
            idx--;
        }
        return (u64)-1;
    }

    default:
        /* Linux x86_64 ABI (Strategy 1): route through the Linux shim;
         * unmapped numbers land at ENOSYS inside linux_syscall(). */
        return linux_syscall(nr, a1, a2, a3, a4, a5);
    }
}
