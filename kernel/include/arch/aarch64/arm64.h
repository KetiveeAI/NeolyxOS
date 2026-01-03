/*
 * NeolyxOS ARM64 Architecture Header
 * 
 * AArch64 (ARM 64-bit) Architecture Support
 * 
 * Target: ARMv8-A and later (Cortex-A53+, Apple Silicon, etc.)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef ARCH_ARM64_H
#define ARCH_ARM64_H

#include <stdint.h>
#include <stddef.h>

/* ============ Architecture Detection ============ */

#if !defined(__aarch64__) && !defined(_M_ARM64)
    #warning "This header is for ARM64 only"
#endif

/* ============ Register Sizes ============ */

#define ARM64_GPR_COUNT     31      /* X0-X30 general purpose */
#define ARM64_FPR_COUNT     32      /* V0-V31 SIMD/FP */

/* ============ Exception Levels ============ */

typedef enum {
    ARM64_EL0 = 0,      /* User mode (applications) */
    ARM64_EL1 = 1,      /* Kernel mode (OS) */
    ARM64_EL2 = 2,      /* Hypervisor */
    ARM64_EL3 = 3,      /* Secure monitor (TrustZone) */
} arm64_el_t;

/* ============ Page Sizes ============ */

typedef enum {
    ARM64_PAGE_4KB   = 12,  /* 4KB granule (most common) */
    ARM64_PAGE_16KB  = 14,  /* 16KB granule (Apple Silicon) */
    ARM64_PAGE_64KB  = 16,  /* 64KB granule */
} arm64_page_granule_t;

#define ARM64_PAGE_SIZE         4096
#define ARM64_PAGE_SHIFT        12

/* ============ Page Table Levels (4KB granule) ============ */

/*
 * Virtual Address Format (48-bit, 4KB pages):
 * 
 * [63:48] - Sign extension (must match bit 47)
 * [47:39] - Level 0 index (PGD)
 * [38:30] - Level 1 index (PUD) 
 * [29:21] - Level 2 index (PMD)
 * [20:12] - Level 3 index (PTE)
 * [11:0]  - Page offset
 */

#define ARM64_VA_BITS           48
#define ARM64_ENTRIES_PER_TABLE 512  /* 2^9 = 512 entries per level */

#define ARM64_L0_SHIFT          39
#define ARM64_L1_SHIFT          30
#define ARM64_L2_SHIFT          21
#define ARM64_L3_SHIFT          12

#define ARM64_L0_SIZE           (1ULL << ARM64_L0_SHIFT)  /* 512GB */
#define ARM64_L1_SIZE           (1ULL << ARM64_L1_SHIFT)  /* 1GB */
#define ARM64_L2_SIZE           (1ULL << ARM64_L2_SHIFT)  /* 2MB */
#define ARM64_L3_SIZE           (1ULL << ARM64_L3_SHIFT)  /* 4KB */

/* ============ Page Table Entry Attributes ============ */

/* Entry type (bits [1:0]) */
#define ARM64_PTE_VALID         (1ULL << 0)
#define ARM64_PTE_TABLE         (1ULL << 1)  /* Points to next level table */
#define ARM64_PTE_BLOCK         (0ULL << 1)  /* Block descriptor (L1/L2 only) */
#define ARM64_PTE_PAGE          (1ULL << 1)  /* Page descriptor (L3 only) */

/* Lower attributes (bits [11:2]) */
#define ARM64_PTE_ATTR_IDX(n)   ((n) << 2)   /* MAIR index [4:2] */
#define ARM64_PTE_NS            (1ULL << 5)  /* Non-secure */
#define ARM64_PTE_AP_RW_EL1     (0ULL << 6)  /* R/W at EL1, no access EL0 */
#define ARM64_PTE_AP_RW_ALL     (1ULL << 6)  /* R/W at EL1 and EL0 */
#define ARM64_PTE_AP_RO_EL1     (2ULL << 6)  /* R/O at EL1, no access EL0 */
#define ARM64_PTE_AP_RO_ALL     (3ULL << 6)  /* R/O at EL1 and EL0 */
#define ARM64_PTE_SH_NON        (0ULL << 8)  /* Non-shareable */
#define ARM64_PTE_SH_OUTER      (2ULL << 8)  /* Outer shareable */
#define ARM64_PTE_SH_INNER      (3ULL << 8)  /* Inner shareable */
#define ARM64_PTE_AF            (1ULL << 10) /* Access flag */

/* Upper attributes (bits [63:51]) */
#define ARM64_PTE_PXN           (1ULL << 53) /* Privileged execute never */
#define ARM64_PTE_UXN           (1ULL << 54) /* User execute never (XN) */

/* Address mask */
#define ARM64_PTE_ADDR_MASK     0x0000FFFFFFFFF000ULL

/* ============ MAIR (Memory Attribute Indirection Register) ============ */

/* Common attribute encodings */
#define ARM64_MAIR_DEVICE_nGnRnE    0x00  /* Device, non-gathering, non-reordering, non-early ack */
#define ARM64_MAIR_DEVICE_nGnRE     0x04  /* Device, non-gathering, non-reordering, early ack */
#define ARM64_MAIR_NORMAL_NC        0x44  /* Normal, non-cacheable */
#define ARM64_MAIR_NORMAL_WT        0xBB  /* Normal, write-through */
#define ARM64_MAIR_NORMAL_WB        0xFF  /* Normal, write-back */

/* MAIR index assignments */
#define ARM64_MAIR_IDX_DEVICE       0
#define ARM64_MAIR_IDX_NORMAL_NC    1
#define ARM64_MAIR_IDX_NORMAL_WT    2
#define ARM64_MAIR_IDX_NORMAL_WB    3

/* ============ System Registers ============ */

/* SCTLR_EL1 bits */
#define SCTLR_EL1_M             (1 << 0)    /* MMU enable */
#define SCTLR_EL1_A             (1 << 1)    /* Alignment check */
#define SCTLR_EL1_C             (1 << 2)    /* Data cache enable */
#define SCTLR_EL1_SA            (1 << 3)    /* Stack alignment check */
#define SCTLR_EL1_I             (1 << 12)   /* Instruction cache enable */
#define SCTLR_EL1_WXN           (1 << 19)   /* Write execute never */

/* TCR_EL1 bits */
#define TCR_EL1_T0SZ(n)         ((64 - (n)) & 0x3F)
#define TCR_EL1_T1SZ(n)         (((64 - (n)) & 0x3F) << 16)
#define TCR_EL1_TG0_4KB         (0ULL << 14)
#define TCR_EL1_TG0_64KB        (1ULL << 14)
#define TCR_EL1_TG0_16KB        (2ULL << 14)
#define TCR_EL1_TG1_16KB        (1ULL << 30)
#define TCR_EL1_TG1_4KB         (2ULL << 30)
#define TCR_EL1_TG1_64KB        (3ULL << 30)
#define TCR_EL1_IPS_32BIT       (0ULL << 32)
#define TCR_EL1_IPS_36BIT       (1ULL << 32)
#define TCR_EL1_IPS_40BIT       (2ULL << 32)
#define TCR_EL1_IPS_48BIT       (5ULL << 32)

/* ============ CPU Context (for context switching) ============ */

typedef struct {
    /* General purpose registers X0-X30 */
    uint64_t x[31];
    
    /* Stack pointer (SP_EL0 for user, SP_EL1 for kernel) */
    uint64_t sp;
    
    /* Program counter */
    uint64_t pc;
    
    /* Processor state */
    uint64_t pstate;    /* SPSR_EL1 */
    
    /* Exception link register */
    uint64_t elr;       /* ELR_EL1 */
    
    /* Thread ID registers */
    uint64_t tpidr_el0; /* Thread ID for EL0 */
    uint64_t tpidr_el1; /* Thread ID for EL1 */
    
    /* SIMD/FP registers (optional, for full context) */
    /* __uint128_t v[32]; */
    uint64_t fpcr;      /* FP control register */
    uint64_t fpsr;      /* FP status register */
} arm64_context_t;

/* ============ Exception Vector Table ============ */

/*
 * Exception vectors at VBAR_EL1 + offset:
 * 
 * Offset   Exception           Source
 * 0x000    Synchronous         Current EL with SP_EL0
 * 0x080    IRQ/vIRQ            Current EL with SP_EL0
 * 0x100    FIQ/vFIQ            Current EL with SP_EL0
 * 0x180    SError/vSError      Current EL with SP_EL0
 * 0x200    Synchronous         Current EL with SP_ELx
 * 0x280    IRQ/vIRQ            Current EL with SP_ELx
 * 0x300    FIQ/vFIQ            Current EL with SP_ELx
 * 0x380    SError/vSError      Current EL with SP_ELx
 * 0x400    Synchronous         Lower EL using AArch64
 * 0x480    IRQ/vIRQ            Lower EL using AArch64
 * 0x500    FIQ/vFIQ            Lower EL using AArch64
 * 0x580    SError/vSError      Lower EL using AArch64
 * 0x600    Synchronous         Lower EL using AArch32
 * 0x680    IRQ/vIRQ            Lower EL using AArch32
 * 0x700    FIQ/vFIQ            Lower EL using AArch32
 * 0x780    SError/vSError      Lower EL using AArch32
 */

#define ARM64_VECTOR_SYNC_EL1_SP0   0x000
#define ARM64_VECTOR_IRQ_EL1_SP0    0x080
#define ARM64_VECTOR_FIQ_EL1_SP0    0x100
#define ARM64_VECTOR_SERR_EL1_SP0   0x180
#define ARM64_VECTOR_SYNC_EL1_SPX   0x200
#define ARM64_VECTOR_IRQ_EL1_SPX    0x280
#define ARM64_VECTOR_FIQ_EL1_SPX    0x300
#define ARM64_VECTOR_SERR_EL1_SPX   0x380
#define ARM64_VECTOR_SYNC_EL0_64    0x400
#define ARM64_VECTOR_IRQ_EL0_64     0x480
#define ARM64_VECTOR_FIQ_EL0_64     0x500
#define ARM64_VECTOR_SERR_EL0_64    0x580

/* ============ GIC (Generic Interrupt Controller) ============ */

/* GICv2/v3 common */
#define GIC_DIST_BASE           0x08000000  /* Typical QEMU virt base */
#define GIC_CPU_BASE            0x08010000
#define GIC_REDIST_BASE         0x080A0000  /* GICv3 redistributor */

/* Distributor registers */
#define GICD_CTLR               0x0000
#define GICD_TYPER              0x0004
#define GICD_ISENABLER(n)       (0x0100 + ((n) * 4))
#define GICD_ICENABLER(n)       (0x0180 + ((n) * 4))
#define GICD_IPRIORITYR(n)      (0x0400 + ((n) * 4))
#define GICD_ITARGETSR(n)       (0x0800 + ((n) * 4))
#define GICD_ICFGR(n)           (0x0C00 + ((n) * 4))

/* CPU interface registers (GICv2) */
#define GICC_CTLR               0x0000
#define GICC_PMR                0x0004
#define GICC_IAR                0x000C
#define GICC_EOIR               0x0010

/* ============ Inline Assembly Helpers ============ */

#ifdef __aarch64__

/* Read system register */
#define ARM64_READ_SYSREG(reg) ({ \
    uint64_t __val; \
    __asm__ volatile("mrs %0, " #reg : "=r"(__val)); \
    __val; \
})

/* Write system register */
#define ARM64_WRITE_SYSREG(reg, val) do { \
    uint64_t __val = (val); \
    __asm__ volatile("msr " #reg ", %0" : : "r"(__val)); \
} while (0)

/* Data/Instruction barrier */
#define ARM64_DSB()     __asm__ volatile("dsb sy" ::: "memory")
#define ARM64_DMB()     __asm__ volatile("dmb sy" ::: "memory")
#define ARM64_ISB()     __asm__ volatile("isb" ::: "memory")

/* TLB invalidate */
#define ARM64_TLBI_ALL()        __asm__ volatile("tlbi vmalle1" ::: "memory")
#define ARM64_TLBI_VA(va)       __asm__ volatile("tlbi vale1, %0" : : "r"(va >> 12))

/* Cache operations */
#define ARM64_IC_IALLU()        __asm__ volatile("ic iallu" ::: "memory")
#define ARM64_DC_CIVAC(addr)    __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory")

/* Wait for interrupt */
#define ARM64_WFI()             __asm__ volatile("wfi")
#define ARM64_WFE()             __asm__ volatile("wfe")
#define ARM64_SEV()             __asm__ volatile("sev")

/* Get current exception level */
static inline uint32_t arm64_current_el(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(val));
    return (val >> 2) & 0x3;
}

/* Enable/disable interrupts */
static inline void arm64_enable_irq(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline void arm64_disable_irq(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

static inline uint64_t arm64_save_irq(void) {
    uint64_t flags;
    __asm__ volatile("mrs %0, daif" : "=r"(flags));
    __asm__ volatile("msr daifset, #2" ::: "memory");
    return flags;
}

static inline void arm64_restore_irq(uint64_t flags) {
    __asm__ volatile("msr daif, %0" : : "r"(flags) : "memory");
}

#endif /* __aarch64__ */

/* ============ Function Declarations ============ */

/* Boot */
void arm64_boot_init(void);
void arm64_el_setup(void);

/* MMU */
void arm64_mmu_init(void);
void arm64_mmu_enable(void);
void arm64_set_ttbr0(uint64_t addr);
void arm64_set_ttbr1(uint64_t addr);

/* Page tables */
int arm64_map_page(uint64_t va, uint64_t pa, uint64_t attr);
int arm64_unmap_page(uint64_t va);
uint64_t arm64_va_to_pa(uint64_t va);

/* Exceptions */
void arm64_exception_init(void);
void arm64_set_vbar(uint64_t addr);

/* GIC */
void arm64_gic_init(void);
void arm64_gic_enable_irq(uint32_t irq);
void arm64_gic_disable_irq(uint32_t irq);
void arm64_gic_eoi(uint32_t irq);

/* Context switch */
void arm64_context_switch(arm64_context_t *old, arm64_context_t *new);

#endif /* ARCH_ARM64_H */
