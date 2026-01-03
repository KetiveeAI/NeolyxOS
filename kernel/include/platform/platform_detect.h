/*
 * NeolyxOS Platform Detection and Initialization
 * 
 * Detects CPU vendor (AMD, Intel, Generic) and loads platform-specific optimizations
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef PLATFORM_DETECT_H
#define PLATFORM_DETECT_H

#include <stdint.h>

/* Platform types */
typedef enum {
    PLATFORM_GENERIC = 0,
    PLATFORM_INTEL   = 1,
    PLATFORM_AMD     = 2,
    PLATFORM_ARM     = 3,   /* Future */
    PLATFORM_RISCV   = 4    /* Future */
} platform_type_t;

/* CPU Features */
typedef struct {
    uint32_t sse:1;
    uint32_t sse2:1;
    uint32_t sse3:1;
    uint32_t ssse3:1;
    uint32_t sse4_1:1;
    uint32_t sse4_2:1;
    uint32_t avx:1;
    uint32_t avx2:1;
    uint32_t avx512:1;
    uint32_t aes_ni:1;
    uint32_t rdrand:1;
    uint32_t bmi1:1;
    uint32_t bmi2:1;
    uint32_t fma:1;
    uint32_t hypervisor:1;
    uint32_t reserved:17;
} cpu_features_t;

/* Platform information */
typedef struct {
    platform_type_t type;
    char vendor_string[13];
    char brand_string[49];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    cpu_features_t features;
    uint32_t cache_line_size;
} platform_info_t;

/* Platform-specific operations */
typedef struct {
    /* Memory operations optimized per platform */
    void (*memcpy_fast)(void *dest, const void *src, size_t n);
    void (*memset_fast)(void *s, int c, size_t n);
    
    /* Power management */
    void (*cpu_idle)(void);
    void (*cpu_halt)(void);
    
    /* Cache operations */
    void (*flush_cache_line)(void *addr);
    void (*invalidate_cache)(void);
    
    /* Performance monitoring */
    void (*enable_perf_counters)(void);
    uint64_t (*read_cycle_counter)(void);
} platform_ops_t;

/* Global platform info */
extern platform_info_t g_platform;
extern platform_ops_t g_platform_ops;

/* Initialization */
void platform_detect(void);
void platform_init(void);

/* CPUID helper */
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
               uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                    : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                    : "a"(leaf), "c"(subleaf));
}

#endif /* PLATFORM_DETECT_H */
