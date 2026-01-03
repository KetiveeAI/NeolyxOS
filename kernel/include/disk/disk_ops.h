/*
 * NeolyxOS Disk Operations API
 * 
 * C backend for NeolyxOS Disk Manager
 * Provides disk scanning, partitioning, formatting operations
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef DISK_OPS_H
#define DISK_OPS_H

#include <stdint.h>

/* ============ Constants ============ */

#define DISK_MAX_DISKS       16
#define DISK_MAX_PARTITIONS  32
#define DISK_NAME_MAX        64

/* Disk types */
#define DISK_TYPE_UNKNOWN    0
#define DISK_TYPE_HDD        1
#define DISK_TYPE_SSD        2
#define DISK_TYPE_NVME       3
#define DISK_TYPE_USB        4
#define DISK_TYPE_VIRTUAL    5

/* Partition filesystem types */
#define FS_TYPE_UNKNOWN      0
#define FS_TYPE_NXFS         1
#define FS_TYPE_FAT32        2
#define FS_TYPE_EXT4         3
#define FS_TYPE_NTFS         4
#define FS_TYPE_SWAP         5
#define FS_TYPE_EFI          6

/* ============ Structures ============ */

/* Disk information */
typedef struct {
    uint32_t disk_id;
    uint8_t  type;              /* DISK_TYPE_* */
    char     name[DISK_NAME_MAX];
    char     model[DISK_NAME_MAX];
    char     serial[DISK_NAME_MAX];
    char     firmware[32];
    
    uint64_t size_bytes;
    uint64_t size_sectors;
    uint32_t sector_size;
    
    /* S.M.A.R.T. data */
    uint8_t  health_percent;    /* 0-100 */
    uint8_t  temperature_c;     /* Celsius */
    uint64_t power_on_hours;
    uint32_t read_speed_mbps;   /* MB/s */
    uint32_t write_speed_mbps;  /* MB/s */
    
    uint8_t  is_boot_disk;
    uint8_t  is_removable;
    uint8_t  is_ssd;
    uint8_t  supports_trim;
    
    uint32_t partition_count;
} DiskInfo;

/* Partition information */
typedef struct {
    uint32_t partition_id;
    uint32_t disk_id;
    uint8_t  fs_type;           /* FS_TYPE_* */
    char     label[DISK_NAME_MAX];
    char     mount_point[DISK_NAME_MAX];
    
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t size_bytes;
    uint64_t used_bytes;
    
    uint8_t  is_mounted;
    uint8_t  is_bootable;
    uint8_t  is_encrypted;
} PartitionInfo;

/* System compatibility info */
typedef struct {
    uint8_t  cpu_compatible;
    uint8_t  gpu_compatible;
    uint8_t  ram_sufficient;
    uint8_t  disk_sufficient;
    uint8_t  uefi_available;
    
    char     cpu_name[64];
    char     gpu_name[64];
    uint32_t ram_mb;
    uint64_t disk_free_bytes;
    char     firmware_type[16];  /* "UEFI" or "BIOS" */
} SystemCompatInfo;

/* ============ Disk Scanning ============ */

/* Initialize disk subsystem */
int disk_ops_init(void);

/* Scan for all disks */
int disk_scan_all(void);

/* Get number of detected disks */
int disk_get_count(void);

/* Get disk information */
int disk_get_info(uint32_t disk_id, DiskInfo *info);

/* Get S.M.A.R.T. data */
int disk_get_smart(uint32_t disk_id, DiskInfo *info);

/* ============ Partition Operations ============ */

/* Get partitions on disk */
int disk_get_partitions(uint32_t disk_id, PartitionInfo *parts, uint32_t max_parts);

/* Get partition count */
int disk_get_partition_count(uint32_t disk_id);

/* Get single partition info */
int disk_get_partition_info(uint32_t disk_id, uint32_t part_id, PartitionInfo *info);

/* Create new partition */
int disk_create_partition(uint32_t disk_id, uint64_t size_mb, uint8_t fs_type, const char *label);

/* Delete partition */
int disk_delete_partition(uint32_t disk_id, uint32_t part_id);

/* Resize partition */
int disk_resize_partition(uint32_t disk_id, uint32_t part_id, uint64_t new_size_mb);

/* ============ Filesystem Operations ============ */

/* Format partition with filesystem */
int disk_format(uint32_t disk_id, uint32_t part_id, uint8_t fs_type, const char *label);

/* Format with NXFS specifically */
int disk_format_nxfs(uint32_t disk_id, uint32_t part_id, const char *label);

/* Mount partition */
int disk_mount(uint32_t disk_id, uint32_t part_id, const char *mount_point);

/* Unmount partition */
int disk_unmount(uint32_t disk_id, uint32_t part_id);

/* Check filesystem */
int disk_check_fs(uint32_t disk_id, uint32_t part_id);

/* ============ Secure Operations ============ */

/* Secure erase entire disk */
int disk_secure_erase(uint32_t disk_id);

/* Wipe partition */
int disk_wipe_partition(uint32_t disk_id, uint32_t part_id);

/* ============ System Compatibility ============ */

/* Check system compatibility */
int system_check_compatibility(SystemCompatInfo *compat);

/* Get minimum requirements */
int system_get_min_requirements(uint32_t *min_ram_mb, uint64_t *min_disk_bytes);

/* ============ Installation ============ */

/* Install NeolyxOS to disk */
int disk_install_neolyx(uint32_t disk_id, uint32_t part_id);

/* Create EFI boot entry */
int disk_create_efi_entry(uint32_t disk_id, uint32_t efi_part_id);

/* ============ Low-level I/O ============ */

/* Read sectors */
int disk_read_sectors(uint32_t disk_id, uint64_t start_lba, uint32_t count, void *buffer);

/* Write sectors */
int disk_write_sectors(uint32_t disk_id, uint64_t start_lba, uint32_t count, const void *buffer);

/* Read block (4KB) */
int disk_read_block(uint32_t disk_id, uint64_t block, void *buffer);

/* Write block (4KB) */
int disk_write_block(uint32_t disk_id, uint64_t block, const void *buffer);

/* Get disk size in blocks */
uint64_t disk_get_size_blocks(uint32_t disk_id);

/* Get partition start block */
uint64_t partition_get_start(uint32_t disk_id, uint32_t part_id);

/* Get partition size in blocks */
uint64_t partition_get_size(uint32_t disk_id, uint32_t part_id);

#endif /* DISK_OPS_H */
