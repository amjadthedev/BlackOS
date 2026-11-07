/* =============================================================================
 *  Black OS - linux.h : Linux x86_64 ABI compatibility layer (Strategy 1)
 *
 *  Goal: run Linux userspace binaries.  Linux uses the standard ELF format
 *  and the "syscall" instruction with its own syscall number ABI.  This layer
 *  translates Linux numbers into Black OS handlers and parses ELF64 images
 *  so the task runner can place segments into the ring-3 user window.
 * ========================================================================== */
#ifndef BLACKOS_LINUX_H
#define BLACKOS_LINUX_H

#include "types.h"

/* Linux x86_64 syscall numbers (userspace -> kernel via `syscall`) */
#define LINUX_SYS_read          0
#define LINUX_SYS_write         1
#define LINUX_SYS_open          2
#define LINUX_SYS_close         3
#define LINUX_SYS_mmap          9
#define LINUX_SYS_exit          60
#define LINUX_SYS_exit_group   231

/* Black OS syscall ENOSYS sentinel */
#define BLACKOS_ENOSYS          ((u64)-38)

/* Result of an ELF64 image parse (used by the Linux task loader). */
struct linux_elf_info {
    u64 entry;        /* e_entry (virtual address)                 */
    u64 load_bias;    /* first PT_LOAD: p_vaddr - p_offset        */
    u64 phoff;        /* e_phoff                                  */
    u64 phentsize;    /* e_phentsize                              */
    u64 phnum;        /* e_phnum                                  */
};

/* Parse a flat blob as an ELF64 executable; fills *out.  Returns 0 on ok. */
int  linux_elf_parse(const void *blob, u64 len, struct linux_elf_info *out);

/* Translate a Linux x86_64 syscall number into a Black OS syscall number. */
u64  linux_syscall_translate(u64 linux_nr);

/* Linux x86_64 syscall dispatcher shim (delegates to Black OS handlers). */
u64  linux_syscall(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif /* BLACKOS_LINUX_H */
