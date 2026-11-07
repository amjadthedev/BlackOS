/* =============================================================================
 *  Black OS - linux.c : Linux x86_64 ABI compatibility layer (Strategy 1)
 *
 *  - Translates Linux x86_64 syscall numbers into Black OS syscall handlers.
 *  - Parses ELF64 images (program headers) for the Linux task loader.
 *
 *  Phase 1 scope: exit / exit_group / write / read / mmap shims + ELF64 parse.
 *  Later phases: fd table, real file-backed syscalls, segment mapping.
 * ========================================================================== */
#include "linux.h"
#include "syscall.h"
#include "string.h"
#include "printk.h"

/* Read a little-endian 64-bit value from an arbitrary byte buffer. */
static u64 ld64(const u8 *q, int off)
{
    return (u64)q[off] |
           ((u64)q[off + 1] << 8) |
           ((u64)q[off + 2] << 16) |
           ((u64)q[off + 3] << 24) |
           ((u64)q[off + 4] << 32) |
           ((u64)q[off + 5] << 40) |
           ((u64)q[off + 6] << 48) |
           ((u64)q[off + 7] << 56);
}

int linux_elf_parse(const void *blob, u64 len, struct linux_elf_info *out)
{
    if (blob == NULL || out == NULL || len < 64)
        return -1;

    const u8 *p = (const u8 *)blob;
    if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F')
        return -1;
    if (p[4] != 2 || p[5] != 1)              /* ELFCLASS64 / ELFDATA2LSB */
        return -1;
    if (ld64(p, 16) != 2)                     /* e_type = ET_EXEC */
        return -1;
    if (ld64(p, 18) != 0x3e)                  /* e_machine = EM_X86_64 */
        return -1;

    struct linux_elf_info info;
    info.entry      = ld64(p, 24);
    info.phoff      = ld64(p, 32);
    info.phentsize  = (u64)((u16)p[54] | ((u16)p[55] << 8));
    info.phnum      = (u64)((u16)p[56] | ((u16)p[57] << 8));
    info.load_bias  = 0;

    u64 first_load_vaddr = 0;
    int have_load = 0;
    for (u64 i = 0; i < info.phnum; i++) {
        const u8 *ph = (const u8 *)blob + info.phoff + i * info.phentsize;
        if ((u32)ld64(ph, 0) != 1)           /* PT_LOAD */
            continue;
        u64 vaddr = ld64(ph, 16);
        u64 offset = ld64(ph, 8);
        if (!have_load) {
            info.load_bias = vaddr - offset;
            first_load_vaddr = vaddr;
            have_load = 1;
        }
    }
    if (!have_load)
        return -1;

    *out = info;
    return 0;
}

u64 linux_syscall_translate(u64 linux_nr)
{
    switch (linux_nr) {
    case LINUX_SYS_read:       return SYS_READ_FILE;
    case LINUX_SYS_write:      return SYS_WRITE;
    case LINUX_SYS_open:       return (u64)-38;
    case LINUX_SYS_close:      return (u64)-38;
    case LINUX_SYS_mmap:       return SYS_ALLOCATE_USER_PAGE;
    case LINUX_SYS_exit:       return SYS_EXIT;
    case LINUX_SYS_exit_group: return SYS_EXIT;
    default:                    return BLACKOS_ENOSYS;
    }
}

u64 linux_syscall(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
    switch (nr) {
    case LINUX_SYS_write:
        return syscall_dispatch(SYS_WRITE, a1, a2, a3, a4, a5);

    case LINUX_SYS_read:
        /* fd-table aware reads land in a later phase; Black OS ABI is
         * path/buffer based (SYS_READ_FILE).  Phase 1: ENOSYS for reads. */
        return (u64)-22;

    case LINUX_SYS_mmap:
        return syscall_dispatch(SYS_ALLOCATE_USER_PAGE, a1, a2, a3, a4, a5);

    case LINUX_SYS_exit:
    case LINUX_SYS_exit_group:
        return syscall_dispatch(SYS_EXIT, a1, a2, a3, a4, a5);

    default:
        return BLACKOS_ENOSYS;
    }
}
