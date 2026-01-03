/*
 * NeolyxOS NXFS Inode Operations
 * 
 * Inode allocation, reading, and writing
 * 
 * Copyright (c) 2024-2025 KetiveeAI and its branches.
 * Licensed under the KetiveeAI License.
 */

#include "nxfs.h"
#include "../../drivers/serial.h"

/* ============ Memory Functions ============ */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, uint64_t n);
extern void *memcpy(void *dest, const void *src, uint64_t n);

/* ============ Block I/O Functions ============ */
extern int nxfs_read_block(uint32_t disk_id, uint64_t block_num, void *buffer);
extern int nxfs_write_block(uint32_t disk_id, uint64_t block_num, const void *buffer);

/* ============ NXFS Layout Constants ============ */
#define NXFS_SUPERBLOCK_BLOCK   0
#define NXFS_INODE_BITMAP_BLOCK 1
#define NXFS_DATA_BITMAP_BLOCK  2
#define NXFS_INODE_TABLE_BLOCK  3
#define NXFS_INODES_PER_BLOCK   (4096 / sizeof(nxfs_inode_t))
#define NXFS_MAX_INODES         1024
#define NXFS_BLOCK_SIZE         4096

/* ============ Filesystem State ============ */
typedef struct {
    uint32_t        disk_id;
    nxfs_superblock_t superblock;
    uint8_t         inode_bitmap[128];  /* 1024 bits = 128 bytes */
    uint8_t         data_bitmap[512];   /* 4096 bits = 512 bytes */
    int             mounted;
} nxfs_state_t;

static nxfs_state_t fs_state;

/* ============ Bitmap Operations ============ */
static inline int bitmap_test(uint8_t *bitmap, uint32_t bit)
{
    return bitmap[bit / 8] & (1 << (bit % 8));
}

static inline void bitmap_set(uint8_t *bitmap, uint32_t bit)
{
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint8_t *bitmap, uint32_t bit)
{
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static int bitmap_find_free(uint8_t *bitmap, uint32_t max_bits)
{
    for (uint32_t i = 0; i < max_bits; i++) {
        if (!bitmap_test(bitmap, i)) {
            return i;
        }
    }
    return -1;
}

/* ============ Superblock Operations ============ */
int nxfs_read_superblock(uint32_t disk_id, nxfs_superblock_t *sb)
{
    uint8_t block[NXFS_BLOCK_SIZE];
    
    int err = nxfs_read_block(disk_id, NXFS_SUPERBLOCK_BLOCK, block);
    if (err) {
        return err;
    }
    
    memcpy(sb, block, sizeof(nxfs_superblock_t));
    
    if (sb->magic != NXFS_MAGIC) {
        serial_print("[NXFS] Invalid magic number\r\n");
        return -1;
    }
    
    return 0;
}

int nxfs_write_superblock(uint32_t disk_id, const nxfs_superblock_t *sb)
{
    uint8_t block[NXFS_BLOCK_SIZE];
    memset(block, 0, NXFS_BLOCK_SIZE);
    memcpy(block, sb, sizeof(nxfs_superblock_t));
    
    return nxfs_write_block(disk_id, NXFS_SUPERBLOCK_BLOCK, block);
}

/* ============ Format Filesystem ============ */
int nxfs_format(uint32_t disk_id, uint64_t total_blocks)
{
    serial_print("[NXFS] Formatting filesystem...\r\n");
    
    /* Initialize superblock */
    nxfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = NXFS_MAGIC;
    sb.version = 1;
    sb.root_inode = 1;  /* Root inode is #1 */
    sb.block_size = NXFS_BLOCK_SIZE;
    sb.block_count = total_blocks;
    
    int err = nxfs_write_superblock(disk_id, &sb);
    if (err) {
        return err;
    }
    
    /* Initialize inode bitmap (mark 0 and 1 as used) */
    uint8_t inode_bitmap[NXFS_BLOCK_SIZE];
    memset(inode_bitmap, 0, NXFS_BLOCK_SIZE);
    bitmap_set(inode_bitmap, 0);  /* Reserved */
    bitmap_set(inode_bitmap, 1);  /* Root inode */
    
    err = nxfs_write_block(disk_id, NXFS_INODE_BITMAP_BLOCK, inode_bitmap);
    if (err) {
        return err;
    }
    
    /* Initialize data bitmap (mark first few blocks as used) */
    uint8_t data_bitmap[NXFS_BLOCK_SIZE];
    memset(data_bitmap, 0, NXFS_BLOCK_SIZE);
    for (int i = 0; i < 10; i++) {  /* Reserve metadata blocks */
        bitmap_set(data_bitmap, i);
    }
    
    err = nxfs_write_block(disk_id, NXFS_DATA_BITMAP_BLOCK, data_bitmap);
    if (err) {
        return err;
    }
    
    /* Create root inode */
    nxfs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.inode_num = 1;
    root_inode.mode = 0040755;  /* Directory */
    root_inode.size = 0;
    memcpy(root_inode.name, "/", 2);
    
    err = nxfs_write_inode(disk_id, 1, &root_inode);
    if (err) {
        return err;
    }
    
    serial_print("[NXFS] Format complete\r\n");
    return 0;
}

/* ============ Mount Filesystem ============ */
int nxfs_mount_fs(uint32_t disk_id)
{
    if (fs_state.mounted) {
        return -1;  /* Already mounted */
    }
    
    /* Read superblock */
    int err = nxfs_read_superblock(disk_id, &fs_state.superblock);
    if (err) {
        return err;
    }
    
    /* Read bitmaps */
    uint8_t block[NXFS_BLOCK_SIZE];
    
    err = nxfs_read_block(disk_id, NXFS_INODE_BITMAP_BLOCK, block);
    if (err) {
        return err;
    }
    memcpy(fs_state.inode_bitmap, block, sizeof(fs_state.inode_bitmap));
    
    err = nxfs_read_block(disk_id, NXFS_DATA_BITMAP_BLOCK, block);
    if (err) {
        return err;
    }
    memcpy(fs_state.data_bitmap, block, sizeof(fs_state.data_bitmap));
    
    fs_state.disk_id = disk_id;
    fs_state.mounted = 1;
    
    serial_print("[NXFS] Filesystem mounted\r\n");
    return 0;
}

/* ============ Unmount Filesystem ============ */
int nxfs_unmount_fs(void)
{
    if (!fs_state.mounted) {
        return -1;
    }
    
    /* Sync bitmaps */
    uint8_t block[NXFS_BLOCK_SIZE];
    
    memset(block, 0, NXFS_BLOCK_SIZE);
    memcpy(block, fs_state.inode_bitmap, sizeof(fs_state.inode_bitmap));
    nxfs_write_block(fs_state.disk_id, NXFS_INODE_BITMAP_BLOCK, block);
    
    memset(block, 0, NXFS_BLOCK_SIZE);
    memcpy(block, fs_state.data_bitmap, sizeof(fs_state.data_bitmap));
    nxfs_write_block(fs_state.disk_id, NXFS_DATA_BITMAP_BLOCK, block);
    
    /* Sync blocks */
    extern int nxfs_sync_blocks(uint32_t disk_id);
    nxfs_sync_blocks(fs_state.disk_id);
    
    fs_state.mounted = 0;
    serial_print("[NXFS] Filesystem unmounted\r\n");
    return 0;
}

/* ============ Inode Operations ============ */

/**
 * nxfs_read_inode - Read inode from disk
 */
int nxfs_read_inode(uint32_t disk_id, uint64_t inode_num, nxfs_inode_t *inode)
{
    if (!inode || inode_num == 0 || inode_num >= NXFS_MAX_INODES) {
        return -1;
    }
    
    /* Calculate which block contains this inode */
    uint64_t block_num = NXFS_INODE_TABLE_BLOCK + (inode_num / NXFS_INODES_PER_BLOCK);
    uint64_t offset = (inode_num % NXFS_INODES_PER_BLOCK) * sizeof(nxfs_inode_t);
    
    uint8_t block[NXFS_BLOCK_SIZE];
    int err = nxfs_read_block(disk_id, block_num, block);
    if (err) {
        return err;
    }
    
    memcpy(inode, block + offset, sizeof(nxfs_inode_t));
    return 0;
}

/**
 * nxfs_write_inode - Write inode to disk
 */
int nxfs_write_inode(uint32_t disk_id, uint64_t inode_num, const nxfs_inode_t *inode)
{
    if (!inode || inode_num == 0 || inode_num >= NXFS_MAX_INODES) {
        return -1;
    }
    
    uint64_t block_num = NXFS_INODE_TABLE_BLOCK + (inode_num / NXFS_INODES_PER_BLOCK);
    uint64_t offset = (inode_num % NXFS_INODES_PER_BLOCK) * sizeof(nxfs_inode_t);
    
    uint8_t block[NXFS_BLOCK_SIZE];
    int err = nxfs_read_block(disk_id, block_num, block);
    if (err) {
        memset(block, 0, NXFS_BLOCK_SIZE);
    }
    
    memcpy(block + offset, inode, sizeof(nxfs_inode_t));
    return nxfs_write_block(disk_id, block_num, block);
}

/**
 * nxfs_alloc_inode - Allocate new inode
 */
int nxfs_alloc_inode(void)
{
    if (!fs_state.mounted) {
        return -1;
    }
    
    int ino = bitmap_find_free(fs_state.inode_bitmap, NXFS_MAX_INODES);
    if (ino < 0) {
        serial_print("[NXFS] No free inodes\r\n");
        return -1;
    }
    
    bitmap_set(fs_state.inode_bitmap, ino);
    return ino;
}

/**
 * nxfs_free_inode - Free inode
 */
int nxfs_free_inode(uint64_t inode_num)
{
    if (!fs_state.mounted || inode_num == 0 || inode_num >= NXFS_MAX_INODES) {
        return -1;
    }
    
    bitmap_clear(fs_state.inode_bitmap, inode_num);
    return 0;
}

/**
 * nxfs_alloc_block - Allocate data block
 */
int nxfs_alloc_block(void)
{
    if (!fs_state.mounted) {
        return -1;
    }
    
    int block = bitmap_find_free(fs_state.data_bitmap, 4096);
    if (block < 0) {
        serial_print("[NXFS] No free blocks\r\n");
        return -1;
    }
    
    bitmap_set(fs_state.data_bitmap, block);
    return block;
}

/**
 * nxfs_free_block - Free data block
 */
int nxfs_free_block(uint64_t block_num)
{
    if (!fs_state.mounted || block_num >= 4096) {
        return -1;
    }
    
    bitmap_clear(fs_state.data_bitmap, block_num);
    return 0;
}

/**
 * nxfs_get_state - Get filesystem state
 */
nxfs_state_t *nxfs_get_state(void)
{
    return fs_state.mounted ? &fs_state : 0;
}