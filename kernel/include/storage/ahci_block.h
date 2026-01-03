/*
 * AHCI Block Device Adapter for NXFS
 * 
 * Bridges AHCI driver to NXFS block device operations.
 * Converts between AHCI sectors (512 bytes) and NXFS blocks (4096 bytes).
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_AHCI_BLOCK_H
#define NEOLYX_AHCI_BLOCK_H

#include <stdint.h>

/* AHCI block device handle */
typedef struct {
    int port;               /* AHCI port number */
    uint64_t sector_count;  /* Total sectors on device/partition */
    uint64_t block_count;   /* Total 4KB blocks (sector_count / 8) */
    uint64_t partition_offset; /* Offset in sectors from start of disk */
} ahci_block_dev_t;

/**
 * Initialize AHCI block device adapter.
 * Must be called before using NXFS with AHCI.
 * 
 * @return 0 on success
 */
int ahci_block_init(void);

/**
 * Open an AHCI port as a block device.
 * 
 * @param port AHCI port number
 * @return Block device handle or NULL on error
 */
ahci_block_dev_t *ahci_block_open(int port);

/**
 * Open an AHCI partition as a block device.
 * 
 * @param port AHCI port number
 * @param start_sector Start sector (LBA) of partition
 * @param sector_count Size of partition in sectors
 * @return Block device handle or NULL on error
 */
ahci_block_dev_t *ahci_block_open_partition(int port, uint64_t start_sector, uint64_t sector_count);

/**
 * Close an AHCI block device.
 */
void ahci_block_close(ahci_block_dev_t *dev);

/**
 * Read a 4KB block from AHCI device.
 * 
 * @param device Block device handle
 * @param block Block number (4KB block)
 * @param buffer Buffer to read into (must be 4096 bytes)
 * @return 0 on success
 */
int ahci_block_read(void *device, uint64_t block, void *buffer);

/**
 * Write a 4KB block to AHCI device.
 * 
 * @param device Block device handle
 * @param block Block number (4KB block)
 * @param buffer Data to write (must be 4096 bytes)
 * @return 0 on success
 */
int ahci_block_write(void *device, uint64_t block, const void *buffer);

/**
 * Get total block count (4KB blocks).
 */
uint64_t ahci_block_get_count(void *device);

/**
 * Get block size (always 4096).
 */
uint32_t ahci_block_get_size(void *device);

#endif /* NEOLYX_AHCI_BLOCK_H */
