/*
 * Intel Platform Optimizations
 * 
 * Optimized memory operations and power management for Intel Core/Xeon CPUs
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "platform/platform_detect.h"

/* Fast memcpy using ERMSB (Enhanced REP MOVSB) */
static void intel_memcpy_fast(void *dest, const void *src, size_t n) {
    /* Intel has ERMSB since Ivy Bridge */
    if (n >= 64) {
        __asm__ volatile(
            "rep movsb"
            : "+D"(dest), "+S"(src), "+c"(n)
            :
            : "memory"
        );
    } else {
        /* Small copies: optimized loop */
        uint64_t *d64 = (uint64_t*)dest;
        const uint64_t *s64 = (const uint64_t*)src;
        
        /* Copy 8 bytes at a time */
        while (n >= 8) {
            *d64++ = *s64++;
            n -= 8;
        }
        
        /* Copy remaining bytes */
        uint8_t *d = (uint8_t*)d64;
        const uint8_t *s = (const uint8_t*)s64;
        while (n--) *d++ = *s++;
    }
}

/* Fast memset */
static void intel_memset_fast(void *s, int c, size_t n) {
    uint8_t value = (uint8_t)c;
    
    if (n >= 64) {
        /* Use REP STOSB for large fills */
        __asm__ volatile(
            "rep stosb"
            : "+D"(s), "+c"(n)
            : "a"(value)
            : "memory"
        );
    } else {
        /* Small fills: optimized loop */
        uint8_t *ptr = (uint8_t*)s;
        while (n--) *ptr++ = value;
    }
}

/* Intel-optimized CPU idle */
static void intel_cpu_idle(void) {
    /* Use HLT */
    __asm__ volatile("hlt");
}

/* Intel CPU halt */
static void intel_cpu_halt(void) {
    __asm__ volatile("cli; hlt");
}

/* Cache line flush */
static void intel_flush_cache_line(void *addr) {
    __asm__ volatile("clflush (%0)" : : "r"(addr) : "memory");
}

/* Invalidate entire cache */
static void intel_invalidate_cache(void) {
    __asm__ volatile("wbinvd" ::: "memory");
}

/* Enable performance counters */
static void intel_enable_perf_counters(void) {
    /* TODO: Set up Intel performance monitoring (IA32_PERFEVTSELx/IA32_PMCx) */
}

/* Read cycle counter (TSC) */
static uint64_t intel_read_cycle_counter(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * platform_intel_init - Initialize Intel-specific optimizations
 */
void platform_intel_init(platform_info_t *info, platform_ops_t *ops) {
    /* Set up Intel-optimized operations */
    ops->memcpy_fast = intel_memcpy_fast;
    ops->memset_fast = intel_memset_fast;
    ops->cpu_idle = intel_cpu_idle;
    ops->cpu_halt = intel_cpu_halt;
    ops->flush_cache_line = intel_flush_cache_line;
    ops->invalidate_cache = intel_invalidate_cache;
    ops->enable_perf_counters = intel_enable_perf_counters;
    ops->read_cycle_counter = intel_read_cycle_counter;
    
    /* Intel-specific microarchitecture optimizations */
    if (info->family == 6) {
        /* Core/Nehalem/Sandy Bridge/Skylake family */
        /* Additional optimizations based on model number */
    }
}
