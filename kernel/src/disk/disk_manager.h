/*
 * NeolyxOS Disk Manager - Interactive Partition Manager
 * 
 * Production-ready disk management UI with:
 * - Framebuffer-based graphical interface
 * - Keyboard navigation
 * - Partition creation/deletion/formatting
 * - NXFS format with verification
 * - Comprehensive error handling
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <stdint.h>

/* ============ Return Codes ============ */
#define DSKM_SUCCESS           0
#define DSKM_ERR_NO_DISKS     -1
#define DSKM_ERR_NO_SPACE     -2
#define DSKM_ERR_INVALID      -3
#define DSKM_ERR_IO           -4
#define DSKM_ERR_CANCELLED    -5
#define DSKM_ERR_FORMAT_FAIL  -6

/* ============ Disk Types ============ */
#define DISK_TYPE_UNKNOWN      0
#define DISK_TYPE_HDD          1
#define DISK_TYPE_SSD          2
#define DISK_TYPE_NVME         3
#define DISK_TYPE_USB          4
#define DISK_TYPE_VIRTUAL      5

/* ============ Partition Types ============ */
#define PART_TYPE_EMPTY        0x00
#define PART_TYPE_NXFS         0x01
#define PART_TYPE_FAT32        0x02
#define PART_TYPE_EXT4         0x03
#define PART_TYPE_SWAP         0x04
#define PART_TYPE_EFI          0x05
#define PART_TYPE_ENCRYPTED    0x10

/* ============ Disk Info ============ */
#define MAX_DISKS             16
#define MAX_PARTITIONS        32
#define DISK_NAME_LEN         64
#define PART_LABEL_LEN        32

typedef struct {
    uint8_t  type;
    uint8_t  active;
    uint64_t start_lba;
    uint64_t size_lba;
    char     label[PART_LABEL_LEN];
    uint8_t  formatted;
    uint8_t  mounted;
} partition_t;

typedef struct {
    uint32_t id;
    uint8_t  type;
    char     name[DISK_NAME_LEN];
    char     model[DISK_NAME_LEN];
    char     serial[32];
    uint64_t capacity_bytes;
    uint32_t sector_size;
    uint32_t partition_count;
    partition_t partitions[MAX_PARTITIONS];
    uint8_t  is_boot_disk;
    uint8_t  is_removable;
} disk_t;

typedef struct {
    uint32_t disk_count;
    disk_t   disks[MAX_DISKS];
    int      selected_disk;
    int      selected_partition;
    int      view_mode;  /* 0 = disk list, 1 = partition list */
} disk_manager_t;

/* ============ UI State ============ */
typedef enum {
    UI_STATE_MAIN,
    UI_STATE_DISK_LIST,
    UI_STATE_PARTITION_LIST,
    UI_STATE_FORMAT_CONFIRM,
    UI_STATE_FORMATTING,
    UI_STATE_CREATE_PARTITION,
    UI_STATE_DELETE_CONFIRM,
    UI_STATE_SUCCESS,
    UI_STATE_ERROR
} ui_state_t;

/* ============ Public API ============ */

/**
 * dm_init - Initialize disk manager
 * 
 * Scans for available disks and initializes the UI state.
 * 
 * @mgr: Pointer to disk manager structure
 * Returns: DSKM_SUCCESS or error code
 */
int dm_init(disk_manager_t *mgr);

/**
 * dm_scan_disks - Scan for available disks
 * 
 * Enumerates all ATA/AHCI/NVMe disks and populates the disk list.
 * 
 * @mgr: Disk manager structure
 * Returns: Number of disks found, or negative error
 */
int dm_scan_disks(disk_manager_t *mgr);

/**
 * dm_run - Run interactive disk manager UI
 * 
 * Main event loop for the disk manager. Displays UI and handles
 * keyboard input until user exits.
 * 
 * @mgr: Disk manager structure
 * @fb_addr: Framebuffer address
 * @fb_width: Framebuffer width
 * @fb_height: Framebuffer height
 * @fb_pitch: Framebuffer pitch
 * Returns: Selected action result or DSKM_ERR_CANCELLED
 */
int dm_run(disk_manager_t *mgr, uint64_t fb_addr, 
           uint32_t fb_width, uint32_t fb_height, uint32_t fb_pitch);

/**
 * dm_format_partition - Format partition with NXFS
 * 
 * Formats the specified partition with NXFS filesystem.
 * Shows progress and verifies the format.
 * 
 * @mgr: Disk manager
 * @disk_idx: Disk index
 * @part_idx: Partition index
 * Returns: DSKM_SUCCESS or error code
 */
int dm_format_partition(disk_manager_t *mgr, int disk_idx, int part_idx);

/**
 * dm_create_partition - Create new partition
 * 
 * Creates a new partition on the specified disk.
 * 
 * @mgr: Disk manager
 * @disk_idx: Disk index
 * @start_lba: Start sector
 * @size_lba: Size in sectors
 * @type: Partition type
 * @label: Partition label
 * Returns: DSKM_SUCCESS or error code
 */
int dm_create_partition(disk_manager_t *mgr, int disk_idx,
                       uint64_t start_lba, uint64_t size_lba,
                       uint8_t type, const char *label);

/**
 * dm_delete_partition - Delete partition
 * 
 * @mgr: Disk manager
 * @disk_idx: Disk index
 * @part_idx: Partition index
 * Returns: DSKM_SUCCESS or error code
 */
int dm_delete_partition(disk_manager_t *mgr, int disk_idx, int part_idx);

#endif /* DISK_MANAGER_H */
