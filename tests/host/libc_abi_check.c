/*
 * nxlibc ABI cross-check: compares every <nx/sys.h> layout, constant and
 * syscall number, and the errno values, with the kernel's own headers
 * (collected by libc_abi_kernel.c). Any difference is a bug in nxlibc's
 * mirror of the kernel ABI.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <nx/sys.h>

#define S(a, b)        (long long)sizeof(a),
#define O(a, b, fa, fb) (long long)offsetof(a, fa),
#define V(a, b)        (long long)(a),
static const long long abi_libc_values[] = {
#include "libc_abi_list.h"
};
#undef S
#undef O
#undef V

#define S(a, b)        "sizeof(" #a ") vs sizeof(" #b ")",
#define O(a, b, fa, fb) "offsetof(" #a ", " #fa ") vs offsetof(" #b ", " #fb ")",
#define V(a, b)        #a " vs " #b,
static const char *const abi_names[] = {
#include "libc_abi_list.h"
};
#undef S
#undef O
#undef V

extern const long long abi_kernel_values[];
extern const size_t abi_kernel_count;

typedef struct { int num; const char *name; } abi_syscall_t;
extern const abi_syscall_t abi_kernel_syscalls[];
extern const size_t abi_kernel_syscall_count;

#define N(x) { NXSYS_##x, #x }
static const abi_syscall_t libc_syscalls[] = {
    N(ABI_QUERY), N(EXIT), N(SLEEP), N(TIME), N(YIELD), N(GETPID), N(GETPPID), N(FORK),
    N(EXEC), N(WAIT), N(BRK), N(SPAWN), N(OPEN), N(CLOSE), N(READ), N(WRITE), N(SEEK),
    N(STAT), N(READDIR), N(MKDIR), N(RMDIR), N(CHDIR), N(GETCWD), N(RENAME), N(SOCKET),
    N(BIND), N(LISTEN), N(ACCEPT), N(CONNECT), N(SEND), N(RECV), N(CLOSESOCK), N(INFO),
    N(REBOOT), N(SHUTDOWN), N(NXGAME_CREATE), N(NXGAME_DESTROY), N(NXGAME_START),
    N(NXGAME_STOP), N(NXGAME_GPU_ACQUIRE), N(NXGAME_GPU_RELEASE), N(NXGAME_GPU_PRESENT),
    N(NXGAME_AUDIO), N(NXGAME_INPUT), N(NXGAME_GET_BUFFER), N(FB_MAP), N(FB_INFO),
    N(FB_FLIP), N(INPUT_POLL), N(INPUT_REGISTER), N(DEBUG_PRINT), N(CURSOR_SET),
    N(RTC_GET), N(RTC_SET), N(RTC_UNIX), N(HANDLE_CLOSE), N(SHM_CREATE), N(SHM_MAP),
    N(SHM_UNMAP), N(CHAN_CREATE), N(CHAN_SEND), N(CHAN_RECV), N(OBJ_WAIT), N(FB_FLIP_RECTS),
    N(HANDLE_DUP), N(PORT_LISTEN), N(PORT_CONNECT), N(PORT_ACCEPT), N(PROC_SET_CLASS),
    N(MEM_PRESSURE_OPEN), N(MEM_PRESSURE_READ), N(MEM_STATS), N(NET_IF_LIST),
};
#define LIBC_SYSCALLS (sizeof libc_syscalls / sizeof libc_syscalls[0])

/* "nxgame.gpu_acquire" -> "NXGAME_GPU_ACQUIRE" */
static void kernel_to_libc_name(const char *in, char *out, size_t cap) {
    size_t i = 0;
    for (; in[i] && i + 1 < cap; i++) {
        char c = in[i];
        if (c == '.') c = '_';
        else if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i] = c;
    }
    out[i] = 0;
}

int main(void) {
    int fail = 0;
    size_t n = sizeof abi_libc_values / sizeof abi_libc_values[0];

    if (n != abi_kernel_count) {
        printf("  FAIL: list length %zu vs %zu (build mismatch)\n", n, abi_kernel_count);
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        if (abi_libc_values[i] != abi_kernel_values[i]) {
            printf("  FAIL: %s: %lld vs %lld\n", abi_names[i], abi_libc_values[i], abi_kernel_values[i]);
            fail++;
        }
    }

    /* every registered syscall has a wrapper number, and no extras */
    size_t matched = 0;
    for (size_t k = 0; k < abi_kernel_syscall_count; k++) {
        char want[64];
        kernel_to_libc_name(abi_kernel_syscalls[k].name, want, sizeof want);
        int found = 0;
        for (size_t j = 0; j < LIBC_SYSCALLS; j++) {
            if (strcmp(libc_syscalls[j].name, want) == 0) {
                found = 1;
                if (libc_syscalls[j].num != abi_kernel_syscalls[k].num) {
                    printf("  FAIL: NXSYS_%s = %d, kernel \"%s\" = %d\n", want, libc_syscalls[j].num,
                           abi_kernel_syscalls[k].name, abi_kernel_syscalls[k].num);
                    fail++;
                }
            }
        }
        if (!found) {
            printf("  FAIL: kernel syscall %d \"%s\" has no NXSYS_%s\n", abi_kernel_syscalls[k].num,
                   abi_kernel_syscalls[k].name, want);
            fail++;
        }
        matched += (size_t)found;
    }
    if (matched != LIBC_SYSCALLS) {
        printf("  FAIL: nxlibc has %zu syscall numbers, the kernel registers %zu\n",
               (size_t)LIBC_SYSCALLS, abi_kernel_syscall_count);
        fail++;
    }

    printf("[libc_abi_check] %zu values, %zu syscalls: %s\n", n, abi_kernel_syscall_count,
           fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
