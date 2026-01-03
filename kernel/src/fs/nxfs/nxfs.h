// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
// Disk Manager interface for NXFS
#ifndef NXFS_DISK_MANAGER_H
#define NXFS_DISK_MANAGER_H

#include <stdint.h>

#define NXFS_MAX_PARTITIONS 16
#define NXFS_MAX_DISKS 8

// Partition types (local to disk manager)
#define NXFS_PARTITION_TYPE_NXFS      0x01
#define NXFS_PARTITION_TYPE_FAT32     0x02
#define NXFS_PARTITION_TYPE_ENCRYPTED 0x10
#define NXFS_PARTITION_TYPE_VIRTUAL   0x20
#define NXFS_PARTITION_TYPE_OTHER     0xFF

// Partition entry (disk manager specific - not the same as NXFS on-disk dirent)
typedef struct nxfs_dm_partition {
    uint8_t  type;           // Partition type
    uint8_t  status;         // 0=inactive, 1=active (mounted)
    uint64_t start_lba;      // Start sector
    uint64_t end_lba;        // End sector
    char     label[32];      // Partition label
} nxfs_partition_t;

// Disk info (disk manager runtime info)
typedef struct nxfs_dm_disk {
    uint32_t disk_id;
    uint64_t size_bytes;
    uint32_t sector_size;
    uint32_t partition_count;
    nxfs_partition_t partitions[NXFS_MAX_PARTITIONS];
    char     name[32];
    uint8_t  is_virtual;     // 1=virtual disk, 0=physical
    uint8_t  is_encrypted;   // 1=encrypted
} nxfs_disk_t;

// Disk Manager (runtime state)
typedef struct nxfs_dm_manager {
    uint32_t disk_count;
    nxfs_disk_t disks[NXFS_MAX_DISKS];
    int selected_disk;
    int selected_partition;
} nxfs_disk_manager_t;

// Disk Manager API
void nxfs_disk_manager_init(nxfs_disk_manager_t* mgr);
void nxfs_scan_disks(nxfs_disk_manager_t* mgr);
int  nxfs_create_partition(nxfs_disk_t* disk, uint64_t start_lba, uint64_t end_lba, uint8_t type, const char* label);
int  nxfs_delete_partition(nxfs_disk_t* disk, int part_idx);
int  nxfs_format_partition(nxfs_disk_t* disk, int part_idx, uint8_t fs_type, int encrypted);
int  nxfs_resize_partition(nxfs_disk_t* disk, int part_idx, uint64_t new_end_lba);
int  nxfs_mount_partition(nxfs_disk_t* disk, int part_idx);
int  nxfs_unmount_partition(nxfs_disk_t* disk, int part_idx);
int  nxfs_create_virtual_disk(nxfs_disk_manager_t* mgr, uint64_t size_bytes, const char* name);
int  nxfs_delete_virtual_disk(nxfs_disk_manager_t* mgr, int disk_idx);

// GUI Integration
void nxfs_show_disk_manager_gui(nxfs_disk_manager_t* mgr);

// App API (for user apps to access disk manager)
int nxfs_get_disk_info(nxfs_disk_manager_t* mgr, int disk_idx, nxfs_disk_t* out_disk);
int nxfs_get_partition_info(nxfs_disk_t* disk, int part_idx, nxfs_partition_t* out_part);

#endif // NXFS_DISK_MANAGER_H