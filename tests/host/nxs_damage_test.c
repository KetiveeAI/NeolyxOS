/*
 * Host unit test for the nxserver damage region
 * (desktop/shell/nxserver/nxs_damage.c).
 *
 * Checks clipping, merging of touching rectangles, the 8-rectangle cap,
 * the "more than half the screen means full" rule, and, with a seeded
 * random walk, that the region always covers every added pixel with
 * disjoint rectangles.
 *
 * Build and run on the development machine:
 *   gcc -std=c11 -O1 -Wall -Wextra -o /tmp/nxs_damage_test nxs_damage_test.c
 *   /tmp/nxs_damage_test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../desktop/shell/nxserver/nxs_damage.c"

static int g_fail, g_checks;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail = 1;
    }
}

#define SW 320
#define SH 200
static uint8_t g_want[SH][SW];

static int covered(const nxs_damage_t *d, int x, int y) {
    if (d->full) return 1;
    for (uint32_t i = 0; i < d->count; i++) {
        const nxs_rect_t *r = &d->rects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) return 1;
    }
    return 0;
}

static int disjoint_and_inside(const nxs_damage_t *d) {
    for (uint32_t i = 0; i < d->count; i++) {
        const nxs_rect_t *a = &d->rects[i];
        if (a->w <= 0 || a->h <= 0 || a->x < 0 || a->y < 0 || a->x + a->w > d->sw || a->y + a->h > d->sh)
            return 0;
        for (uint32_t j = i + 1; j < d->count; j++) {
            const nxs_rect_t *b = &d->rects[j];
            if (a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h) return 0;
        }
    }
    return 1;
}

static uint32_t g_seed = 12345;
static uint32_t rnd(uint32_t n) {
    g_seed = g_seed * 1103515245u + 12345u;
    return (g_seed >> 8) % n;
}

int main(void) {
    nxs_damage_t d;

    /* ---- Basics ---- */
    nxs_damage_init(&d, SW, SH);
    check(nxs_damage_empty(&d) && nxs_damage_area(&d) == 0, "new region is empty");
    nxs_damage_add(&d, 10, 10, 0, 5);
    nxs_damage_add(&d, 10, 10, 5, -1);
    nxs_damage_add(&d, SW, 0, 10, 10);
    nxs_damage_add(&d, -20, -20, 20, 20);
    check(nxs_damage_empty(&d), "empty and off-screen rects are ignored");
    nxs_damage_add(&d, -5, -5, 15, 15);
    check(d.count == 1 && d.rects[0].x == 0 && d.rects[0].y == 0 && d.rects[0].w == 10 && d.rects[0].h == 10,
          "rect clipped at the top-left");
    nxs_damage_add(&d, SW - 4, SH - 3, 100, 100);
    check(d.count == 2 && nxs_damage_area(&d) == 100 + 12, "rect clipped at the bottom-right");
    nxs_damage_add(&d, INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX);
    nxs_damage_add(&d, INT64_MIN, INT64_MIN, INT64_MAX, INT64_MAX);
    check(d.count == 2 && !d.full, "huge values neither overflow nor damage anything");

    /* ---- Merging ---- */
    nxs_damage_clear(&d);
    check(nxs_damage_empty(&d), "clear empties the region");
    nxs_damage_add(&d, 0, 0, 10, 10);
    nxs_damage_add(&d, 5, 5, 10, 10);
    check(d.count == 1 && d.rects[0].w == 15 && d.rects[0].h == 15, "overlapping rects merge");
    nxs_damage_add(&d, 15, 0, 5, 5);
    check(d.count == 1 && d.rects[0].w == 20, "a rect sharing an edge merges");
    nxs_damage_add(&d, 30, 0, 5, 5);
    check(d.count == 2, "a separate rect stays separate");
    nxs_damage_add(&d, 18, 0, 14, 2);
    check(d.count == 1 && d.rects[0].w == 35, "a bridge merges both rects");

    /* ---- Cap at 8 ---- */
    nxs_damage_clear(&d);
    for (int i = 0; i < 9; i++) nxs_damage_add(&d, i * 30, 0, 4, 4);
    check(d.count == NXS_DAMAGE_RECTS && !d.full, "nine separate rects are kept as eight");
    check(disjoint_and_inside(&d), "capped rects stay disjoint");
    int all = 1;
    for (int i = 0; i < 9; i++) if (!covered(&d, i * 30 + 1, 1)) all = 0;
    check(all, "capped region still covers every rect");
    check(nxs_damage_area(&d) == 7 * 16 + 34 * 4, "cap merges one neighbouring pair, not everything");

    /* ---- Half the screen means full ---- */
    nxs_damage_clear(&d);
    nxs_damage_add(&d, 0, 0, SW, SH / 2);
    check(!d.full && nxs_damage_area(&d) == (uint64_t)SW * SH / 2, "exactly half is still partial");
    nxs_damage_add(&d, 0, SH - 1, 1, 1);
    check(d.full && nxs_damage_area(&d) == (uint64_t)SW * SH, "more than half becomes full");
    nxs_damage_add(&d, 3, 3, 3, 3);
    check(d.full && d.count == 0, "adding to a full region keeps it full");
    nxs_damage_clear(&d);
    nxs_damage_add_full(&d);
    check(!nxs_damage_empty(&d) && d.full, "add_full");

    /* ---- Uninitialized and NULL ---- */
    nxs_damage_t z;
    nxs_damage_init(&z, 0, 100);
    nxs_damage_add(&z, 0, 0, 10, 10);
    nxs_damage_add_full(&z);
    check(nxs_damage_empty(&z), "zero-size screen never damages");
    nxs_damage_add(NULL, 0, 0, 1, 1);
    nxs_damage_clear(NULL);
    check(nxs_damage_empty(NULL) && nxs_damage_area(NULL) == 0, "NULL region is safe");

    /* ---- Random walk: coverage, disjointness, cap ---- */
    int ok_cover = 1, ok_disjoint = 1, ok_cap = 1;
    for (int round = 0; round < 2000; round++) {
        nxs_damage_clear(&d);
        memset(g_want, 0, sizeof(g_want));
        int n = 1 + (int)rnd(20);
        for (int k = 0; k < n; k++) {
            int32_t x = (int32_t)rnd(SW + 40) - 20, y = (int32_t)rnd(SH + 40) - 20;
            int32_t w = (int32_t)rnd(round % 4 == 0 ? 120 : 24), h = (int32_t)rnd(round % 4 == 0 ? 90 : 24);
            nxs_damage_add(&d, x, y, w, h);
            for (int yy = y; yy < y + h; yy++)
                for (int xx = x; xx < x + w; xx++)
                    if (xx >= 0 && yy >= 0 && xx < SW && yy < SH) g_want[yy][xx] = 1;
        }
        for (int yy = 0; yy < SH; yy++)
            for (int xx = 0; xx < SW; xx++)
                if (g_want[yy][xx] && !covered(&d, xx, yy)) ok_cover = 0;
        if (!disjoint_and_inside(&d)) ok_disjoint = 0;
        if (d.count > NXS_DAMAGE_RECTS) ok_cap = 0;
    }
    check(ok_cover, "random: every added pixel is covered");
    check(ok_disjoint, "random: rects are disjoint and on screen");
    check(ok_cap, "random: never more than 8 rects");

    printf("%d checks\n", g_checks);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail;
}
