/*
 * nxserver - per-frame damage region (see nxs_damage.h)
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxs_damage.h"

static int64_t r_area(const nxs_rect_t *r) { return (int64_t)r->w * (int64_t)r->h; }

/* Overlapping or sharing an edge: merging such a pair never costs more
 * drawing than keeping both, and keeps the stored rectangles disjoint. */
static int r_touch(const nxs_rect_t *a, const nxs_rect_t *b) {
    return a->x <= b->x + b->w && b->x <= a->x + a->w &&
           a->y <= b->y + b->h && b->y <= a->y + a->h;
}

static nxs_rect_t r_union(const nxs_rect_t *a, const nxs_rect_t *b) {
    int32_t x0 = a->x < b->x ? a->x : b->x;
    int32_t y0 = a->y < b->y ? a->y : b->y;
    int32_t x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
    int32_t y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
    nxs_rect_t u = { x0, y0, x1 - x0, y1 - y0 };
    return u;
}

static void d_remove(nxs_damage_t *d, uint32_t i) {
    d->rects[i] = d->rects[d->count - 1];
    d->count--;
}

/* Insert r (already clipped, non-empty) keeping the rectangles disjoint
 * and at most NXS_DAMAGE_RECTS. */
static void d_insert(nxs_damage_t *d, nxs_rect_t r) {
    for (;;) {
        /* Absorb every rectangle r touches; the union can reach others,
         * so scan again after each merge. */
        int merged = 1;
        while (merged) {
            merged = 0;
            for (uint32_t i = 0; i < d->count; i++) {
                if (r_touch(&r, &d->rects[i])) {
                    r = r_union(&r, &d->rects[i]);
                    d_remove(d, i);
                    merged = 1;
                    break;
                }
            }
        }
        if (d->count < NXS_DAMAGE_RECTS) {
            d->rects[d->count++] = r;
            return;
        }
        /* Full: merge the pair (r included) whose bounding box adds the
         * least area, then insert the result again. */
        int64_t best = -1;
        uint32_t bi = 0, bj = 0;    /* bj == count stands for r */
        for (uint32_t i = 0; i < d->count; i++) {
            for (uint32_t j = i + 1; j <= d->count; j++) {
                const nxs_rect_t *a = &d->rects[i];
                const nxs_rect_t *b = j < d->count ? &d->rects[j] : &r;
                nxs_rect_t u = r_union(a, b);
                int64_t waste = r_area(&u) - r_area(a) - r_area(b);
                if (best < 0 || waste < best) { best = waste; bi = i; bj = j; }
            }
        }
        nxs_rect_t a = d->rects[bi];
        if (bj == d->count) {
            r = r_union(&a, &r);
            d_remove(d, bi);
        } else {
            nxs_rect_t b = d->rects[bj];
            nxs_rect_t u = r_union(&a, &b);
            /* Remove the higher index first so the lower one stays valid */
            d_remove(d, bj);
            d_remove(d, bi);
            d->rects[d->count++] = r;   /* room: two removed, one added */
            r = u;
        }
    }
}

void nxs_damage_init(nxs_damage_t *d, int32_t sw, int32_t sh) {
    if (!d) return;
    d->sw = sw > 0 && sh > 0 ? sw : 0;
    d->sh = sw > 0 && sh > 0 ? sh : 0;
    d->full = 0;
    d->count = 0;
}

void nxs_damage_clear(nxs_damage_t *d) {
    if (!d) return;
    d->full = 0;
    d->count = 0;
}

void nxs_damage_add_full(nxs_damage_t *d) {
    if (!d || d->sw == 0) return;
    d->full = 1;
    d->count = 0;
}

void nxs_damage_add(nxs_damage_t *d, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!d || d->sw == 0 || d->full) return;
    if (w <= 0 || h <= 0) return;
    /* Clip in 64 bits; inputs are bounded so the sums cannot overflow */
    const int64_t lim = (int64_t)1 << 40;
    if (x < -lim || y < -lim || x > lim || y > lim || w > lim || h > lim) return;
    int64_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int64_t x1 = x + w > d->sw ? d->sw : x + w;
    int64_t y1 = y + h > d->sh ? d->sh : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    nxs_rect_t r = { (int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0) };
    d_insert(d, r);
    if (nxs_damage_area(d) * 2 > (uint64_t)d->sw * (uint64_t)d->sh) nxs_damage_add_full(d);
}

int nxs_damage_empty(const nxs_damage_t *d) {
    return !d || (!d->full && d->count == 0);
}

uint64_t nxs_damage_area(const nxs_damage_t *d) {
    if (!d) return 0;
    if (d->full) return (uint64_t)d->sw * (uint64_t)d->sh;
    uint64_t a = 0;
    for (uint32_t i = 0; i < d->count; i++) a += (uint64_t)r_area(&d->rects[i]);
    return a;
}
