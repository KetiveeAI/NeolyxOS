/*
 * Generic Platform Implementation (Fallback)
 * 
 * Basic implementations for unknown or unsupported CPUs
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "platform/platform_detect.h"

/* Generic memcpy */
static void generic_memcpy_fast(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

/* Generic memset */
static void generic_memset_fast(void *s, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)s;
    uint8_t value = (uint8_t)c;
    while (n--) *ptr++ = value;
}

/* Generic CPU idle */
static void generic_cpu_idle(void) {
    __asm__ volatile("hlt");
}

/* Generic CPU halt */
static void generic_cpu_halt(void) {
    __asm__ volatile("cli; hlt");
}

/* Generic cache flush (no-op if not supported) */
static void generic_flush_cache_line(void *addr) {
    (void)addr;
}

/* Generic cache invalidation (no-op) */
static void generic_invalidate_cache(void) {
}

/* Generic perf counters (no-op) */
static void generic_enable_perf_counters(void) {
}

/* Generic cycle counter */
static uint64_t generic_read_cycle_counter(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * platform_generic_init - Initialize generic (fallback) operations
 */
void platform_generic_init(platform_info_t *info, platform_ops_t *ops) {
    (void)info;
    
    ops->memcpy_fast = generic_memcpy_fast;
    ops->memset_fast = generic_memset_fast;
    ops->cpu_idle = generic_cpu_idle;
    ops->cpu_halt = generic_cpu_halt;
    ops->flush_cache_line = generic_flush_cache_line;
    ops->invalidate_cache = generic_invalidate_cache;
    ops->enable_perf_counters = generic_enable_perf_counters;
    ops->read_cycle_counter = generic_read_cycle_counter;
}
