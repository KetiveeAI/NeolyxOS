/*
 * NeolyxOS ARM64 MMU Implementation
 * 
 * 4-level page tables with 4KB granule
 * 
 * Virtual Address Layout (48-bit):
 *   0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF: User space (TTBR0)
 *   0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF: Kernel space (TTBR1)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "arch/aarch64/arm64.h"
#include <stdint.h>

/* External serial for debug */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Page Table Storage ============ */

/* 4KB aligned page tables */
static uint64_t __attribute__((aligned(4096))) l0_table[512];  /* PGD */
static uint64_t __attribute__((aligned(4096))) l1_table[512];  /* PUD */
static uint64_t __attribute__((aligned(4096))) l2_table[512];  /* PMD */
static uint64_t __attribute__((aligned(4096))) l3_table[512];  /* PTE */

/* Additional L2 tables for more mappings */
static uint64_t __attribute__((aligned(4096))) l2_device[512]; /* Device memory */

/* ============ Helpers ============ */

static void nx_memset64(uint64_t *p, uint64_t val, size_t count) {
    while (count--) *p++ = val;
}

static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ MAIR Setup ============ */

static void arm64_setup_mair(void) {
    /*
     * MAIR_EL1 layout:
     *   Attr0: Device-nGnRnE  (0x00) - Strongly ordered device
     *   Attr1: Normal NC      (0x44) - Non-cacheable
     *   Attr2: Normal WT      (0xBB) - Write-through
     *   Attr3: Normal WB      (0xFF) - Write-back (default for RAM)
     */
    uint64_t mair = 
        ((uint64_t)ARM64_MAIR_DEVICE_nGnRnE << 0) |
        ((uint64_t)ARM64_MAIR_NORMAL_NC << 8) |
        ((uint64_t)ARM64_MAIR_NORMAL_WT << 16) |
        ((uint64_t)ARM64_MAIR_NORMAL_WB << 24);
    
#ifdef __aarch64__
    ARM64_WRITE_SYSREG(mair_el1, mair);
#endif
}

/* ============ TCR Setup ============ */

static void arm64_setup_tcr(void) {
    /*
     * TCR_EL1 configuration:
     *   - T0SZ = 16 (48-bit VA for TTBR0)
     *   - T1SZ = 16 (48-bit VA for TTBR1)
     *   - TG0 = 4KB granule for TTBR0
     *   - TG1 = 4KB granule for TTBR1
     *   - IPS = 48-bit PA
     *   - Inner/Outer shareable, WB cacheable
     */
    uint64_t tcr = 
        TCR_EL1_T0SZ(48) |
        TCR_EL1_T1SZ(48) |
        TCR_EL1_TG0_4KB |
        TCR_EL1_TG1_4KB |
        TCR_EL1_IPS_48BIT |
        (1ULL << 8) |   /* TTBR0 inner shareable */
        (1ULL << 10) |  /* TTBR0 outer shareable */
        (1ULL << 24) |  /* TTBR1 inner shareable */
        (1ULL << 26);   /* TTBR1 outer shareable */
    
#ifdef __aarch64__
    ARM64_WRITE_SYSREG(tcr_el1, tcr);
    ARM64_ISB();
#endif
}

/* ============ Identity Map First 1GB ============ */

static void arm64_identity_map(void) {
    /* Clear tables */
    nx_memset64(l0_table, 0, 512);
    nx_memset64(l1_table, 0, 512);
    nx_memset64(l2_table, 0, 512);
    
    /* L0[0] -> L1 table */
    l0_table[0] = ((uint64_t)l1_table) | ARM64_PTE_VALID | ARM64_PTE_TABLE;
    
    /* L1[0] -> L2 table (first 1GB) */
    l1_table[0] = ((uint64_t)l2_table) | ARM64_PTE_VALID | ARM64_PTE_TABLE;
    
    /* Map first 1GB as 2MB blocks (normal memory, WB cacheable) */
    for (int i = 0; i < 512; i++) {
        uint64_t pa = (uint64_t)i * ARM64_L2_SIZE;  /* 2MB blocks */
        
        /* Use block descriptors for 2MB mappings */
        l2_table[i] = pa | 
                      ARM64_PTE_VALID |
                      ARM64_PTE_BLOCK |        /* Block descriptor, not table */
                      ARM64_PTE_AF |
                      ARM64_PTE_SH_INNER |
                      ARM64_PTE_ATTR_IDX(ARM64_MAIR_IDX_NORMAL_WB) |
                      ARM64_PTE_AP_RW_EL1;
    }
    
    serial_puts("[ARM64] Identity mapped 0-1GB\n");
}

/* ============ Map Device Memory ============ */

static void arm64_map_devices(void) {
    nx_memset64(l2_device, 0, 512);
    
    /* L1[1] -> L2_device table (1GB-2GB range for devices) */
    l1_table[1] = ((uint64_t)l2_device) | ARM64_PTE_VALID | ARM64_PTE_TABLE;
    
    /* Map typical MMIO regions as device memory */
    /* QEMU virt machine GIC at 0x08000000 */
    int gic_idx = 0x08000000 / ARM64_L2_SIZE;
    l2_device[gic_idx] = 0x08000000 |
                         ARM64_PTE_VALID |
                         ARM64_PTE_BLOCK |
                         ARM64_PTE_AF |
                         ARM64_PTE_ATTR_IDX(ARM64_MAIR_IDX_DEVICE) |
                         ARM64_PTE_AP_RW_EL1 |
                         ARM64_PTE_PXN |
                         ARM64_PTE_UXN;
    
    /* UART at 0x09000000 */
    int uart_idx = 0x09000000 / ARM64_L2_SIZE;
    l2_device[uart_idx] = 0x09000000 |
                          ARM64_PTE_VALID |
                          ARM64_PTE_BLOCK |
                          ARM64_PTE_AF |
                          ARM64_PTE_ATTR_IDX(ARM64_MAIR_IDX_DEVICE) |
                          ARM64_PTE_AP_RW_EL1 |
                          ARM64_PTE_PXN |
                          ARM64_PTE_UXN;
    
    serial_puts("[ARM64] Mapped device memory\n");
}

/* ============ Kernel Higher-Half Map ============ */

static void arm64_kernel_map(void) {
    /*
     * Map kernel at 0xFFFF_0000_0000_0000 (higher half)
     * This uses TTBR1
     * 
     * For now, we'll use identity mapping for TTBR1 as well
     * In production, kernel would be at specific VA
     */
    
    /* TTBR1 shares L0 for simplicity in early boot */
    /* Real implementation would have separate tables */
    
    serial_puts("[ARM64] Kernel mapping configured\n");
}

/* ============ Public API ============ */

void arm64_mmu_early_init(void) {
    serial_puts("[ARM64] MMU early init\n");
    
    /* Set up memory attributes */
    arm64_setup_mair();
    
    /* Configure translation control */
    arm64_setup_tcr();
    
    /* Create identity mapping for first 1GB */
    arm64_identity_map();
    
    /* Map device MMIO regions */
    arm64_map_devices();
    
    /* Configure kernel mapping */
    arm64_kernel_map();
    
    /* Set TTBR0 and TTBR1 */
    arm64_set_ttbr0((uint64_t)l0_table);
    arm64_set_ttbr1((uint64_t)l0_table);  /* Same for now */
    
    /* Invalidate TLBs */
#ifdef __aarch64__
    ARM64_TLBI_ALL();
    ARM64_DSB();
    ARM64_ISB();
#endif
    
    serial_puts("[ARM64] Page tables ready, enabling MMU...\n");
    
    /* Enable MMU */
    arm64_mmu_enable();
    
    serial_puts("[ARM64] MMU enabled!\n");
}

void arm64_set_ttbr0(uint64_t addr) {
#ifdef __aarch64__
    ARM64_WRITE_SYSREG(ttbr0_el1, addr);
    ARM64_ISB();
#else
    (void)addr;
#endif
}

void arm64_set_ttbr1(uint64_t addr) {
#ifdef __aarch64__
    ARM64_WRITE_SYSREG(ttbr1_el1, addr);
    ARM64_ISB();
#else
    (void)addr;
#endif
}

void arm64_mmu_enable(void) {
#ifdef __aarch64__
    /* Read current SCTLR */
    uint64_t sctlr = ARM64_READ_SYSREG(sctlr_el1);
    
    /* Enable MMU, caches */
    sctlr |= SCTLR_EL1_M;   /* MMU enable */
    sctlr |= SCTLR_EL1_C;   /* Data cache enable */
    sctlr |= SCTLR_EL1_I;   /* Instruction cache enable */
    
    /* Write back */
    ARM64_WRITE_SYSREG(sctlr_el1, sctlr);
    ARM64_ISB();
#endif
}

int arm64_map_page(uint64_t va, uint64_t pa, uint64_t attr) {
    /* Get indices for each level */
    int l0_idx = (va >> ARM64_L0_SHIFT) & 0x1FF;
    int l1_idx = (va >> ARM64_L1_SHIFT) & 0x1FF;
    int l2_idx = (va >> ARM64_L2_SHIFT) & 0x1FF;
    int l3_idx = (va >> ARM64_L3_SHIFT) & 0x1FF;
    
    /* Check L0 entry */
    if (!(l0_table[l0_idx] & ARM64_PTE_VALID)) {
        return -1;  /* Need to allocate L1 table */
    }
    
    /* Check L1 entry */
    uint64_t *l1 = (uint64_t*)(l0_table[l0_idx] & ARM64_PTE_ADDR_MASK);
    if (!(l1[l1_idx] & ARM64_PTE_VALID)) {
        return -2;  /* Need to allocate L2 table */
    }
    
    /* Check L2 entry */
    uint64_t *l2 = (uint64_t*)(l1[l1_idx] & ARM64_PTE_ADDR_MASK);
    if (!(l2[l2_idx] & ARM64_PTE_VALID)) {
        return -3;  /* Need to allocate L3 table */
    }
    
    /* Get L3 table */
    uint64_t *l3 = (uint64_t*)(l2[l2_idx] & ARM64_PTE_ADDR_MASK);
    
    /* Set page entry */
    l3[l3_idx] = (pa & ARM64_PTE_ADDR_MASK) | attr | ARM64_PTE_VALID | ARM64_PTE_PAGE;
    
    /* Invalidate TLB for this VA */
#ifdef __aarch64__
    ARM64_TLBI_VA(va);
    ARM64_DSB();
    ARM64_ISB();
#endif
    
    return 0;
}

int arm64_unmap_page(uint64_t va) {
    int l0_idx = (va >> ARM64_L0_SHIFT) & 0x1FF;
    int l1_idx = (va >> ARM64_L1_SHIFT) & 0x1FF;
    int l2_idx = (va >> ARM64_L2_SHIFT) & 0x1FF;
    int l3_idx = (va >> ARM64_L3_SHIFT) & 0x1FF;
    
    if (!(l0_table[l0_idx] & ARM64_PTE_VALID)) return -1;
    
    uint64_t *l1 = (uint64_t*)(l0_table[l0_idx] & ARM64_PTE_ADDR_MASK);
    if (!(l1[l1_idx] & ARM64_PTE_VALID)) return -2;
    
    uint64_t *l2 = (uint64_t*)(l1[l1_idx] & ARM64_PTE_ADDR_MASK);
    if (!(l2[l2_idx] & ARM64_PTE_VALID)) return -3;
    
    uint64_t *l3 = (uint64_t*)(l2[l2_idx] & ARM64_PTE_ADDR_MASK);
    
    l3[l3_idx] = 0;
    
#ifdef __aarch64__
    ARM64_TLBI_VA(va);
    ARM64_DSB();
    ARM64_ISB();
#endif
    
    return 0;
}

uint64_t arm64_va_to_pa(uint64_t va) {
    /* Use AT instruction for hardware translation */
#ifdef __aarch64__
    __asm__ volatile("at s1e1r, %0" : : "r"(va));
    ARM64_ISB();
    
    uint64_t par = ARM64_READ_SYSREG(par_el1);
    
    if (par & 1) {
        return 0;  /* Translation failed */
    }
    
    return (par & 0x0000FFFFFFFFF000ULL) | (va & 0xFFF);
#else
    return va;  /* Identity map on non-ARM */
#endif
}
