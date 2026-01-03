/*
 * NeolyxOS Desktop - Userspace Stubs
 * 
 * These functions replace kernel dependencies with syscall-based
 * or userspace implementations for the desktop shell.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "nxsyscall.h"

/* ============ Memory Allocator (Simple Bump Allocator) ============ */

static uint8_t g_heap[1024 * 1024];  /* 1MB heap */
static uint64_t g_heap_pos = 0;

void *kmalloc(uint64_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~7ULL;
    
    if (g_heap_pos + size > sizeof(g_heap)) {
        return 0;  /* Out of memory */
    }
    
    void *ptr = &g_heap[g_heap_pos];
    g_heap_pos += size;
    return ptr;
}

void kfree(void *ptr) {
    /* Simple allocator doesn't support free */
    (void)ptr;
}

/* ============ Serial/Debug Output (via syscall) ============ */

/* SYS_DEBUG_PRINT = 127, use syscall1 from nxsyscall.h */
#define SYS_DEBUG_PRINT 127

void serial_putc(char c) {
    /* Print single char by using a temp string */
    char buf[2] = {c, 0};
    syscall1(SYS_DEBUG_PRINT, (int64_t)buf);
}

void serial_puts(const char *s) {
    if (s) {
        syscall1(SYS_DEBUG_PRINT, (int64_t)s);
    }
}

/* ============ Timer (via syscall) ============ */

uint64_t pit_get_ticks(void) {
    /* Use SYS_TIME syscall (3) to get milliseconds since boot */
    return (uint64_t)syscall0(3);  /* SYS_TIME */
}

/* ============ Graphics Functions (stubs for now) ============ */

void fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    (void)x; (void)y; (void)w; (void)h; (void)color; (void)alpha;
    /* TODO: Implement with framebuffer syscall */
}

void fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)color;
    /* TODO: Implement with framebuffer syscall */
}

void fill_circle(int cx, int cy, int r, uint32_t color) {
    (void)cx; (void)cy; (void)r; (void)color;
    /* TODO: Implement with framebuffer syscall */
}

void draw_text_large(int x, int y, const char *text, uint32_t color) {
    (void)x; (void)y; (void)text; (void)color;
    /* TODO: Implement with syscall */
}

/* ============ Session Management (stubs) ============ */

void session_lock(void) {
    /* TODO: Implement via syscall */
}

void session_unlock(void) {
    /* TODO: Implement via syscall */
}

/* ============ Memset (required by some code) ============ */

void *memset(void *s, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* ============ Cursor Syscall (kernel cursor compositor) ============ */

#define SYS_CURSOR_SET 128

void nx_cursor_set(int32_t x, int32_t y, int visible) {
    syscall3(SYS_CURSOR_SET, (int64_t)x, (int64_t)y, (int64_t)visible);
}
