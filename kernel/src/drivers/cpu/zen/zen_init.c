// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <stdint.h>

/* External kernel logging */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* CPUID helper */
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

static void serial_hex32(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

int zen_platform_init(void) {
    uint32_t eax, ebx, ecx, edx;
    
    serial_puts("[ZEN] Initializing AMD Zen platform\n");
    
    /* Get CPU family/model from CPUID leaf 1 */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    
    uint32_t stepping = eax & 0xF;
    uint32_t base_model = (eax >> 4) & 0xF;
    uint32_t base_family = (eax >> 8) & 0xF;
    uint32_t ext_model = (eax >> 16) & 0xF;
    uint32_t ext_family = (eax >> 20) & 0xFF;
    
    uint32_t family = base_family + ext_family;
    uint32_t model = (ext_model << 4) | base_model;
    
    serial_puts("[ZEN] Family: ");
    serial_hex32(family);
    serial_puts(" Model: ");
    serial_hex32(model);
    serial_puts(" Stepping: ");
    serial_hex32(stepping);
    serial_puts("\n");
    
    /* Identify Zen generation */
    const char *gen_name = "Unknown";
    
    if (family == 0x17) {
        /* Zen 1 / Zen+ / Zen 2 */
        if (model <= 0x0F) {
            gen_name = "Zen 1 (Summit Ridge)";
        } else if (model <= 0x2F) {
            gen_name = "Zen+ (Pinnacle Ridge)";
        } else if (model >= 0x30 && model <= 0x3F) {
            gen_name = "Zen 2 (Rome/Matisse)";
        } else if (model >= 0x60 && model <= 0x7F) {
            gen_name = "Zen 2 (Renoir/Lucienne)";
        } else if (model >= 0x90 && model <= 0x9F) {
            gen_name = "Zen 2 (Van Gogh)";
        } else {
            gen_name = "Zen 1/2 Family";
        }
    } else if (family == 0x19) {
        /* Zen 3 / Zen 4 */
        if (model <= 0x0F) {
            gen_name = "Zen 3 (Vermeer)";
        } else if (model >= 0x10 && model <= 0x1F) {
            gen_name = "Zen 3 (Milan)";
        } else if (model >= 0x40 && model <= 0x5F) {
            gen_name = "Zen 3+ (Rembrandt)";
        } else if (model >= 0x60 && model <= 0x7F) {
            gen_name = "Zen 4 (Raphael/Genoa)";
        } else if (model >= 0xA0) {
            gen_name = "Zen 4 (Phoenix)";
        } else {
            gen_name = "Zen 3/4 Family";
        }
    } else if (family == 0x1A) {
        /* Zen 5 */
        gen_name = "Zen 5 (Granite Ridge)";
    }
    
    serial_puts("[ZEN] Detected: ");
    serial_puts(gen_name);
    serial_puts("\n");
    
    /* Check for extended features */
    cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    
    if (ecx & (1 << 11)) {
        serial_puts("[ZEN] SSE4a supported\n");
    }
    if (edx & (1 << 26)) {
        serial_puts("[ZEN] 1GB pages supported\n");
    }
    
    /* Check for RDRAND/RDSEED */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (ecx & (1 << 30)) {
        serial_puts("[ZEN] RDRAND supported\n");
    }
    
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    if (ebx & (1 << 18)) {
        serial_puts("[ZEN] RDSEED supported\n");
    }
    if (ebx & (1 << 29)) {
        serial_puts("[ZEN] SHA extensions supported\n");
    }
    
    /* Initialize P-state driver (weak call) */
    extern int zen_pstate_init(void) __attribute__((weak));
    if (zen_pstate_init) {
        zen_pstate_init();
    }
    
    serial_puts("[ZEN] Platform initialization complete\n");
    return 0;
}

core_initcall(zen_platform_init); 