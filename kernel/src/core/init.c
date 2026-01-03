/*
 * NeolyxOS Init System Implementation
 * 
 * System initialization and RAM loading
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "init.h"
#include "../drivers/serial.h"
#include "../services/service_manager.h"

/* ============ External Functions ============ */
extern void heap_init(uint64_t start_addr, uint64_t size);
extern int pci_init(void);
extern int nvme_init(void);
extern int usb_init(void);
extern int kbd_init(void);
extern int svc_init(void);
extern int svc_start_all(void);

/* ============ Global State ============ */
static boot_info_t *g_boot_info = 0;
static ram_info_t g_ram_info;
static int g_init_stage = INIT_STAGE_EARLY;

/* ============ Memory Region Storage ============ */
#define MAX_MEMORY_REGIONS 64
static memory_region_t memory_regions[MAX_MEMORY_REGIONS];

/* ============ Logging ============ */

static void log_stage(const char *stage) {
    serial_puts("[INIT] === ");
    serial_puts(stage);
    serial_puts(" ===\r\n");
}

static void log_ok(const char *msg) {
    serial_puts("[INIT] [OK] ");
    serial_puts(msg);
    serial_puts("\r\n");
}

static void log_fail(const char *msg) {
    serial_puts("[INIT] [FAIL] ");
    serial_puts(msg);
    serial_puts("\r\n");
}

static void log_info(const char *msg) {
    serial_puts("[INIT] ");
    serial_puts(msg);
    serial_puts("\r\n");
}

/* ============ Memory Initialization ============ */

static void parse_memory_map(void) {
    if (!g_boot_info || !g_boot_info->memory_map_addr) {
        log_fail("No memory map from bootloader");
        return;
    }
    
    g_ram_info.total_memory = 0;
    g_ram_info.usable_memory = 0;
    g_ram_info.reserved_memory = 0;
    g_ram_info.region_count = 0;
    
    /* Parse memory map entries */
    uint64_t map_addr = g_boot_info->memory_map_addr;
    uint32_t map_size = g_boot_info->memory_map_size;
    uint32_t entry_size = g_boot_info->memory_map_entry_size;
    
    if (entry_size == 0) entry_size = 24; /* Default UEFI entry size */
    
    int count = map_size / entry_size;
    if (count > MAX_MEMORY_REGIONS) count = MAX_MEMORY_REGIONS;
    
    for (int i = 0; i < count; i++) {
        uint64_t *entry = (uint64_t*)(map_addr + i * entry_size);
        
        memory_region_t *region = &memory_regions[g_ram_info.region_count];
        region->base = entry[0];
        region->length = entry[1];
        region->type = entry[2] & 0xFFFFFFFF;
        region->attributes = 0;
        
        g_ram_info.total_memory += region->length;
        
        if (region->type == MEM_TYPE_AVAILABLE) {
            g_ram_info.usable_memory += region->length;
        } else {
            g_ram_info.reserved_memory += region->length;
        }
        
        g_ram_info.region_count++;
    }
    
    g_ram_info.regions = memory_regions;
    
    /* Log memory info */
    log_info("Memory map parsed:");
    
    char buf[32];
    uint64_t mb = g_ram_info.total_memory / (1024 * 1024);
    int idx = 0;
    if (mb == 0) {
        buf[idx++] = '0';
    } else {
        char tmp[16];
        int j = 0;
        while (mb > 0) { tmp[j++] = '0' + (mb % 10); mb /= 10; }
        while (j > 0) buf[idx++] = tmp[--j];
    }
    buf[idx] = '\0';
    
    serial_puts("[INIT]   Total: ");
    serial_puts(buf);
    serial_puts(" MB\r\n");
    
    mb = g_ram_info.usable_memory / (1024 * 1024);
    idx = 0;
    if (mb == 0) {
        buf[idx++] = '0';
    } else {
        char tmp[16];
        int j = 0;
        while (mb > 0) { tmp[j++] = '0' + (mb % 10); mb /= 10; }
        while (j > 0) buf[idx++] = tmp[--j];
    }
    buf[idx] = '\0';
    
    serial_puts("[INIT]   Usable: ");
    serial_puts(buf);
    serial_puts(" MB\r\n");
}

static void init_heap(void) {
    /* Find largest usable region for heap */
    uint64_t best_base = 0;
    uint64_t best_size = 0;
    
    for (int i = 0; i < g_ram_info.region_count; i++) {
        memory_region_t *r = &memory_regions[i];
        
        if (r->type != MEM_TYPE_AVAILABLE) continue;
        
        /* Skip low memory (< 1MB) */
        if (r->base < 0x100000) continue;
        
        /* Skip regions overlapping kernel */
        if (g_boot_info->kernel_phys_base >= r->base &&
            g_boot_info->kernel_phys_base < r->base + r->length) {
            continue;
        }
        
        if (r->length > best_size) {
            best_base = r->base;
            best_size = r->length;
        }
    }
    
    if (best_size == 0) {
        /* Fallback: use fixed heap location */
        best_base = 0x200000; /* 2MB */
        best_size = 16 * 1024 * 1024; /* 16MB */
        log_info("Using fallback heap location");
    }
    
    /* Limit heap to 64MB for now */
    if (best_size > 64 * 1024 * 1024) {
        best_size = 64 * 1024 * 1024;
    }
    
    g_ram_info.heap_start = best_base;
    g_ram_info.heap_size = best_size;
    
    heap_init(best_base, best_size);
    
    char buf[32];
    uint64_t mb = best_size / (1024 * 1024);
    int idx = 0;
    if (mb == 0) {
        buf[idx++] = '0';
    } else {
        char tmp[16];
        int j = 0;
        while (mb > 0) { tmp[j++] = '0' + (mb % 10); mb /= 10; }
        while (j > 0) buf[idx++] = tmp[--j];
    }
    buf[idx] = '\0';
    
    serial_puts("[INIT] Heap initialized: ");
    serial_puts(buf);
    serial_puts(" MB\r\n");
}

/* ============ Public API ============ */

int init_early(boot_info_t *boot_info) {
    log_stage("EARLY INIT");
    
    if (!boot_info) {
        log_fail("No boot info");
        return -1;
    }
    
    g_boot_info = boot_info;
    g_init_stage = INIT_STAGE_EARLY;
    
    /* Validate boot info magic */
    if (boot_info->magic != 0x4E454F58) { /* 'NEOX' */
        log_fail("Invalid boot info magic");
        return -1;
    }
    
    log_ok("Boot info validated");
    
    return 0;
}

int init_memory(void) {
    log_stage("MEMORY INIT");
    g_init_stage = INIT_STAGE_MEMORY;
    
    /* Parse memory map */
    parse_memory_map();
    
    /* Initialize heap */
    init_heap();
    
    log_ok("Memory initialized");
    return 0;
}

int init_drivers(void) {
    log_stage("DRIVER INIT");
    g_init_stage = INIT_STAGE_DRIVERS;
    
    /* PCI bus */
    log_info("Initializing PCI...");
    pci_init();
    
    /* Storage */
    log_info("Initializing NVMe...");
    nvme_init();
    
    /* USB */
    log_info("Initializing USB...");
    usb_init();
    
    /* Keyboard */
    log_info("Initializing keyboard...");
    kbd_init();
    
    log_ok("Drivers initialized");
    return 0;
}

int init_filesystem(void) {
    log_stage("FILESYSTEM INIT");
    g_init_stage = INIT_STAGE_FILESYSTEM;
    
    /* TODO: Mount root filesystem */
    log_info("Filesystem mounting not yet implemented");
    
    return 0;
}

int init_services(void) {
    log_stage("SERVICE INIT");
    g_init_stage = INIT_STAGE_SERVICES;
    
    /* Initialize service manager */
    svc_init();
    
    /* Start auto-start services */
    svc_start_all();
    
    log_ok("Services initialized");
    return 0;
}

int init_network(void) {
    log_stage("NETWORK INIT");
    g_init_stage = INIT_STAGE_NETWORK;
    
    /* TODO: Initialize network stack */
    log_info("Network not yet implemented");
    
    return 0;
}

int init_user(void) {
    log_stage("USER INIT");
    g_init_stage = INIT_STAGE_USER;
    
    /* TODO: Start login/desktop */
    log_info("User environment not yet implemented");
    
    return 0;
}

int init_run(boot_info_t *boot_info) {
    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts("      NeolyxOS Initialization\r\n");
    serial_puts("========================================\r\n\r\n");
    
    /* Stage 0: Early init */
    if (init_early(boot_info) != 0) {
        return INIT_STAGE_EARLY;
    }
    
    /* Stage 1: Memory */
    if (init_memory() != 0) {
        return INIT_STAGE_MEMORY;
    }
    
    /* Stage 2: Drivers */
    if (init_drivers() != 0) {
        return INIT_STAGE_DRIVERS;
    }
    
    /* Stage 3: Filesystem */
    if (init_filesystem() != 0) {
        return INIT_STAGE_FILESYSTEM;
    }
    
    /* Stage 4: Services */
    if (init_services() != 0) {
        return INIT_STAGE_SERVICES;
    }
    
    /* Stage 5: Network */
    if (init_network() != 0) {
        return INIT_STAGE_NETWORK;
    }
    
    /* Stage 6: User */
    if (init_user() != 0) {
        return INIT_STAGE_USER;
    }
    
    g_init_stage = INIT_STAGE_COMPLETE;
    
    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts("    INITIALIZATION COMPLETE!\r\n");
    serial_puts("========================================\r\n\r\n");
    
    return 0;
}

int init_get_stage(void) {
    return g_init_stage;
}

ram_info_t *init_get_ram_info(void) {
    return &g_ram_info;
}

boot_info_t *init_get_boot_info(void) {
    return g_boot_info;
}
