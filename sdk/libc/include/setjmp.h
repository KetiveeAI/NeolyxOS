/*
 * nxlibc - <setjmp.h>
 *
 * Non-local jumps for x86_64. A jmp_buf holds the callee-saved registers
 * (RBX, RBP, R12-R15), the stack pointer, the return address, and the
 * MXCSR and x87 control words, which the x86_64 calling convention also
 * treats as callee-saved.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_SETJMP_H
#define NXLIBC_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long jmp_buf[9];

int __nxc_setjmp(jmp_buf env) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf env, int val);

/* ISO C requires setjmp to be a macro */
#define setjmp(env) __nxc_setjmp(env)

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_SETJMP_H */
