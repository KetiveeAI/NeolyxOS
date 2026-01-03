/*
 * NeolyxOS FAT32/exFAT Filesystem Driver
 * 
 * Universal compatibility filesystem for USB drives, SD cards, etc.
 * Supports FAT12, FAT16, FAT32, and exFAT.
 * 
 * Features:
 *   - Full read/write support
 *   - Long filename support (LFN)
 *   - exFAT for large files (>4GB)
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_FAT_H
#define NEOLYX_FAT_H

#include <stdint.h>
#include <stddef.h>
#include "fs/vfs.h"

/* ============ FAT Constants ============ */

#define FAT_SECTOR_SIZE     512

/* FAT Types */
#define FAT_TYPE_FAT12      12
#define FAT_TYPE_FAT16      16
#define FAT_TYPE_FAT32      32
#define FAT_TYPE_EXFAT      64

/* Attribute flags */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F  /* Long filename entry */

/* Special cluster values (FAT32) */
#define FAT32_EOC           0x0FFFFFF8  /* End of cluster chain */
#define FAT32_BAD           0x0FFFFFF7  /* Bad cluster */
#define FAT32_FREE          0x00000000  /* Free cluster */

/* ============ On-Disk Structures ============ */

/* BIOS Parameter Block (common) */
typedef struct {
    uint8_t  jmp[3];            /* Jump instruction */
    char     oem_name[8];       /* OEM identifier */
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;  /* FAT12/16 only */
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;       /* FAT12/16 only */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed)) fat_bpb_t;

/* Extended BPB for FAT32 */
typedef struct {
    fat_bpb_t bpb;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

/* Directory entry (8.3 format) */
typedef struct {
    char     name[8];           /* Filename (padded with spaces) */
    char     ext[3];            /* Extension */
    uint8_t  attr;              /* Attributes */
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;  /* High 16 bits (FAT32) */
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_lo;  /* Low 16 bits */
    uint32_t file_size;
} __attribute__((packed)) fat_dirent_t;

/* Long filename entry */
typedef struct {
    uint8_t  order;             /* Sequence number */
    uint16_t name1[5];          /* Characters 1-5 */
    uint8_t  attr;              /* Always 0x0F */
    uint8_t  type;              /* Always 0 */
    uint8_t  checksum;
    uint16_t name2[6];          /* Characters 6-11 */
    uint16_t zero;              /* Always 0 */
    uint16_t name3[2];          /* Characters 12-13 */
} __attribute__((packed)) fat_lfn_t;

/* ============ In-Memory Structures ============ */

/* FAT mount info */
typedef struct {
    void *device;
    int fat_type;               /* FAT_TYPE_* */
    
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    
    uint32_t fat_start;         /* First FAT sector */
    uint32_t fat_sectors;       /* Sectors per FAT */
    uint32_t root_start;        /* Root directory sector (FAT12/16) */
    uint32_t root_sectors;      /* Root directory sectors (FAT12/16) */
    uint32_t data_start;        /* First data sector */
    uint32_t total_clusters;
    
    uint32_t root_cluster;      /* Root cluster (FAT32) */
    
    char volume_label[12];
    
    vfs_node_t *root;
} fat_mount_t;

/* ============ FAT API ============ */

/**
 * Initialize FAT driver and register with VFS.
 */
void fat_init(void);

/**
 * Mount FAT filesystem.
 */
int fat_mount(vfs_mount_t *mount);

/**
 * Unmount FAT filesystem.
 */
int fat_unmount(vfs_mount_t *mount);

/**
 * Sync FAT to disk.
 */
int fat_sync(vfs_mount_t *mount);

/* ============ Cluster Operations ============ */

/**
 * Read a cluster.
 */
int fat_read_cluster(fat_mount_t *fat, uint32_t cluster, void *buffer);

/**
 * Write a cluster.
 */
int fat_write_cluster(fat_mount_t *fat, uint32_t cluster, const void *buffer);

/**
 * Get next cluster in chain.
 */
uint32_t fat_get_next_cluster(fat_mount_t *fat, uint32_t cluster);

/**
 * Allocate a free cluster.
 */
uint32_t fat_alloc_cluster(fat_mount_t *fat);

/**
 * Free a cluster chain.
 */
int fat_free_chain(fat_mount_t *fat, uint32_t start);

#endif /* NEOLYX_FAT_H */
