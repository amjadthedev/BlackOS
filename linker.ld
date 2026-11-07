/* =============================================================================
 *  Black OS - Phase 2: custom kernel linker script
 *
 *  Load address 0x100000 (1 MiB, classic kernel model).  GRUB's ELF loader
 *  places every PT_LOAD segment at its stated VMA; boot code identity-maps
 *  0x00000000-0x3FFFFFFF, so physical == virtual for the whole kernel.
 *
 *  Section order matters: the multiboot2 header must sit inside the first
 *  32 KiB of the loaded image, so it leads the file, boot stub second.
 * ========================================================================== */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_BASE = 1M;

SECTIONS
{
    . = KERNEL_BASE;

    /* Bookends used by the physical memory manager (pmm.c) to keep the
     * kernel image itself out of the free-frame pool. */
    __kernel_start = .;

    .multiboot : ALIGN(8)
    {
        KEEP(*(.multiboot_header))
    }

    .boot : ALIGN(16)
    {
        KEEP(*(.boot))
    }

    .text : ALIGN(4096)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4096)
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    /* Everything below the page-fit is fixed up by the next alignment. */
    . = ALIGN(4096);
    __kernel_end = .;

    /* The kernel carries no unwinding tables, notes, or build ids. */
    /DISCARD/ :
    {
        *(.eh_frame .eh_frame_hdr)
        *(.note .note.* .gnu.hash .comment)
    }
}
