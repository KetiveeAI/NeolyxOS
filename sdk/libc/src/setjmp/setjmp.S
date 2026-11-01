/*
 * nxlibc - setjmp/longjmp for x86_64.
 *
 * jmp_buf layout (9 x 8 bytes):
 *   0 rbx   8 rbp   16 r12   24 r13   32 r14   40 r15
 *   48 rsp (as seen by the caller of setjmp)   56 return address
 *   64 MXCSR (low 32 bits) and x87 control word (bits 32-47)
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

    .text

    .globl __nxc_setjmp
    .type __nxc_setjmp, @function
__nxc_setjmp:
    mov     %rbx, 0(%rdi)
    mov     %rbp, 8(%rdi)
    mov     %r12, 16(%rdi)
    mov     %r13, 24(%rdi)
    mov     %r14, 32(%rdi)
    mov     %r15, 40(%rdi)
    lea     8(%rsp), %rdx           /* rsp after our return */
    mov     %rdx, 48(%rdi)
    mov     (%rsp), %rdx            /* return address */
    mov     %rdx, 56(%rdi)
    stmxcsr 64(%rdi)
    fnstcw  68(%rdi)
    xor     %eax, %eax
    ret
    .size __nxc_setjmp, .-__nxc_setjmp

    /* _Noreturn void longjmp(jmp_buf env, int val): setjmp returns val, or 1 if val is 0 */
    .globl longjmp
    .type longjmp, @function
longjmp:
    mov     %esi, %eax
    test    %eax, %eax
    jnz     1f
    mov     $1, %eax
1:
    ldmxcsr 64(%rdi)
    fldcw   68(%rdi)
    mov     0(%rdi), %rbx
    mov     8(%rdi), %rbp
    mov     16(%rdi), %r12
    mov     24(%rdi), %r13
    mov     32(%rdi), %r14
    mov     40(%rdi), %r15
    mov     48(%rdi), %rsp
    jmp     *56(%rdi)
    .size longjmp, .-longjmp

    .section .note.GNU-stack, "", @progbits
