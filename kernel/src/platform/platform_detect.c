/*
 * NeolyxOS Platform Detection and Initialization
 * 
 * Detects CPU vendor and initializes platform-specific optimizations
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "platform/platform_detect.h"
#include "../drivers/serial.h"

/* Global platform state */
platform_info_t g_platform = {0};
platform_ops_t g_platform_ops = {0};

/* Forward declarations for platform-specific init */
extern void platform_amd_init(platform_info_t *info, platform_ops_t *ops);
extern void platform_intel_init(platform_info_t *info, platform_ops_t *ops);
extern void platform_arm_init(platform_info_t *info, platform_ops_t *ops);
extern void platform_generic_init(platform_info_t *info, platform_ops_t *ops);

/*
 * platform_detect - Detect CPU vendor and features via CPUID
 */
void platform_detect(void) {
    uint32_t eax, ebx, ecx, edx;
    
    /* Get vendor string (CPUID leaf 0) */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    
    /* Store vendor string (ebx, edx, ecx order) */
    *(uint32_t*)(g_platform.vendor_string + 0) = ebx;
    *(uint32_t*)(g_platform.vendor_string + 4) = edx;
    *(uint32_t*)(g_platform.vendor_string + 8) = ecx;
    g_platform.vendor_string[12] = '\0';
    
    /* Determine vendor type */
    if (ebx == 0x68747541 && edx == 0x69746E65 && ecx == 0x444D4163) {
        /* "AuthenticAMD" */
        g_platform.type = PLATFORM_AMD;
    } else if (ebx == 0x756E6547 && edx == 0x49656E69 && ecx == 0x6C65746E) {
        /* "GenuineIntel" */
        g_platform.type = PLATFORM_INTEL;
    } else {
        g_platform.type = PLATFORM_GENERIC;
    }
    
    /* Get CPU family/model/stepping (CPUID leaf 1) */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    
    g_platform.stepping = eax & 0xF;
    g_platform.model = (eax >> 4) & 0xF;
    g_platform.family = (eax >> 8) & 0xF;
    
    /* Extended model/family for modern CPUs */
    if (g_platform.family == 0xF) {
        g_platform.family += (eax >> 20) & 0xFF;
    }
    if (g_platform.family == 6 || g_platform.family == 0xF) {
        g_platform.model += ((eax >> 16) & 0xF) << 4;
    }
    
    /* Detect CPU features from CPUID leaf 1 */
    g_platform.features.sse = (edx >> 25) & 1;
    g_platform.features.sse2 = (edx >> 26) & 1;
    g_platform.features.sse3 = ecx & 1;
    g_platform.features.ssse3 = (ecx >> 9) & 1;
    g_platform.features.sse4_1 = (ecx >> 19) & 1;
    g_platform.features.sse4_2 = (ecx >> 20) & 1;
    g_platform.features.avx = (ecx >> 28) & 1;
    g_platform.features.aes_ni = (ecx >> 25) & 1;
    g_platform.features.rdrand = (ecx >> 30) & 1;
    g_platform.features.fma = (ecx >> 12) & 1;
    g_platform.features.hypervisor = (ecx >> 31) & 1;
    
    /* Extended features (CPUID leaf 7) */
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    g_platform.features.avx2 = (ebx >> 5) & 1;
    g_platform.features.bmi1 = (ebx >> 3) & 1;
    g_platform.features.bmi2 = (ebx >> 8) & 1;
    g_platform.features.avx512 = (ebx >> 16) & 1;
    
    /* Get cache line size (CPUID leaf 1, ebx bits 15:8) */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    g_platform.cache_line_size = ((ebx >> 8) & 0xFF) * 8;
    
    /* Get brand string (CPUID 0x80000002-0x80000004) */
    uint32_t *brand = (uint32_t*)g_platform.brand_string;
    cpuid(0x80000002, 0, &brand[0], &brand[1], &brand[2], &brand[3]);
    cpuid(0x80000003, 0, &brand[4], &brand[5], &brand[6], &brand[7]);
    cpuid(0x80000004, 0, &brand[8], &brand[9], &brand[10], &brand[11]);
    g_platform.brand_string[48] = '\0';
    
    /* Trim leading spaces from brand string */
    char *start = g_platform.brand_string;
    while (*start == ' ') start++;
    if (start != g_platform.brand_string) {
        char *dst = g_platform.brand_string;
        while (*start) *dst++ = *start++;
        *dst = '\0';
    }
}

/*
 * platform_init - Initialize platform-specific optimizations
 */
void platform_init(void) {
    serial_puts("[PLATFORM] Detected: ");
    serial_puts(g_platform.vendor_string);
    serial_puts(" (");
    serial_puts(g_platform.brand_string);
    serial_puts(")\r\n");
    
    /* Initialize platform-specific operations */
    switch (g_platform.type) {
        case PLATFORM_AMD:
            serial_puts("[PLATFORM] Initializing AMD optimizations\r\n");
            platform_amd_init(&g_platform, &g_platform_ops);
            break;
            
        case PLATFORM_INTEL:
            serial_puts("[PLATFORM] Initializing Intel optimizations\r\n");
            platform_intel_init(&g_platform, &g_platform_ops);
            break;
        
        case PLATFORM_ARM:
            serial_puts("[PLATFORM] Initializing ARM optimizations\r\n");
            platform_arm_init(&g_platform, &g_platform_ops);
            break;
            
        default:
            serial_puts("[PLATFORM] Using generic optimizations\r\n");
            platform_generic_init(&g_platform, &g_platform_ops);
            break;
    }
    
    /* Log detected features */
    serial_puts("[PLATFORM] Features: ");
    if (g_platform.features.avx512) serial_puts("AVX512 ");
    if (g_platform.features.avx2) serial_puts("AVX2 ");
    if (g_platform.features.avx) serial_puts("AVX ");
    if (g_platform.features.sse4_2) serial_puts("SSE4.2 ");
    if (g_platform.features.aes_ni) serial_puts("AES-NI ");
    serial_puts("\r\n");
}
