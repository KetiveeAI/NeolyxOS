/*
 * NeolyxOS NTFS Filesystem Driver (Read-Only)
 * 
 * Read-only support for Windows NTFS partitions.
 * 
 * Features:
 *   - Read files and directories
 *   - Long filename support
 *   - Large file support (>4GB)
 *   - Compressed files (read)
 * 
 * Note: Write support is complex and deferred to Phase 5+.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NTFS_H
#define NEOLYX_NTFS_H

#include <stdint.h>
#include <stddef.h>
#include "fs/vfs.h"

/* ============ NTFS Constants ============ */

#define NTFS_SECTOR_SIZE        512
#define NTFS_SIGNATURE          0x5346544E  /* "NTFS" */

/* File record flags */
#define NTFS_FILE_IN_USE        0x0001
#define NTFS_FILE_DIRECTORY     0x0002

/* Attribute types */
#define NTFS_ATTR_STANDARD_INFO     0x10
#define NTFS_ATTR_ATTRIBUTE_LIST    0x20
#define NTFS_ATTR_FILENAME          0x30
#define NTFS_ATTR_OBJECT_ID         0x40
#define NTFS_ATTR_SECURITY_DESC     0x50
#define NTFS_ATTR_VOLUME_NAME       0x60
#define NTFS_ATTR_VOLUME_INFO       0x70
#define NTFS_ATTR_DATA              0x80
#define NTFS_ATTR_INDEX_ROOT        0x90
#define NTFS_ATTR_INDEX_ALLOC       0xA0
#define NTFS_ATTR_BITMAP            0xB0
#define NTFS_ATTR_END               0xFFFFFFFF

/* Special MFT entries */
#define NTFS_MFT_SELF               0
#define NTFS_MFT_MIRROR             1
#define NTFS_MFT_LOGFILE            2
#define NTFS_MFT_VOLUME             3
#define NTFS_MFT_ATTDEF             4
#define NTFS_MFT_ROOT               5
#define NTFS_MFT_BITMAP             6
#define NTFS_MFT_BOOT               7
#define NTFS_MFT_BADCLUS            8

/* ============ On-Disk Structures ============ */

/* NTFS Boot Sector */
typedef struct {
    uint8_t  jmp[3];
    char     oem_id[8];             /* "NTFS    " */
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  unused1[3];
    uint16_t unused2;
    uint8_t  media_descriptor;
    uint16_t unused3;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t unused4;
    uint32_t unused5;
    uint64_t total_sectors;
    uint64_t mft_lcn;               /* Logical cluster of MFT */
    uint64_t mft_mirror_lcn;
    int8_t   clusters_per_file_record;
    uint8_t  unused6[3];
    int8_t   clusters_per_index;
    uint8_t  unused7[3];
    uint64_t volume_serial;
    uint32_t checksum;
} __attribute__((packed)) ntfs_boot_t;

/* MFT File Record Header */
typedef struct {
    char     signature[4];          /* "FILE" */
    uint16_t update_seq_offset;
    uint16_t update_seq_count;
    uint64_t lsn;
    uint16_t sequence;
    uint16_t link_count;
    uint16_t attrs_offset;
    uint16_t flags;
    uint32_t bytes_used;
    uint32_t bytes_allocated;
    uint64_t base_record;
    uint16_t next_attr_id;
} __attribute__((packed)) ntfs_file_record_t;

/* Attribute Header */
typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t  non_resident;
    uint8_t  name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t instance;
    union {
        struct {
            uint32_t value_length;
            uint16_t value_offset;
            uint16_t indexed;
        } resident;
        struct {
            uint64_t lowest_vcn;
            uint64_t highest_vcn;
            uint16_t runlist_offset;
            uint16_t compression_unit;
            uint32_t padding;
            uint64_t allocated_size;
            uint64_t data_size;
            uint64_t initialized_size;
        } non_resident;
    };
} __attribute__((packed)) ntfs_attr_header_t;

/* Filename Attribute */
typedef struct {
    uint64_t parent_ref;
    uint64_t creation_time;
    uint64_t modification_time;
    uint64_t mft_modification_time;
    uint64_t access_time;
    uint64_t allocated_size;
    uint64_t data_size;
    uint32_t flags;
    uint32_t reparse;
    uint8_t  name_length;
    uint8_t  name_type;
    uint16_t name[255];             /* UTF-16LE filename */
} __attribute__((packed)) ntfs_filename_attr_t;

/* Index Entry Header */
typedef struct {
    uint64_t mft_ref;
    uint16_t length;
    uint16_t stream_length;
    uint8_t  flags;
    uint8_t  padding[3];
} __attribute__((packed)) ntfs_index_entry_t;

/* ============ In-Memory Structures ============ */

typedef struct {
    void *device;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint64_t mft_lcn;
    uint32_t bytes_per_file_record;
    uint64_t total_sectors;
    
    uint8_t *mft_buffer;            /* Buffer for reading MFT records */
    vfs_node_t *root;
} ntfs_mount_t;

/* ============ NTFS API ============ */

/**
 * Initialize NTFS driver and register with VFS.
 */
void ntfs_init(void);

/**
 * Mount NTFS (read-only).
 */
int ntfs_mount(vfs_mount_t *mount);

/**
 * Unmount NTFS.
 */
int ntfs_unmount(vfs_mount_t *mount);

/* ============ MFT Operations ============ */

/**
 * Read an MFT record.
 */
int ntfs_read_mft_record(ntfs_mount_t *ntfs, uint64_t mft_num, void *buffer);

/**
 * Find attribute in file record.
 */
ntfs_attr_header_t *ntfs_find_attr(void *record, uint32_t type);

/**
 * Read attribute data.
 */
int64_t ntfs_read_attr(ntfs_mount_t *ntfs, void *record, uint32_t type, 
                       void *buffer, uint64_t offset, uint64_t size);

#endif /* NEOLYX_NTFS_H */
