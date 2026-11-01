/*
 * NeolyxOS memory reclaim policy (K2 part 3) - kernel internal
 *
 * When the desktop or the kernel would take free memory below half the
 * reserve, the kernel ends the BACKGROUND app holding the most private
 * pages (owner decision, option A, 2026-11-01). FOREGROUND apps, system
 * processes and kernel threads are never chosen, and at most one app is
 * ended every NX_RECLAIM_INTERVAL_MS.
 *
 * The choice is kept free of kernel state so tests/host/mem_reclaim_test.c
 * can check it on the host.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_MEM_RECLAIM_H
#define NEOLYX_MEM_RECLAIM_H

#include <stdint.h>

#define NX_RECLAIM_INTERVAL_MS  2000u   /* at most one app ended per interval */

typedef struct {
    uint32_t pid;
    uint32_t eligible;      /* 1: a live BACKGROUND app that is not running now */
    uint64_t pages;         /* private pages it holds */
} nx_reclaim_cand_t;

/*
 * Index of the eligible candidate with the most private pages, or -1 when
 * none holds any. Ties go to the first in the array (the lowest pid when
 * the caller scans pids in order), so the choice is deterministic.
 */
static inline int nx_reclaim_pick(const nx_reclaim_cand_t *c, uint32_t n) {
    if (!c) return -1;
    int best = -1;
    for (uint32_t i = 0; i < n; i++) {
        if (!c[i].eligible || c[i].pages == 0) continue;
        if (best < 0 || c[i].pages > c[best].pages) best = (int)i;
    }
    return best;
}

/* Whether another app may be ended at tick `now` (ms): none was ended
 * yet, or the last one was at least the interval ago. */
static inline int nx_reclaim_due(uint64_t now, uint64_t last, int have_last) {
    if (!have_last) return 1;
    if (now < last) return 0;           /* clock went back: wait */
    return now - last >= NX_RECLAIM_INTERVAL_MS;
}

#endif /* NEOLYX_MEM_RECLAIM_H */
