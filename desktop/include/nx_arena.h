/*
 * NeolyxOS Arena Allocator
 * 
 * Production-grade bump allocator for desktop rendering buffers.
 * Eliminates fragmentation by using a single brk reserve with
 * sub-allocation via pointer arithmetic.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef _NX_ARENA_H_
#define _NX_ARENA_H_

#include <stdint.h>
#include <stddef.h>

#define NX_ARENA_ALIGN_DEFAULT  16
#define NX_ARENA_MAX_SIZE       (64 * 1024 * 1024)  /* 64MB max */

typedef struct nx_arena {
    uint8_t  *base;         /* Start of arena memory */
    uint64_t  capacity;     /* Total arena size in bytes */
    uint64_t  offset;       /* Current allocation offset */
    uint64_t  peak;         /* Peak usage for diagnostics */
    uint32_t  alloc_count;  /* Number of allocations */
    uint32_t  flags;        /* Arena state flags */
} nx_arena_t;

/* Arena flags */
#define NX_ARENA_FLAG_INITIALIZED  0x0001
#define NX_ARENA_FLAG_LOCKED       0x0002

/*
 * Initialize arena with specified reserve size.
 * Uses brk syscall to allocate contiguous memory.
 *
 * Returns 0 on success, -1 on failure.
 */
int nx_arena_init(nx_arena_t *arena, uint64_t reserve_size);

/*
 * Allocate memory from arena with specified alignment.
 * Fast bump allocation - O(1).
 *
 * Returns pointer on success, NULL if arena exhausted.
 */
void *nx_arena_alloc(nx_arena_t *arena, uint64_t size, uint64_t align);

/*
 * Convenience macro for default-aligned allocation.
 */
#define nx_arena_alloc_default(arena, size) \
    nx_arena_alloc(arena, size, NX_ARENA_ALIGN_DEFAULT)

/*
 * Reset arena for reuse. Bulk free - O(1).
 * Does not release memory back to kernel.
 */
void nx_arena_reset(nx_arena_t *arena);

/*
 * Release arena memory back to kernel.
 * Arena must be reinitialized before further use.
 */
void nx_arena_destroy(nx_arena_t *arena);

/*
 * Get arena statistics.
 */
static inline uint64_t nx_arena_used(const nx_arena_t *arena) {
    return arena ? arena->offset : 0;
}

static inline uint64_t nx_arena_remaining(const nx_arena_t *arena) {
    return arena ? (arena->capacity - arena->offset) : 0;
}

static inline uint64_t nx_arena_peak(const nx_arena_t *arena) {
    return arena ? arena->peak : 0;
}

#endif /* _NX_ARENA_H_ */
