/* nx_malloc.c - Simple heap allocator for userspace
 * 
 * Uses a header at the start of the heap region to store state, avoiding BSS issues.
 * BSS may not be properly zeroed in freestanding environments.
 * 
 * DESIGN: The heap is pre-mapped by kernel at boot (64MB reserve).
 * We just track allocation within that pre-mapped region.
 */

#include "../include/nxsyscall.h"

/* Heap state stored at start of heap region - not in BSS */
typedef struct {
    uint64_t magic;       /* 0xHEAPHEAP to verify initialized */
    uint64_t heap_start;  /* Start of usable heap (after this header) */
    uint64_t heap_end;    /* Current end of allocated heap */
} heap_header_t;

#define HEAP_MAGIC 0x4845415048454150ULL  /* "HEAPHEAP" in ASCII */

/* Cached pointer to heap - we verify by reading the heap header's magic */
/* MUST be volatile to prevent compiler from caching in register across calls */
static volatile uint64_t g_heap_base = 0;
static volatile int g_heap_initializing = 0;  /* Guard against re-entry */

/* Get heap header - queries kernel for base if not cached */
static heap_header_t* get_heap_header(void) {
    /* Already initialized? */
    if (g_heap_base != 0) {
        heap_header_t* hdr = (heap_header_t*)g_heap_base;
        if (hdr->magic == HEAP_MAGIC) {
            return hdr;
        }
    }
    
    /* Guard against re-entry during init */
    if (g_heap_initializing) {
        return 0;
    }
    g_heap_initializing = 1;
    
    /* Query kernel for heap start - the heap is ALREADY mapped by kernel at boot (64MB).
     * We don't need to grow via brk - just use the memory directly.
     * This is the dynamic approach used by production OSes. */
    int64_t base = nx_brk(0);
    if (base <= 0) {
        g_heap_initializing = 0;
        return 0;
    }
    
    /* Calculate header end - heap is pre-mapped so we can use it directly */
    uint64_t hdr_end = ((uint64_t)base + sizeof(heap_header_t) + 15) & ~15ULL;
    
    /* Initialize header (memory is already mapped, no brk grow needed) */
    heap_header_t* hdr = (heap_header_t*)base;
    hdr->magic = HEAP_MAGIC;
    hdr->heap_start = hdr_end;
    hdr->heap_end = hdr_end;
    
    g_heap_base = (uint64_t)base;
    g_heap_initializing = 0;
    
    /* NOTE: Do NOT call nx_debug_print here - the syscall clobbers RDX
     * which the compiler uses to hold 'hdr' after inlining. */
    return hdr;
}

void* nx_malloc(uint64_t size) {
    /* Get heap header */
    volatile heap_header_t* hdr = (volatile heap_header_t*)get_heap_header();
    if (!hdr) return 0;
    if (size == 0) return 0;
    
    /* Align to 16 bytes */
    size = (size + 15) & ~15;
    
    /* Read from heap header */
    uint64_t heap_end = hdr->heap_end;
    uint64_t new_end = heap_end + size;
    uint64_t heap_max = hdr->heap_start + (64 * 1024 * 1024);
    
    if (new_end > heap_max) return 0;
    
    /* Update heap end */
    hdr->heap_end = new_end;
    
    return (void*)heap_end;
}

void nx_mfree(void *ptr) {
    (void)ptr;  /* Simple allocator - no individual frees */
}
