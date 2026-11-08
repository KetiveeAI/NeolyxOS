/*
 * nxlibc - qsort and bsearch.
 *
 * qsort is an introsort: quicksort with median-of-three pivots, insertion
 * sort for short ranges, and heapsort when the recursion gets too deep, so
 * it is O(n log n) for every input, including adversarial ones. It uses no
 * heap memory and a bounded stack. It is not stable (ISO C does not ask).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdint.h>

typedef int (*cmp_fn)(const void *, const void *);

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size) {
    if (a == b) return;
    while (size >= 8) {
        uint64_t t;
        __builtin_memcpy(&t, a, 8);
        __builtin_memcpy(a, b, 8);
        __builtin_memcpy(b, &t, 8);
        a += 8;
        b += 8;
        size -= 8;
    }
    while (size--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

#define ELEM(i) (base + (i) * size)

static void insertion_sort(unsigned char *base, size_t n, size_t size, cmp_fn cmp) {
    for (size_t i = 1; i < n; i++) {
        for (size_t j = i; j > 0 && cmp(ELEM(j - 1), ELEM(j)) > 0; j--) {
            swap_bytes(ELEM(j - 1), ELEM(j), size);
        }
    }
}

static void sift_down(unsigned char *base, size_t root, size_t n, size_t size, cmp_fn cmp) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n) return;
        if (child + 1 < n && cmp(ELEM(child), ELEM(child + 1)) < 0) child++;
        if (cmp(ELEM(root), ELEM(child)) >= 0) return;
        swap_bytes(ELEM(root), ELEM(child), size);
        root = child;
    }
}

static void heap_sort(unsigned char *base, size_t n, size_t size, cmp_fn cmp) {
    for (size_t i = n / 2; i > 0; i--) sift_down(base, i - 1, n, size, cmp);
    for (size_t end = n - 1; end > 0; end--) {
        swap_bytes(ELEM(0), ELEM(end), size);
        sift_down(base, 0, end, size, cmp);
    }
}

static void intro_sort(unsigned char *base, size_t n, size_t size, cmp_fn cmp, int depth) {
    while (n > 16) {
        if (depth-- == 0) {
            heap_sort(base, n, size, cmp);
            return;
        }
        /* median of three to the front */
        size_t mid = n / 2;
        if (cmp(ELEM(mid), ELEM(0)) < 0) swap_bytes(ELEM(mid), ELEM(0), size);
        if (cmp(ELEM(n - 1), ELEM(0)) < 0) swap_bytes(ELEM(n - 1), ELEM(0), size);
        if (cmp(ELEM(n - 1), ELEM(mid)) < 0) swap_bytes(ELEM(n - 1), ELEM(mid), size);
        swap_bytes(ELEM(0), ELEM(mid), size);       /* pivot at 0 */

        /* Hoare partition around ELEM(0) */
        size_t i = 0, j = n;
        for (;;) {
            do { i++; } while (i < n && cmp(ELEM(i), ELEM(0)) < 0);
            do { j--; } while (cmp(ELEM(j), ELEM(0)) > 0);
            if (i >= j) break;
            swap_bytes(ELEM(i), ELEM(j), size);
        }
        swap_bytes(ELEM(0), ELEM(j), size);         /* pivot to its place */

        /* recurse into the smaller side, loop on the larger */
        size_t left = j, right = n - j - 1;
        if (left < right) {
            intro_sort(base, left, size, cmp, depth);
            base = ELEM(j + 1);
            n = right;
        } else {
            intro_sort(ELEM(j + 1), right, size, cmp, depth);
            n = left;
        }
    }
    insertion_sort(base, n, size, cmp);
}

void qsort(void *base, size_t count, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || !compar || count < 2 || size == 0) return;
    int depth = 0;
    for (size_t n = count; n > 1; n >>= 1) depth += 2;
    intro_sort((unsigned char *)base, count, size, compar, depth);
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compar)(const void *, const void *)) {
    if (!base || !compar || size == 0) return NULL;
    const unsigned char *lo = (const unsigned char *)base;
    while (count > 0) {
        size_t half = count / 2;
        const unsigned char *mid = lo + half * size;
        int c = compar(key, mid);
        if (c == 0) return (void *)mid;
        if (c > 0) {
            lo = mid + size;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return NULL;
}
