/*
 * AMD Zen Platform Optimizations
 * 
 * Optimized memory operations and power management for AMD Ryzen/EPYC CPUs
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "platform/platform_detect.h"

/* AMD-specific CPUID leaves */
#define AMD_CPUID_FEATURES 0x80000001

/* Fast memcpy using AVX2 (if available) or REP MOVSB */
static void amd_memcpy_fast(void *dest, const void *src, size_t n) {
    /* AMD Zen has optimized REP MOVSB (>= Zen 2) */
    if (n >= 128) {
        __asm__ volatile(
            "rep movsb"
            : "+D"(dest), "+S"(src), "+c"(n)
            :
            : "memory"
        );
    } else {
        /* Small copies: use simple loop */
        uint8_t *d = (uint8_t*)dest;
        const uint8_t *s = (const uint8_t*)src;
        while (n--) *d++ = *s++;
    }
}

/* Fast memset using REP STOSB */
static void amd_memset_fast(void *s, int c, size_t n) {
    uint8_t value = (uint8_t)c;
    __asm__ volatile(
        "rep stosb"
        : "+D"(s), "+c"(n)
        : "a"(value)
        : "memory"
    );
}

/* AMD-optimized CPU idle (uses MWAIT if available) */
static void amd_cpu_idle(void) {
    /* Use HLT for now - MWAIT requires setup */
    __asm__ volatile("hlt");
}

/* AMD CPU halt */
static void amd_cpu_halt(void) {
    __asm__ volatile("cli; hlt");
}

/* Cache line flush */
static void amd_flush_cache_line(void *addr) {
    __asm__ volatile("clflush (%0)" : : "r"(addr) : "memory");
}

/* Invalidate entire cache */
static void amd_invalidate_cache(void) {
    __asm__ volatile("wbinvd" ::: "memory");
}

/* Enable performance counters (requires MSR access) */
static void amd_enable_perf_counters(void) {
    /* TODO: Set up AMD performance monitoring */
}

/* Read cycle counter (TSC) */
static uint64_t amd_read_cycle_counter(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * platform_amd_init - Initialize AMD-specific optimizations
 */
void platform_amd_init(platform_info_t *info, platform_ops_t *ops) {
    /* Set up AMD-optimized operations */
    ops->memcpy_fast = amd_memcpy_fast;
    ops->memset_fast = amd_memset_fast;
    ops->cpu_idle = amd_cpu_idle;
    ops->cpu_halt = amd_cpu_halt;
    ops->flush_cache_line = amd_flush_cache_line;
    ops->invalidate_cache = amd_invalidate_cache;
    ops->enable_perf_counters = amd_enable_perf_counters;
    ops->read_cycle_counter = amd_read_cycle_counter;
    
    /* AMD Zen-specific optimizations */
    if (info->family == 0x17 || info->family == 0x19 || info->family == 0x1A) {
        /* Zen (0x17), Zen2/Zen3 (0x19), Zen4/Zen5 (0x1A) detected */
        /* Call Zen driver initialization if linked */
        extern int zen_platform_init(void) __attribute__((weak));
        if (zen_platform_init) {
            zen_platform_init();
        }
    }
}

