/*
 * nxlibc - process entry point (_start) for C programs on NeolyxOS.
 *
 * Kernel entry ABI (kernel/src/core/process.c, setup_user_stack):
 *   RDI = argc, RSI = argv; argv[argc] is NULL and the environment
 *   (NULL-terminated) starts at argv[argc + 1]. Interrupts are on and the
 *   FPU/SSE state is the kernel's clean default.
 *
 * _start clears the frame pointer (end of stack traces), aligns the stack
 * to 16 bytes as the C calling convention requires, and calls
 * __nxc_start(argc, argv, envp), which never returns.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

    .text
    .globl _start
    .type _start, @function
_start:
    xor     %ebp, %ebp
    and     $-16, %rsp
    /* envp = argv + argc + 1 (argv may be NULL when argc is 0) */
    xor     %edx, %edx
    test    %rsi, %rsi
    jz      1f
    lea     8(%rsi,%rdi,8), %rdx
1:
    call    __nxc_start
    /* __nxc_start does not return; if it ever did, stop here */
2:
    pause
    jmp     2b
    .size _start, .-_start

    .section .note.GNU-stack, "", @progbits
