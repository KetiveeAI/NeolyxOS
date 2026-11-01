/*
 * nx_malloc.c - heap allocator for NeolyxOS userspace (desktop, REOX apps)
 *
 * A boundary-tag allocator over the process heap (brk). Freed memory is
 * reused: blocks are coalesced with free neighbours and kept in size-class
 * free lists. It replaces a bump allocator whose free() did nothing and
 * whose realloc() did not copy, which made the desktop grow by ~4 MiB per
 * app drawer open until its 64 MiB heap reserve ran out.
 *
 * Layout: the heap is one run of blocks from g_heap.base to g_heap.top.
 * Every block starts with a 16-byte header (so payloads stay 16-byte
 * aligned); free blocks also hold their list links in the payload.
 *
 *   used block:  [magic USED | prev_size | size] [payload ...]
 *   free block:  [magic FREE | prev_size | size] [next | prev] [...]
 *
 * prev_size lets free() merge with the block before; size reaches the block
 * after. When the free block at the top of the heap reaches
 * NXM_TRIM_THRESHOLD, free() gives it back to the system with brk (the
 * kernel unmaps the pages, K2), keeping NXM_GROW_MIN of slack so a
 * malloc/free cycle at the top does not cost a syscall each time.
 * nx_malloc_trim() returns everything above the last used block on demand
 * (for example on a memory pressure warning).
 *
 * Errors are never silent: an invalid or double free is refused and
 * logged (rate-limited), and running out of heap is logged once per burst.
 * Single-threaded by design: NeolyxOS processes have one thread.
 * Blocks stay below 4 GiB (prev_size is 32 bits): requests are capped at
 * 2 GiB and the kernel's heap reserve is 64 MiB.
 *
 * Build flag NX_MALLOC_POISON fills freed payloads with 0xDD so a
 * use-after-free shows up as garbage instead of silently working.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "../include/nxsyscall.h"

/* ============ Layout ============ */

typedef struct nxm_block {
    uint32_t magic;             /* NXM_USED or NXM_FREE */
    uint32_t prev_size;         /* size of the block before (0 for the first) */
    uint64_t size;              /* whole block in bytes, header included, multiple of 16 */
} nxm_block_t;

typedef struct nxm_links {      /* payload of a free block */
    nxm_block_t *next;
    nxm_block_t *prev;
} nxm_links_t;

#define NXM_USED        0x4E584D55u     /* "NXMU" */
#define NXM_FREE        0x4E584D46u     /* "NXMF" */
#define NXM_STATE_MAGIC 0x4E584D414C4C4F43ULL   /* "NXMALLOC" */

#define NXM_HDR         ((uint64_t)sizeof(nxm_block_t))           /* 16 */
#define NXM_MIN_BLOCK   (NXM_HDR + (uint64_t)sizeof(nxm_links_t)) /* 32 */
#define NXM_ALIGN       16ULL
#define NXM_GROW_MIN    (256ULL * 1024ULL)      /* brk step, limits syscalls */
#define NXM_TRIM_THRESHOLD (1024ULL * 1024ULL)  /* free top + slack that triggers a trim */
#define NXM_MAX_REQUEST (1ULL << 31)            /* 2 GiB; keeps every size within prev_size's 32 bits */
#define NXM_BINS        32
#define NXM_LOG_MAX     16                      /* error lines per process */

_Static_assert(sizeof(nxm_block_t) == 16, "header must keep payloads 16-byte aligned");
_Static_assert(sizeof(nxm_links_t) == 16, "links must fit the minimum payload");

typedef struct {
    uint64_t magic;             /* NXM_STATE_MAGIC once initialised */
    uint8_t *base;              /* first block */
    uint8_t *top;               /* end of the last block */
    uint8_t *mapped;            /* current break (top <= mapped) */
    nxm_block_t *last;          /* block that ends at top, or NULL */
    nxm_block_t *bins[NXM_BINS];
    uint64_t in_use;            /* bytes in used blocks, headers included */
    uint32_t errors_logged;
    int out_of_memory;          /* set while requests fail, for one log line */
} nxm_state_t;

static nxm_state_t g_heap;

/* ============ Logging ============ */

static void nxm_log(const char *what, uint64_t value) {
    if (g_heap.errors_logged >= NXM_LOG_MAX) return;
    g_heap.errors_logged++;

    char line[128];
    int n = 0;
    const char *p = "[MALLOC] ";
    while (*p && n < 100) line[n++] = *p++;
    while (*what && n < 100) line[n++] = *what++;
    line[n++] = ' ';
    line[n++] = '0';
    line[n++] = 'x';
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((value >> i) & 0xF);
        line[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    }
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
    if (g_heap.errors_logged == NXM_LOG_MAX) {
        nx_debug_print("[MALLOC] further allocator errors are not logged\n");
    }
}

/* ============ Helpers ============ */

static void nxm_fill(void *dst, uint8_t v, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = v;
}

static void nxm_copy(void *dst, const void *src, uint64_t n) {
    uint64_t *d8 = (uint64_t *)dst;
    const uint64_t *s8 = (const uint64_t *)src;
    while (n >= 8) { *d8++ = *s8++; n -= 8; }   /* both 16-byte aligned */
    uint8_t *d = (uint8_t *)d8;
    const uint8_t *s = (const uint8_t *)s8;
    while (n--) *d++ = *s++;
}

static inline nxm_links_t *links_of(nxm_block_t *b) {
    return (nxm_links_t *)((uint8_t *)b + NXM_HDR);
}

static inline nxm_block_t *next_of(nxm_block_t *b) {
    uint8_t *n = (uint8_t *)b + b->size;
    return (n < g_heap.top) ? (nxm_block_t *)n : (nxm_block_t *)0;
}

static inline nxm_block_t *prev_of(nxm_block_t *b) {
    if ((uint8_t *)b == g_heap.base || b->prev_size == 0) return (nxm_block_t *)0;
    return (nxm_block_t *)((uint8_t *)b - b->prev_size);
}

/* Bin k holds free blocks of size [32 << k, 64 << k); the last bin is open-ended */
static int bin_of(uint64_t size) {
    int k = 0;
    uint64_t s = size >> 6;     /* 32..63 -> 0 */
    while (s && k < NXM_BINS - 1) { s >>= 1; k++; }
    return k;
}

static void bin_insert(nxm_block_t *b) {
    int k = bin_of(b->size);
    nxm_links_t *l = links_of(b);
    l->prev = (nxm_block_t *)0;
    l->next = g_heap.bins[k];
    if (l->next) links_of(l->next)->prev = b;
    g_heap.bins[k] = b;
}

static void bin_remove(nxm_block_t *b) {
    nxm_links_t *l = links_of(b);
    if (l->prev) links_of(l->prev)->next = l->next;
    else g_heap.bins[bin_of(b->size)] = l->next;
    if (l->next) links_of(l->next)->prev = l->prev;
    l->next = l->prev = (nxm_block_t *)0;
}

/* Tell the block after b (if any) b's current size */
static void fix_next_prev_size(nxm_block_t *b) {
    nxm_block_t *n = next_of(b);
    if (n) n->prev_size = (uint32_t)b->size;
    else g_heap.last = b;
}

/* ============ Heap growth ============ */

static int nxm_init(void) {
    if (g_heap.magic == NXM_STATE_MAGIC) return 0;

    nxm_fill(&g_heap, 0, sizeof(g_heap));
    int64_t base = nx_brk(0);
    if (base <= 0) {
        nx_debug_print("[MALLOC] no heap: brk(0) failed\n");
        return -1;
    }
    uint64_t start = ((uint64_t)base + NXM_ALIGN - 1) & ~(NXM_ALIGN - 1);
    g_heap.base = (uint8_t *)start;
    g_heap.top = (uint8_t *)start;
    g_heap.mapped = (uint8_t *)base;
    g_heap.magic = NXM_STATE_MAGIC;
    return 0;
}

/* Make sure [.., end) is mapped. @return 0, or -1 when the reserve is used up */
static int nxm_map_to(uint8_t *end) {
    if (end <= g_heap.mapped) return 0;
    uint64_t want = (uint64_t)(end - g_heap.mapped);
    if (want < NXM_GROW_MIN) want = NXM_GROW_MIN;
    int64_t got = nx_brk((uint64_t)g_heap.mapped + want);
    if (got < 0 || (uint64_t)got < (uint64_t)end) {
        /* The step may not fit near the reserve's end: try the exact size */
        got = nx_brk((uint64_t)end);
        if (got < 0 || (uint64_t)got < (uint64_t)end) return -1;
    }
    g_heap.mapped = (uint8_t *)got;
    return 0;
}

/*
 * Get a free block of at least need bytes at the top of the heap, growing it.
 * The block is returned in use (not in any bin). @return block or NULL
 */
static nxm_block_t *grow_top(uint64_t need) {
    nxm_block_t *b;
    if (g_heap.last && g_heap.last->magic == NXM_FREE) {
        /* Extend the free block that already ends at the top */
        b = g_heap.last;
        if (nxm_map_to((uint8_t *)b + need) != 0) return (nxm_block_t *)0;
        bin_remove(b);
        g_heap.top = (uint8_t *)b + need;
        b->size = need;
    } else {
        b = (nxm_block_t *)g_heap.top;
        if (nxm_map_to(g_heap.top + need) != 0) return (nxm_block_t *)0;
        b->prev_size = g_heap.last ? (uint32_t)g_heap.last->size : 0;
        b->size = need;
        g_heap.top += need;
    }
    b->magic = NXM_USED;
    g_heap.last = b;
    return b;
}

/* Split a used block so it is exactly need bytes, freeing the rest */
static void split_tail(nxm_block_t *b, uint64_t need) {
    if (b->size < need + NXM_MIN_BLOCK) return;
    nxm_block_t *rest = (nxm_block_t *)((uint8_t *)b + need);
    rest->size = b->size - need;
    rest->prev_size = (uint32_t)need;
    rest->magic = NXM_FREE;
    b->size = need;
    fix_next_prev_size(rest);
    /* Merge the tail with a free block after it */
    nxm_block_t *n = next_of(rest);
    if (n && n->magic == NXM_FREE) {
        bin_remove(n);
        rest->size += n->size;
        fix_next_prev_size(rest);
    }
    bin_insert(rest);
}

/* Bytes a request needs, header included. @return 0 if too large */
static uint64_t block_need(uint64_t size) {
    if (size == 0 || size > NXM_MAX_REQUEST) return 0;
    uint64_t need = (size + NXM_HDR + NXM_ALIGN - 1) & ~(NXM_ALIGN - 1);
    return (need < NXM_MIN_BLOCK) ? NXM_MIN_BLOCK : need;
}

/* Validate a pointer handed to free/realloc. @return its block, or NULL */
static nxm_block_t *block_of(void *ptr, const char *who) {
    uint8_t *p = (uint8_t *)ptr;
    if (g_heap.magic != NXM_STATE_MAGIC || p < g_heap.base + NXM_HDR || p >= g_heap.top ||
        ((uint64_t)p & (NXM_ALIGN - 1)) != 0) {
        nxm_log(who, (uint64_t)p);
        return (nxm_block_t *)0;
    }
    nxm_block_t *b = (nxm_block_t *)(p - NXM_HDR);
    if (b->magic == NXM_FREE) {
        nxm_log("double free refused:", (uint64_t)p);
        return (nxm_block_t *)0;
    }
    if (b->magic != NXM_USED || b->size < NXM_MIN_BLOCK || (b->size & (NXM_ALIGN - 1)) != 0 ||
        b->size > (uint64_t)(g_heap.top - (uint8_t *)b)) {
        nxm_log("refused: no valid block header at", (uint64_t)p);
        return (nxm_block_t *)0;
    }
    return b;
}

/* ============ Public API ============ */

static uint64_t nxm_trim(uint64_t slack);

void *nx_malloc(uint64_t size) {
    uint64_t need = block_need(size);
    if (need == 0) return (void *)0;
    if (nxm_init() != 0) return (void *)0;

    /* Smallest fitting free block: first fit in its own bin, then any
     * block of a larger bin (all of which fit) */
    nxm_block_t *b = (nxm_block_t *)0;
    int k = bin_of(need);
    for (nxm_block_t *c = g_heap.bins[k]; c; c = links_of(c)->next) {
        if (c->size >= need) { b = c; break; }
    }
    for (int j = k + 1; !b && j < NXM_BINS; j++) b = g_heap.bins[j];

    if (b) {
        bin_remove(b);
        b->magic = NXM_USED;
        split_tail(b, need);
    } else {
        b = grow_top(need);
        if (!b) {
            if (!g_heap.out_of_memory) nxm_log("out of heap, request bytes:", size);
            g_heap.out_of_memory = 1;
            return (void *)0;
        }
    }
    g_heap.out_of_memory = 0;
    g_heap.in_use += b->size;
    return (uint8_t *)b + NXM_HDR;
}

void nx_mfree(void *ptr) {
    if (!ptr) return;
    nxm_block_t *b = block_of(ptr, "invalid free refused:");
    if (!b) return;

    g_heap.in_use -= b->size;
    b->magic = NXM_FREE;
#ifdef NX_MALLOC_POISON
    if (b->size > NXM_MIN_BLOCK) {
        nxm_fill((uint8_t *)b + NXM_MIN_BLOCK, 0xDD, b->size - NXM_MIN_BLOCK);
    }
#endif

    nxm_block_t *n = next_of(b);
    if (n && n->magic == NXM_FREE) {
        bin_remove(n);
        b->size += n->size;
    }
    nxm_block_t *p = prev_of(b);
    if (p && p->magic == NXM_FREE) {
        bin_remove(p);
        p->size += b->size;
        b = p;
    }
    fix_next_prev_size(b);
    bin_insert(b);

    /* A large free run at the top goes back to the system */
    if (b == g_heap.last && (uint64_t)(g_heap.mapped - (uint8_t *)b) >= NXM_TRIM_THRESHOLD + NXM_GROW_MIN) {
        nxm_trim(NXM_GROW_MIN);
    }
}

/* ============ Giving memory back ============ */

/*
 * Drop the free block at the top (if any) and lower the break to the
 * page above the last used block plus slack. @return bytes released
 */
static uint64_t nxm_trim(uint64_t slack) {
    if (g_heap.magic != NXM_STATE_MAGIC) return 0;
    nxm_block_t *b = g_heap.last;
    if (b && b->magic == NXM_FREE) {
        nxm_block_t *p = prev_of(b);
        bin_remove(b);
        g_heap.top = (uint8_t *)b;
        g_heap.last = p;
    }
    uint64_t keep = ((uint64_t)g_heap.top + slack + 4095ULL) & ~4095ULL;
    if (keep >= (uint64_t)g_heap.mapped) return 0;

    int64_t got = nx_brk(keep);
    if (got < 0 || (uint64_t)got > (uint64_t)g_heap.mapped || (uint64_t)got < (uint64_t)g_heap.top) {
        nxm_log("brk refused a heap trim to", keep);
        return 0;
    }
    uint64_t released = (uint64_t)(g_heap.mapped - (uint8_t *)got);
    g_heap.mapped = (uint8_t *)got;
    return released;
}

uint64_t nx_malloc_trim(void) {
    return nxm_trim(0);
}

void *nx_calloc(uint64_t count, uint64_t size) {
    if (count != 0 && size > NXM_MAX_REQUEST / count) return (void *)0;   /* overflow */
    uint64_t total = count * size;
    void *p = nx_malloc(total);
    if (p) nxm_fill(p, 0, total);
    return p;
}

void *nx_realloc(void *ptr, uint64_t size) {
    if (!ptr) return nx_malloc(size);
    if (size == 0) {
        nx_mfree(ptr);
        return (void *)0;
    }
    nxm_block_t *b = block_of(ptr, "invalid realloc refused:");
    if (!b) return (void *)0;
    uint64_t need = block_need(size);
    if (need == 0) return (void *)0;

    if (b->size >= need) {                          /* shrink in place */
        g_heap.in_use -= b->size;
        split_tail(b, need);
        g_heap.in_use += b->size;
        return ptr;
    }

    nxm_block_t *n = next_of(b);
    if (n && n->magic == NXM_FREE && b->size + n->size >= need) {   /* absorb the next block */
        g_heap.in_use -= b->size;
        bin_remove(n);
        b->size += n->size;
        fix_next_prev_size(b);
        split_tail(b, need);
        g_heap.in_use += b->size;
        return ptr;
    }

    if (b == g_heap.last && nxm_map_to((uint8_t *)b + need) == 0) {  /* grow at the top */
        g_heap.in_use += need - b->size;
        g_heap.top = (uint8_t *)b + need;
        b->size = need;
        return ptr;
    }

    void *q = nx_malloc(size);
    if (!q) return (void *)0;                       /* the old block stays valid */
    nxm_copy(q, ptr, b->size - NXM_HDR);
    nx_mfree(ptr);
    return q;
}

void nx_malloc_usage(uint64_t *in_use, uint64_t *heap_size) {
    if (in_use) *in_use = (g_heap.magic == NXM_STATE_MAGIC) ? g_heap.in_use : 0;
    if (heap_size) {
        *heap_size = (g_heap.magic == NXM_STATE_MAGIC) ? (uint64_t)(g_heap.top - g_heap.base) : 0;
    }
}
