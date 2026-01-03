/*
 * NeolyxOS Virtual Memory Manager
 * 
 * Manages virtual address space and page tables (4-level paging).
 * Higher-half kernel mapping.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_VMM_H
#define NEOLYX_VMM_H

#include <stdint.h>
#include <stddef.h>

/* ============ Constants ============ */

#define PAGE_SIZE           4096
#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITETHROUGH   (1ULL << 3)
#define PAGE_NOCACHE        (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_HUGE           (1ULL << 7)
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NO_EXECUTE     (1ULL << 63)

/* Kernel higher-half base */
#define KERNEL_BASE         0xFFFF800000000000ULL

/* Page table entry count */
#define PT_ENTRIES          512

/* ============ VMM API ============ */

/**
 * Initialize the virtual memory manager.
 * Sets up 4-level paging with higher-half kernel.
 */
void vmm_init(void);

/**
 * Map a virtual address to a physical address.
 * 
 * @param virt   Virtual address (page-aligned)
 * @param phys   Physical address (page-aligned)
 * @param flags  Page flags (PAGE_PRESENT | PAGE_WRITABLE | ...)
 * @return 0 on success, -1 on failure
 */
int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);

/**
 * Map a range of virtual addresses.
 * 
 * @param virt   Virtual address (page-aligned)
 * @param phys   Physical address (page-aligned)
 * @param size   Size in bytes (rounded to pages)
 * @param flags  Page flags
 * @return 0 on success, -1 on failure
 */
int vmm_map_range(uint64_t virt, uint64_t phys, size_t size, uint64_t flags);

/**
 * Unmap a virtual address.
 * 
 * @param virt Virtual address to unmap
 */
void vmm_unmap(uint64_t virt);

/**
 * Get the physical address mapped to a virtual address.
 * 
 * @param virt Virtual address
 * @return Physical address, or 0 if not mapped
 */
uint64_t vmm_get_physical(uint64_t virt);

/**
 * Allocate virtual memory (with backing physical pages).
 * 
 * @param pages Number of pages to allocate
 * @return Virtual address, or 0 on failure
 */
uint64_t vmm_alloc_pages(size_t pages);

/**
 * Free virtual memory.
 * 
 * @param virt  Virtual address
 * @param pages Number of pages to free
 */
void vmm_free_pages(uint64_t virt, size_t pages);

/**
 * Flush TLB for a specific address.
 */
void vmm_flush_tlb(uint64_t virt);

/**
 * Flush entire TLB.
 */
void vmm_flush_tlb_all(void);

/* ============ Address Space Management (VM/Process Support) ============ */

/**
 * Create a new address space (for new process or VM).
 * Kernel mappings are shared automatically.
 * 
 * @return Physical address of new PML4, or 0 on failure
 */
uint64_t vmm_create_address_space(void);

/**
 * Switch to a different address space.
 * 
 * @param pml4_phys Physical address of PML4
 */
void vmm_switch_address_space(uint64_t pml4_phys);

/**
 * Get current address space (CR3 value).
 */
uint64_t vmm_get_current_address_space(void);

/**
 * Map virtual memory into a specific address space.
 * Used for setting up process/VM memory before switching.
 * 
 * @param pml4_phys Target address space
 * @param virt      Virtual address
 * @param phys      Physical address
 * @param flags     Page flags
 * @return 0 on success, -1 on failure
 */
int vmm_map_in_space(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/**
 * Debug: Print page table walk for a virtual address.
 */
void vmm_debug_walk(uint64_t virt);

#endif /* NEOLYX_VMM_H */
