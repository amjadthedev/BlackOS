; =============================================================================
;  Black OS - setjmp.s: _setjmp / longjmp for x86-64
;
;  glibc's jmp_buf is struct __jmp_buf_tag { __jmp_buf __jmpbuf; ... } with
;  __jmp_buf = long[8] laid out (sysdeps/x86_64/jmpbuf-offsets.h) as:
;
;      0 rbx   1 rbp   2 r12   3 r13   4 r14   5 r15   6 rsp   7 rip
;
;  followed by int __mask_was_saved at offset 64, which we keep 0 (no signal
;  mask is saved - _setjmp semantics).  Duktape reaches this through
;  DUK_SETJMP -> setjmp macro -> _setjmp and DUK_LONGJMP -> longjmp; the
;  fortify wrappers (__longjmp_chk) and plain aliases are exported too.
;
;  The C side (kfmt.c abort) and ctx_js.c arm a jmp_buf so a fatal JS error
;  unwinds back to a recovery point instead of halting the machine.
; =============================================================================
bits 64
default rel

global _setjmp
global setjmp
global _longjmp
global longjmp
global __longjmp_chk

section .text

; ---------------------------------------------------------------------------
; int _setjmp(jmp_buf env)
;   Save callee-saved registers + caller continuation; return 0.
; ---------------------------------------------------------------------------
_setjmp:
setjmp:
    mov     [rdi + 0*8], rbx
    mov     [rdi + 1*8], rbp
    mov     [rdi + 2*8], r12
    mov     [rdi + 3*8], r13
    mov     [rdi + 4*8], r14
    mov     [rdi + 5*8], r15
    lea     rax, [rsp + 8]              ; rsp as the caller sees it after ret
    mov     [rdi + 6*8], rax
    mov     rax, [rsp]                  ; return address
    mov     [rdi + 7*8], rax
    mov     dword [rdi + 64], 0         ; __mask_was_saved = 0
    xor     eax, eax                    ; setjmp returns 0
    ret

; ---------------------------------------------------------------------------
; void longjmp(jmp_buf env, int val)
;   Restore state and jump; a val of 0 becomes 1 (POSIX).
; ---------------------------------------------------------------------------
longjmp:
_longjmp:
__longjmp_chk:
    mov     rbx, [rdi + 0*8]
    mov     rbp, [rdi + 1*8]
    mov     r12, [rdi + 2*8]
    mov     r13, [rdi + 3*8]
    mov     r14, [rdi + 4*8]
    mov     r15, [rdi + 5*8]
    mov     rsp, [rdi + 6*8]
    mov     rcx, [rdi + 7*8]            ; target rip
    mov     eax, esi                    ; return value for the setjmp site
    test    eax, eax                    ; longjmp(env, 0) => 1
    jnz     .val_ok
    mov     eax, 1
.val_ok:
    jmp     rcx
