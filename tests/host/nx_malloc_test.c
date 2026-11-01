/*
 * Host unit test for the userspace heap allocator (desktop/lib/nx_malloc.c).
 *
 * The allocator source is included directly so the test can walk the heap
 * and check every invariant after each operation. brk() and debug output
 * are provided by a fake syscall layer (NX_HOST_PREVIEW), with a 64 MiB
 * reserve like the kernel's USER_HEAP_RESERVE.
 *
 * Build and run on the development machine:
 *   gcc -std=c11 -O1 -Wall -Wextra -DNX_HOST_PREVIEW -o /tmp/nx_malloc_test nx_malloc_test.c
 *   /tmp/nx_malloc_test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../desktop/lib/nx_malloc.c"

/* ============ Fake syscalls ============ */

#define RESERVE (64u << 20)
static uint8_t *g_mem;          /* page-aligned reserve */
static uint64_t g_brk;          /* current break (absolute address) */
static int g_logged;            /* [MALLOC] lines printed */

int64_t nx_host_syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    (void)a2; (void)a3;
    if (num == 10) {                                    /* brk */
        uint64_t lo = (uint64_t)g_mem + 8;              /* unaligned start, like a real break */
        if (g_brk == 0) g_brk = lo;
        if (a1 == 0) return (int64_t)g_brk;
        uint64_t want = ((uint64_t)a1 + 4095) & ~4095ULL;
        if ((uint64_t)a1 < lo || want > (uint64_t)g_mem + RESERVE) return -12;
        if (want > g_brk) g_brk = want;
        return (int64_t)g_brk;
    }
    if (num == 127) {                                   /* debug_print */
        fputs((const char *)a1, stdout);
        g_logged++;
        return 0;
    }
    return -38;
}

int64_t nx_host_syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    (void)a4;
    return nx_host_syscall(num, a1, a2, a3);
}

/* ============ Checks ============ */

static int g_fail;
static int g_checks;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail = 1;
    }
}

/* Walk every block: sizes, prev_size links, no two free neighbours, every
 * free block in exactly its bin, and in_use matches the used blocks. */
static int heap_valid(void) {
    if (g_heap.magic != NXM_STATE_MAGIC) return 1;      /* nothing allocated yet */
    uint64_t used = 0, free_bytes = 0, free_blocks = 0;
    uint32_t prev = 0;
    int prev_free = 0;
    nxm_block_t *last = 0;
    for (uint8_t *p = g_heap.base; p < g_heap.top;) {
        nxm_block_t *b = (nxm_block_t *)p;
        if (b->size < NXM_MIN_BLOCK || (b->size & 15) || p + b->size > g_heap.top) return 0;
        if (b->prev_size != prev) return 0;
        if (b->magic == NXM_USED) { used += b->size; prev_free = 0; }
        else if (b->magic == NXM_FREE) {
            if (prev_free) return 0;                    /* not coalesced */
            free_bytes += b->size;
            free_blocks++;
            prev_free = 1;
            /* it must be in its bin */
            int found = 0;
            for (nxm_block_t *c = g_heap.bins[bin_of(b->size)]; c; c = links_of(c)->next)
                if (c == b) { found = 1; break; }
            if (!found) return 0;
        } else return 0;
        prev = (uint32_t)b->size;
        last = b;
        p += b->size;
    }
    if (last != g_heap.last) return 0;
    if (used != g_heap.in_use) return 0;
    uint64_t binned = 0;
    for (int k = 0; k < NXM_BINS; k++) {
        for (nxm_block_t *c = g_heap.bins[k]; c; c = links_of(c)->next) {
            if (c->magic != NXM_FREE || bin_of(c->size) != k) return 0;
            binned++;
            if (binned > free_blocks) return 0;         /* a loop or a stale entry */
        }
    }
    return binned == free_blocks && used + free_bytes == (uint64_t)(g_heap.top - g_heap.base);
}

static int pattern_ok(const uint8_t *p, uint64_t n, uint8_t v) {
    for (uint64_t i = 0; i < n; i++) if (p[i] != (uint8_t)(v + i)) return 0;
    return 1;
}

static void pattern_fill(uint8_t *p, uint64_t n, uint8_t v) {
    for (uint64_t i = 0; i < n; i++) p[i] = (uint8_t)(v + i);
}

/* xorshift64: deterministic, so a failure reproduces */
static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

#define SLOTS 512

int main(void) {
    g_mem = aligned_alloc(4096, RESERVE);
    if (!g_mem) { puts("cannot allocate the fake reserve"); return 2; }

    /* Basics */
    check(nx_malloc(0) == 0, "malloc(0) is NULL");
    uint8_t *a = nx_malloc(1);
    check(a && ((uint64_t)a & 15) == 0, "malloc(1) is 16-byte aligned");
    uint8_t *b = nx_malloc(100);
    check(b && b != a && ((uint64_t)b & 15) == 0, "second block is distinct and aligned");
    check(heap_valid(), "heap valid after two mallocs");
    nx_mfree(a);
    check(heap_valid(), "heap valid after a free");
    uint8_t *a2 = nx_malloc(1);
    check(a2 == a, "a freed block is reused");
    nx_mfree(a2);
    nx_mfree(b);
    check(heap_valid() && g_heap.in_use == 0, "everything freed: in_use is 0");

    /* Double, invalid and NULL frees are refused, logged, harmless */
    uint8_t *c = nx_malloc(64);
    nx_mfree(c);
    int before = g_logged;
    nx_mfree(c);
    check(g_logged == before + 1 && heap_valid(), "double free is refused and logged");
    nx_mfree((void *)0x1234);
    check(g_logged == before + 2 && heap_valid(), "free of a foreign pointer is refused and logged");
    uint8_t *d = nx_malloc(64);
    nx_mfree(d + 16);
    check(g_logged == before + 3 && heap_valid(), "free of an interior pointer is refused and logged");
    nx_mfree(d);
    nx_mfree(0);
    check(heap_valid(), "free(NULL) is ignored");

    /* calloc */
    check(nx_calloc((uint64_t)1 << 33, (uint64_t)1 << 33) == 0, "calloc overflow is NULL");
    uint8_t *z = nx_malloc(4096);
    memset(z, 0xAB, 4096);
    nx_mfree(z);
    uint8_t *zc = nx_calloc(1024, 4);
    int zero = 1;
    for (int i = 0; i < 4096; i++) if (zc[i]) zero = 0;
    check(zc && zero, "calloc zeroes reused memory");
    nx_mfree(zc);

    /* realloc keeps contents: grow in place, grow by moving, shrink */
    uint8_t *r = nx_malloc(100);
    pattern_fill(r, 100, 7);
    uint8_t *blocker = nx_malloc(32);            /* stops in-place growth */
    uint8_t *r2 = nx_realloc(r, 5000);
    check(r2 && pattern_ok(r2, 100, 7), "realloc that moves keeps the contents");
    uint8_t *r3 = nx_realloc(r2, 10);
    check(r3 == r2 && pattern_ok(r3, 10, 7), "realloc shrink stays in place");
    nx_mfree(blocker);
    uint8_t *r4 = nx_realloc(r3, 20000);
    check(r4 && pattern_ok(r4, 10, 7), "realloc grow keeps the contents");
    check(nx_realloc(r4, 0) == 0 && heap_valid(), "realloc(p, 0) frees");
    uint8_t *r5 = nx_realloc(0, 50);
    check(r5 != 0, "realloc(NULL, n) allocates");
    nx_mfree(r5);
    check(heap_valid() && g_heap.in_use == 0, "everything freed after realloc tests");

    /* Out of heap: NULL and logged, then recovery */
    uint8_t *big1 = nx_malloc(40u << 20);
    uint8_t *big2 = nx_malloc(40u << 20);
    check(big1 && !big2, "a request past the 64 MiB reserve fails");
    nx_mfree(big1);
    big2 = nx_malloc(40u << 20);
    check(big2 != 0, "after freeing, the same request succeeds");
    nx_mfree(big2);
    check(heap_valid(), "heap valid after the out-of-heap case");

    /* Randomized stress with contents checks and a full invariant walk */
    static uint8_t *slot[SLOTS];
    static uint64_t len[SLOTS];
    static uint8_t tag[SLOTS];
    int ok = 1;
    for (int step = 0; step < 200000 && ok; step++) {
        int i = (int)(rnd() % SLOTS);
        uint64_t op = rnd() % 10;
        uint64_t n = (rnd() % 4 == 0) ? (rnd() % 262144) + 1 : (rnd() % 2048) + 1;
        if (slot[i] && !pattern_ok(slot[i], len[i] < 256 ? len[i] : 256, tag[i])) ok = 0;
        if (op < 4) {                              /* malloc */
            if (slot[i]) { nx_mfree(slot[i]); slot[i] = 0; }
            slot[i] = nx_malloc(n);
            if (!slot[i]) { ok = 0; break; }
            len[i] = n;
            tag[i] = (uint8_t)rnd();
            pattern_fill(slot[i], n < 256 ? n : 256, tag[i]);
        } else if (op < 7) {                       /* free */
            nx_mfree(slot[i]);
            slot[i] = 0;
        } else {                                   /* realloc */
            uint8_t *q = nx_realloc(slot[i], n);
            if (!q) { ok = 0; break; }
            uint64_t keep = (slot[i] ? (len[i] < n ? len[i] : n) : 0);
            if (keep > 256) keep = 256;
            if (slot[i] && !pattern_ok(q, keep, tag[i])) ok = 0;
            slot[i] = q;
            len[i] = n;
            tag[i] = (uint8_t)rnd();
            pattern_fill(q, n < 256 ? n : 256, tag[i]);
        }
        if (step % 1000 == 0 && !heap_valid()) ok = 0;
    }
    check(ok && heap_valid(), "200000 random malloc/free/realloc steps keep contents and invariants");
    for (int i = 0; i < SLOTS; i++) { nx_mfree(slot[i]); slot[i] = 0; }
    check(heap_valid() && g_heap.in_use == 0, "everything freed after the stress run");

    /* The regression: the same allocation pattern repeated must not grow the
     * heap (the old allocator grew by the whole pattern every round) */
    uint64_t heap_after_first = 0;
    for (int round = 0; round < 50; round++) {
        uint8_t *frame[8];
        for (int k = 0; k < 8; k++) frame[k] = nx_malloc((uint64_t)(k + 1) * 512u * 1024u);  /* 18 MiB */
        for (int k = 0; k < 8; k++) nx_mfree(frame[k]);
        uint64_t used = 0, heap = 0;
        nx_malloc_usage(&used, &heap);
        if (round == 0) heap_after_first = heap;
        if (heap != heap_after_first || used != 0) { ok = 0; break; }
    }
    check(ok, "50 rounds of 18 MiB allocate+free keep the heap size constant");

    printf("%d checks, %d allocator log lines\n", g_checks, g_logged);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    free(g_mem);
    return g_fail;
}
