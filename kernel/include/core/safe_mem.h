/*
 * NeolyxOS Safe Memory Operations
 * 
 * Provides bounded, null-safe memory operations to prevent
 * buffer overflows and use-after-free vulnerabilities.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SAFE_MEM_H
#define NEOLYX_SAFE_MEM_H

#include <stdint.h>

/* ============ Safe Memory Operations ============ */

/**
 * safe_memcpy - Bounded memory copy with overflow check
 * 
 * @dst: Destination buffer
 * @dst_size: Destination buffer size
 * @src: Source buffer
 * @count: Bytes to copy
 * 
 * Returns: 0 on success, -1 on error (null ptr or overflow)
 * 
 * Guarantees:
 * - Will never write beyond dst_size
 * - Will never read from NULL
 * - Will never write to NULL
 */
int safe_memcpy(void *dst, uint64_t dst_size, const void *src, uint64_t count);

/**
 * safe_memset - Bounded memory set
 * 
 * @dst: Destination buffer
 * @dst_size: Destination buffer size
 * @val: Value to set
 * @count: Bytes to set
 * 
 * Returns: 0 on success, -1 on error
 */
int safe_memset(void *dst, uint64_t dst_size, int val, uint64_t count);

/**
 * safe_strcpy - Bounded string copy
 * 
 * @dst: Destination buffer
 * @dst_size: Destination buffer size (must include null terminator)
 * @src: Source string
 * 
 * Returns: 0 on success, -1 on error
 * 
 * Guarantees:
 * - Destination is always null-terminated
 * - Will never write beyond dst_size
 * - Truncates if source is too long
 */
int safe_strcpy(char *dst, uint64_t dst_size, const char *src);

/**
 * safe_strcat - Bounded string concatenation
 * 
 * @dst: Destination buffer (with existing string)
 * @dst_size: Total destination buffer size
 * @src: String to append
 * 
 * Returns: 0 on success, -1 on error
 */
int safe_strcat(char *dst, uint64_t dst_size, const char *src);

/**
 * safe_strlen - Safe string length with max limit
 * 
 * @str: String to measure
 * @max_len: Maximum length to check
 * 
 * Returns: String length (capped at max_len)
 */
uint64_t safe_strlen(const char *str, uint64_t max_len);

/* ============ Safe Allocation ============ */

/**
 * safe_alloc - Allocate and zero memory
 * 
 * @size: Size to allocate
 * 
 * Returns: Zeroed memory or NULL
 */
void *safe_alloc(uint64_t size);

/**
 * safe_realloc - Safe reallocation with copy
 * 
 * @ptr: Existing pointer (can be NULL)
 * @old_size: Current size
 * @new_size: New size
 * 
 * Returns: New pointer or NULL
 */
void *safe_realloc(void *ptr, uint64_t old_size, uint64_t new_size);

/**
 * safe_free - Null-safe free with poison
 * 
 * @ptr_ptr: Pointer to pointer (set to NULL after free)
 * @size: Size of allocation (for poisoning)
 * 
 * Guarantees:
 * - Safe to call with NULL
 * - Pointer is set to NULL after free
 * - Memory is poisoned to catch use-after-free
 */
void safe_free(void **ptr_ptr, uint64_t size);

/* ============ Memory Validation ============ */

/**
 * safe_ptr_check - Check if pointer is valid
 * 
 * @ptr: Pointer to check
 * 
 * Returns: 1 if valid, 0 if NULL
 */
static inline int safe_ptr_check(const void *ptr) {
    return ptr != NULL;
}

/**
 * safe_bounds_check - Check if access is within bounds
 * 
 * @offset: Offset into buffer
 * @access_size: Size of access
 * @buf_size: Total buffer size
 * 
 * Returns: 1 if within bounds, 0 if overflow
 */
static inline int safe_bounds_check(uint64_t offset, uint64_t access_size, uint64_t buf_size) {
    /* Check for overflow in addition */
    if (offset + access_size < offset) return 0;
    return (offset + access_size) <= buf_size;
}

/* ============ Poison Values ============ */

#define SAFE_POISON_FREED    0xDE  /* Freed memory pattern */
#define SAFE_POISON_UNINIT   0xCD  /* Uninitialized memory pattern */
#define SAFE_POISON_GUARD    0xFD  /* Buffer guard pattern */

#endif /* NEOLYX_SAFE_MEM_H */
