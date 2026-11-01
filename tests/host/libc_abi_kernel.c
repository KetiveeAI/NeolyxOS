/*
 * nxlibc ABI cross-check, kernel side: the values of libc_abi_list.h and
 * the registered syscall table, taken from the kernel's own headers.
 * Built with -iquote kernel/include; contains no code that runs anything.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include "core/syscall.h"
#include "core/nx_abi.h"
#include "core/process.h"
#include "core/nx_ipc.h"
#include "core/nx_mem.h"
#include "core/nx_net.h"
#include "core/nx_fb.h"
#include "fs/vfs.h"
#include "drivers/rtc.h"
#include "net/tcpip.h"

#define S(a, b)        (long long)sizeof(b),
#define O(a, b, fa, fb) (long long)offsetof(b, fb),
#define V(a, b)        (long long)(b),
const long long abi_kernel_values[] = {
#include "libc_abi_list.h"
};
#undef S
#undef O
#undef V
const size_t abi_kernel_count = sizeof abi_kernel_values / sizeof abi_kernel_values[0];

typedef struct { int num; const char *name; } abi_syscall_t;
#define NX_SYSCALL(num, name, fn, argc, priv, flags) { num, name },
const abi_syscall_t abi_kernel_syscalls[] = {
#include "core/syscall_table.def"
};
#undef NX_SYSCALL
const size_t abi_kernel_syscall_count = sizeof abi_kernel_syscalls / sizeof abi_kernel_syscalls[0];
