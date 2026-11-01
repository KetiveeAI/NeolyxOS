/*
 * Host unit test for the physical memory manager (kernel/src/mm/pmm.c).
 *
 * Checks the K0 changes:
 *   - the search hint gives exactly the lowest free page, like the old
 *     scan from page 0 (compared against a reference model over a seeded
 *     random alloc/free walk, single and contiguous allocations);
 *   - every path releases the lock (a nested acquire is reported);
 *   - double frees, unaligned and out-of-range frees are refused and do
 *     not change the statistics;
 *   - free/used counters stay consistent with the bitmap.
 *
 * Build and run from the repository root:
 *   gcc -std=c11 -O1 -Wall -Wextra -DPMM_HOST_TEST -Ikernel/include -no-pie \
 *       -Wl,--defsym=_kernel_end=0x200000 -o /tmp/pmm_test tests/host/pmm_test.c
 *   /tmp/pmm_test
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_fail, g_checks, g_lock_errors;

static void pmm_host_lock_error(void) { g_lock_errors++; }

/* Kernel symbols pmm.c expects */
void serial_puts(const char *s) { (void)s; }
void serial_putc(char c) { (void)c; }
/* _kernel_end comes from the link line (--defsym), like the kernel's linker script */
void addrspace_reserve_window(void) {}

#include "../../kernel/src/mm/pmm.c"

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail = 1;
    }
}

/* Reference model: one byte per page, 1 = used */
#define TEST_PAGES (64ULL * 1024)          /* 256 MiB of test RAM */
#define NO_RUN (~0ULL)
static uint8_t g_ref[TEST_PAGES];

static uint64_t ref_lowest(uint64_t count) {
    uint64_t run = 0;
    for (uint64_t p = 0; p < TEST_PAGES; p++) {
        run = g_ref[p] ? 0 : run + 1;
        if (run == count) return p + 1 - count;
    }
    return NO_RUN;
}

static uint64_t rng = 0x9E3779B97F4A7C15ULL;
static uint64_t next_rand(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
}

int main(void) {
    /* Memory map: usable 0-256 MiB with a reserved hole at 64-65 MiB */
    uefi_memory_descriptor_t map[3];
    memset(map, 0, sizeof map);
    map[0].type = UEFI_CONVENTIONAL; map[0].physical_start = 0;
    map[0].num_pages = (64ULL << 20) / PAGE_SIZE;
    map[1].type = UEFI_RESERVED;     map[1].physical_start = 64ULL << 20;
    map[1].num_pages = (1ULL << 20) / PAGE_SIZE;
    map[2].type = UEFI_CONVENTIONAL; map[2].physical_start = 65ULL << 20;
    map[2].num_pages = TEST_PAGES - (65ULL << 20) / PAGE_SIZE;
    pmm_init(map, sizeof map, sizeof map[0]);

    for (uint64_t p = 0; p < TEST_PAGES; p++) g_ref[p] = (uint8_t)bitmap_test(p);
    check(g_ref[0] && g_ref[255], "first 1 MiB reserved");
    check(g_ref[256] && g_ref[511] && !g_ref[512], "kernel image (1-2 MiB) reserved, 2 MiB free");
    check(g_ref[(64ULL << 20) / PAGE_SIZE], "reserved hole stays used");
    check(pmm_lock == 0 && g_lock_errors == 0, "lock released after init");

    pmm_stats_t st0;
    pmm_get_stats(&st0);

    /* Random walk: allocations must equal the reference's lowest choice */
    uint64_t live[4096];
    uint64_t live_n[4096];
    int nlive = 0, mismatches = 0, allocs = 0, contig = 0;
    for (int step = 0; step < 200000; step++) {
        uint64_t r = next_rand();
        if (nlive < 4096 && (r % 3 != 0 || nlive == 0)) {
            uint64_t count = (r >> 8) % 8 == 0 ? 1 + (r >> 16) % 37 : 1;
            uint64_t want = ref_lowest(count);
            uint64_t got = count == 1 ? pmm_alloc_page() : pmm_alloc_pages(count);
            if (want == NO_RUN) {
                if (got != 0) mismatches++;
                continue;
            }
            if (got != want * PAGE_SIZE) { mismatches++; continue; }
            for (uint64_t i = 0; i < count; i++) g_ref[want + i] = 1;
            allocs++;
            if (count > 1) contig++;
            live[nlive] = got;
            live_n[nlive] = count;
            nlive++;
        } else {
            int k = (int)((r >> 4) % (uint64_t)nlive);
            pmm_free_pages(live[k], live_n[k]);
            for (uint64_t i = 0; i < live_n[k]; i++) g_ref[live[k] / PAGE_SIZE + i] = 0;
            live[k] = live[nlive - 1];
            live_n[k] = live_n[nlive - 1];
            nlive--;
        }
    }
    check(allocs > 100000 && contig > 5000, "walk exercised single and contiguous allocations");
    check(mismatches == 0, "hinted search returns the lowest free page(s)");
    check(pmm_lock == 0 && g_lock_errors == 0, "every path releases the lock");

    uint64_t bitmap_used = 0;
    for (uint64_t p = 0; p < TEST_PAGES; p++) bitmap_used += (uint64_t)(bitmap_test(p) && !g_ref[p]);
    check(bitmap_used == 0, "bitmap matches the reference model");

    /* Error paths leave the counters alone */
    pmm_stats_t a, b;
    pmm_get_stats(&a);
    uint64_t free_page = ref_lowest(1) * PAGE_SIZE;
    pmm_free_page(free_page);                 /* double free (page is free) */
    pmm_free_page(free_page + 12);            /* unaligned */
    pmm_free_page(MAX_PHYSICAL_MEMORY + PAGE_SIZE);   /* out of range */
    pmm_get_stats(&b);
    check(a.free_pages == b.free_pages && a.used_pages == b.used_pages,
          "refused frees do not change statistics");
    check(pmm_alloc_pages(MAX_PAGES + 1) == 0, "oversized contiguous request refused");
    check(pmm_alloc_pages(0) == 0, "zero-page request refused");

    /* Free everything: back to the initial counters */
    for (int k = 0; k < nlive; k++) pmm_free_pages(live[k], live_n[k]);
    pmm_stats_t end;
    pmm_get_stats(&end);
    check(end.free_pages == st0.free_pages && end.used_pages == st0.used_pages,
          "counters return to the initial state");
    check(pmm_alloc_page() == 0x200000, "after freeing all, lowest page is the first after the kernel image");
    check(pmm_lock == 0 && g_lock_errors == 0, "lock released at the end");

    /* Exhaustion: allocate every page, then one more fails cleanly */
    uint64_t got_all = 0;
    while (pmm_alloc_page() != 0) got_all++;
    check(pmm_alloc_page() == 0, "exhausted PMM returns 0");
    check(pmm_alloc_pages(2) == 0, "exhausted PMM refuses contiguous");
    check(pmm_lock == 0 && g_lock_errors == 0, "lock released on failure paths");
    (void)got_all;

    printf("%s: %d checks\n", g_fail ? "FAIL" : "PASS", g_checks);
    return g_fail;
}
