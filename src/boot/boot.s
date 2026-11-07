; =============================================================================
;  Black OS - Phase 1: Multiboot2 header + real-mode startup, long-mode entry
;
;  * The Multiboot2 header lets GRUB (or any MB2 loader) recognise the kernel.
;  * GRUB enters us in 32-bit protected mode, paging OFF:
;        EAX = 0x36D76289 (multiboot2 boot magic)
;        EBX = physical address of the multiboot2 information structure
;  * We build page tables here, enable PAE + Long Mode (EFER.LME) + Paging,
;    load our own GDT, and perform the far jump into 64-bit code.
;  * Assembled with:  nasm -f elf64  (mixed 32/64-bit code is intentional)
; =============================================================================

[bits 32]

; ---------------------------------------------------------------------------
; Multiboot2 header constants
; ---------------------------------------------------------------------------
%define MB2_MAGIC        0xE85250D6          ; header signature
%define MB2_ARCH_I386    0                    ; architecture: i386 (PM entry)
%define MB2_HDR_LEN      (mb2_header_end - mb2_header_start)
%define MB2_CHECKSUM     (-(MB2_MAGIC + MB2_ARCH_I386 + MB2_HDR_LEN))

%define MB2_BOOT_MAGIC   0x36D76289           ; value the loader leaves in EAX

; ---------------------------------------------------------------------------
; GDT selector values (see gdt64 below)
; ---------------------------------------------------------------------------
%define GDT_CODE64       0x08
%define GDT_DATA64       0x10
%define GDT_USER_CODE64  0x20                 ; DPL3 code  (SYSRET target)
%define GDT_USER_DATA64  0x28                 ; DPL3 data

section .multiboot_header
align 8
mb2_header_start:
        dd  MB2_MAGIC                       ; magic
        dd  MB2_ARCH_I386                   ; architecture
        dd  MB2_HDR_LEN                     ; header length
        dd  MB2_CHECKSUM                    ; checksum: everything sums to 0
        ; ---- framebuffer tag: request a linear 800x600x32 RGB target ----
        align 8
        dw  5                               ; type 5 = framebuffer tag
        dw  1                               ; flags bit0: honour the fields
        dd  24                              ; size (padded to 8)
        dd  800                             ; framebuffer_width
        dd  600                             ; framebuffer_height
        dd  32                              ; framebuffer_depth
        db  1                               ; console_type 1 = linear RGB
        db  0, 0, 0                         ; padding
        ; ---- mandatory end tag ----
        align 8
        dw  0                               ; type = MB2_TAG_END
        dw  0                               ; flags
        dd  8                               ; size
mb2_header_end:

; ---------------------------------------------------------------------------
; Bootstrap entry.  This section is placed first in the image right behind
; the multiboot header by the linker script (see linker.ld).
; ---------------------------------------------------------------------------
section .boot
align 16
global _start
_start:
        cli                                 ; interrupts off until the IDT is up
        mov     esp, boot_stack_top         ; temporary boot stack
        xor     ebp, ebp                    ; terminate stack backtraces
        ; EBX = multiboot2 info physical pointer.
        ; The pointer lives inside the first 1 MiB of RAM, which we are about
        ; to identity-map, so the physical address doubles as a valid pointer
        ; after paging is enabled.  EBX is preserved untouched all the way to
        ; the call into C.

        ; -------------------------------------------------------------------
        ; Build page tables: identity-map the first 1 GiB with 2 MiB pages
        ;   PML4[0]    -> PDPT          (covers 512 GiB of VA)
        ;   PDPT[0]    -> page-dir      (covers 1 GiB of VA)
        ;   PD[i]      -> i * 2 MiB  |  P | W | PS     (huge page)
        ; The three tables were reserved consecutively and 4K-aligned in
        ; .bss below, so one memset covers all 12 KiB.
        ; -------------------------------------------------------------------
        xor     eax, eax
        mov     edi, pml4
        mov     ecx, (4096 / 4) * 3         ; wipe PML4 + PDPT + page-dir
        rep     stosd

        ; PML4[0] = PDPT, present | writable
        mov     eax, pdpt
        or      eax, 0x03
        mov     dword [pml4], eax
        mov     dword [pml4 + 4], 0         ; physical memory is < 4 GiB

        ; PDPT[0] = page directory, present | writable
        mov     eax, pd_page_dir
        or      eax, 0x03
        mov     dword [pdpt], eax
        mov     dword [pdpt + 4], 0

        ; page-dir: 512 entries of 2 MiB huge pages 0x00000000..0x3FFFFFFF
        mov     edi, pd_page_dir
        mov     eax, 0x00000083             ; flags P|W|PS, base = 0
.fill_page_dir:
        mov     [edi], eax                  ; low dword: base | flags
        add     eax, 0x00200000             ; next 2 MiB chunk
        add     edi, 8                      ; next 64-bit PDE slot
        loop    .fill_page_dir

        ; -------------------------------------------------------------------
        ; Activate paging -> long mode
        ; -------------------------------------------------------------------
        mov     eax, pml4                   ; CR3 = root of page tables
        mov     cr3, eax

        mov     eax, cr4
        or      eax, 1 << 5                 ; PAE  (physical address extension)
        mov     cr4, eax

        mov     ecx, 0xC0000080             ; MSR_EFER
        rdmsr
        or      eax, 1 << 8                 ; EFER.LME (long mode enable)
        wrmsr

        mov     eax, cr0
        or      eax, 0x80000000             ; CR0.PG (paging on -> long mode
        mov     cr0, eax                    ;  active, still 32-bit contents)

        ; -------------------------------------------------------------------
        ; Own GDT: GRUB's 32-bit code segment cannot be used for the jump
        ; into 64-bit code (L=0), so we install a flat long-mode GDT first.
        ; -------------------------------------------------------------------
        lgdt    [gdt_descriptor]
        mov     ax, GDT_DATA64
        mov     ds, ax
        mov     es, ax
        mov     ss, ax

        jmp     GDT_CODE64:long_mode_entry  ; far jump -> 64-bit code segment

; ---------------------------------------------------------------------------
; 64-bit entry.  From here on: long mode, plain C calling convention.
; ---------------------------------------------------------------------------
[bits 64]
long_mode_entry:
        mov     ax, GDT_DATA64              ; refresh data selectors
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax

        mov     esp, boot_stack_top         ; reload the stack in long mode
        xor     ebp, ebp

        mov     edi, ebx                    ; arg1: multiboot2 info (physical)
        mov     esi, MB2_BOOT_MAGIC         ; arg2: boot magic (triple check)
        call    kmain                       ; extern from kernel_main.c

.panic_halt:
        cli
        hlt
        jmp     .panic_halt                 ; kmain returned = unrecoverable

; ---------------------------------------------------------------------------
; Boot page tables + stack (BSS; GRUB's ELF loader zero-fills this section).
; The three tables MUST stay consecutive because boot code wipes them with a
; single rep stosd from &pml4.
; ---------------------------------------------------------------------------
section .bss
alignb 4096
global pml4
global pdpt
global pd_page_dir
global boot_stack_bottom
global boot_stack_top

pml4:           resb 4096                  ; level-4 table   (root, in CR3)
pdpt:           resb 4096                  ; level-3 table
pd_page_dir:    resb 4096                  ; level-2 table (2 MiB huge pages)

alignb 16
boot_stack_bottom:
                resb 16384                 ; 16 KiB boot stack
boot_stack_top:

; ---------------------------------------------------------------------------
; Flat long-mode GDT.  All our memory is < 4 GiB, so in 32-bit mode the lgdt
; only reads the low 6 bytes of the descriptor (limit + base[31:0]) - the
; extra dq padding keeps the upper base dword at zero.  The full 64-bit base
; form is likewise harmless once we execute in 64-bit code.
; ---------------------------------------------------------------------------
section .rodata
align 8
gdt64:
        dq 0x0000000000000000               ; 0x00  null descriptor
        dq 0x00AF9A000000FFFF               ; 0x08  kernel code: P|S|DPL0|
                                            ;        exec/read, L=1, D=0, G=1
        dq 0x00CF92000000FFFF               ; 0x10  kernel data: P|S|DPL0|
                                            ;        read/write, 4 GiB limit
        dq 0x00AFFA000000FFFF               ; 0x18 (unused; keep alignment)
        dq 0x0020FA000000FFFF               ; 0x20 user code: P|S|DPL3|
                                            ;        exec/read, L=1
        dq 0x0020F2000000FFFF               ; 0x28 user data: P|S|DPL3|
                                            ;        read/write
gdt64_end:
gdt_descriptor:
        dw  gdt64_end - gdt64 - 1           ; limit
        dd  gdt64                           ; base bits 31:0 (already 0 above)
        dd  0                               ; base bits 63:32 = 0
