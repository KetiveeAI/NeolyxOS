/*
 * Platform to KDRV Integration
 * 
 * Connects platform detection to kernel drivers (.kdrv system)
 * Allows drivers to access platform-specific optimizations
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef PLATFORM_KDRV_H
#define PLATFORM_KDRV_H

#include "platform/platform_detect.h"

/* KDRV Platform Interface */
typedef struct {
    /* Platform information accessible to all .kdrv drivers */
    platform_type_t type;
    const char *vendor_string;
    const char *brand_string;
    cpu_features_t features;
    
    /* Platform-optimized operations */
    void (*fast_memcpy)(void *dest, const void *src, size_t n);
    void (*fast_memset)(void *s, int c, size_t n);
    void (*cache_flush)(void *addr);
    uint64_t (*read_cycles)(void);
    
    /* Power management */
    void (*cpu_idle)(void);
    
} platform_kdrv_ops_t;

/* Global platform operations for KDRV drivers */
extern platform_kdrv_ops_t g_platform_kdrv_ops;

/* Initialize platform-kdrv integration */
void platform_kdrv_init(void);

/* Check if platform supports feature */
int platform_has_feature(const char *feature);

/* Get platform type string */
const char *platform_get_type_string(void);

/*
 * Convenience macros for .kdrv drivers
 */
#define KDRV_MEMCPY(dst, src, n)  g_platform_kdrv_ops.fast_memcpy(dst, src, n)
#define KDRV_MEMSET(s, c, n)      g_platform_kdrv_ops.fast_memset(s, c, n)
#define KDRV_CACHE_FLUSH(addr)    g_platform_kdrv_ops.cache_flush(addr)
#define KDRV_READ_CYCLES()        g_platform_kdrv_ops.read_cycles()
#define KDRV_CPU_IDLE()           g_platform_kdrv_ops.cpu_idle()

/* Feature check macros */
#define KDRV_HAS_AVX()     (g_platform_kdrv_ops.features.avx)
#define KDRV_HAS_AVX2()    (g_platform_kdrv_ops.features.avx2)
#define KDRV_HAS_AVX512()  (g_platform_kdrv_ops.features.avx512)
#define KDRV_HAS_AES()     (g_platform_kdrv_ops.features.aes_ni)
#define KDRV_HAS_SSE42()   (g_platform_kdrv_ops.features.sse4_2)

/* Platform type checks */
#define KDRV_IS_AMD()      (g_platform_kdrv_ops.type == PLATFORM_AMD)
#define KDRV_IS_INTEL()    (g_platform_kdrv_ops.type == PLATFORM_INTEL)
#define KDRV_IS_ARM()      (g_platform_kdrv_ops.type == PLATFORM_ARM)

#endif /* PLATFORM_KDRV_H */
