/*
 * NeolyxOS Safe Memory Operations Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../include/core/safe_mem.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ Safe Memory Copy ============ */

int safe_memcpy(void *dst, uint64_t dst_size, const void *src, uint64_t count) {
    /* Null pointer checks */
    if (!dst || !src) {
        return -1;
    }
    
    /* Bounds check */
    if (count > dst_size) {
        serial_puts("[SAFE_MEM] memcpy: count exceeds dst_size, truncating\n");
        count = dst_size;
    }
    
    /* Size sanity check */
    if (count > 0x100000000ULL) {  /* 4GB max */
        serial_puts("[SAFE_MEM] memcpy: size too large\n");
        return -1;
    }
    
    /* Perform copy */
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    
    for (uint64_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    
    return 0;
}

int safe_memset(void *dst, uint64_t dst_size, int val, uint64_t count) {
    /* Null pointer check */
    if (!dst) {
        return -1;
    }
    
    /* Bounds check */
    if (count > dst_size) {
        count = dst_size;
    }
    
    /* Size sanity check */
    if (count > 0x100000000ULL) {
        return -1;
    }
    
    /* Perform set */
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)val;
    
    for (uint64_t i = 0; i < count; i++) {
        d[i] = v;
    }
    
    return 0;
}

/* ============ Safe String Operations ============ */

int safe_strcpy(char *dst, uint64_t dst_size, const char *src) {
    /* Null pointer checks */
    if (!dst) {
        return -1;
    }
    
    /* Ensure at least 1 byte for null terminator */
    if (dst_size == 0) {
        return -1;
    }
    
    /* Handle null source */
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    
    /* Copy with bounds check */
    uint64_t i = 0;
    while (src[i] && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    
    /* Always null terminate */
    dst[i] = '\0';
    
    return 0;
}

int safe_strcat(char *dst, uint64_t dst_size, const char *src) {
    /* Null checks */
    if (!dst || !src || dst_size == 0) {
        return -1;
    }
    
    /* Find end of dst */
    uint64_t dst_len = 0;
    while (dst[dst_len] && dst_len < dst_size) {
        dst_len++;
    }
    
    /* No room left */
    if (dst_len >= dst_size - 1) {
        return 0;  /* Silently succeed - already full */
    }
    
    /* Append with bounds check */
    uint64_t i = 0;
    while (src[i] && (dst_len + i) < dst_size - 1) {
        dst[dst_len + i] = src[i];
        i++;
    }
    
    /* Always null terminate */
    dst[dst_len + i] = '\0';
    
    return 0;
}

uint64_t safe_strlen(const char *str, uint64_t max_len) {
    if (!str) {
        return 0;
    }
    
    uint64_t len = 0;
    while (str[len] && len < max_len) {
        len++;
    }
    
    return len;
}

/* ============ Safe Allocation ============ */

void *safe_alloc(uint64_t size) {
    /* Size check */
    if (size == 0 || size > 0x100000000ULL) {
        return NULL;
    }
    
    /* Allocate */
    void *ptr = kmalloc(size);
    if (!ptr) {
        return NULL;
    }
    
    /* Zero initialize */
    safe_memset(ptr, size, 0, size);
    
    return ptr;
}

void *safe_realloc(void *ptr, uint64_t old_size, uint64_t new_size) {
    /* Handle null ptr as alloc */
    if (!ptr) {
        return safe_alloc(new_size);
    }
    
    /* Size check */
    if (new_size == 0) {
        safe_free(&ptr, old_size);
        return NULL;
    }
    
    if (new_size > 0x100000000ULL) {
        return NULL;
    }
    
    /* Allocate new buffer */
    void *new_ptr = safe_alloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy old data */
    uint64_t copy_size = (old_size < new_size) ? old_size : new_size;
    safe_memcpy(new_ptr, new_size, ptr, copy_size);
    
    /* Free old buffer */
    safe_free(&ptr, old_size);
    
    return new_ptr;
}

void safe_free(void **ptr_ptr, uint64_t size) {
    /* Null checks */
    if (!ptr_ptr || !*ptr_ptr) {
        return;
    }
    
    void *ptr = *ptr_ptr;
    
    /* Poison memory before freeing to detect use-after-free */
    if (size > 0 && size < 0x100000000ULL) {
        safe_memset(ptr, size, SAFE_POISON_FREED, size);
    }
    
    /* Free the memory */
    kfree(ptr);
    
    /* Set pointer to NULL to prevent dangling reference */
    *ptr_ptr = NULL;
}
