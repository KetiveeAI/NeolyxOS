/*
 * NeolyxOS Kernel Heap Allocator Implementation
 * 
 * First-fit free list allocator with coalescing.
 * Production-quality, clean code.
 * 
 * Copyright (c) 2025 KetiveeAI
 * ketivee
 */

#include "mm/kheap.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Block Header ============ */

#define HEAP_MAGIC 0x4B48 /* "KH" - Kernel Heap */

typedef struct heap_block {
    uint16_t magic;             /* Magic number for validation */
    uint16_t flags;             /* Flags (bit 0 = used) */
    uint32_t size;              /* Size of data area (not including header) */
    struct heap_block *next;    /* Next block in free list (if free) */
    struct heap_block *prev;    /* Previous block in free list (if free) */
} __attribute__((packed)) heap_block_t;

#define BLOCK_USED  0x0001
#define HEADER_SIZE sizeof(heap_block_t)

/* ============ Heap State ============ */

static heap_block_t *heap_start = NULL;
static heap_block_t *heap_free_list = NULL;
static uint64_t heap_size = 0;
static uint64_t heap_used = 0;
static uint64_t alloc_count = 0;
static uint64_t free_count = 0;

/* ============ Helper Functions ============ */

static void *heap_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void *heap_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_puts(&buf[i + 1]);
}

/* ============ Heap Implementation ============ */

void heap_init(void) {
    serial_puts("[HEAP] Initializing kernel heap...\n");
    
    /* Allocate initial heap from PMM */
    size_t pages = HEAP_INITIAL_SIZE / PAGE_SIZE;
    uint64_t heap_phys = pmm_alloc_pages(pages);
    
    if (heap_phys == 0) {
        serial_puts("[HEAP] ERROR: Failed to allocate heap pages!\n");
        return;
    }
    
    /* For now, identity map (VMM not set up yet) */
    heap_start = (heap_block_t *)heap_phys;
    heap_size = HEAP_INITIAL_SIZE;
    
    /* Initialize single free block spanning entire heap */
    heap_start->magic = HEAP_MAGIC;
    heap_start->flags = 0;  /* Free */
    heap_start->size = heap_size - HEADER_SIZE;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    heap_free_list = heap_start;
    
    serial_puts("[HEAP] Ready: ");
    serial_dec(HEAP_INITIAL_SIZE / 1024);
    serial_puts(" KB heap at 0x");
    
    /* Print address */
    const char hex[] = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(heap_phys >> i) & 0xF]);
    }
    serial_puts("\n");
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    if (heap_start == NULL) {
        serial_puts("[HEAP] ERROR: Heap not initialized!\n");
        return NULL;
    }
    
    /* Align size to 8 bytes */
    size = (size + 7) & ~7;
    if (size < HEAP_MIN_ALLOC - HEADER_SIZE) {
        size = HEAP_MIN_ALLOC - HEADER_SIZE;
    }
    
    /* Find first fit */
    heap_block_t *block = heap_free_list;
    heap_block_t *best = NULL;
    
    while (block != NULL) {
        if (block->magic != HEAP_MAGIC) {
            serial_puts("[HEAP] ERROR: Heap corruption detected!\n");
            return NULL;
        }
        
        if (!(block->flags & BLOCK_USED) && block->size >= size) {
            best = block;
            break;  /* First fit */
        }
        block = block->next;
    }
    
    if (best == NULL) {
        serial_puts("[HEAP] ERROR: Out of heap memory!\n");
        return NULL;
    }
    
    /* Split block if significantly larger */
    if (best->size >= size + HEADER_SIZE + HEAP_MIN_ALLOC) {
        /* Create new free block after this one */
        heap_block_t *new_block = (heap_block_t *)((uint8_t *)best + HEADER_SIZE + size);
        new_block->magic = HEAP_MAGIC;
        new_block->flags = 0;
        new_block->size = best->size - size - HEADER_SIZE;
        new_block->next = best->next;
        new_block->prev = best;
        
        if (best->next) {
            best->next->prev = new_block;
        }
        best->next = new_block;
        best->size = size;
    }
    
    /* Mark as used */
    best->flags |= BLOCK_USED;
    
    /* Remove from free list */
    if (best == heap_free_list) {
        heap_free_list = best->next;
    }
    if (best->prev) {
        best->prev->next = best->next;
    }
    if (best->next) {
        best->next->prev = best->prev;
    }
    
    heap_used += best->size + HEADER_SIZE;
    alloc_count++;
    
    /* Return pointer to data area (after header) */
    return (void *)((uint8_t *)best + HEADER_SIZE);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        heap_memset(ptr, 0, size);
    }
    return ptr;
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    /* Simple implementation: over-allocate and align */
    if (alignment < 8) alignment = 8;
    void *ptr = kmalloc(size + alignment);
    if (ptr == NULL) return NULL;
    
    /* Align pointer */
    uint64_t addr = (uint64_t)ptr;
    uint64_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    
    /* Store original pointer before aligned data */
    /* (This is a simplified implementation) */
    return (void *)aligned;
}

void *krealloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    /* Get old block */
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - HEADER_SIZE);
    
    if (block->magic != HEAP_MAGIC) {
        serial_puts("[HEAP] ERROR: Invalid pointer to krealloc!\n");
        return NULL;
    }
    
    /* If shrinking or same size, just return */
    if (size <= block->size) {
        return ptr;
    }
    
    /* Need to grow: allocate new block and copy */
    void *new_ptr = kmalloc(size);
    if (new_ptr == NULL) return NULL;
    
    heap_memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    
    return new_ptr;
}

void kfree(void *ptr) {
    if (ptr == NULL) return;
    
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - HEADER_SIZE);
    
    /* Validate */
    if (block->magic != HEAP_MAGIC) {
        serial_puts("[HEAP] ERROR: Invalid pointer to kfree!\n");
        return;
    }
    
    if (!(block->flags & BLOCK_USED)) {
        serial_puts("[HEAP] WARNING: Double-free detected!\n");
        return;
    }
    
    /* Mark as free */
    block->flags &= ~BLOCK_USED;
    heap_used -= block->size + HEADER_SIZE;
    free_count++;
    
    /* ========== Coalesce adjacent free blocks ========== */
    
    /* Forward coalesce: merge with physically next block if free */
    heap_block_t *next_phys = (heap_block_t *)((uint8_t *)block + HEADER_SIZE + block->size);
    uint64_t next_offset = (uint64_t)((uint8_t *)next_phys - (uint8_t *)heap_start);
    
    if (next_offset < heap_size && next_phys->magic == HEAP_MAGIC 
        && !(next_phys->flags & BLOCK_USED)) {
        /* Remove next_phys from free list before merging */
        if (next_phys == heap_free_list) {
            heap_free_list = next_phys->next;
        }
        if (next_phys->prev) {
            next_phys->prev->next = next_phys->next;
        }
        if (next_phys->next) {
            next_phys->next->prev = next_phys->prev;
        }
        /* Absorb next block's space (its header + data become our data) */
        block->size += HEADER_SIZE + next_phys->size;
        /* Invalidate absorbed header to catch stale pointer bugs */
        next_phys->magic = 0;
    }
    
    /* Backward coalesce: find physically previous block by walking from heap_start.
     * This is O(n) but correct. A footer-based approach would be O(1) but requires
     * a format change — defer to a later optimization pass. */
    heap_block_t *prev_phys = NULL;
    heap_block_t *scan = heap_start;
    uint64_t scanned = 0;
    
    while (scanned < heap_size) {
        heap_block_t *scan_next = (heap_block_t *)((uint8_t *)scan + HEADER_SIZE + scan->size);
        if (scan_next == block) {
            prev_phys = scan;
            break;
        }
        if (scan->magic != HEAP_MAGIC) break;  /* Corruption guard */
        scanned += HEADER_SIZE + scan->size;
        scan = scan_next;
    }
    
    if (prev_phys && prev_phys->magic == HEAP_MAGIC 
        && !(prev_phys->flags & BLOCK_USED)) {
        /* prev_phys is already in the free list — just grow it */
        prev_phys->size += HEADER_SIZE + block->size;
        /* Invalidate absorbed header */
        block->magic = 0;
        /* Don't add 'block' to free list — prev_phys already covers the range */
        return;
    }
    
    /* Add block to free list (front) */
    block->next = heap_free_list;
    block->prev = NULL;
    if (heap_free_list) {
        heap_free_list->prev = block;
    }
    heap_free_list = block;
}

void heap_get_stats(heap_stats_t *stats) {
    if (stats == NULL) return;
    
    stats->total_size = heap_size;
    stats->used_size = heap_used;
    stats->free_size = heap_size - heap_used;
    stats->alloc_count = alloc_count;
    stats->free_count = free_count;
    stats->largest_free = 0;  /* TODO: Calculate */
}

void heap_debug_print(void) {
    serial_puts("\n=== Heap Status ===\n");
    serial_puts("Total Size:  ");
    serial_dec(heap_size / 1024);
    serial_puts(" KB\n");
    serial_puts("Used:        ");
    serial_dec(heap_used / 1024);
    serial_puts(" KB\n");
    serial_puts("Free:        ");
    serial_dec((heap_size - heap_used) / 1024);
    serial_puts(" KB\n");
    serial_puts("Allocations: ");
    serial_dec(alloc_count);
    serial_puts("\n");
    serial_puts("Frees:       ");
    serial_dec(free_count);
    serial_puts("\n");
    serial_puts("===================\n\n");
}

int heap_check(void) {
    /* Walk all blocks and verify magic numbers */
    heap_block_t *block = heap_start;
    uint64_t checked = 0;
    
    while (checked < heap_size) {
        if (block->magic != HEAP_MAGIC) {
            serial_puts("[HEAP] Corruption at offset ");
            serial_dec(checked);
            serial_puts("!\n");
            return -1;
        }
        checked += HEADER_SIZE + block->size;
        block = (heap_block_t *)((uint8_t *)block + HEADER_SIZE + block->size);
    }
    
    return 0;
}
