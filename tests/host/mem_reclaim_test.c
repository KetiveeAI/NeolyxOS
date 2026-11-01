/*
 * Host unit test for the K2 part 3 reclaim policy
 * (kernel/include/core/mem_reclaim.h).
 *
 * Checks:
 *   - nothing is chosen when no candidate is eligible or none holds pages;
 *   - the eligible candidate with the most private pages is chosen, and a
 *     larger but ineligible one (FOREGROUND, system, running) never is;
 *   - ties go to the first candidate (lowest pid), so the choice is
 *     deterministic;
 *   - the rate limit: the first reclaim is always allowed, the next only
 *     NX_RECLAIM_INTERVAL_MS later, and a clock that goes back waits;
 *   - over a simulated 20 s of continuous shortage, at most one app is
 *     ended per interval, largest first.
 *
 * Build and run from the repository root:
 *   gcc -std=c11 -O1 -Wall -Wextra -Ikernel/include \
 *       -o /tmp/mem_reclaim_test tests/host/mem_reclaim_test.c
 *   /tmp/mem_reclaim_test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "../../kernel/include/core/mem_reclaim.h"
#include "../../kernel/include/core/nx_mem.h"

static int g_fail, g_checks;

#define CHECK(cond, ...) do {                                   \
    g_checks++;                                                 \
    if (!(cond)) {                                              \
        g_fail++;                                               \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
    }                                                           \
} while (0)

static void test_pick(void) {
    CHECK(nx_reclaim_pick(NULL, 3) == -1, "NULL list picks nothing");

    nx_reclaim_cand_t none[3] = {
        { 1, 0, 9000 },     /* desktop: not eligible */
        { 2, 0, 5000 },     /* FOREGROUND app */
        { 3, 1, 0 },        /* background app holding nothing */
    };
    CHECK(nx_reclaim_pick(none, 3) == -1, "no eligible app with pages: nothing chosen");
    nx_reclaim_cand_t one[1] = { { 9, 1, 50 } };
    CHECK(nx_reclaim_pick(one, 0) == -1, "empty list picks nothing");

    nx_reclaim_cand_t c[6] = {
        { 1, 0, 90000 },    /* desktop (largest, never chosen) */
        { 4, 1, 16384 },    /* background 64 MB */
        { 5, 0, 70000 },    /* foreground, larger */
        { 6, 1, 20000 },    /* background, largest eligible */
        { 7, 1, 100 },
        { 8, 0, 80000 },    /* the running process */
    };
    CHECK(nx_reclaim_pick(c, 6) == 3, "largest background app chosen (got %d)", nx_reclaim_pick(c, 6));
    c[3].eligible = 0;
    CHECK(nx_reclaim_pick(c, 6) == 1, "next largest once it is gone (got %d)", nx_reclaim_pick(c, 6));

    nx_reclaim_cand_t tie[3] = { { 10, 1, 500 }, { 11, 1, 500 }, { 12, 1, 499 } };
    CHECK(nx_reclaim_pick(tie, 3) == 0, "tie goes to the lowest pid");
    CHECK(nx_reclaim_pick(tie, 1) == 0, "count limits the scan");
    CHECK(nx_reclaim_pick(tie + 2, 1) == 0, "single candidate");
}

static void test_due(void) {
    CHECK(nx_reclaim_due(0, 0, 0) == 1, "first reclaim at boot allowed");
    CHECK(nx_reclaim_due(123456, 0, 0) == 1, "first reclaim any time allowed");
    CHECK(nx_reclaim_due(1000, 1000, 1) == 0, "not again at the same tick");
    CHECK(nx_reclaim_due(1000 + NX_RECLAIM_INTERVAL_MS - 1, 1000, 1) == 0, "not 1 ms early");
    CHECK(nx_reclaim_due(1000 + NX_RECLAIM_INTERVAL_MS, 1000, 1) == 1, "allowed after exactly the interval");
    CHECK(nx_reclaim_due(999, 1000, 1) == 0, "clock went back: wait");
    CHECK(NX_RECLAIM_INTERVAL_MS == 2000u, "interval is 2 s (owner decision)");
}

/* 20 s of shortage checked every ms: ends at most one app per interval,
 * always the largest eligible one left */
static void test_sustained(void) {
    nx_reclaim_cand_t c[8];
    for (int i = 0; i < 8; i++) {
        c[i].pid = (uint32_t)(10 + i);
        c[i].eligible = (i % 2 == 0);           /* 4 background apps */
        c[i].pages = (uint64_t)(1000 * (i + 1));
    }
    uint64_t last = 0;
    int have = 0, ended = 0, order_ok = 1;
    uint64_t prev_pages = ~0ULL;
    uint64_t times[8];
    for (uint64_t now = 0; now < 20000; now++) {
        if (!nx_reclaim_due(now, last, have)) continue;
        int i = nx_reclaim_pick(c, 8);
        if (i < 0) continue;
        if (c[i].pages > prev_pages) order_ok = 0;
        prev_pages = c[i].pages;
        c[i].eligible = 0;                       /* ended */
        times[ended++] = now;
        last = now;
        have = 1;
    }
    CHECK(ended == 4, "all 4 background apps ended, no others (got %d)", ended);
    CHECK(order_ok, "largest first");
    int spaced = 1;
    for (int k = 1; k < ended; k++) {
        if (times[k] - times[k - 1] < NX_RECLAIM_INTERVAL_MS) spaced = 0;
    }
    CHECK(spaced, "at least 2 s between reclaims");
    CHECK(ended >= 2 && times[1] - times[0] == NX_RECLAIM_INTERVAL_MS, "next one exactly 2 s later");
}

static void test_exit_code(void) {
    /* Must not look like a CPU fault (128 + vector 0..31) or a normal exit */
    CHECK(NX_EXIT_RECLAIMED > 159 || NX_EXIT_RECLAIMED < 128, "not a fault exit code");
    CHECK(NX_EXIT_RECLAIMED != 0 && NX_EXIT_RECLAIMED != 1, "not a normal exit code");
}

int main(void) {
    test_pick();
    test_due();
    test_sustained();
    test_exit_code();
    printf("mem_reclaim_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
