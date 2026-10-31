/*
 * NeolyxOS Ticker - runs alongside the desktop
 *
 * A small user program used to verify multi-process support:
 *   - It is linked at the same address as the desktop (0x1000000). It can
 *     only run without corrupting the desktop if each process has its own
 *     address space.
 *   - It grows its heap with brk() and checks the memory it gets.
 *   - It prints a heartbeat once a second with sleep(), so the desktop
 *     keeps rendering between ticks, then exits cleanly.
 *
 * Launched from the desktop's app drawer (/apps/ticker on the boot disk)
 * or with sys_spawn("/apps/ticker", ...).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_SLEEP       2
#define SYS_TIME        3
#define SYS_GETPID      5
#define SYS_BRK         10
#define SYS_DEBUG_PRINT 127

#define TICKER_BEATS    8
#define TICKER_HEAP     (64 * 1024)

static inline int64_t sc1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sc0(int64_t num) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

/* ============ Output ============ */

static char g_line[160];
static int g_len;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + v % 10); v /= 10; }
    out_str(&buf[i]);
}

static void out_hex(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = hex[(v >> (60 - 4 * i)) & 0xF];
    buf[18] = '\0';
    out_str(buf);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

/* ============ Program ============ */

/* Lives in this program's .data, at an address the desktop also uses */
static volatile uint64_t g_marker = 0x5449434B45520001ULL;

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

void _start(long argc, char **argv) {
    uint64_t pid = (uint64_t)sc0(SYS_GETPID);

    out_str("[TICKER] PID ");
    out_dec(pid);
    out_str(" started, argc=");
    out_dec((uint64_t)argc);
    if (argc > 0 && argv && argv[0]) {
        out_str(" argv[0]=");
        out_str(argv[0]);
    }
    out_str("\n");
    flush();

    out_str("[TICKER] code at ");
    out_hex((uint64_t)&_start);
    out_str(", data at ");
    out_hex((uint64_t)&g_marker);
    out_str(" (desktop uses the same range)\n");
    flush();

    /* Heap: grow with brk and check every byte we were given */
    int64_t base = sc1(SYS_BRK, 0);
    int64_t top = (base > 0) ? sc1(SYS_BRK, base + TICKER_HEAP) : -1;
    if (base <= 0 || top < base + TICKER_HEAP) {
        out_str("[TICKER] FAIL: brk did not grow the heap\n");
        flush();
        finish(2);
    }
    volatile uint8_t *heap = (volatile uint8_t *)base;
    for (uint64_t i = 0; i < TICKER_HEAP; i++) heap[i] = (uint8_t)(i * 7 + pid);
    for (uint64_t i = 0; i < TICKER_HEAP; i++) {
        if (heap[i] != (uint8_t)(i * 7 + pid)) {
            out_str("[TICKER] FAIL: heap mismatch at ");
            out_hex(base + i);
            out_str("\n");
            flush();
            finish(3);
        }
    }
    out_str("[TICKER] heap ");
    out_hex((uint64_t)base);
    out_str(" +64KB ok\n");
    flush();

    for (int beat = 1; beat <= TICKER_BEATS; beat++) {
        out_str("[TICKER] PID ");
        out_dec(pid);
        out_str(" tick ");
        out_dec((uint64_t)beat);
        out_str("/");
        out_dec(TICKER_BEATS);
        out_str(" at ");
        out_dec((uint64_t)sc0(SYS_TIME));
        out_str(" ms\n");
        flush();
        g_marker++;
        sc1(SYS_SLEEP, 1000);
    }

    if (g_marker != 0x5449434B45520001ULL + TICKER_BEATS) {
        out_str("[TICKER] FAIL: private data changed under us\n");
        flush();
        finish(4);
    }

    out_str("[TICKER] PID ");
    out_dec(pid);
    out_str(" finished, exiting\n");
    flush();
    finish(0);
}
