/*
 * NeolyxOS cpuhog - K0b scheduling test
 *
 * Two ordinary (NORMAL class) processes that never sleep. With the K0b
 * scheduler the desktop (SYSTEM class) should keep near-idle frame timing
 * and input latency while they run; compare the desktop's
 * "[DESKTOP] Frames:" lines with and without cpuhog.
 *
 *   /apps/cpuhog         spawns one copy of itself ("child"), then spins
 *   /apps/cpuhog child   spins
 *
 * Each copy spins for CH_RUN_MS and prints how many loop iterations it got
 * per second, so the CPU share of NORMAL work is visible too (it must not
 * drop to zero: SYSTEM gets at most 3 of every 4 turns).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_TIME        3   /* PIT ticks, 1 per ms */
#define SYS_GETPID      5
#define SYS_SPAWN       13
#define SYS_DEBUG_PRINT 127

#define CH_RUN_MS       20000
#define CH_REPORT_MS    1000

static inline int64_t sc0(int64_t n) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t sc1(int64_t n, int64_t a1) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t sc3(int64_t n, int64_t a1, int64_t a2, int64_t a3) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

/* ============ Output ============ */

static char g_line[160];
static int g_len;

static void out_str(const char *s) {
    while (s && *s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_dec(int64_t v) {
    char buf[21];
    int i = 20;
    uint64_t u = (v < 0) ? (uint64_t)(-v) : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) out_str("-");
    out_str(&buf[i]);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

void _start(long argc, char **argv) {
    int64_t pid = sc0(SYS_GETPID);
    int is_child = (argc > 1 && argv && str_eq(argv[1], "child"));

    if (!is_child) {
        static const char *child_argv[] = { "/apps/cpuhog", "child", 0 };
        int64_t c = sc3(SYS_SPAWN, (int64_t)"/apps/cpuhog", (int64_t)child_argv, 0);
        out_str("[CPUHOG] PID "); out_dec(pid);
        if (c > 0) { out_str(" spawned second hog PID "); out_dec(c); }
        else { out_str(" could not spawn second hog ("); out_dec(c); out_str(")"); }
        out_str("\n");
        flush();
    }

    int64_t start = sc0(SYS_TIME);
    int64_t next_report = start + CH_REPORT_MS;
    uint64_t loops = 0, total = 0;
    volatile uint64_t sink = 0;

    for (;;) {
        for (int i = 0; i < 1000; i++) sink += (uint64_t)i;
        loops++;
        int64_t now = sc0(SYS_TIME);
        if (now >= next_report) {
            out_str("[CPUHOG] PID "); out_dec(pid);
            out_str(": "); out_dec((int64_t)loops);
            out_str(" kloops in the last second\n");
            flush();
            total += loops;
            loops = 0;
            next_report += CH_REPORT_MS;
        }
        if (now - start >= CH_RUN_MS) break;
    }

    out_str("[CPUHOG] PID "); out_dec(pid);
    out_str(" done: "); out_dec((int64_t)(total + loops));
    out_str(" kloops in "); out_dec(CH_RUN_MS / 1000); out_str(" s\n");
    flush();
    (void)sink;
    finish(0);
}
