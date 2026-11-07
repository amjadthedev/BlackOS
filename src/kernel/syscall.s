; =============================================================================
;  Black OS - SYSCALL/SYSRET ring-3 ramp (Module 4)
;
;  MSRs ( programmed by syscall_arch_init() in syscall.c ):
;      IA32_STAR  [47:32] = 0x0009   -> SYSCALL lands in kernel CS 0x08,
;                                       SS becomes 0x10
;                 [63:48] = 0x0020   -> SYSRET returns to user CS 0x23,
;                                       SS becomes 0x2B
;      IA32_LSTAR         = syscall_entry
;      IA32_FMASK         = 0x202     -> IF cleared on entry, rest of the
;                                       user RFLAGS replaced at SYSRET
;
;  Entry ABI (the user side, Linux-flavoured):
;      rax = syscall number, rdi..r9 = arguments
;  Kernel dispatcher: u64 syscall_dispatch(nr, a1..a5) in C.
;
;  User entry: syscall_run_user(rip, user_rsp) IRETs to ring 3 and only
;  "returns" when the task calls sys_exit (syscall_finish_user), which
;  rewinds onto the exact saved kernel continuation point.
; =============================================================================
[bits 64]

extern syscall_dispatch                 ; C, syscall.c
extern syscall_kernel_rsp               ; C-visible BSS slot (next entry stack)

global syscall_entry
global syscall_run_user
global syscall_finish_user
global syscall_kstack_top
global syscall_kernel_rsp

section .text

; ── slot: where sys_exit rewinds to ────────────────────────────────────────
section .bss
align 16
syscall_kstack_bottom:
        resq 4096                       ; 32 KiB kernel-side syscall stack
syscall_kstack_top:

syscall_kernel_rsp:
        resq 1                          /* current kernel RSP for syscall entry */

syscall_saved_kernel_rsp:
        resq 1                          ; continuation rsp for run_user
syscall_saved_user_rsp:
        resq 1                          ; user rsp parked by syscall_entry
syscall_user_rcx:
        resq 1
syscall_user_r11:
        resq 1

section .text

; ──────────────────────────────────────────────────────────────────────────
; syscall_entry: LSTAR target.  CPL0 already (CS from STAR[47:32]).
; scratch regs rax, rcx, r11, rdi, rsi, rdx are caller-clobbered per the
; System V ABI, but we preserve them anyway because the dispatcher consumer
; table treats every argument register as live.
; ──────────────────────────────────────────────────────────────────────────
syscall_entry:
        cli
        mov [rel syscall_saved_user_rsp], rsp
        mov [rel syscall_user_rcx], rcx
        mov [rel syscall_user_r11], r11

        mov rax, [rel syscall_kernel_rsp]       ; first entry primes BSS
        test rax, rax
        jnz .stack_ok
        mov rax, syscall_kstack_top
.stack_ok:
        mov rsp, rax
        mov qword [rel syscall_kernel_rsp], syscall_kstack_top

        ; park all GP regs (dispatcher + callee-save boundary)
        push rbp
        push rbx
        push r12
        push r13
        push r14
        push r15
        sub rsp, 8                       ; 16-byte alignment

        sti                              ; long syscalls may take IRQs

        ; ABI melange: nr -> rdi, spilled user regs into arg slots
        mov rdi, rax                     ; rax = syscall number
        mov rcx, r10                     ; arg4 rides r10 on the user side
        ; rsi, rdx, r8, r9 already carry args 1,2,4,5
        call syscall_dispatch

        cli
        add rsp, 8
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rbp
        mov rcx, [rel syscall_user_rcx]
        mov r11, [rel syscall_user_r11]
        mov rsp, [rel syscall_saved_user_rsp]
        sysretq

; ──────────────────────────────────────────────────────────────────────────
; syscall_run_user(rip=rdi, user_rsp=rsi): IRET into ring 3.
;  * the caller's kernel stack (boot stack) is parked in
;    syscall_saved_kernel_rsp; sys_exit unwinds straight to it
;  * the kernel-side syscall stack slot is primed with the static BSS stack
; ──────────────────────────────────────────────────────────────────────────
syscall_run_user:
        mov [rel syscall_saved_kernel_rsp], rsp

        mov rax, syscall_kstack_top
        mov [rel syscall_kernel_rsp], rax

        ; IRET frame (user SS must match STAR[63:48]+8 = 0x002B)
        push qword 0x0000002B            ; SS  (user data)
        push rsi                         ; user RSP
        push qword 0x0000000000000202    ; RFLAGS: IF set
        push qword 0x00000023            ; CS  (user code, DPL3)
        push rdi                         ; user RIP
        iretq

; unreachable: user returns only via syscall / sys_exit

; ──────────────────────────────────────────────────────────────────────────
; syscall_finish_user(exit_code=rdi): re-enters the kernel continuation
; invoked by the dispatcher when SYS_EXIT is seen (never SYSRETs back).
; ──────────────────────────────────────────────────────────────────────────
syscall_finish_user:
        mov rsp, [rel syscall_saved_kernel_rsp]

        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        mov rax, rdi                     ; run_user reports the exit code
        ret
