/*
 * nxlibc host test: the allocator (malloc, free, calloc, realloc,
 * aligned_alloc), its consistency checker, trimming and out-of-memory
 * behaviour. Run with "--double-free" or "--bad-free" it must abort
 * (exit status 134); libc.mk checks that.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <nx/libc.h>
#include "libc_test.h"

static uint64_t g_seed = 0x243F6A8885A308D3ull;
static uint64_t rnd(void) {
    g_seed ^= g_seed >> 12;
    g_seed ^= g_seed << 25;
    g_seed ^= g_seed >> 27;
    return g_seed * 0x2545F4914F6CDD1Dull;
}

static void fill(unsigned char *p, size_t n, unsigned char tag) {
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(tag + i * 7);
}
static int verify(const unsigned char *p, size_t n, unsigned char tag) {
    for (size_t i = 0; i < n; i++) if (p[i] != (unsigned char)(tag + i * 7)) return 0;
    return 1;
}

static void test_basic(void) {
    void *p = malloc(0);
    CHECK(p != NULL);
    free(p);
    free(NULL);
    p = malloc(1);
    CHECK(p != NULL && ((uintptr_t)p & 15) == 0);
    free(p);

    unsigned char *c = calloc(100, 7);
    CHECK(c != NULL);
    int zero = 1;
    for (int i = 0; i < 700; i++) if (c[i]) zero = 0;
    CHECK(zero);
    free(c);

    errno = 0;
    CHECK(calloc(SIZE_MAX / 2, 4) == NULL && errno == ENOMEM);
    errno = 0;
    CHECK(malloc(SIZE_MAX - 8) == NULL && errno == ENOMEM);
    errno = 0;
    CHECK(malloc((size_t)1 << 40) == NULL && errno == ENOMEM);

    for (size_t al = 16; al <= 65536; al <<= 1) {
        void *a = aligned_alloc(al, al * 3);
        CHECK(a != NULL && ((uintptr_t)a & (al - 1)) == 0);
        if (a) memset(a, 0x5A, al * 3);
        free(a);
    }
    errno = 0;
    CHECK(aligned_alloc(24, 48) == NULL && errno == EINVAL);
    CHECK(nxc_heap_check() == 0);
}

static void test_realloc(void) {
    unsigned char *p = realloc(NULL, 10);
    CHECK(p != NULL);
    fill(p, 10, 1);
    for (size_t n = 10; n < 300000; n = n * 3 + 1) {
        unsigned char *q = realloc(p, n * 3 + 1);
        CHECK(q != NULL);
        if (!q) return;
        CHECK(verify(q, n, 1));
        fill(q, n * 3 + 1, 1);
        p = q;
    }
    unsigned char *q = realloc(p, 5);    /* shrink keeps the prefix */
    CHECK(q != NULL && verify(q, 5, 1));
    p = q;
    /* realloc(p, 0) returns a minimal block and does not free p */
    q = realloc(p, 0);
    CHECK(q != NULL);
    free(q);
    errno = 0;
    p = malloc(64);
    q = realloc(p, SIZE_MAX - 4);
    CHECK(q == NULL && errno == ENOMEM);
    free(p);                             /* still owned after a failed realloc */
    CHECK(nxc_heap_check() == 0);
}

#define SLOTS 2048
static unsigned char *g_ptr[SLOTS];
static size_t g_len[SLOTS];

static size_t pick_size(void) {
    uint64_t r = rnd();
    switch (r & 7) {
    case 0: return (size_t)(r >> 32) % 8;
    case 1: case 2: case 3: return (size_t)(r >> 32) % 256;
    case 4: case 5: return (size_t)(r >> 32) % 4096;
    case 6: return (size_t)(r >> 32) % 65536;
    default: return (size_t)(r >> 32) % (1u << 18);
    }
}

static void test_stress(void) {
    int bad_data = 0, bad_check = 0, oom = 0;
    for (int iter = 0; iter < 200000; iter++) {
        int i = (int)(rnd() % SLOTS);
        unsigned char tag = (unsigned char)i;
        if (g_ptr[i]) {
            if (!verify(g_ptr[i], g_len[i], tag)) bad_data++;
            if (rnd() & 1) {
                free(g_ptr[i]);
                g_ptr[i] = NULL;
                g_len[i] = 0;
            } else {
                size_t n = pick_size();
                unsigned char *q = realloc(g_ptr[i], n);
                if (!q) { oom++; continue; }
                size_t keep = n < g_len[i] ? n : g_len[i];
                if (!verify(q, keep, tag)) bad_data++;
                fill(q, n, tag);
                g_ptr[i] = q;
                g_len[i] = n;
            }
        } else {
            size_t n = pick_size();
            unsigned char *p = (rnd() & 3) ? malloc(n) : calloc(1, n);
            if (!p) { oom++; continue; }
            fill(p, n, tag);
            g_ptr[i] = p;
            g_len[i] = n;
        }
        if ((iter % 10000) == 0 && nxc_heap_check() != 0) bad_check++;
    }
    CHECK_INT(bad_data, 0);
    CHECK_INT(bad_check, 0);
    CHECK_INT(oom, 0);

    nxc_heap_stats_t st;
    CHECK(nxc_heap_stats(&st) == 0);
    for (int i = 0; i < SLOTS; i++) {
        if (g_ptr[i] && !verify(g_ptr[i], g_len[i], (unsigned char)i)) bad_data++;
        free(g_ptr[i]);
        g_ptr[i] = NULL;
    }
    CHECK_INT(bad_data, 0);
    CHECK(nxc_heap_check() == 0);
    CHECK(nxc_heap_stats(&st) == 0);
    CHECK_INT(st.used_blocks, 0);
    /* everything freed: at most the kept tail stays mapped */
    CHECK(st.heap_bytes <= 256 * 1024);
    printf("  stress: peak heap %zu KB, now %zu KB\n", st.peak_heap_bytes / 1024, st.heap_bytes / 1024);
}

static void test_out_of_memory(void) {
    /* fill the 64 MB brk reserve with 1 MB blocks, then give it all back */
    static void *blocks[128];
    int n = 0;
    errno = 0;
    while (n < 128) {
        void *p = malloc(1 << 20);
        if (!p) break;
        memset(p, 0xEE, 1 << 20);
        blocks[n++] = p;
    }
    CHECK(n > 32 && n < 128);
    CHECK_INT(errno, ENOMEM);
    CHECK(nxc_heap_check() == 0);
    for (int i = 0; i < n; i++) free(blocks[i]);
    CHECK(nxc_heap_check() == 0);
    void *p = malloc(16 << 20);
    CHECK(p != NULL);
    free(p);
    nxc_heap_trim();
    nxc_heap_stats_t st;
    nxc_heap_stats(&st);
    CHECK(st.heap_bytes <= 256 * 1024);
}

static void test_stats_errors(void) {
    errno = 0;
    CHECK(nxc_heap_stats(NULL) == -1 && errno == EINVAL);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--double-free") == 0) {
        void *p = malloc(32);
        free(p);
        free(p);
        printf("double free was not caught\n");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--bad-free") == 0) {
        char *p = malloc(64);
        free(p + 16);
        printf("bad free was not caught\n");
        return 0;
    }
    test_basic();
    test_realloc();
    test_stress();
    test_out_of_memory();
    test_stats_errors();
    return t_finish("libc_malloc");
}
