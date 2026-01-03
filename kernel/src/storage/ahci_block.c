/*
 * AHCI Block Device Adapter for NXFS
 * 
 * Converts AHCI 512-byte sectors to NXFS 4096-byte blocks.
 * Uses PMM for DMA-safe buffers.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "storage/ahci_block.h"
#include "drivers/ahci.h"
#include "fs/nxfs.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Constants ============ */

#define SECTOR_SIZE     512
#define BLOCK_SIZE      4096
#define SECTORS_PER_BLOCK (BLOCK_SIZE / SECTOR_SIZE)  /* 8 */

/* ============ Memory Helpers ============ */

static void ab_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/* ============ Block Operations ============ */

static nxfs_block_ops_t ahci_nxfs_ops;

int ahci_block_read(void *device, uint64_t block, void *buffer) {
    ahci_block_dev_t *dev = (ahci_block_dev_t *)device;
    if (!dev) return -1;
    
    /* Convert block number to sector number and add offset */
    uint64_t sector = (block * SECTORS_PER_BLOCK) + dev->partition_offset;
    
    /* Check bounds */
    if (sector + SECTORS_PER_BLOCK > dev->sector_count) {
        serial_puts("[AHCI_BLK] Read beyond device end\n");
        return -1;
    }
    
    /* Allocate DMA-safe buffer */
    uint64_t dma_phys = pmm_alloc_page();
    if (!dma_phys) {
        serial_puts("[AHCI_BLK] Failed to allocate DMA buffer\n");
        return -1;
    }
    
    uint8_t *dma_buf = (uint8_t *)dma_phys;
    
    /* Read 8 sectors (4KB) */
    if (ahci_read(dev->port, sector, SECTORS_PER_BLOCK, dma_buf) != 0) {
        pmm_free_page(dma_phys);
        serial_puts("[AHCI_BLK] AHCI read failed\n");
        return -1;
    }
    
    /* Copy to user buffer */
    ab_memcpy(buffer, dma_buf, BLOCK_SIZE);
    
    pmm_free_page(dma_phys);
    return 0;
}

int ahci_block_write(void *device, uint64_t block, const void *buffer) {
    ahci_block_dev_t *dev = (ahci_block_dev_t *)device;
    if (!dev) return -1;
    
    /* Convert block number to sector number and add offset */
    uint64_t sector = (block * SECTORS_PER_BLOCK) + dev->partition_offset;
    
    /* Check bounds */
    if (sector + SECTORS_PER_BLOCK > dev->sector_count) {
        return -1;
    }
    
    /* Allocate DMA-safe buffer */
    uint64_t dma_phys = pmm_alloc_page();
    if (!dma_phys) return -1;
    
    uint8_t *dma_buf = (uint8_t *)dma_phys;
    
    /* Copy from user buffer */
    ab_memcpy(dma_buf, buffer, BLOCK_SIZE);
    
    /* Write 8 sectors (4KB) */
    if (ahci_write(dev->port, sector, SECTORS_PER_BLOCK, dma_buf) != 0) {
        pmm_free_page(dma_phys);
        return -1;
    }
    
    pmm_free_page(dma_phys);
    return 0;
}

uint64_t ahci_block_get_count(void *device) {
    ahci_block_dev_t *dev = (ahci_block_dev_t *)device;
    if (!dev) return 0;
    return dev->block_count;
}

uint32_t ahci_block_get_size(void *device) {
    (void)device;
    return BLOCK_SIZE;
}

/* ============ Initialization ============ */

static ahci_block_dev_t block_devices[8];
static int block_device_count = 0;

ahci_block_dev_t *ahci_block_open(int port) {
    if (block_device_count >= 8) return NULL;
    
    /* Get drive info from AHCI */
    ahci_drive_t drive;
    
    /* We need to probe the specific port - use ahci_identify */
    if (ahci_identify(port, &drive) != 0) {
        serial_puts("[AHCI_BLK] Failed to identify drive\n");
        return NULL;
    }
    
    ahci_block_dev_t *dev = &block_devices[block_device_count++];
    dev->port = port;
    dev->sector_count = drive.sectors;
    dev->block_count = drive.sectors / SECTORS_PER_BLOCK;
    dev->partition_offset = 0; /* Whole disk */
    
    serial_puts("[AHCI_BLK] Opened port ");
    serial_putc('0' + port);
    serial_puts("\n");
    
    return dev;
}

ahci_block_dev_t *ahci_block_open_partition(int port, uint64_t start_sector, uint64_t sector_count) {
    if (block_device_count >= 8) return NULL;
    
    ahci_block_dev_t *dev = &block_devices[block_device_count++];
    dev->port = port;
    dev->sector_count = sector_count;
    dev->block_count = sector_count / SECTORS_PER_BLOCK;
    dev->partition_offset = start_sector;
    
    serial_puts("[AHCI_BLK] Opened partition on port ");
    serial_putc('0' + port);
    serial_puts("\n");
    
    return dev;
}

void ahci_block_close(ahci_block_dev_t *dev) {
    (void)dev;
    /* For now, nothing to clean up */
}

int ahci_block_init(void) {
    serial_puts("[AHCI_BLK] Initializing AHCI block adapter...\n");
    
    /* Set up NXFS block operations */
    ahci_nxfs_ops.read_block = ahci_block_read;
    ahci_nxfs_ops.write_block = ahci_block_write;
    ahci_nxfs_ops.get_block_count = ahci_block_get_count;
    ahci_nxfs_ops.get_block_size = ahci_block_get_size;
    
    /* Register with NXFS */
    nxfs_set_block_ops(&ahci_nxfs_ops);
    
    block_device_count = 0;
    
    serial_puts("[AHCI_BLK] Ready\n");
    return 0;
}
