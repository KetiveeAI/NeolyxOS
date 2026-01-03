/*
 * NXStorage Kernel Driver (nxstor.kdrv)
 * 
 * NeolyxOS Unified Storage Kernel Driver
 * 
 * Architecture:
 *   [ NXStor Kernel Driver ] ← Unified Storage API
 *        ↕ Backend Drivers
 *   [ ATA | AHCI | NVMe ] ← Hardware-specific
 *        ↕ Block Layer
 *   [ File Systems ]
 * 
 * Supports: ATA, AHCI (SATA), NVMe
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXSTOR_KDRV_H
#define NXSTOR_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXSTOR_KDRV_VERSION    "1.0.0"
#define NXSTOR_KDRV_ABI        1

/* ============ Storage Types ============ */

typedef enum {
    NXSTOR_TYPE_NONE = 0,
    NXSTOR_TYPE_ATA,        /* IDE/PATA */
    NXSTOR_TYPE_AHCI,       /* SATA */
    NXSTOR_TYPE_NVME,       /* NVMe SSD */
    NXSTOR_TYPE_USB,        /* USB Mass Storage */
    NXSTOR_TYPE_SCSI,       /* SCSI */
    NXSTOR_TYPE_VIRTUAL,    /* Ramdisk, etc. */
} nxstor_type_t;

/* ============ Media Types ============ */

typedef enum {
    NXSTOR_MEDIA_HDD = 0,   /* Hard Disk Drive */
    NXSTOR_MEDIA_SSD,       /* Solid State Drive */
    NXSTOR_MEDIA_OPTICAL,   /* CD/DVD/Blu-ray */
    NXSTOR_MEDIA_REMOVABLE, /* USB stick, SD card */
    NXSTOR_MEDIA_VIRTUAL,   /* Ramdisk */
} nxstor_media_t;

/* ============ Device Status ============ */

typedef enum {
    NXSTOR_STATUS_NONE = 0,
    NXSTOR_STATUS_ONLINE,
    NXSTOR_STATUS_OFFLINE,
    NXSTOR_STATUS_ERROR,
    NXSTOR_STATUS_REMOVED,
} nxstor_status_t;

/* ============ Device Info ============ */

typedef struct {
    uint32_t        id;
    nxstor_type_t   type;
    nxstor_media_t  media;
    nxstor_status_t status;
    
    /* Identification */
    char            model[48];
    char            serial[24];
    char            firmware[12];
    
    /* Geometry */
    uint64_t        total_sectors;
    uint32_t        sector_size;
    uint64_t        total_bytes;
    
    /* Performance hints */
    uint32_t        max_transfer_kb;
    uint8_t         supports_trim;
    uint8_t         supports_ncq;
    uint8_t         is_rotational;
    
    /* Controller info */
    uint8_t         controller_id;
    uint8_t         port;
} nxstor_device_info_t;

/* ============ I/O Statistics ============ */

typedef struct {
    uint64_t        reads;
    uint64_t        writes;
    uint64_t        bytes_read;
    uint64_t        bytes_written;
    uint64_t        errors;
    uint64_t        retries;
} nxstor_stats_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize storage subsystem
 */
int nxstor_kdrv_init(void);

/**
 * Shutdown storage subsystem
 */
void nxstor_kdrv_shutdown(void);

/**
 * Get number of storage devices
 */
int nxstor_kdrv_device_count(void);

/**
 * Get device info
 */
int nxstor_kdrv_device_info(int index, nxstor_device_info_t *info);

/**
 * Read sectors from device
 * 
 * @param device_id: Device index
 * @param lba: Starting logical block address
 * @param count: Number of sectors to read
 * @param buffer: Buffer for data (must be count * sector_size bytes)
 * 
 * Returns: Number of bytes read, or negative on error
 */
int nxstor_kdrv_read(int device_id, uint64_t lba, uint32_t count, void *buffer);

/**
 * Write sectors to device
 */
int nxstor_kdrv_write(int device_id, uint64_t lba, uint32_t count, const void *buffer);

/**
 * Sync device (flush caches)
 */
int nxstor_kdrv_sync(int device_id);

/**
 * TRIM/Discard sectors (SSD optimization)
 */
int nxstor_kdrv_trim(int device_id, uint64_t lba, uint64_t count);

/**
 * Get I/O statistics
 */
int nxstor_kdrv_get_stats(int device_id, nxstor_stats_t *stats);

/**
 * Reset device
 */
int nxstor_kdrv_reset(int device_id);

/**
 * Get type name
 */
const char *nxstor_kdrv_type_name(nxstor_type_t type);

/**
 * Get media name
 */
const char *nxstor_kdrv_media_name(nxstor_media_t media);

/**
 * Debug output
 */
void nxstor_kdrv_debug(void);

#endif /* NXSTOR_KDRV_H */
