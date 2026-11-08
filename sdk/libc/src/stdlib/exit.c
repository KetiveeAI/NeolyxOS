/*
 * nxlibc - program termination: exit, atexit, quick_exit, _Exit, abort.
 *
 * exit() runs atexit handlers in reverse order of registration, flushes
 * and closes every stream, then ends the process. abort() writes a line
 * to the kernel debug log, does not flush streams (ISO C leaves that
 * open; unflushed data may belong to a damaged state), and ends the
 * process with status NXC_ABORT_STATUS (134).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <errno.h>
#include <nx/sys.h>
#include "../internal.h"

#define ATEXIT_MAX      32      /* ISO C requires at least 32 */
#define NXC_ABORT_STATUS 134

static void (*g_atexit[ATEXIT_MAX])(void);
static int g_atexit_n;
static void (*g_quick[ATEXIT_MAX])(void);
static int g_quick_n;
static int g_exiting;

int atexit(void (*fn)(void)) {
    if (!fn) {
        errno = EINVAL;
        return -1;
    }
    if (g_atexit_n >= ATEXIT_MAX) {
        errno = ENOMEM;
        return -1;
    }
    g_atexit[g_atexit_n++] = fn;
    return 0;
}

int at_quick_exit(void (*fn)(void)) {
    if (!fn) {
        errno = EINVAL;
        return -1;
    }
    if (g_quick_n >= ATEXIT_MAX) {
        errno = ENOMEM;
        return -1;
    }
    g_quick[g_quick_n++] = fn;
    return 0;
}

void __nxc_run_atexit(void) {
    /* A handler may register more handlers; they run too, in order */
    while (g_atexit_n > 0) {
        void (*fn)(void) = g_atexit[--g_atexit_n];
        fn();
    }
}

_Noreturn void exit(int status) {
    if (g_exiting) {
        /* exit() called from an atexit handler: ISO C undefined; finish
         * without running the handlers again */
        __nxc_log("nxlibc: exit() called again during exit; ending now\n");
        __nxc_stdio_flush_all();
        nxsys_exit(status);
    }
    g_exiting = 1;
    __nxc_run_atexit();
    __nxc_stdio_flush_all();
    nxsys_exit(status);
}

_Noreturn void quick_exit(int status) {
    while (g_quick_n > 0) {
        void (*fn)(void) = g_quick[--g_quick_n];
        fn();
    }
    nxsys_exit(status);
}

_Noreturn void _Exit(int status) {
    nxsys_exit(status);
}

_Noreturn void abort(void) {
    __nxc_log("nxlibc: abort() called; ending the process\n");
    nxsys_exit(NXC_ABORT_STATUS);
}
