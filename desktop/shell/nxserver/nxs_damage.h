/*
 * nxserver - per-frame damage region
 *
 * The set of screen rectangles that must be redrawn in the next frame
 * (docs/desktop/proposals/nxserver-window-protocol.md, Decision 3):
 *  - rectangles are clipped to the screen and kept disjoint (overlapping or
 *    touching rectangles are merged into their bounding box);
 *  - at most NXS_DAMAGE_RECTS rectangles are kept; one more merges the two
 *    whose bounding box wastes the least area;
 *  - once the rectangles cover more than half of the screen, the region
 *    becomes "full" and the whole screen is redrawn.
 *
 * Pure functions with no syscalls and no shell state, so the same code runs
 * in the desktop and in tests/host/nxs_damage_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXSERVER_NXS_DAMAGE_H
#define NXSERVER_NXS_DAMAGE_H

#include <stdint.h>

#define NXS_DAMAGE_RECTS 8

typedef struct {
    int32_t x, y, w, h;         /* w, h > 0 for every stored rectangle */
} nxs_rect_t;

typedef struct {
    int32_t    sw, sh;          /* screen size; 0 = not initialized */
    int        full;            /* the whole screen is damaged */
    uint32_t   count;           /* rects[0..count) when !full */
    nxs_rect_t rects[NXS_DAMAGE_RECTS];
} nxs_damage_t;

/* Empty region for a sw x sh screen (sizes <= 0 give a region that ignores
 * every rectangle and is never damaged). */
void nxs_damage_init(nxs_damage_t *d, int32_t sw, int32_t sh);

/* Forget all damage (start of the next frame). */
void nxs_damage_clear(nxs_damage_t *d);

/* Add a rectangle in screen pixels. 64-bit clipping, so any input is safe;
 * empty or off-screen rectangles are ignored. */
void nxs_damage_add(nxs_damage_t *d, int64_t x, int64_t y, int64_t w, int64_t h);

/* Damage the whole screen. */
void nxs_damage_add_full(nxs_damage_t *d);

/* 1 when nothing needs drawing. */
int nxs_damage_empty(const nxs_damage_t *d);

/* Damaged pixels (the screen area when full). Rectangles are disjoint, so
 * this is exact. */
uint64_t nxs_damage_area(const nxs_damage_t *d);

#endif /* NXSERVER_NXS_DAMAGE_H */
