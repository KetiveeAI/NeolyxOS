/*
 * Platform to KDRV Integration Implementation
 * 
 * Bridges platform detection with kernel driver system
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "platform/platform_kdrv.h"
#include "platform/platform_detect.h"

/* Global platform ops for KDRV drivers */
platform_kdrv_ops_t g_platform_kdrv_ops = {0};

extern void serial_puts(const char *s);

/*
 * platform_kdrv_init - Initialize platform-kdrv bridge
 * 
 * Called after platform_init() to expose platform ops to all .kdrv drivers
 */
void platform_kdrv_init(void) {
    /* Copy platform info to KDRV interface */
    g_platform_kdrv_ops.type = g_platform.type;
    g_platform_kdrv_ops.vendor_string = g_platform.vendor_string;
    g_platform_kdrv_ops.brand_string = g_platform.brand_string;
    g_platform_kdrv_ops.features = g_platform.features;
    
    /* Expose platform-optimized operations */
    g_platform_kdrv_ops.fast_memcpy = g_platform_ops.memcpy_fast;
    g_platform_kdrv_ops.fast_memset = g_platform_ops.memset_fast;
    g_platform_kdrv_ops.cache_flush = g_platform_ops.flush_cache_line;
    g_platform_kdrv_ops.read_cycles = g_platform_ops.read_cycle_counter;
    g_platform_kdrv_ops.cpu_idle = g_platform_ops.cpu_idle;
    
    serial_puts("[PLATFORM-KDRV] Platform ops exported to kernel drivers\r\n");
}

/*
 * platform_has_feature - Check if CPU supports a feature
 */
int platform_has_feature(const char *feature) {
    if (!feature) return 0;
    
    /* Simple string comparison for common features */
    if (__builtin_strcmp(feature, "avx") == 0) return g_platform.features.avx;
    if (__builtin_strcmp(feature, "avx2") == 0) return g_platform.features.avx2;
    if (__builtin_strcmp(feature, "avx512") == 0) return g_platform.features.avx512;
    if (__builtin_strcmp(feature, "sse4_2") == 0) return g_platform.features.sse4_2;
    if (__builtin_strcmp(feature, "aes") == 0) return g_platform.features.aes_ni;
    if (__builtin_strcmp(feature, "fma") == 0) return g_platform.features.fma;
    
    return 0;
}

/*
 * platform_get_type_string - Get human-readable platform type
 */
const char *platform_get_type_string(void) {
    switch (g_platform.type) {
        case PLATFORM_AMD: return "AMD";
        case PLATFORM_INTEL: return "Intel";
        case PLATFORM_ARM: return "ARM";
        case PLATFORM_RISCV: return "RISC-V";
        default: return "Generic";
    }
}
