/*
 * NeolyxOS Physical Memory Manager
 * 
 * Manages physical page frames using a bitmap allocator.
 * This is the foundation - everything else depends on this.
 * 
 * Design:
 *   - 4KB page granularity
 *   - Bitmap tracks free/used pages
 *   - UEFI memory map parsing
 *   - Thread-safe (when we add SMP)
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_PMM_H
#define NEOLYX_PMM_H

#include <stdint.h>
#include <stddef.h>

/* ============ Constants ============ */

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGES_PER_BYTE      8

/* Page alignment helpers - use these everywhere */
#define ALIGN_DOWN(addr)    ((addr) & ~(PAGE_SIZE - 1))
#define ALIGN_UP(addr)      (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define IS_PAGE_ALIGNED(addr) (((addr) & (PAGE_SIZE - 1)) == 0)

/* Maximum supported physical memory (16 GB) */
#define MAX_PHYSICAL_MEMORY (16ULL * 1024 * 1024 * 1024)
#define MAX_PAGES           (MAX_PHYSICAL_MEMORY / PAGE_SIZE)
#define BITMAP_SIZE         (MAX_PAGES / 8)

/* ============ Page Index Conversion ============ */

/* Convert physical address to page index */
static inline uint64_t pmm_addr_to_page(uint64_t addr) {
    return addr >> PAGE_SHIFT;
}

/* Convert page index to physical address */
static inline uint64_t pmm_page_to_addr(uint64_t page) {
    return page << PAGE_SHIFT;
}

/*
 * ============ UEFI Memory Reclaim Rules ============
 * 
 * Reclaimable after ExitBootServices:
 *   - UEFI_LOADER_CODE
 *   - UEFI_LOADER_DATA
 *   - UEFI_BOOT_SERVICES_CODE
 *   - UEFI_BOOT_SERVICES_DATA
 *   - UEFI_CONVENTIONAL
 *   - UEFI_ACPI_RECLAIM (after ACPI tables are copied)
 *
 * NEVER reclaim (firmware needs forever):
 *   - UEFI_RUNTIME_SERVICES_CODE
 *   - UEFI_RUNTIME_SERVICES_DATA
 *   - UEFI_ACPI_NVS
 *   - UEFI_MMIO
 *   - UEFI_MMIO_PORT
 */

/* ============ UEFI Memory Types ============ */

typedef enum {
    UEFI_RESERVED = 0,
    UEFI_LOADER_CODE = 1,
    UEFI_LOADER_DATA = 2,
    UEFI_BOOT_SERVICES_CODE = 3,
    UEFI_BOOT_SERVICES_DATA = 4,
    UEFI_RUNTIME_SERVICES_CODE = 5,
    UEFI_RUNTIME_SERVICES_DATA = 6,
    UEFI_CONVENTIONAL = 7,
    UEFI_UNUSABLE = 8,
    UEFI_ACPI_RECLAIM = 9,
    UEFI_ACPI_NVS = 10,
    UEFI_MMIO = 11,
    UEFI_MMIO_PORT = 12,
    UEFI_PAL_CODE = 13,
    UEFI_PERSISTENT = 14,
} uefi_memory_type_t;

/* UEFI Memory Descriptor 
 * Note: UEFI uses EFI_MEMORY_DESCRIPTOR which has padding.
 * The Type is UINT32 but is padded to 8-byte alignment.
 * Total size is typically 48 bytes but the exact size is passed
 * in memory_map_desc_size from the bootloader.
 * 
 * We iterate using desc_size from bootloader, not sizeof(this struct).
 */
typedef struct {
    uint32_t type;
    uint32_t padding;           /* UEFI pads Type to 8 bytes */
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t num_pages;
    uint64_t attribute;
} __attribute__((packed)) uefi_memory_descriptor_t;

/* ============ PMM Statistics ============ */

typedef struct {
    uint64_t total_pages;       /* Total pages in system */
    uint64_t used_pages;        /* Currently allocated */
    uint64_t free_pages;        /* Available for allocation */
    uint64_t reserved_pages;    /* Reserved (MMIO, runtime services) */
    uint64_t total_memory;      /* Total RAM in bytes */
    uint64_t free_memory;       /* Free RAM in bytes */
} pmm_stats_t;

/* ============ PMM API ============ */

/**
 * Initialize the physical memory manager.
 * 
 * @param memory_map      Pointer to UEFI memory map
 * @param map_size        Size of memory map in bytes
 * @param descriptor_size Size of each descriptor
 * 
 * Call this once during kernel initialization.
 * After calling, UEFI boot services memory is reclaimable.
 */
void pmm_init(void *memory_map, uint64_t map_size, uint64_t descriptor_size);

/**
 * Allocate a single physical page.
 * 
 * @return Physical address of allocated page, or 0 on failure.
 * 
 * The page is zeroed before return (security).
 */
uint64_t pmm_alloc_page(void);

/**
 * Allocate contiguous physical pages.
 * 
 * @param count Number of pages to allocate.
 * @return Physical address of first page, or 0 on failure.
 * 
 * All pages are zeroed before return (security).
 */
uint64_t pmm_alloc_pages(size_t count);

/**
 * Free a previously allocated page.
 * 
 * @param page Physical address of page to free.
 * 
 * Double-free is detected and logged (security).
 */
void pmm_free_page(uint64_t page);

/**
 * Free contiguous pages.
 * 
 * @param page  Physical address of first page.
 * @param count Number of pages to free.
 */
void pmm_free_pages(uint64_t page, size_t count);

/**
 * Get PMM statistics.
 * 
 * @param stats Pointer to stats structure to fill.
 */
void pmm_get_stats(pmm_stats_t *stats);

/**
 * Mark a region as reserved (cannot be allocated).
 * 
 * @param start Physical address (page-aligned).
 * @param size  Size in bytes (rounded up to pages).
 * 
 * Used for MMIO regions, kernel, framebuffer, etc.
 */
void pmm_reserve_region(uint64_t start, uint64_t size);

/**
 * Check if a physical address is within allocatable memory.
 * 
 * @param addr Physical address to check.
 * @return 1 if valid, 0 otherwise.
 */
int pmm_is_valid_address(uint64_t addr);

/**
 * Check if PMM is initialized.
 * 
 * @return 1 if initialized, 0 otherwise.
 * 
 * Use this to catch bugs early - avoid allocs before pmm_init().
 */
int pmm_is_initialized(void);

/* ============ Debug ============ */

/**
 * Print PMM status to serial console.
 */
void pmm_debug_print(void);

/*
 * TODO: SMP Support
 * When SMP is enabled, protect bitmap with spinlock:
 *   void pmm_lock(void);
 *   void pmm_unlock(void);
 */

#endif /* NEOLYX_PMM_H */
