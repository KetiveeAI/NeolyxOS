/*
 * NeolyxOS fbtest - fb.flip_rects (syscall 148, ABI 1.2) from an app
 *
 * Checks docs/desktop/proposals/nxserver-window-protocol.md, "Kernel:
 * fb.flip_rects": the call is only for system processes (the desktop), and
 * the privilege check comes before every argument check, so an app always
 * gets -EPERM and learns nothing about the screen. Also checks that
 * abi.query reports ABI 1.2 and NX_ABI_FLAG_FB_RECTS.
 *
 * The copying and clipping are exercised by the desktop (the only system
 * process) and by tests/host/nx_fb_clip_test.c.
 *
 * Output: "[FBTEST] PASS/FAIL ..." lines and one final
 * "[FBTEST] RESULT: PASS" or "[FBTEST] RESULT: FAIL". Exit code 0 or 1.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_fb.h"

#define SYS_ABI_QUERY      0
#define SYS_EXIT           1
#define SYS_GETPID         5
#define SYS_DEBUG_PRINT    127
#define SYS_FB_FLIP_RECTS  148

#define EPERM              1

typedef struct {            /* nx_abi_info_t */
    uint16_t major, minor, patch, flags;
    uint32_t syscall_count;
    uint32_t reserved[3];
} abi_info_t;

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

static int64_t flip_rects(const void *src, const nx_fb_rect_t *r, uint64_t n) {
    return sc3(SYS_FB_FLIP_RECTS, (int64_t)src, (int64_t)r, (int64_t)n);
}

/* ============ Output ============ */

static char g_line[160];
static int g_len;
static int g_failed;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_sdec(int64_t v) {
    char buf[22];
    int i = 21;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 1) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) buf[--i] = '-';
    out_str(&buf[i]);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void check(int ok, const char *what, int64_t got) {
    out_str(ok ? "[FBTEST] PASS " : "[FBTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_sdec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

/* A small source buffer is enough: the call must refuse before reading it */
static uint32_t g_pixels[64];

void _start(void) {
    out_str("[FBTEST] PID ");
    out_sdec(sc1(SYS_GETPID, 0));
    out_str(" started\n");
    flush();

    abi_info_t abi;
    int64_t r = sc1(SYS_ABI_QUERY, (int64_t)&abi);
    check(r == 0 && abi.minor >= 2, "abi.query reports ABI 1.2 or later (minor)", r == 0 ? abi.minor : r);
    check(r == 0 && (abi.flags & NX_ABI_FLAG_FB_RECTS), "abi.query reports NX_ABI_FLAG_FB_RECTS", r == 0 ? abi.flags : r);

    nx_fb_rect_t one = { 0, 0, 8, 8 };
    nx_fb_rect_t many[NX_FB_RECTS_MAX + 1];
    for (int i = 0; i <= NX_FB_RECTS_MAX; i++) {
        many[i].x = i;
        many[i].y = i;
        many[i].w = 1;
        many[i].h = 1;
    }

    r = flip_rects(g_pixels, &one, 1);
    check(r == -EPERM, "fb.flip_rects with valid arguments is -EPERM for an app", r);
    r = flip_rects(0, &one, 1);
    check(r == -EPERM, "fb.flip_rects with a NULL buffer is -EPERM (privilege first)", r);
    r = flip_rects(g_pixels, 0, 1);
    check(r == -EPERM, "fb.flip_rects with NULL rects is -EPERM (privilege first)", r);
    r = flip_rects(g_pixels, &one, 0);
    check(r == -EPERM, "fb.flip_rects with count 0 is -EPERM (privilege first)", r);
    r = flip_rects(g_pixels, many, NX_FB_RECTS_MAX + 1);
    check(r == -EPERM, "fb.flip_rects with 65 rects is -EPERM (privilege first)", r);
    r = flip_rects((const void *)0xFFFF800000000000ULL, &one, 1);
    check(r == -EPERM, "fb.flip_rects with a kernel pointer is -EPERM (privilege first)", r);

    /* Many refusals in a row: the kernel keeps running (log is rate-limited) */
    int all = 1;
    for (int i = 0; i < 100; i++) if (flip_rects(g_pixels, &one, 1) != -EPERM) all = 0;
    check(all, "100 more refused calls all give -EPERM", all);

    out_str(g_failed ? "[FBTEST] RESULT: FAIL\n" : "[FBTEST] RESULT: PASS\n");
    flush();
    sc1(SYS_EXIT, g_failed ? 1 : 0);
    for (;;) { __asm__ volatile("pause"); }
}
