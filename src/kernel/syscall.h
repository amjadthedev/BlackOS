/* =============================================================================
 *  Black OS - stable kernel ABI: Phase 9 telemetry + ring-3 SYSCALL surface
 * ========================================================================== */
#ifndef BLACKOS_SYSCALL_H
#define BLACKOS_SYSCALL_H

#include "types.h"

/* ── syscall numbers (ABI, invoked with rax; args rdi..r9) ────────────────  */
#define SYS_EXIT             0
#define SYS_UPTIME           1
#define SYS_WRITE            2
#define SYS_READ_FILE        3
#define SYS_ALLOCATE_USER_PAGE 4
#define SYS_DRAW_PIXEL_BUFFER  5
#define SYS_GET_PROCESS_LIST   6
#define SYS_SOCKET           7
#define SYS_KILL_PROCESS     8

/* ── task table ───────────────────────────────────────────────────────────  */
#define SYS_TASK_STATE_RUNNING    0
#define SYS_TASK_STATE_BACKGROUND 1

struct sys_task {
    char name[24];
    u32  pid;
    u32  state;
    u64  age_ms;                /* time since the task was created           */
};

u32  sys_task_count(void);
int  sys_task_get(u32 index, struct sys_task *out);   /* -1 if out of range */

/* ── system telemetry ─────────────────────────────────────────────────────  */
u64  sys_uptime_ms(void);
u64  sys_mem_frames_total(void);
u64  sys_mem_frames_free(void);
u64  sys_mem_used_bytes(void);
u64  sys_mem_total_bytes(void);

/* ── window manager ───────────────────────────────────────────────────────  */
u32  sys_window_count(void);

/* ── ring-3 plumbing ──────────────────────────────────────────────────────  */
void syscall_arch_init(void);    /* program STAR/LSTAR/FMASK, EFER.SCE      */

/* Drop a user image into the ring-3 window and run it; returns exit code. */
u64  syscall_run_task(const void *blob, u64 blob_len, u64 entry_off,
                      u64 stack_top_va);
/* The dispatcher hook (called from syscall_entry, asm).                   */
u64  syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif /* BLACKOS_SYSCALL_H */
