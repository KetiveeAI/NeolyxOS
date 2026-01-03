/*
 * NeolyxOS Kernel Heap Allocator
 * 
 * Memory allocation functions for kernel use
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the kernel heap */
void heap_init(uint64_t start_addr, size_t size);

/* Allocate memory from the kernel heap */
void *kmalloc(size_t size);

/* Free memory back to the kernel heap */
void kfree(void *ptr);

/* Get heap statistics */
void heap_stats(size_t *total_size, size_t *used_size, size_t *free_size);

#endif /* MM_HEAP_H */
