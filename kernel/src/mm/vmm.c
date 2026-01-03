/*
 * NeolyxOS Virtual Memory Manager Implementation
 * 
 * 4-level paging (PML4 → PDP → PD → PT) for x86_64.
 * Integrates with PMM for dynamic page table allocation.
 * Higher-half kernel mapping support.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "mm/vmm.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Memory operations */
static void vmm_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

/* ============ Constants ============ */

#define PML4_ENTRIES        512
#define PDP_ENTRIES         512
#define PD_ENTRIES          512
#define PT_ENTRIES_COUNT    512

/* Page table indices from virtual address */
#define PML4_INDEX(virt)    (((virt) >> 39) & 0x1FF)
#define PDP_INDEX(virt)     (((virt) >> 30) & 0x1FF)
#define PD_INDEX(virt)      (((virt) >> 21) & 0x1FF)
#define PT_INDEX(virt)      (((virt) >> 12) & 0x1FF)

/* Address masks */
#define PAGE_FRAME_MASK     0x000FFFFFFFFFF000ULL
#define PAGE_FLAGS_MASK     0xFFF0000000000FFFULL

/* ============ VMM State ============ */

/* Root page table (PML4) physical address */
static uint64_t *vmm_pml4 = 0;

/* Initialization flag */
static int vmm_initialized = 0;

/* Virtual address space allocator */
static uint64_t vmm_next_vaddr = 0xFFFF900000000000ULL;  /* After kernel space */

/* ============ Helper Functions ============ */

static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/**
 * Allocate a zeroed physical page for page tables
 */
static uint64_t vmm_alloc_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        serial_puts("[VMM] ERROR: Cannot allocate page table!\n");
        return 0;
    }
    /* PMM already zeros pages */
    return phys;
}

/**
 * Get or create page table entry
 * 
 * If entry doesn't exist and create=1, allocates a new table.
 */
static uint64_t *vmm_get_or_create_entry(uint64_t *table, int index, int create) {
    if (table[index] & PAGE_PRESENT) {
        /* Entry exists, return pointer to next level */
        return (uint64_t*)(table[index] & PAGE_FRAME_MASK);
    }
    
    if (!create) {
        return 0;
    }
    
    /* Allocate new table */
    uint64_t new_table = vmm_alloc_table();
    if (new_table == 0) {
        return 0;
    }
    
    /* Set entry with present + writable flags */
    table[index] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
    
    return (uint64_t*)new_table;
}

/* ============ VMM Implementation ============ */

void vmm_init(void) {
    serial_puts("[VMM] Initializing virtual memory manager...\n");
    
    /* Check PMM is ready */
    if (!pmm_is_initialized()) {
        serial_puts("[VMM] ERROR: PMM not initialized!\n");
        return;
    }
    
    /* Allocate PML4 from PMM */
    uint64_t pml4_phys = vmm_alloc_table();
    if (pml4_phys == 0) {
        serial_puts("[VMM] ERROR: Cannot allocate PML4!\n");
        return;
    }
    
    vmm_pml4 = (uint64_t*)pml4_phys;
    
    /* Identity map first 4GB for device access and early boot compatibility */
    serial_puts("[VMM] Identity mapping first 4GB...\n");
    
    for (uint64_t addr = 0; addr < 0x100000000ULL; addr += 0x200000) {
        /* Use 2MB huge pages for efficiency */
        uint64_t virt = addr;
        uint64_t pml4_idx = PML4_INDEX(virt);
        uint64_t pdp_idx = PDP_INDEX(virt);
        uint64_t pd_idx = PD_INDEX(virt);
        
        /* Ensure PDP exists */
        uint64_t *pdp = vmm_get_or_create_entry(vmm_pml4, pml4_idx, 1);
        if (!pdp) continue;
        
        /* Ensure PD exists */
        uint64_t *pd = vmm_get_or_create_entry(pdp, pdp_idx, 1);
        if (!pd) continue;
        
        /* Map 2MB page (PAGE_HUGE flag) */
        pd[pd_idx] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    }
    
    /* Map kernel to higher half (mirror mapping) */
    serial_puts("[VMM] Mapping kernel to higher half...\n");
    
    /* Map first 16MB of physical memory to KERNEL_BASE */
    for (uint64_t offset = 0; offset < 0x1000000; offset += PAGE_SIZE) {
        vmm_map(KERNEL_BASE + offset, offset, PAGE_PRESENT | PAGE_WRITABLE);
    }
    
    /* Load new page tables */
    serial_puts("[VMM] Loading PML4 at ");
    serial_hex64(pml4_phys);
    serial_puts("\n");
    
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    
    vmm_initialized = 1;
    serial_puts("[VMM] Virtual memory manager ready\n");
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!vmm_pml4) {
        return -1;
    }
    
    /* Ensure alignment */
    virt &= PAGE_FRAME_MASK;
    phys &= PAGE_FRAME_MASK;
    
    /* Get indices */
    int pml4_idx = PML4_INDEX(virt);
    int pdp_idx = PDP_INDEX(virt);
    int pd_idx = PD_INDEX(virt);
    int pt_idx = PT_INDEX(virt);
    
    /* Walk/create page tables */
    uint64_t *pdp = vmm_get_or_create_entry(vmm_pml4, pml4_idx, 1);
    if (!pdp) return -1;
    
    uint64_t *pd = vmm_get_or_create_entry(pdp, pdp_idx, 1);
    if (!pd) return -1;
    
    uint64_t *pt = vmm_get_or_create_entry(pd, pd_idx, 1);
    if (!pt) return -1;
    
    /* Set page table entry */
    pt[pt_idx] = phys | (flags & PAGE_FLAGS_MASK) | PAGE_PRESENT;
    
    /* Invalidate TLB */
    vmm_flush_tlb(virt);
    
    return 0;
}

int vmm_map_range(uint64_t virt, uint64_t phys, size_t size, uint64_t flags) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        if (vmm_map(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) != 0) {
            /* Rollback on failure */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(virt + j * PAGE_SIZE);
            }
            return -1;
        }
    }
    
    return 0;
}

void vmm_unmap(uint64_t virt) {
    if (!vmm_pml4) return;
    
    virt &= PAGE_FRAME_MASK;
    
    int pml4_idx = PML4_INDEX(virt);
    int pdp_idx = PDP_INDEX(virt);
    int pd_idx = PD_INDEX(virt);
    int pt_idx = PT_INDEX(virt);
    
    /* Walk page tables (don't create) */
    if (!(vmm_pml4[pml4_idx] & PAGE_PRESENT)) return;
    uint64_t *pdp = (uint64_t*)(vmm_pml4[pml4_idx] & PAGE_FRAME_MASK);
    
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return;
    uint64_t *pd = (uint64_t*)(pdp[pdp_idx] & PAGE_FRAME_MASK);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return;
    uint64_t *pt = (uint64_t*)(pd[pd_idx] & PAGE_FRAME_MASK);
    
    /* Clear entry */
    pt[pt_idx] = 0;
    
    vmm_flush_tlb(virt);
}

uint64_t vmm_get_physical(uint64_t virt) {
    if (!vmm_pml4) return 0;
    
    int pml4_idx = PML4_INDEX(virt);
    int pdp_idx = PDP_INDEX(virt);
    int pd_idx = PD_INDEX(virt);
    int pt_idx = PT_INDEX(virt);
    
    /* Walk page tables */
    if (!(vmm_pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pdp = (uint64_t*)(vmm_pml4[pml4_idx] & PAGE_FRAME_MASK);
    
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t*)(pdp[pdp_idx] & PAGE_FRAME_MASK);
    
    /* Check for 2MB huge page */
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & PAGE_FRAME_MASK) | (virt & 0x1FFFFF);
    }
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pt = (uint64_t*)(pd[pd_idx] & PAGE_FRAME_MASK);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    
    return (pt[pt_idx] & PAGE_FRAME_MASK) | (virt & 0xFFF);
}

uint64_t vmm_alloc_pages(size_t pages) {
    if (!vmm_initialized || pages == 0) {
        return 0;
    }
    
    /* Get virtual address from allocator */
    uint64_t virt = vmm_next_vaddr;
    vmm_next_vaddr += pages * PAGE_SIZE;
    
    /* Allocate physical pages and map */
    for (size_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            /* Out of physical memory - unmap what we did */
            for (size_t j = 0; j < i; j++) {
                uint64_t addr = virt + j * PAGE_SIZE;
                uint64_t p = vmm_get_physical(addr);
                vmm_unmap(addr);
                if (p) pmm_free_page(p);
            }
            return 0;
        }
        
        if (vmm_map(virt + i * PAGE_SIZE, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            pmm_free_page(phys);
            /* Cleanup previous */
            for (size_t j = 0; j < i; j++) {
                uint64_t addr = virt + j * PAGE_SIZE;
                uint64_t p = vmm_get_physical(addr);
                vmm_unmap(addr);
                if (p) pmm_free_page(p);
            }
            return 0;
        }
    }
    
    return virt;
}

void vmm_free_pages(uint64_t virt, size_t pages) {
    if (!vmm_initialized) return;
    
    for (size_t i = 0; i < pages; i++) {
        uint64_t addr = virt + i * PAGE_SIZE;
        uint64_t phys = vmm_get_physical(addr);
        vmm_unmap(addr);
        if (phys) {
            pmm_free_page(phys);
        }
    }
}

void vmm_flush_tlb(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_flush_tlb_all(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/* ============ Extended API for VM Support ============ */

/**
 * Create a new address space (for new process or VM)
 * 
 * @return Physical address of new PML4, or 0 on failure
 */
uint64_t vmm_create_address_space(void) {
    uint64_t new_pml4 = vmm_alloc_table();
    if (new_pml4 == 0) return 0;
    
    uint64_t *pml4 = (uint64_t*)new_pml4;
    
    /* Copy kernel mappings (upper half) to new address space */
    /* Entries 256-511 are kernel space */
    for (int i = 256; i < 512; i++) {
        pml4[i] = vmm_pml4[i];
    }
    
    return new_pml4;
}

/**
 * Switch to a different address space
 * 
 * @param pml4_phys Physical address of PML4
 */
void vmm_switch_address_space(uint64_t pml4_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

/**
 * Get current address space (CR3)
 */
uint64_t vmm_get_current_address_space(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/**
 * Map virtual memory into a specific address space
 */
int vmm_map_in_space(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = (uint64_t*)pml4_phys;
    
    virt &= PAGE_FRAME_MASK;
    phys &= PAGE_FRAME_MASK;
    
    int pml4_idx = PML4_INDEX(virt);
    int pdp_idx = PDP_INDEX(virt);
    int pd_idx = PD_INDEX(virt);
    int pt_idx = PT_INDEX(virt);
    
    /* Walk/create in target address space */
    uint64_t *pdp = vmm_get_or_create_entry(pml4, pml4_idx, 1);
    if (!pdp) return -1;
    
    uint64_t *pd = vmm_get_or_create_entry(pdp, pdp_idx, 1);
    if (!pd) return -1;
    
    uint64_t *pt = vmm_get_or_create_entry(pd, pd_idx, 1);
    if (!pt) return -1;
    
    pt[pt_idx] = phys | (flags & PAGE_FLAGS_MASK) | PAGE_PRESENT;
    
    return 0;
}

/**
 * Debug: Print page table walk
 */
void vmm_debug_walk(uint64_t virt) {
    serial_puts("\n[VMM] Page walk for ");
    serial_hex64(virt);
    serial_puts(":\n");
    
    if (!vmm_pml4) {
        serial_puts("  PML4 not initialized\n");
        return;
    }
    
    int pml4_idx = PML4_INDEX(virt);
    serial_puts("  PML4[");
    serial_putc('0' + pml4_idx / 100);
    serial_putc('0' + (pml4_idx / 10) % 10);
    serial_putc('0' + pml4_idx % 10);
    serial_puts("] = ");
    serial_hex64(vmm_pml4[pml4_idx]);
    serial_puts("\n");
    
    if (!(vmm_pml4[pml4_idx] & PAGE_PRESENT)) {
        serial_puts("  → Not present\n");
        return;
    }
    
    uint64_t *pdp = (uint64_t*)(vmm_pml4[pml4_idx] & PAGE_FRAME_MASK);
    int pdp_idx = PDP_INDEX(virt);
    serial_puts("  PDP[");
    serial_putc('0' + pdp_idx / 100);
    serial_putc('0' + (pdp_idx / 10) % 10);
    serial_putc('0' + pdp_idx % 10);
    serial_puts("] = ");
    serial_hex64(pdp[pdp_idx]);
    serial_puts("\n");
    
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) {
        serial_puts("  → Not present\n");
        return;
    }
    
    uint64_t *pd = (uint64_t*)(pdp[pdp_idx] & PAGE_FRAME_MASK);
    int pd_idx = PD_INDEX(virt);
    serial_puts("  PD[");
    serial_putc('0' + pd_idx / 100);
    serial_putc('0' + (pd_idx / 10) % 10);
    serial_putc('0' + pd_idx % 10);
    serial_puts("] = ");
    serial_hex64(pd[pd_idx]);
    
    if (pd[pd_idx] & PAGE_HUGE) {
        serial_puts(" (2MB huge page)\n");
        uint64_t phys = (pd[pd_idx] & PAGE_FRAME_MASK) | (virt & 0x1FFFFF);
        serial_puts("  → Physical: ");
        serial_hex64(phys);
        serial_puts("\n");
        return;
    }
    serial_puts("\n");
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        serial_puts("  → Not present\n");
        return;
    }
    
    uint64_t *pt = (uint64_t*)(pd[pd_idx] & PAGE_FRAME_MASK);
    int pt_idx = PT_INDEX(virt);
    serial_puts("  PT[");
    serial_putc('0' + pt_idx / 100);
    serial_putc('0' + (pt_idx / 10) % 10);
    serial_putc('0' + pt_idx % 10);
    serial_puts("] = ");
    serial_hex64(pt[pt_idx]);
    serial_puts("\n");
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        serial_puts("  → Not present\n");
        return;
    }
    
    uint64_t phys = (pt[pt_idx] & PAGE_FRAME_MASK) | (virt & 0xFFF);
    serial_puts("  → Physical: ");
    serial_hex64(phys);
    serial_puts("\n");
}
