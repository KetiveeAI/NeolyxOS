/*
 * NeolyxOS Arena Allocator Implementation
 * 
 * Bump allocator optimized for frame-based rendering workloads.
 * Pattern: XNU kalloc zones + Linux BPF arena
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nx_arena.h"
#include "../include/nxsyscall.h"

int nx_arena_init(nx_arena_t *arena, uint64_t reserve_size) {
    if (!arena) return -1;
    if (reserve_size == 0 || reserve_size > NX_ARENA_MAX_SIZE) return -1;
    
    /* Align reserve to page boundary */
    reserve_size = (reserve_size + 4095) & ~4095ULL;
    
    /* Allocate via brk */
    void *mem = nx_alloc(reserve_size);
    if (!mem) return -1;
    
    arena->base = (uint8_t *)mem;
    arena->capacity = reserve_size;
    arena->offset = 0;
    arena->peak = 0;
    arena->alloc_count = 0;
    arena->flags = NX_ARENA_FLAG_INITIALIZED;
    
    return 0;
}

void *nx_arena_alloc(nx_arena_t *arena, uint64_t size, uint64_t align) {
    if (!arena || !(arena->flags & NX_ARENA_FLAG_INITIALIZED)) return NULL;
    if (size == 0) return NULL;
    if (align == 0) align = NX_ARENA_ALIGN_DEFAULT;
    
    /* Ensure alignment is power of 2 */
    if (align & (align - 1)) return NULL;
    
    /* Align current offset */
    uint64_t aligned_offset = (arena->offset + align - 1) & ~(align - 1);
    
    /* Check if allocation fits */
    if (aligned_offset + size > arena->capacity) {
        return NULL;  /* Arena exhausted */
    }
    
    void *ptr = arena->base + aligned_offset;
    arena->offset = aligned_offset + size;
    arena->alloc_count++;
    
    /* Track peak usage */
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }
    
    return ptr;
}

void nx_arena_reset(nx_arena_t *arena) {
    if (!arena || !(arena->flags & NX_ARENA_FLAG_INITIALIZED)) return;
    
    arena->offset = 0;
    arena->alloc_count = 0;
    /* peak is intentionally preserved for diagnostics */
}

void nx_arena_destroy(nx_arena_t *arena) {
    if (!arena || !(arena->flags & NX_ARENA_FLAG_INITIALIZED)) return;
    
    if (arena->base) {
        nx_free(arena->base);
    }
    
    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
    arena->peak = 0;
    arena->alloc_count = 0;
    arena->flags = 0;
}
