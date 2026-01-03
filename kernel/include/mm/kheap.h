/*
 * NeolyxOS Kernel Heap Allocator
 * 
 * Provides kmalloc()/kfree() for dynamic kernel memory allocation.
 * Uses a simple first-fit free list allocator.
 * 
 * Design:
 *   - Backed by physical pages from PMM
 *   - First-fit allocation with coalescing
 *   - Thread-safe (when SMP is added)
 *   - Debugging support
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_HEAP_H
#define NEOLYX_HEAP_H

#include <stdint.h>
#include <stddef.h>

/* ============ Configuration ============ */

/* Initial heap size (grows automatically) */
#define HEAP_INITIAL_SIZE   (16 * 1024 * 1024)  /* 16 MB - enough for back buffer + allocations */

/* Maximum heap size */
#define HEAP_MAX_SIZE       (256 * 1024 * 1024) /* 256 MB */

/* Minimum allocation size (includes header overhead) */
#define HEAP_MIN_ALLOC      32

/* ============ Heap Statistics ============ */

typedef struct {
    uint64_t total_size;        /* Total heap size */
    uint64_t used_size;         /* Currently allocated */
    uint64_t free_size;         /* Available */
    uint64_t alloc_count;       /* Number of allocations */
    uint64_t free_count;        /* Number of frees */
    uint64_t largest_free;      /* Largest free block */
} heap_stats_t;

/* ============ Heap API ============ */

/**
 * Initialize the kernel heap.
 * Must be called after PMM is initialized.
 */
void heap_init(void);

/**
 * Allocate memory from the kernel heap.
 * 
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 * 
 * Memory is NOT zeroed (use kzalloc for zeroed memory).
 */
void *kmalloc(size_t size);

/**
 * Allocate zeroed memory.
 * 
 * @param size Number of bytes to allocate.
 * @return Pointer to zeroed memory, or NULL on failure.
 */
void *kzalloc(size_t size);

/**
 * Allocate aligned memory.
 * 
 * @param size      Number of bytes to allocate.
 * @param alignment Required alignment (must be power of 2).
 * @return Aligned pointer, or NULL on failure.
 */
void *kmalloc_aligned(size_t size, size_t alignment);

/**
 * Reallocate memory.
 * 
 * @param ptr  Existing allocation (can be NULL).
 * @param size New size.
 * @return Pointer to reallocated memory, or NULL on failure.
 * 
 * If ptr is NULL, behaves like kmalloc().
 * If size is 0, behaves like kfree() and returns NULL.
 */
void *krealloc(void *ptr, size_t size);

/**
 * Free allocated memory.
 * 
 * @param ptr Pointer returned by kmalloc/kzalloc/krealloc.
 * 
 * NULL is safe to pass (no-op).
 * Double-free is detected and logged.
 */
void kfree(void *ptr);

/**
 * Get heap statistics.
 * 
 * @param stats Pointer to stats structure to fill.
 */
void heap_get_stats(heap_stats_t *stats);

/**
 * Print heap debug information.
 */
void heap_debug_print(void);

/**
 * Check heap integrity (for debugging).
 * 
 * @return 0 if heap is valid, -1 if corruption detected.
 */
int heap_check(void);

#endif /* NEOLYX_HEAP_H */
