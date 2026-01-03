/*
 * NeolyxOS Init System
 * 
 * Production-ready system initialization with:
 * - RAM initialization from boot info
 * - Essential service startup
 * - Driver initialization
 * - Filesystem mounting
 * - User environment preparation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef INIT_H
#define INIT_H

#include <stdint.h>

/* ============ Init Stages ============ */
#define INIT_STAGE_EARLY        0  /* Before memory is fully available */
#define INIT_STAGE_MEMORY       1  /* Memory subsystem initialization */
#define INIT_STAGE_DRIVERS      2  /* Hardware drivers */
#define INIT_STAGE_FILESYSTEM   3  /* Filesystem mounting */
#define INIT_STAGE_SERVICES     4  /* System services */
#define INIT_STAGE_NETWORK      5  /* Network stack */
#define INIT_STAGE_USER         6  /* User environment */
#define INIT_STAGE_COMPLETE     7  /* Init complete */

/* ============ Boot Info (from bootloader) ============ */
typedef struct {
    uint32_t magic;
    uint32_t version;
    
    /* Memory map */
    uint64_t memory_map_addr;
    uint32_t memory_map_size;
    uint32_t memory_map_entry_size;
    
    /* Framebuffer */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    
    /* Kernel location */
    uint64_t kernel_phys_base;
    uint64_t kernel_size;
    
    /* ACPI */
    uint64_t acpi_rsdp;
    
    /* Boot disk */
    uint32_t boot_disk_id;
    uint32_t boot_partition;
    
} boot_info_t;

/* ============ Memory Region Types ============ */
#define MEM_TYPE_AVAILABLE      1
#define MEM_TYPE_RESERVED       2
#define MEM_TYPE_ACPI_RECLAIM   3
#define MEM_TYPE_ACPI_NVS       4
#define MEM_TYPE_BAD            5
#define MEM_TYPE_KERNEL         10

/* ============ Memory Region ============ */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} memory_region_t;

/* ============ RAM Info ============ */
typedef struct {
    uint64_t total_memory;
    uint64_t usable_memory;
    uint64_t reserved_memory;
    uint64_t kernel_memory;
    
    memory_region_t *regions;
    int region_count;
    
    /* Heap */
    uint64_t heap_start;
    uint64_t heap_size;
    
} ram_info_t;

/* ============ Init Callbacks ============ */
typedef int (*init_fn)(void);

typedef struct {
    const char *name;
    int stage;
    int priority;
    init_fn func;
} init_entry_t;

/* ============ Public API ============ */

/**
 * init_early - Early initialization (before memory)
 * 
 * @boot_info: Boot information from bootloader
 * 
 * Returns: 0 on success
 */
int init_early(boot_info_t *boot_info);

/**
 * init_memory - Initialize memory subsystem
 * 
 * Parses memory map and sets up heap.
 * 
 * Returns: 0 on success
 */
int init_memory(void);

/**
 * init_drivers - Initialize hardware drivers
 * 
 * Initializes PCI, storage, and input drivers.
 * 
 * Returns: 0 on success
 */
int init_drivers(void);

/**
 * init_filesystem - Mount filesystems
 * 
 * Mounts root filesystem and other partitions.
 * 
 * Returns: 0 on success
 */
int init_filesystem(void);

/**
 * init_services - Start system services
 * 
 * Starts essential services from service manager.
 * 
 * Returns: 0 on success
 */
int init_services(void);

/**
 * init_network - Initialize network stack
 * 
 * Returns: 0 on success
 */
int init_network(void);

/**
 * init_user - Prepare user environment
 * 
 * Starts login/desktop.
 * 
 * Returns: 0 on success
 */
int init_user(void);

/**
 * init_run - Run full init sequence
 * 
 * @boot_info: Boot information from bootloader
 * 
 * Returns: 0 on success, stage number on failure
 */
int init_run(boot_info_t *boot_info);

/**
 * init_get_stage - Get current init stage
 */
int init_get_stage(void);

/**
 * init_get_ram_info - Get RAM information
 */
ram_info_t *init_get_ram_info(void);

/**
 * init_get_boot_info - Get boot information
 */
boot_info_t *init_get_boot_info(void);

#endif /* INIT_H */
