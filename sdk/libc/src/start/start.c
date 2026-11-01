/*
 * nxlibc - library start-up: called by _start (crt/crt0.S), runs main.
 *
 * Order:
 *   1. a known floating-point environment (x87: 64-bit precision, round to
 *      nearest, exceptions masked; SSE: MXCSR 0x1F80), so results are the
 *      same on every machine whatever state the kernel handed over;
 *   2. argument and environment checks, the start time for clock();
 *   3. the kernel ABI query and standard streams;
 *   4. constructors (.init_array);
 *   5. exit(main(argc, argv, envp)).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdint.h>
#include <nx/sys.h>
#include "../internal.h"

extern int main(int argc, char **argv, char **envp);

/* Linker-provided bounds of the constructor table (crt/nxapp.ld). Weak so
 * programs linked with another script still start. */
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));

static char *g_empty_env[1] = { NULL };

void __nxc_init(int argc, char **argv, char **envp) {
    /* 1. floating-point environment */
    uint16_t fcw = 0x037F;
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("fldcw %0" : : "m"(fcw));
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));

    /* 2. environment (arguments were checked by __nxc_start) */
    (void)argc;
    (void)argv;
    __nxc_environ = envp ? envp : g_empty_env;
    __nxc_start_ms = nxsys_time();

    /* 3. kernel features and standard streams */
    (void)nxsys_abi_flags();
    __nxc_stdio_init();

    /* 4. constructors */
    if (__init_array_start && __init_array_end) {
        for (void (**fn)(void) = __init_array_start; fn < __init_array_end; fn++) {
            if (*fn) (*fn)();
        }
    }
}

_Noreturn void __nxc_start(long argc, char **argv, char **envp) NXC_HIDDEN;

_Noreturn void __nxc_start(long argc, char **argv, char **envp) {
    static char *no_args[1] = { NULL };
    if (argc < 0 || argc > 4096 || (argc > 0 && !argv)) {
        __nxc_log("nxlibc: start-up: invalid argc/argv from the kernel; ignoring arguments\n");
        argc = 0;
        argv = no_args;
        envp = NULL;
    }
    if (!argv) argv = no_args;
    __nxc_init((int)argc, argv, envp);
    exit(main((int)argc, argv, __nxc_environ));
}
