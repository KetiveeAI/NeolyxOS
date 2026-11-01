/*
 * Host unit test for nx_fb_clip_rect() (kernel/include/core/nx_fb.h), the
 * rectangle clipping used by fb.flip_rects (syscall 148).
 *
 * Build and run on the development machine:
 *   gcc -std=c11 -Wall -Wextra -iquote ../../kernel/include -o /tmp/nx_fb_clip_test nx_fb_clip_test.c
 *   /tmp/nx_fb_clip_test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdint.h>
#include "core/nx_fb.h"

static int g_fail;

static void expect(const char *what, int32_t x, int32_t y, uint32_t w, uint32_t h,
                   int want, uint32_t wx0, uint32_t wy0, uint32_t wx1, uint32_t wy1) {
    nx_fb_rect_t r = { x, y, w, h };
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int got = nx_fb_clip_rect(&r, 1280, 800, &x0, &y0, &x1, &y1);
    int ok = (got == want) && (!want || (x0 == wx0 && y0 == wy0 && x1 == wx1 && y1 == wy1));
    printf("%s %s -> %d x[%u,%u) y[%u,%u)\n", ok ? "PASS" : "FAIL", what, got, x0, x1, y0, y1);
    if (!ok) g_fail = 1;
}

int main(void) {
    expect("inside", 10, 20, 30, 40, 1, 10, 20, 40, 60);
    expect("whole screen", 0, 0, 1280, 800, 1, 0, 0, 1280, 800);
    expect("larger than screen", -5, -5, 5000, 5000, 1, 0, 0, 1280, 800);
    expect("negative origin", -10, -20, 30, 40, 1, 0, 0, 20, 20);
    expect("past right/bottom", 1270, 790, 100, 100, 1, 1270, 790, 1280, 800);
    expect("width 0", 10, 10, 0, 10, 0, 0, 0, 0, 0);
    expect("height 0", 10, 10, 10, 0, 0, 0, 0, 0, 0);
    expect("left of screen", -100, 10, 100, 10, 0, 0, 0, 0, 0);
    expect("below screen", 10, 800, 10, 10, 0, 0, 0, 0, 0);
    expect("right of screen", 1280, 0, 1, 1, 0, 0, 0, 0, 0);
    expect("INT32_MIN origin, UINT32_MAX size", INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX,
           1, 0, 0, 1280, 800);
    expect("INT32_MAX origin, UINT32_MAX size (no wrap)", INT32_MAX, INT32_MAX, UINT32_MAX, UINT32_MAX,
           0, 0, 0, 0, 0);
    expect("one pixel at the last corner", 1279, 799, 1, 1, 1, 1279, 799, 1280, 800);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail;
}
