/*
 * NeolyxOS Device Tree (FDT) Parser
 * 
 * Parses Flattened Device Tree blobs for ARM64
 * 
 * The DTB is passed by bootloader/firmware and describes:
 *   - Memory regions
 *   - CPU topology
 *   - Device addresses
 *   - Interrupt routing
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef FDT_PARSER_H
#define FDT_PARSER_H

#include <stdint.h>
#include <stddef.h>

/* ============ FDT Magic ============ */

#define FDT_MAGIC           0xD00DFEED
#define FDT_BEGIN_NODE      0x00000001
#define FDT_END_NODE        0x00000002
#define FDT_PROP            0x00000003
#define FDT_NOP             0x00000004
#define FDT_END             0x00000009

/* ============ FDT Header ============ */

typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} __attribute__((packed)) fdt_header_t;

/* ============ Memory Reservation ============ */

typedef struct {
    uint64_t address;
    uint64_t size;
} __attribute__((packed)) fdt_reserve_entry_t;

/* ============ Parsed Results ============ */

#define FDT_MAX_MEM_REGIONS     16
#define FDT_MAX_DEVICES         64
#define FDT_MAX_CPUS            16

typedef struct {
    uint64_t base;
    uint64_t size;
} fdt_mem_region_t;

typedef struct {
    char     compatible[64];
    uint64_t reg_base;
    uint64_t reg_size;
    uint32_t interrupts[4];
    uint8_t  interrupt_count;
} fdt_device_t;

typedef struct {
    uint32_t id;
    char     compatible[32];
    uint64_t reg;
    uint8_t  enabled;
} fdt_cpu_t;

typedef struct {
    uint8_t             valid;
    
    /* Memory */
    int                 mem_region_count;
    fdt_mem_region_t    mem_regions[FDT_MAX_MEM_REGIONS];
    uint64_t            total_memory;
    
    /* CPUs */
    int                 cpu_count;
    fdt_cpu_t           cpus[FDT_MAX_CPUS];
    
    /* Devices */
    int                 device_count;
    fdt_device_t        devices[FDT_MAX_DEVICES];
    
    /* Platform info */
    char                model[64];
    char                compatible[64];
    
    /* Serial/UART */
    uint64_t            uart_base;
    uint32_t            uart_irq;
    
    /* Interrupt controller */
    uint64_t            gic_dist_base;
    uint64_t            gic_cpu_base;
    uint64_t            gic_redist_base;
    uint8_t             gic_version;
    
    /* Timer */
    uint32_t            timer_irq;
    
    /* Chosen node */
    char                bootargs[256];
    uint64_t            initrd_start;
    uint64_t            initrd_end;
    uint64_t            linux_stdout;
} fdt_info_t;

/* ============ Public API ============ */

/**
 * Parse FDT blob
 * 
 * @param fdt_addr Address of DTB in memory
 * @param info Output parsed info
 * @return 0 on success, negative on error
 */
int fdt_parse(void *fdt_addr, fdt_info_t *info);

/**
 * Validate FDT header
 */
int fdt_validate(void *fdt_addr);

/**
 * Get total FDT size
 */
uint32_t fdt_totalsize(void *fdt_addr);

/**
 * Find node by path
 * 
 * @param fdt_addr FDT base address
 * @param path Node path (e.g., "/memory", "/cpus/cpu@0")
 * @return Offset to node, or -1 if not found
 */
int fdt_find_node(void *fdt_addr, const char *path);

/**
 * Get property value
 * 
 * @param fdt_addr FDT base address  
 * @param node_offset Offset to node
 * @param name Property name
 * @param len Output length of property
 * @return Pointer to property value, or NULL
 */
const void *fdt_get_property(void *fdt_addr, int node_offset, 
                             const char *name, int *len);

/**
 * Read reg property (address + size pairs)
 */
int fdt_read_reg(void *fdt_addr, int node_offset, 
                 uint64_t *addr, uint64_t *size);

/**
 * Read string property
 */
const char *fdt_read_string(void *fdt_addr, int node_offset, 
                            const char *name);

/**
 * Print FDT info for debug
 */
void fdt_debug_print(const fdt_info_t *info);

#endif /* FDT_PARSER_H */
