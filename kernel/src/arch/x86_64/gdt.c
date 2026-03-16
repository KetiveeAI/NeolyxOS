/*
 * NeolyxOS GDT and TSS Implementation
 * 
 * Global Descriptor Table with proper TSS for Ring 3 support.
 * x86_64 requires a 16-byte TSS descriptor in GDT.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);

/* Memory functions */
extern void *memset(void *s, int c, uint64_t n);

/* ============ GDT Structures ============ */

/* Standard GDT Entry (8 bytes) */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

/* Extended GDT Entry for TSS (16 bytes in x86_64) */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;    /* Upper 32 bits of base (x86_64) */
    uint32_t reserved;
} __attribute__((packed)) gdt_tss_entry_t;

/* GDT Pointer for LGDT instruction */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

/* ============ TSS Structure (x86_64) ============ */

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          /* Kernel stack pointer (Ring 0) */
    uint64_t rsp1;          /* Ring 1 stack (not used) */
    uint64_t rsp2;          /* Ring 2 stack (not used) */
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt Stack Table 1 */
    uint64_t ist2;          /* Interrupt Stack Table 2 */
    uint64_t ist3;          /* Interrupt Stack Table 3 */
    uint64_t ist4;          /* Interrupt Stack Table 4 */
    uint64_t ist5;          /* Interrupt Stack Table 5 */
    uint64_t ist6;          /* Interrupt Stack Table 6 */
    uint64_t ist7;          /* Interrupt Stack Table 7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O Permission Bitmap offset */
} __attribute__((packed)) tss64_t;

/* ============ GDT Layout ============ */

/*
 * GDT Entry Layout (SYSRET compatible):
 *
 * SYSRET in 64-bit mode calculates:
 *   CS = (STAR[63:48] + 16) | 3
 *   SS = (STAR[63:48] + 8) | 3
 *
 * With STAR[63:48] = 0x18:
 *   SS = 0x23, CS = 0x2B
 *
 *   0: Null descriptor
 *   1: Kernel Code (0x08)
 *   2: Kernel Data (0x10)
 *   3: User Code 32-bit placeholder (0x18)
 *   4: User Data   (0x20 | 3 = 0x23)
 *   5: User Code 64-bit (0x28 | 3 = 0x2B)
 *   6-7: TSS descriptor (16 bytes, selector 0x30)
 *
 * Total: 8 entries
 */

#define GDT_ENTRIES 8

/* ============ Global State ============ */

/* GDT table (aligned for performance) */
static gdt_entry_t gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static gdt_ptr_t gdt_ptr;

/* TSS structure (aligned) */
static tss64_t tss __attribute__((aligned(16)));

/* Kernel stack for TSS (16 KB) */
#define KERNEL_STACK_SIZE 16384
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

/* IST1 stack for exception handling (8 KB) - separate from main kernel stack */
#define IST1_STACK_SIZE 8192
static uint8_t ist1_stack[IST1_STACK_SIZE] __attribute__((aligned(16)));

/* Global kernel stack pointer for syscall_entry (assembly accessible) */
uint64_t g_kernel_rsp0 = 0;

/* ============ Helper Functions ============ */

static void gdt_set_entry(int num, uint32_t base, uint32_t limit, 
                          uint8_t access, uint8_t gran) {
    gdt[num].limit_low    = (limit & 0xFFFF);
    gdt[num].base_low     = (base & 0xFFFF);
    gdt[num].base_middle  = (base >> 16) & 0xFF;
    gdt[num].access       = access;
    gdt[num].granularity  = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].base_high    = (base >> 24) & 0xFF;
}

static void gdt_set_tss(int num, uint64_t base, uint32_t limit) {
    /* TSS descriptor is 16 bytes in x86_64 - spans 2 GDT entries */
    gdt_tss_entry_t *tss_entry = (gdt_tss_entry_t *)&gdt[num];
    
    tss_entry->limit_low    = (limit & 0xFFFF);
    tss_entry->base_low     = (base & 0xFFFF);
    tss_entry->base_middle  = (base >> 16) & 0xFF;
    tss_entry->access       = 0x89;  /* Present, TSS 64-bit Available */
    tss_entry->granularity  = ((limit >> 16) & 0x0F);
    tss_entry->base_high    = (base >> 24) & 0xFF;
    tss_entry->base_upper   = (base >> 32) & 0xFFFFFFFF;
    tss_entry->reserved     = 0;
}

/* ============ TSS Functions ============ */

void tss_init(void) {
    /* Clear TSS */
    memset(&tss, 0, sizeof(tss64_t));
    
    /* Set kernel stack pointer for Ring 0 */
    /* RSP0 is used when transitioning from Ring 3 to Ring 0 on interrupt */
    tss.rsp0 = (uint64_t)&kernel_stack[KERNEL_STACK_SIZE - 16];
    
    /* Set IST1 for exception handling (page faults, GPF, etc.)
     * This provides a dedicated stack for exceptions, preventing triple faults
     * when RSP0 might be in an inconsistent state during userspace execution */
    tss.ist1 = (uint64_t)&ist1_stack[IST1_STACK_SIZE - 16];
    
    /* Also set global for syscall_entry (assembly) */
    g_kernel_rsp0 = tss.rsp0;
    
    /* Set I/O Permission Bitmap offset to end of TSS (no IOPB) */
    tss.iopb_offset = sizeof(tss64_t);
    
    serial_puts("[TSS] Initialized with RSP0 = ");
    /* Print address (simplified) */
    serial_puts("0x");
    const char *hex = "0123456789ABCDEF";
    uint64_t addr = tss.rsp0;
    for (int i = 60; i >= 0; i -= 4) {
        char c = hex[(addr >> i) & 0xF];
        extern void serial_putc(char c);
        serial_putc(c);
    }
    serial_puts("\n");
    serial_puts("[TSS] IST1 = 0x");
    addr = tss.ist1;
    for (int i = 60; i >= 0; i -= 4) {
        char c = hex[(addr >> i) & 0xF];
        extern void serial_putc(char c);
        serial_putc(c);
    }
    serial_puts("\n");
}

void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}

uint64_t tss_get_kernel_stack(void) {
    return tss.rsp0;
}

/* ============ GDT Functions ============ */

/* External assembly function to load GDT */
extern void gdt_load(uint64_t gdt_ptr);

void gdt_init(void) {
    serial_puts("[GDT] Initializing Global Descriptor Table...\n");
    
    /* Initialize TSS first */
    tss_init();
    
    /* Set GDT pointer */
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    
    /* Entry 0: Null descriptor (required) */
    gdt_set_entry(0, 0, 0, 0, 0);
    
    /* Entry 1: Kernel Code Segment (selector 0x08)
     * Access: Present | Ring 0 | Code | Executable | Readable
     * Granularity: Long mode (64-bit) */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
    
    /* Entry 2: Kernel Data Segment (selector 0x10)
     * Access: Present | Ring 0 | Data | Writable */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    
    /* Entry 3: User Code 32-bit placeholder (selector 0x18)
     * Not used directly, but SYSRET math requires this position.
     * Configured as Ring 3 code for safety. */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);  /* 32-bit compat code */
    
    /* Entry 4: User Data Segment (selector 0x20, DPL=3 → 0x23)
     * SYSRET SS = (0x18 + 8) | 3 = 0x23
     * Access: Present | Ring 3 | Data | Writable */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    
    /* Entry 5: User Code 64-bit Segment (selector 0x28, DPL=3 → 0x2B)
     * SYSRET CS = (0x18 + 16) | 3 = 0x2B
     * Access: Present | Ring 3 | Code | Executable | Readable
     * Granularity: Long mode (64-bit) */
    gdt_set_entry(5, 0, 0xFFFFFFFF, 0xFA, 0xAF);
    
    /* Entry 6-7: TSS Descriptor (16 bytes, selector 0x30)
     * TSS base points to our tss structure */
    gdt_set_tss(6, (uint64_t)&tss, sizeof(tss64_t) - 1);
    
    /* Load new GDT */
    gdt_load((uint64_t)&gdt_ptr);
    
    /* Load Task Register with TSS selector (now at 0x30) */
    __asm__ volatile("mov $0x30, %%ax; ltr %%ax" : : : "ax");
    
    serial_puts("[GDT] Loaded with TSS at selector 0x30\n");
    serial_puts("[GDT] Ring 3 segments: CS=0x2B, DS=0x23\n");
}

/* Get TSS pointer (for process switching) */
tss64_t *tss_get(void) {
    return &tss;
}