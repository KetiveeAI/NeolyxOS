/*
 * nxlibc - the heap allocator (malloc, calloc, realloc, aligned_alloc, free).
 *
 * The heap is the process's program break region (sys brk; the kernel
 * reserves 64 MB per process). It is a sequence of blocks with no gaps:
 *
 *   heap_base                                            heap_end (= break)
 *   | hdr | payload ... | hdr | payload ... | ... | hdr | payload ... |
 *
 * Each block starts with a 16-byte header:
 *   size_flags   block size in bytes (header included, multiple of 16, >= 32)
 *                | BLK_INUSE
 *   prev_size16  size of the block before it / 16 (0 for the first block)
 *   check        a hash of the other fields and the block's own address
 *
 * The check lets free() and realloc() refuse pointers the allocator never
 * returned, blocks that were already freed, and headers that were
 * overwritten (a buffer overflow from the block before). Any of these ends
 * the process with a log line: a damaged heap is never used further.
 *
 * Free blocks keep doubly linked list pointers in their payload and live
 * in size-segregated bins: one bin per 16-byte size below 1 KB, then four
 * bins per power of two. A bitmap marks non-empty bins. Two free blocks are
 * never adjacent (they are merged on free), so a free block's neighbours
 * are always in use.
 *
 * The heap grows with brk in steps of at least 64 KB and gives memory back
 * when the last block is free and larger than 512 KB, keeping 128 KB.
 * Large blocks will move to anonymous mmap once the kernel offers it (ABI
 * flag FILEIO, stream S3).
 *
 * No user threads exist, so there is no locking.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <nx/sys.h>
#include <nx/libc.h>
#include "../internal.h"

#define ALIGN          16u
#define HDR_SIZE       16u
#define MIN_BLOCK      32u                   /* header + two list pointers */
#define BLK_INUSE      0x1ull
#define SIZE_MASK      (~(uint64_t)15)
#define GROW_MIN       (64u * 1024u)         /* smallest brk step */
#define TRIM_AT        (512u * 1024u)        /* trim when the last free block exceeds this */
#define TRIM_KEEP      (128u * 1024u)        /* bytes kept after a trim */
#define PAGE           4096u
#define MAX_REQUEST    ((size_t)1 << 40)     /* far above the 64 MB reserve; avoids overflow */
#define CHECK_SALT     0x4E584C4942433031ull /* "NXLIBC01" */

#define SMALL_BINS     64                    /* sizes 0..1008 by 16 (bins 0,1 unused) */
#define NBINS          (SMALL_BINS + 4 * 30) /* up to 2^40 */

typedef struct blk {
    uint64_t size_flags;
    uint32_t prev_size16;
    uint32_t check;
    /* free blocks only: */
    struct blk *next;
    struct blk *prev;
} blk_t;

_Static_assert(sizeof(blk_t) == MIN_BLOCK, "free block layout");

static uintptr_t g_base;        /* first block; 0 until the first allocation */
static uintptr_t g_end;         /* end of the last block == our break */
static uintptr_t g_peak;        /* highest g_end seen */
static blk_t    *g_bins[NBINS];
static uint64_t  g_binmap[(NBINS + 63) / 64];
static blk_t    *g_last;        /* block ending at g_end; NULL when the heap is empty */

/* ============ Headers ============ */

static inline uint64_t blk_size(const blk_t *b) { return b->size_flags & SIZE_MASK; }
static inline int blk_inuse(const blk_t *b) { return (int)(b->size_flags & BLK_INUSE); }

static uint32_t blk_hash(const blk_t *b) {
    uint64_t x = b->size_flags ^ ((uint64_t)b->prev_size16 << 32) ^ (uint64_t)(uintptr_t)b ^ CHECK_SALT;
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return (uint32_t)x;
}

static inline void blk_seal(blk_t *b) { b->check = blk_hash(b); }

/* Every change to a block's size goes through here, so the last-block
 * pointer is kept here too (callers update g_end first). */
static void blk_set(blk_t *b, uint64_t size, int inuse, uint64_t prev_size) {
    b->size_flags = size | (inuse ? BLK_INUSE : 0);
    b->prev_size16 = (uint32_t)(prev_size / ALIGN);
    blk_seal(b);
    if ((uintptr_t)b + size == g_end) g_last = b;
}

static inline blk_t *blk_next(const blk_t *b) {
    uintptr_t n = (uintptr_t)b + blk_size(b);
    return (n < g_end) ? (blk_t *)n : NULL;
}

static inline blk_t *blk_prev(const blk_t *b) {
    if (b->prev_size16 == 0) return NULL;
    return (blk_t *)((uintptr_t)b - (uintptr_t)b->prev_size16 * ALIGN);
}

static inline void *blk_payload(blk_t *b) { return (void *)((uintptr_t)b + HDR_SIZE); }

/* Is b a well-formed block header inside the heap? */
static int blk_valid(const blk_t *b) {
    uintptr_t a = (uintptr_t)b;
    if (g_base == 0 || a < g_base || a > g_end - MIN_BLOCK || (a & (ALIGN - 1))) return 0;
    if (b->check != blk_hash(b)) return 0;
    uint64_t size = blk_size(b);
    if (size < MIN_BLOCK || size > g_end - a) return 0;
    if (a == g_base) {
        if (b->prev_size16 != 0) return 0;
    } else {
        uint64_t prev = (uint64_t)b->prev_size16 * ALIGN;
        if (prev < MIN_BLOCK || prev > a - g_base) return 0;
    }
    return 1;
}

static _Noreturn void heap_corrupt(const char *what, const void *p) {
    __nxc_log_ptr(what, p);
    __nxc_fatal("heap corruption detected");
}

/* Tells the block after b (if any) that b's size is now size */
static void set_next_prev_size(blk_t *b) {
    blk_t *n = blk_next(b);
    if (n) {
        n->prev_size16 = (uint32_t)(blk_size(b) / ALIGN);
        blk_seal(n);
    }
}

/* ============ Bins ============ */

static unsigned bin_index(uint64_t size) {
    if (size < 1024) return (unsigned)(size / ALIGN);
    unsigned log2 = 63u - (unsigned)__builtin_clzll(size);     /* >= 10 */
    unsigned sub = (unsigned)((size >> (log2 - 2)) & 3u);      /* next two bits */
    unsigned idx = SMALL_BINS + (log2 - 10u) * 4u + sub;
    return (idx < NBINS) ? idx : NBINS - 1;
}

static void bin_insert(blk_t *b) {
    unsigned i = bin_index(blk_size(b));
    b->prev = NULL;
    b->next = g_bins[i];
    if (g_bins[i]) g_bins[i]->prev = b;
    g_bins[i] = b;
    g_binmap[i / 64] |= 1ull << (i % 64);
}

static void bin_remove(blk_t *b) {
    unsigned i = bin_index(blk_size(b));
    if (b->prev) {
        if (b->prev->next != b) heap_corrupt("free list link broken at", b);
        b->prev->next = b->next;
    } else {
        if (g_bins[i] != b) heap_corrupt("free list head broken at", b);
        g_bins[i] = b->next;
    }
    if (b->next) {
        if (b->next->prev != b) heap_corrupt("free list link broken at", b);
        b->next->prev = b->prev;
    }
    if (!g_bins[i]) g_binmap[i / 64] &= ~(1ull << (i % 64));
    b->next = b->prev = NULL;
}

/* Smallest free block of at least need bytes, removed from its bin */
static blk_t *bin_take(uint64_t need) {
    unsigned i = bin_index(need);
    for (;;) {
        /* next non-empty bin at or after i */
        unsigned w = i / 64;
        if (w >= sizeof(g_binmap) / sizeof(g_binmap[0])) return NULL;
        uint64_t bits = g_binmap[w] & (~0ull << (i % 64));
        while (!bits) {
            if (++w >= sizeof(g_binmap) / sizeof(g_binmap[0])) return NULL;
            bits = g_binmap[w];
        }
        i = w * 64 + (unsigned)__builtin_ctzll(bits);
        /* exact bins: any block fits; others: best fit within the bin */
        blk_t *best = NULL;
        for (blk_t *b = g_bins[i]; b; b = b->next) {
            if (!blk_valid(b) || blk_inuse(b)) heap_corrupt("bad free block", b);
            uint64_t s = blk_size(b);
            if (s >= need && (!best || s < blk_size(best))) {
                best = b;
                if (s == need || i < SMALL_BINS) break;
            }
        }
        if (best) {
            bin_remove(best);
            return best;
        }
        i++;
    }
}

/* ============ Splitting and merging ============ */

/* b is in use (or about to be) and has at least need bytes: cut off the
 * tail as a new free block if it is big enough to be one. */
static void split_tail(blk_t *b, uint64_t need) {
    uint64_t size = blk_size(b);
    if (size - need < MIN_BLOCK) return;
    blk_t *rest = (blk_t *)((uintptr_t)b + need);
    blk_set(b, need, blk_inuse(b), (uint64_t)b->prev_size16 * ALIGN);
    blk_set(rest, size - need, 0, need);
    set_next_prev_size(rest);
    /* The block after rest is in use (free neighbours never exist), except
     * when b was freshly merged; merge again to keep the rule. */
    blk_t *n = blk_next(rest);
    if (n && !blk_inuse(n)) {
        bin_remove(n);
        blk_set(rest, size - need + blk_size(n), 0, need);
        set_next_prev_size(rest);
    }
    bin_insert(rest);
}

/* Merges free block b with free neighbours; b must not be in a bin.
 * Returns the merged block (not in a bin). */
static blk_t *merge(blk_t *b) {
    blk_t *n = blk_next(b);
    if (n) {
        if (!blk_valid(n)) heap_corrupt("block after a freed block is damaged:", n);
        if (!blk_inuse(n)) {
            bin_remove(n);
            blk_set(b, blk_size(b) + blk_size(n), 0, (uint64_t)b->prev_size16 * ALIGN);
        }
    }
    blk_t *p = blk_prev(b);
    if (p) {
        if (!blk_valid(p)) heap_corrupt("block before a freed block is damaged:", p);
        if (blk_size(p) != (uint64_t)b->prev_size16 * ALIGN) heap_corrupt("size chain broken at", b);
        if (!blk_inuse(p)) {
            bin_remove(p);
            blk_set(p, blk_size(p) + blk_size(b), 0, (uint64_t)p->prev_size16 * ALIGN);
            b = p;
        }
    }
    set_next_prev_size(b);
    return b;
}

/* ============ Growing and trimming ============ */

static int heap_init(void) {
    if (g_base) return 0;
    long cur = nxsys_brk(NULL);
    if (cur <= 0) {
        __nxc_log("nxlibc: malloc: this process has no heap (brk query failed)\n");
        return -1;
    }
    uintptr_t base = ((uintptr_t)cur + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    g_base = g_end = g_peak = base;
    return 0;
}

/* Adds at least need bytes at the end of the heap as one free block (merged
 * with a free last block). Returns that block (not in a bin) or NULL. */
static blk_t *heap_grow(uint64_t need) {
    long cur = nxsys_brk(NULL);
    if (cur < 0 || (uintptr_t)cur < g_end) {
        __nxc_log("nxlibc: malloc: brk query failed\n");
        return NULL;
    }
    if ((uintptr_t)cur != g_end) {
        /* Someone else moved the break: two allocators share this heap */
        __nxc_fatal("the program break was moved by other code; link only one heap allocator");
    }
    uint64_t extra = need;
    blk_t *tail = g_last;
    if (tail && !blk_inuse(tail)) {
        extra = (need > blk_size(tail)) ? need - blk_size(tail) : 0;
    }
    if (extra < GROW_MIN) extra = GROW_MIN;
    uintptr_t want = g_end + extra;
    want = (want + PAGE - 1) & ~(uintptr_t)(PAGE - 1);
    if (want < g_end) return NULL;
    long got = nxsys_brk((void *)want);
    if (got < 0 || (uintptr_t)got < want) {
        /* Try exactly what is needed, without the 64 KB step */
        uint64_t exact = need;
        if (tail && !blk_inuse(tail)) exact = need - blk_size(tail);
        want = (g_end + exact + PAGE - 1) & ~(uintptr_t)(PAGE - 1);
        got = nxsys_brk((void *)want);
        if (got < 0 || (uintptr_t)got < want) return NULL;
    }
    uintptr_t new_end = (uintptr_t)got & ~(uintptr_t)(ALIGN - 1);
    blk_t *b = (blk_t *)g_end;
    uint64_t prev = tail ? blk_size(tail) : 0;
    uint64_t size = new_end - g_end;
    g_end = new_end;
    if (g_end > g_peak) g_peak = g_end;
    if (tail && !blk_inuse(tail)) {
        bin_remove(tail);
        blk_set(tail, blk_size(tail) + size, 0, (uint64_t)tail->prev_size16 * ALIGN);
        b = tail;
    } else {
        blk_set(b, size, 0, prev);
    }
    return b;
}

/* Gives memory at the end back to the kernel when the last block is a big
 * free block. b is that block, not in a bin. Returns bytes released. */
static size_t heap_trim_block(blk_t *b) {
    uint64_t size = blk_size(b);
    if ((uintptr_t)b + size != g_end || size <= TRIM_KEEP) return 0;
    uintptr_t keep_end = ((uintptr_t)b + TRIM_KEEP + PAGE - 1) & ~(uintptr_t)(PAGE - 1);
    if (keep_end >= g_end) return 0;
    long got = nxsys_brk((void *)keep_end);
    if (got < 0 || (uintptr_t)got != keep_end) {
        __nxc_log("nxlibc: malloc: the kernel refused to shrink the heap; keeping the memory\n");
        return 0;
    }
    size_t released = g_end - keep_end;
    g_end = keep_end;
    blk_set(b, keep_end - (uintptr_t)b, 0, (uint64_t)b->prev_size16 * ALIGN);
    return released;
}

/* ============ Public API ============ */

static uint64_t request_size(size_t n) {
    uint64_t need = ((uint64_t)n + HDR_SIZE + ALIGN - 1) & SIZE_MASK;
    return (need < MIN_BLOCK) ? MIN_BLOCK : need;
}

static blk_t *alloc_block(size_t n) {
    if (n > MAX_REQUEST) {
        errno = ENOMEM;
        return NULL;
    }
    if (heap_init() != 0) {
        errno = ENOMEM;
        return NULL;
    }
    uint64_t need = request_size(n);
    blk_t *b = bin_take(need);
    if (!b) {
        b = heap_grow(need);
        if (!b) {
            errno = ENOMEM;
            return NULL;
        }
    }
    blk_set(b, blk_size(b), 1, (uint64_t)b->prev_size16 * ALIGN);
    split_tail(b, need);
    return b;
}

void *malloc(size_t n) {
    blk_t *b = alloc_block(n);
    return b ? blk_payload(b) : NULL;
}

/* Logs "nxlibc: <fn>: <what> 0x<p>" as one line and ends the process */
static _Noreturn void bad_pointer(const char *fn, const char *what, const void *p, const char *fatal) {
    char msg[96];
    size_t n = __nxc_strlcpy(msg, fn, sizeof msg);
    if (n < sizeof msg) __nxc_strlcpy(msg + n, what, sizeof msg - n);
    __nxc_log_ptr(msg, p);
    __nxc_fatal(fatal);
}

/* The block of a pointer handed to free/realloc, after every check */
static blk_t *owned_block(void *p, const char *fn) {
    uintptr_t a = (uintptr_t)p;
    blk_t *b = (blk_t *)(a - HDR_SIZE);
    if ((a & (ALIGN - 1)) || g_base == 0 || a < g_base + HDR_SIZE || a > g_end || !blk_valid(b)) {
        bad_pointer(fn, ": pointer not from malloc (or its header was overwritten):", p,
                    "invalid pointer passed to the allocator");
    }
    if (!blk_inuse(b)) {
        bad_pointer(fn, ": block already freed:", p, "double free");
    }
    return b;
}

static void release_block(blk_t *b) {
    blk_set(b, blk_size(b), 0, (uint64_t)b->prev_size16 * ALIGN);
    b = merge(b);
    if (b == g_last && blk_size(b) > TRIM_AT) heap_trim_block(b);
    bin_insert(b);
}

void free(void *p) {
    if (!p) return;
    release_block(owned_block(p, "free"));
}

void *calloc(size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = count * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/* realloc(p, 0) returns a valid minimum-size block (and keeps the data's
 * first bytes): it never frees p behind the caller's back. */
void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    blk_t *b = owned_block(p, "realloc");
    if (n > MAX_REQUEST) {
        errno = ENOMEM;
        return NULL;
    }
    uint64_t need = request_size(n);
    uint64_t size = blk_size(b);

    if (need <= size) {
        split_tail(b, need);
        return p;
    }

    /* Grow into a free next block */
    blk_t *nx = blk_next(b);
    if (nx) {
        if (!blk_valid(nx)) heap_corrupt("block after a reallocated block is damaged:", nx);
        if (!blk_inuse(nx) && size + blk_size(nx) >= need) {
            bin_remove(nx);
            blk_set(b, size + blk_size(nx), 1, (uint64_t)b->prev_size16 * ALIGN);
            set_next_prev_size(b);
            split_tail(b, need);
            return p;
        }
    } else {
        /* Last block: extend the heap in place */
        uint64_t extra = need - size;
        uintptr_t want = (g_end + extra + PAGE - 1) & ~(uintptr_t)(PAGE - 1);
        long cur = nxsys_brk(NULL);
        if (cur >= 0 && (uintptr_t)cur == g_end && want > g_end) {
            long got = nxsys_brk((void *)want);
            if (got >= 0 && (uintptr_t)got >= want) {
                uintptr_t new_end = (uintptr_t)got & ~(uintptr_t)(ALIGN - 1);
                uint64_t grown = size + (new_end - g_end);
                g_end = new_end;
                if (g_end > g_peak) g_peak = g_end;
                blk_set(b, grown, 1, (uint64_t)b->prev_size16 * ALIGN);
                split_tail(b, need);
                return p;
            }
        }
    }

    /* Move */
    blk_t *nb = alloc_block(n);
    if (!nb) return NULL;   /* errno = ENOMEM, p stays valid */
    memcpy(blk_payload(nb), p, size - HDR_SIZE);
    release_block(b);
    return blk_payload(nb);
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || alignment > ((size_t)1 << 30)) {
        errno = EINVAL;
        return NULL;
    }
    if (alignment <= ALIGN) return malloc(size);
    if (size > MAX_REQUEST) {
        errno = ENOMEM;
        return NULL;
    }
    /* Room to move the start forward by up to alignment + one block */
    blk_t *b = alloc_block(size + alignment + MIN_BLOCK);
    if (!b) return NULL;
    uintptr_t pay = (uintptr_t)blk_payload(b);
    uintptr_t aligned = (pay + alignment - 1) & ~(uintptr_t)(alignment - 1);
    while (aligned != pay && aligned - pay < MIN_BLOCK) aligned += alignment;
    if (aligned != pay) {
        /* Cut the leading part off as its own block and free it */
        uint64_t lead = aligned - pay;
        uint64_t total = blk_size(b);
        uint64_t prev = (uint64_t)b->prev_size16 * ALIGN;
        blk_t *a = (blk_t *)(aligned - HDR_SIZE);
        blk_set(b, lead, 1, prev);
        blk_set(a, total - lead, 1, lead);
        set_next_prev_size(a);
        release_block(b);
        b = a;
    }
    split_tail(b, request_size(size));
    return blk_payload(b);
}

/* ============ Extensions (<nx/libc.h>) ============ */

int nxc_heap_stats(nxc_heap_stats_t *out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!g_base) return 0;
    out->heap_bytes = g_end - g_base;
    out->peak_heap_bytes = g_peak - g_base;
    for (uintptr_t a = g_base; a < g_end;) {
        blk_t *b = (blk_t *)a;
        if (!blk_valid(b)) break;
        uint64_t s = blk_size(b);
        if (blk_inuse(b)) {
            out->used_bytes += s;
            out->used_blocks++;
        } else {
            out->free_bytes += s;
            out->free_blocks++;
            if (s > out->largest_free) out->largest_free = s;
        }
        a += s;
    }
    return 0;
}

int nxc_heap_check(void) {
    if (!g_base) return 0;
    size_t free_walk = 0;
    uint64_t prev_size = 0;
    int prev_free = 0;
    blk_t *last = NULL;
    for (uintptr_t a = g_base; a < g_end;) {
        blk_t *b = (blk_t *)a;
        if (!blk_valid(b)) {
            __nxc_log_ptr("heap check: damaged header at", b);
            return -1;
        }
        if ((uint64_t)b->prev_size16 * ALIGN != prev_size) {
            __nxc_log_ptr("heap check: wrong previous size at", b);
            return -1;
        }
        if (!blk_inuse(b)) {
            if (prev_free) {
                __nxc_log_ptr("heap check: two free blocks next to each other at", b);
                return -1;
            }
            free_walk++;
        }
        prev_free = !blk_inuse(b);
        prev_size = blk_size(b);
        last = b;
        a += blk_size(b);
    }
    if (last != g_last) {
        __nxc_log_ptr("heap check: last-block pointer is wrong:", g_last);
        return -1;
    }
    size_t free_bins = 0;
    for (unsigned i = 0; i < NBINS; i++) {
        int marked = (int)((g_binmap[i / 64] >> (i % 64)) & 1);
        if (marked != (g_bins[i] != NULL)) {
            __nxc_log("nxlibc: heap check: bin bitmap disagrees with bin list\n");
            return -1;
        }
        blk_t *prev = NULL;
        for (blk_t *b = g_bins[i]; b; b = b->next) {
            if (!blk_valid(b) || blk_inuse(b) || bin_index(blk_size(b)) != i || b->prev != prev) {
                __nxc_log_ptr("heap check: bad free-list entry", b);
                return -1;
            }
            if (++free_bins > free_walk) {
                __nxc_log("nxlibc: heap check: free list longer than the heap (loop?)\n");
                return -1;
            }
            prev = b;
        }
    }
    if (free_bins != free_walk) {
        __nxc_log("nxlibc: heap check: a free block is missing from the free lists\n");
        return -1;
    }
    return 0;
}

size_t nxc_heap_trim(void) {
    if (!g_last || blk_inuse(g_last)) return 0;
    blk_t *b = g_last;
    bin_remove(b);
    size_t released = heap_trim_block(b);
    bin_insert(b);
    return released;
}
