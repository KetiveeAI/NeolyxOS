#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <mm/heap.h>

// Heap block header
typedef struct heap_block {
    struct heap_block* next;
    size_t size;
    bool used;
} heap_block_t;

// Heap management
static heap_block_t* heap_start = NULL;
static heap_block_t* heap_end = NULL;
static size_t heap_size = 0;

// Initialize heap
void heap_init(uint64_t start_addr, size_t size) {
    heap_start = (heap_block_t*)start_addr;
    heap_end = (heap_block_t*)(start_addr + size);
    heap_size = size;

    // Initialize first block
    heap_start->next = NULL;
    heap_start->size = size - sizeof(heap_block_t);
    heap_start->used = false;
}

// Allocate memory from heap
void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // Align size to 8 bytes
    size = (size + 7) & ~7;

    heap_block_t* current = heap_start;
    
    while (current != NULL) {
        if (!current->used && current->size >= size) {
            // Check if we can split the block
            if (current->size >= size + sizeof(heap_block_t) + 16) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)current + sizeof(heap_block_t) + size);
                new_block->next = current->next;
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->used = false;
                
                current->next = new_block;
                current->size = size;
            }
            
            current->used = true;
            return (void*)((uint8_t*)current + sizeof(heap_block_t));
        }
        current = current->next;
    }
    
    return NULL; // Out of memory
}

// Free memory
void kfree(void* ptr) {
    if (ptr == NULL) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->used = false;

    // Merge with next block if it's also free
    if (block->next != NULL && !block->next->used) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
    }

    // Merge with previous block if it's also free
    heap_block_t* prev = heap_start;
    while (prev != NULL && prev->next != block) {
        prev = prev->next;
    }
    
    if (prev != NULL && !prev->used) {
        prev->size += sizeof(heap_block_t) + block->size;
        prev->next = block->next;
    }
}

// Get heap statistics
void heap_stats(size_t* total_size, size_t* used_size, size_t* free_size) {
    *total_size = heap_size;
    *used_size = 0;
    *free_size = 0;

    heap_block_t* current = heap_start;
    while (current != NULL) {
        if (current->used) {
            *used_size += current->size;
        } else {
            *free_size += current->size;
        }
        current = current->next;
    }
}

// Backup dummy implementations for future reference
// void* heap_alloc(size_t size) {
//     // TODO: Replace with real allocator
//     return 0;
// }
//
// void heap_free(void* ptr) {
//     // TODO: Replace with real free logic
// }

// Allocate zeroed memory
void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (ptr) {
        uint8_t* p = (uint8_t*)ptr;
        while (size--) *p++ = 0;
    }
    return ptr;
}