/*
 * NeolyxOS Block Device Interface
 * 
 * Generic block device abstraction layer.
 * Filesystem and VFS use this interface - backend can be NVMe, AHCI, ATA, etc.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

/* ============ Constants ============ */

#define BLOCK_MAX_DEVICES       16
#define BLOCK_NAME_MAX          32
#define BLOCK_SECTOR_SIZE       512

/* Block device types */
typedef enum {
    BLOCK_TYPE_UNKNOWN = 0,
    BLOCK_TYPE_NVME,
    BLOCK_TYPE_AHCI,
    BLOCK_TYPE_ATA,
    BLOCK_TYPE_USB,
    BLOCK_TYPE_RAMDISK,
} block_type_t;

/* Block device flags */
#define BLOCK_FLAG_REMOVABLE    0x01
#define BLOCK_FLAG_READONLY     0x02
#define BLOCK_FLAG_PRESENT      0x04

/* ============ Block Device Operations ============ */

struct block_device;

typedef struct {
    /**
     * Read blocks from device.
     * @param dev: Block device
     * @param lba: Starting logical block address
     * @param count: Number of blocks to read
     * @param buffer: Output buffer
     * @return: Bytes read, or negative error code
     */
    int (*read)(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
    
    /**
     * Write blocks to device.
     * @param dev: Block device
     * @param lba: Starting logical block address
     * @param count: Number of blocks to write
     * @param buffer: Input buffer
     * @return: Bytes written, or negative error code
     */
    int (*write)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
    
    /**
     * Flush device buffers to media.
     * @param dev: Block device
     * @return: 0 on success, negative error code
     */
    int (*flush)(struct block_device *dev);
    
    /**
     * Get device capacity in bytes.
     * @param dev: Block device
     * @return: Capacity in bytes
     */
    uint64_t (*get_capacity)(struct block_device *dev);
    
} block_ops_t;

/* ============ Block Device Structure ============ */

typedef struct block_device {
    /* Device identification */
    char            name[BLOCK_NAME_MAX];   /* e.g., "nvme0n1", "sda" */
    block_type_t    type;
    uint32_t        flags;
    
    /* Geometry */
    uint64_t        total_blocks;
    uint32_t        block_size;             /* Usually 512 or 4096 */
    
    /* Operations */
    block_ops_t     *ops;
    
    /* Backend-specific private data */
    void            *private_data;
    
    /* Device index (for iteration) */
    int             index;
    
} block_device_t;

/* ============ Block Subsystem API ============ */

/**
 * Initialize block subsystem.
 */
void block_init(void);

/**
 * Register a block device.
 * @param dev: Populated block device structure
 * @return: Device index (>=0) or negative error
 */
int block_register(block_device_t *dev);

/**
 * Unregister a block device.
 * @param index: Device index from block_register
 * @return: 0 on success
 */
int block_unregister(int index);

/**
 * Get block device by index.
 * @param index: Device index
 * @return: Block device or NULL
 */
block_device_t *block_get(int index);

/**
 * Get block device by name.
 * @param name: Device name (e.g., "nvme0n1")
 * @return: Block device or NULL
 */
block_device_t *block_find(const char *name);

/**
 * Get number of registered block devices.
 */
int block_count(void);

/* ============ High-Level Block I/O ============ */

/**
 * Read from any block device.
 * Uses the device's ops->read internally.
 */
int block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer);

/**
 * Write to any block device.
 * Uses the device's ops->write internally.
 */
int block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer);

/**
 * Flush block device.
 */
int block_flush(block_device_t *dev);

/**
 * Get capacity of block device in bytes.
 */
uint64_t block_capacity(block_device_t *dev);

#endif /* BLOCK_H */
