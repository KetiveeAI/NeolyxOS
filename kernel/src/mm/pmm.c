/*
 * NeolyxOS Physical Memory Manager Implementation
 * 
 * Bitmap-based page frame allocator.
 * Production-quality, security-focused, clean code.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "mm/pmm.h"

/* ============ External Dependencies ============ */

/* Serial debug output (from minimal kernel) */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Memory operations (we implement these ourselves - no libc) */
static void *pmm_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

/* ============ PMM State ============ */

/* Initialization flag - catch bugs early */
static int pmm_initialized = 0;

/* Bitmap: 1 bit per page, 0 = free, 1 = used/reserved */
static uint8_t pmm_bitmap[BITMAP_SIZE];

/* Statistics */
static pmm_stats_t pmm_stats;

/* Highest usable physical address */
static uint64_t pmm_max_address = 0;

/* Spinlock for bitmap operations.
 * Current implementation: CLI/STI (single-core safe).
 * Upgrade to ticket spinlock when SMP is enabled. */
static volatile int pmm_lock = 0;

static inline void pmm_spin_lock(void) {
    __asm__ volatile("cli");
    while (__sync_lock_test_and_set(&pmm_lock, 1)) {
        __asm__ volatile("pause");
    }
}

static inline void pmm_spin_unlock(void) {
    __sync_lock_release(&pmm_lock);
    __asm__ volatile("sti");
}

/* Kernel end symbol from linker script */
extern uint8_t _kernel_end[];

/* ============ Helper Functions ============ */

/* Convert physical address to page index */
static inline uint64_t addr_to_page(uint64_t addr) {
    return addr >> PAGE_SHIFT;
}

/* Convert page index to physical address */
static inline uint64_t page_to_addr(uint64_t page) {
    return page << PAGE_SHIFT;
}

/* Set bit in bitmap (mark page as used) */
static inline void bitmap_set(uint64_t page) {
    if (page < MAX_PAGES) {
        pmm_bitmap[page / 8] |= (1 << (page % 8));
    }
}

/* Clear bit in bitmap (mark page as free) */
static inline void bitmap_clear(uint64_t page) {
    if (page < MAX_PAGES) {
        pmm_bitmap[page / 8] &= ~(1 << (page % 8));
    }
}

/* Test bit in bitmap (1 = used, 0 = free) */
static inline int bitmap_test(uint64_t page) {
    if (page >= MAX_PAGES) return 1;  /* Out of range = used */
    return (pmm_bitmap[page / 8] >> (page % 8)) & 1;
}

/* Print hex value to serial */
static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) {
        serial_putc('0');
        return;
    }
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_puts(&buf[i + 1]);
}

/* ============ PMM Implementation ============ */

void pmm_init(void *memory_map, uint64_t map_size, uint64_t descriptor_size) {
    serial_puts("[PMM] Initializing physical memory manager...\n");
    
    /* Initialize bitmap - mark all pages as used initially */
    pmm_memset(pmm_bitmap, 0xFF, BITMAP_SIZE);
    
    /* Initialize statistics */
    pmm_memset(&pmm_stats, 0, sizeof(pmm_stats));
    
    /* Parse UEFI memory map */
    uint8_t *entry = (uint8_t *)memory_map;
    uint64_t entries = map_size / descriptor_size;
    
    serial_puts("[PMM] Memory map entries: ");
    serial_dec(entries);
    serial_puts("\n");
    
    for (uint64_t i = 0; i < entries; i++) {
        uefi_memory_descriptor_t *desc = (uefi_memory_descriptor_t *)entry;
        
        uint64_t start = desc->physical_start;
        uint64_t pages = desc->num_pages;
        uint64_t end = start + (pages * PAGE_SIZE);
        
        /* Track highest address */
        if (end > pmm_max_address) {
            pmm_max_address = end;
        }
        
        /* Update total pages count */
        pmm_stats.total_pages += pages;
        
        /* Check if this memory is usable */
        int usable = 0;
        const char *type_name = "Unknown";
        
        switch (desc->type) {
            case UEFI_LOADER_CODE:
            case UEFI_LOADER_DATA:
            case UEFI_BOOT_SERVICES_CODE:
            case UEFI_BOOT_SERVICES_DATA:
            case UEFI_CONVENTIONAL:
                /* These are usable after ExitBootServices */
                usable = 1;
                type_name = "Usable";
                break;
                
            case UEFI_ACPI_RECLAIM:
                type_name = "ACPI Reclaim";
                /* Usable after ACPI tables are processed */
                usable = 1;
                break;
                
            case UEFI_RUNTIME_SERVICES_CODE:
            case UEFI_RUNTIME_SERVICES_DATA:
                type_name = "Runtime Services";
                /* NEVER free - firmware needs this forever */
                pmm_stats.reserved_pages += pages;
                break;
                
            case UEFI_RESERVED:
            case UEFI_UNUSABLE:
            case UEFI_ACPI_NVS:
            case UEFI_MMIO:
            case UEFI_MMIO_PORT:
            case UEFI_PAL_CODE:
                type_name = "Reserved/MMIO";
                pmm_stats.reserved_pages += pages;
                break;
                
            default:
                pmm_stats.reserved_pages += pages;
                break;
        }
        
        /* Mark usable pages as free in bitmap */
        if (usable) {
            uint64_t start_page = addr_to_page(start);
            for (uint64_t p = 0; p < pages; p++) {
                if (start_page + p < MAX_PAGES) {
                    bitmap_clear(start_page + p);
                    pmm_stats.free_pages++;
                }
            }
        }
        
        /* Debug output for significant regions (> 4MB) */
        if (pages > 1024) {
            serial_puts("  ");
            serial_hex64(start);
            serial_puts(" - ");
            serial_hex64(end);
            serial_puts(" (");
            serial_dec(pages * PAGE_SIZE / 1024 / 1024);
            serial_puts(" MB) ");
            serial_puts(type_name);
            serial_puts("\n");
        }
        
        entry += descriptor_size;
    }
    
    /* Reserve first 1 MB (legacy BIOS area, VGA, etc.) */
    pmm_reserve_region(0, 1024 * 1024);
    
    /* Reserve kernel binary region (0x100000 to _kernel_end)
     * Without this, pmm_alloc_page could hand out pages
     * that overlap kernel code/data — instant corruption. */
    uint64_t kernel_start = 0x100000;  /* Must match linker.ld */
    uint64_t kernel_end_addr = (uint64_t)_kernel_end;
    if (kernel_end_addr > kernel_start) {
        pmm_reserve_region(kernel_start, kernel_end_addr - kernel_start);
        serial_puts("[PMM] Reserved kernel region: 0x100000 - ");
        serial_hex64(kernel_end_addr);
        serial_puts("\n");
    }
    
    /* Calculate final stats */
    pmm_stats.total_memory = pmm_stats.total_pages * PAGE_SIZE;
    pmm_stats.free_memory = pmm_stats.free_pages * PAGE_SIZE;
    pmm_stats.used_pages = pmm_stats.total_pages - pmm_stats.free_pages - pmm_stats.reserved_pages;
    
    serial_puts("[PMM] Ready: ");
    serial_dec(pmm_stats.free_memory / 1024 / 1024);
    serial_puts(" MB free, ");
    serial_dec(pmm_stats.total_memory / 1024 / 1024);
    serial_puts(" MB total\n");
    
    /* Mark as initialized */
    pmm_initialized = 1;
}

uint64_t pmm_alloc_page(void) {
    pmm_spin_lock();
    
    /* Find first free page */
    for (uint64_t byte = 0; byte < BITMAP_SIZE; byte++) {
        if (pmm_bitmap[byte] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                uint64_t page = byte * 8 + bit;
                if (!bitmap_test(page)) {
                    bitmap_set(page);
                    pmm_stats.free_pages--;
                    pmm_stats.used_pages++;
                    uint64_t addr = page_to_addr(page);
                    pmm_spin_unlock();
                    return addr;
                }
            }
        }
    }
    
    pmm_spin_unlock();
    serial_puts("[PMM] ERROR: Out of physical memory!\n");
    return 0;
}

uint64_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();
    
    /* Find contiguous free pages */
    uint64_t consecutive = 0;
    uint64_t start_page = 0;
    
    for (uint64_t page = 0; page < MAX_PAGES; page++) {
        if (!bitmap_test(page)) {
            if (consecutive == 0) {
                start_page = page;
            }
            consecutive++;
            if (consecutive == count) {
                /* Found enough contiguous pages */
                for (uint64_t p = 0; p < count; p++) {
                    bitmap_set(start_page + p);
                }
                pmm_stats.free_pages -= count;
                pmm_stats.used_pages += count;
                
                uint64_t addr = page_to_addr(start_page);
                
                /* Note: Page zeroing removed - caller must zero after mapping */
                
                return addr;
            }
        } else {
            consecutive = 0;
        }
    }
    
    serial_puts("[PMM] ERROR: Cannot allocate ");
    serial_dec(count);
    serial_puts(" contiguous pages!\n");
    return 0;
}

void pmm_free_page(uint64_t page_addr) {
    if (page_addr == 0) return;
    if (page_addr & (PAGE_SIZE - 1)) {
        serial_puts("[PMM] WARNING: Freeing unaligned address!\n");
        return;
    }
    
    uint64_t page = addr_to_page(page_addr);
    
    if (page >= MAX_PAGES) {
        serial_puts("[PMM] WARNING: Freeing invalid address!\n");
        return;
    }
    
    pmm_spin_lock();
    
    /* Check for double-free (security) */
    if (!bitmap_test(page)) {
        pmm_spin_unlock();
        serial_puts("[PMM] WARNING: Double-free detected at ");
        serial_hex64(page_addr);
        serial_puts("!\n");
        return;
    }
    
    bitmap_clear(page);
    pmm_stats.free_pages++;
    pmm_stats.used_pages--;
    pmm_spin_unlock();
}

void pmm_free_pages(uint64_t page_addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(page_addr + i * PAGE_SIZE);
    }
}

void pmm_reserve_region(uint64_t start, uint64_t size) {
    uint64_t start_page = addr_to_page(start);
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (uint64_t p = 0; p < pages; p++) {
        uint64_t page = start_page + p;
        if (page < MAX_PAGES && !bitmap_test(page)) {
            bitmap_set(page);
            if (pmm_stats.free_pages > 0) {
                pmm_stats.free_pages--;
            }
            pmm_stats.reserved_pages++;
        }
    }
}

void pmm_get_stats(pmm_stats_t *stats) {
    if (stats) {
        stats->total_pages = pmm_stats.total_pages;
        stats->used_pages = pmm_stats.used_pages;
        stats->free_pages = pmm_stats.free_pages;
        stats->reserved_pages = pmm_stats.reserved_pages;
        stats->total_memory = pmm_stats.total_memory;
        stats->free_memory = pmm_stats.free_pages * PAGE_SIZE;
    }
}

int pmm_is_valid_address(uint64_t addr) {
    if (addr >= pmm_max_address) return 0;
    uint64_t page = addr_to_page(addr);
    if (page >= MAX_PAGES) return 0;
    return 1;
}

int pmm_is_initialized(void) {
    return pmm_initialized;
}

void pmm_debug_print(void) {
    serial_puts("\n=== PMM Status ===\n");
    serial_puts("Total Memory:    ");
    serial_dec(pmm_stats.total_memory / 1024 / 1024);
    serial_puts(" MB\n");
    serial_puts("Free Memory:     ");
    serial_dec(pmm_stats.free_pages * PAGE_SIZE / 1024 / 1024);
    serial_puts(" MB (");
    serial_dec(pmm_stats.free_pages);
    serial_puts(" pages)\n");
    serial_puts("Used Memory:     ");
    serial_dec(pmm_stats.used_pages * PAGE_SIZE / 1024 / 1024);
    serial_puts(" MB (");
    serial_dec(pmm_stats.used_pages);
    serial_puts(" pages)\n");
    serial_puts("Reserved:        ");
    serial_dec(pmm_stats.reserved_pages * PAGE_SIZE / 1024 / 1024);
    serial_puts(" MB (");
    serial_dec(pmm_stats.reserved_pages);
    serial_puts(" pages)\n");
    serial_puts("==================\n\n");
}

/* Convenience getters for system info */
uint64_t pmm_get_total_memory(void) {
    return pmm_stats.total_memory;
}

uint64_t pmm_get_free_memory(void) {
    return pmm_stats.free_pages * PAGE_SIZE;
}

