/*
 * NeolyxOS Hello World User Program
 * 
 * Minimal userspace application that demonstrates:
 * - Syscall interface (sys_write, sys_exit)
 * - Running in Ring 3 (user mode)
 * - Clean ELF format for the loader
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

/* ============ Syscall Numbers ============ */
/* Must match kernel/include/core/syscall.h */

#define SYS_EXIT    1
#define SYS_WRITE   24

/* File descriptors */
#define STDOUT_FD   1

/* ============ Syscall Wrapper ============ */

static inline long syscall1(long num, long arg1) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num), "D" (arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num), "D" (arg1), "S" (arg2), "d" (arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* ============ Libc-like Functions ============ */

static int strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void print(const char *msg) {
    syscall3(SYS_WRITE, STDOUT_FD, (long)msg, strlen(msg));
}

static void exit(int code) {
    syscall1(SYS_EXIT, code);
    /* Should not return, but loop just in case */
    while (1) { }
}

/* ============ Main Entry Point ============ */

void _start(void) {
    print("Hello from NeolyxOS userspace!\n");
    print("This program is running in Ring 3.\n");
    print("Syscalls are working correctly.\n");
    
    exit(0);
}
