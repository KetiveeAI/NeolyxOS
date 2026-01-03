/*
 * NeolyxOS NXFS - Native Filesystem
 * 
 * Custom filesystem for NeolyxOS with optional encryption.
 * Modern, fast, secure.
 * 
 * Features:
 *   - 64-bit addressing (supports huge files)
 *   - B+tree directories (fast lookup)
 *   - Extent-based allocation (less fragmentation)
 *   - Optional AES-256 encryption per-volume
 *   - Checksums for data integrity
 * 
 * Two modes:
 *   - NXFS (standard, unencrypted)
 *   - NXFS-E (encrypted, for sensitive data)
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NXFS_H
#define NEOLYX_NXFS_H

#include <stdint.h>
#include <stddef.h>
#include "fs/vfs.h"

/* ============ NXFS Constants ============ */

#define NXFS_MAGIC              0x4E584653      /* "NXFS" */
#define NXFS_VERSION            1
#define NXFS_BLOCK_SIZE         4096
#define NXFS_NAME_MAX           255

/* Superblock location */
#define NXFS_SUPERBLOCK_OFFSET  0

/* Encryption modes */
#define NXFS_ENCRYPT_NONE       0
#define NXFS_ENCRYPT_AES256     1

/* Inode flags */
#define NXFS_INODE_FILE         0x0001
#define NXFS_INODE_DIR          0x0002
#define NXFS_INODE_SYMLINK      0x0004
#define NXFS_INODE_ENCRYPTED    0x0100
#define NXFS_INODE_IMMUTABLE    0x0200
#define NXFS_INODE_SYSTEM       0x0400  /* Cannot be deleted by user */

/* Mount flags */
#define NXFS_MOUNT_RDONLY       0x0001
#define NXFS_MOUNT_ENCRYPTED    0x0002

/* ============ On-Disk Structures ============ */

/* Superblock - block 0 */
typedef struct {
    uint32_t magic;                 /* NXFS_MAGIC */
    uint32_t version;               /* Filesystem version */
    uint32_t block_size;            /* Block size (4096) */
    uint32_t flags;                 /* Filesystem flags */
    
    uint64_t total_blocks;          /* Total blocks in filesystem */
    uint64_t free_blocks;           /* Free blocks */
    uint64_t total_inodes;          /* Total inodes */
    uint64_t free_inodes;           /* Free inodes */
    
    uint64_t inode_table_block;     /* Block number of inode table */
    uint64_t block_bitmap_block;    /* Block number of block bitmap */
    uint64_t inode_bitmap_block;    /* Block number of inode bitmap */
    uint64_t root_inode;            /* Inode number of root directory */
    
    uint32_t encryption_mode;       /* NXFS_ENCRYPT_* */
    uint8_t  encryption_key_hash[32]; /* SHA-256 of encryption key */
    uint8_t  encryption_iv[16];     /* Initialization vector */
    
    uint64_t created;               /* Creation timestamp */
    uint64_t mounted;               /* Last mount timestamp */
    uint32_t mount_count;           /* Number of times mounted */
    uint32_t max_mount_count;       /* Max mounts before fsck */
    
    char     volume_name[64];       /* Volume label */
    char     uuid[16];              /* Volume UUID */
    
    uint32_t checksum;              /* Superblock checksum */
    uint8_t  reserved[3896];        /* Pad to 4096 bytes */
} __attribute__((packed)) nxfs_superblock_t;

/* Inode - 256 bytes */
typedef struct {
    uint32_t flags;                 /* Inode flags */
    uint32_t permissions;           /* rwxrwxrwx */
    uint32_t uid;                   /* Owner user ID */
    uint32_t gid;                   /* Owner group ID */
    
    uint64_t size;                  /* File size */
    uint64_t blocks;                /* Number of blocks used */
    
    uint64_t created;               /* Creation time */
    uint64_t modified;              /* Modification time */
    uint64_t accessed;              /* Access time */
    
    uint32_t link_count;            /* Hard link count */
    uint32_t checksum;              /* Data checksum */
    
    /* Extent-based allocation (up to 12 extents inline) */
    struct {
        uint64_t start_block;       /* Starting block */
        uint32_t block_count;       /* Number of blocks */
        uint32_t logical_start;     /* Logical block offset */
    } extents[12];
    
    uint64_t extent_tree;           /* Block of extent tree (if > 12 extents) */
    
    uint8_t  reserved[8];           /* Padding to 256 bytes */
} __attribute__((packed)) nxfs_inode_t;

/* Directory entry - 32 bytes */
typedef struct {
    uint64_t inode;                 /* Inode number */
    uint16_t record_len;            /* Total record length */
    uint8_t  name_len;              /* Name length */
    uint8_t  file_type;             /* File type */
    char     name[20];              /* Filename (variable, up to 255) */
} __attribute__((packed)) nxfs_dirent_t;

/* ============ In-Memory Structures ============ */

/* NXFS mount info */
typedef struct {
    nxfs_superblock_t superblock;
    void *device;                   /* Block device */
    uint8_t *block_bitmap;          /* Block allocation bitmap */
    uint8_t *inode_bitmap;          /* Inode allocation bitmap */
    vfs_node_t *root;               /* Root directory vnode */
    uint32_t flags;                 /* Mount flags */
    
    /* Encryption state */
    int encrypted;
    uint8_t encryption_key[32];     /* AES-256 key (in memory only) */
} nxfs_mount_t;

/* ============ NXFS API ============ */

/**
 * Initialize NXFS and register with VFS.
 */
void nxfs_init(void);

/**
 * Format a device with NXFS.
 * 
 * @param device        Block device
 * @param total_blocks  Total blocks
 * @param volume_name   Volume label
 * @param encrypted     Enable encryption
 * @param key           Encryption key (32 bytes, NULL if not encrypted)
 * @return 0 on success
 */
int nxfs_format(void *device, uint64_t total_blocks, const char *volume_name,
                int encrypted, const uint8_t *key);

/**
 * Mount NXFS.
 * Called by VFS.
 */
int nxfs_mount(vfs_mount_t *mount);

/**
 * Unmount NXFS.
 */
int nxfs_unmount(vfs_mount_t *mount);

/**
 * Sync NXFS to disk.
 */
int nxfs_sync(vfs_mount_t *mount);

/* ============ Block Device Interface ============ */

/* Block device operations (to be provided by disk driver) */
typedef struct {
    int (*read_block)(void *device, uint64_t block, void *buffer);
    int (*write_block)(void *device, uint64_t block, const void *buffer);
    uint64_t (*get_block_count)(void *device);
    uint32_t (*get_block_size)(void *device);
} nxfs_block_ops_t;

/**
 * Set block device operations.
 */
void nxfs_set_block_ops(nxfs_block_ops_t *ops);

#endif /* NEOLYX_NXFS_H */
