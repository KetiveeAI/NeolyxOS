/*
 * NeolyxOS NXFS File Operations
 * 
 * File read/write with block mapping
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
extern int nxfs_zero_block(uint32_t disk_id, uint64_t block_num);

/* ============ Inode Functions ============ */
extern int nxfs_read_inode(uint32_t disk_id, uint64_t inode_num, nxfs_inode_t *inode);
extern int nxfs_write_inode(uint32_t disk_id, uint64_t inode_num, const nxfs_inode_t *inode);
extern int nxfs_alloc_block(void);
extern int nxfs_free_block(uint64_t block_num);

/* ============ Constants ============ */
#define NXFS_BLOCK_SIZE     4096
#define NXFS_DIRECT_BLOCKS  8
#define NXFS_DATA_START     10  /* First data block */

/* ============ Block Mapping ============ */

/**
 * nxfs_get_block - Get block number for file offset
 */
static int64_t nxfs_get_block(nxfs_inode_t *inode, uint32_t block_index, int allocate)
{
    if (block_index >= NXFS_DIRECT_BLOCKS) {
        /* Indirect blocks not yet implemented */
        serial_print("[NXFS] Indirect blocks not supported\r\n");
        return -1;
    }
    
    uint64_t block_num = inode->direct_blocks[block_index];
    
    if (block_num == 0 && allocate) {
        /* Allocate new block */
        int new_block = nxfs_alloc_block();
        if (new_block < 0) {
            return -1;
        }
        
        inode->direct_blocks[block_index] = NXFS_DATA_START + new_block;
        block_num = inode->direct_blocks[block_index];
    }
    
    return block_num;
}

/* ============ File Operations ============ */

/**
 * nxfs_file_read - Read data from file
 */
int nxfs_file_read(uint32_t disk_id, nxfs_inode_t *inode, 
                   uint64_t offset, void *buffer, uint32_t size)
{
    if (!inode || !buffer || size == 0) {
        return -1;
    }
    
    /* Check bounds */
    if (offset >= inode->size) {
        return 0;  /* EOF */
    }
    
    if (offset + size > inode->size) {
        size = inode->size - offset;
    }
    
    uint8_t *dest = (uint8_t *)buffer;
    uint32_t bytes_read = 0;
    uint8_t block_buf[NXFS_BLOCK_SIZE];
    
    while (bytes_read < size) {
        uint32_t block_index = (offset + bytes_read) / NXFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_read) % NXFS_BLOCK_SIZE;
        uint32_t to_read = NXFS_BLOCK_SIZE - block_offset;
        
        if (to_read > size - bytes_read) {
            to_read = size - bytes_read;
        }
        
        int64_t block_num = nxfs_get_block(inode, block_index, 0);
        
        if (block_num <= 0) {
            /* Sparse block - return zeros */
            memset(dest + bytes_read, 0, to_read);
        } else {
            int err = nxfs_read_block(disk_id, block_num, block_buf);
            if (err) {
                return bytes_read > 0 ? bytes_read : err;
            }
            memcpy(dest + bytes_read, block_buf + block_offset, to_read);
        }
        
        bytes_read += to_read;
    }
    
    return bytes_read;
}

/**
 * nxfs_file_write - Write data to file
 */
int nxfs_file_write(uint32_t disk_id, nxfs_inode_t *inode,
                    uint64_t offset, const void *buffer, uint32_t size)
{
    if (!inode || !buffer || size == 0) {
        return -1;
    }
    
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t bytes_written = 0;
    uint8_t block_buf[NXFS_BLOCK_SIZE];
    
    while (bytes_written < size) {
        uint32_t block_index = (offset + bytes_written) / NXFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_written) % NXFS_BLOCK_SIZE;
        uint32_t to_write = NXFS_BLOCK_SIZE - block_offset;
        
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }
        
        int64_t block_num = nxfs_get_block(inode, block_index, 1);
        if (block_num < 0) {
            return bytes_written > 0 ? bytes_written : -1;
        }
        
        /* If partial block write, read existing data first */
        if (block_offset != 0 || to_write != NXFS_BLOCK_SIZE) {
            int err = nxfs_read_block(disk_id, block_num, block_buf);
            if (err) {
                memset(block_buf, 0, NXFS_BLOCK_SIZE);
            }
        }
        
        memcpy(block_buf + block_offset, src + bytes_written, to_write);
        
        int err = nxfs_write_block(disk_id, block_num, block_buf);
        if (err) {
            return bytes_written > 0 ? bytes_written : err;
        }
        
        bytes_written += to_write;
    }
    
    /* Update file size if needed */
    if (offset + bytes_written > inode->size) {
        inode->size = offset + bytes_written;
    }
    
    return bytes_written;
}

/**
 * nxfs_file_truncate - Truncate file to specified size
 */
int nxfs_file_truncate(uint32_t disk_id, nxfs_inode_t *inode, uint64_t new_size)
{
    if (!inode) {
        return -1;
    }
    
    if (new_size >= inode->size) {
        /* Extending - just update size */
        inode->size = new_size;
        return 0;
    }
    
    /* Shrinking - free blocks */
    uint32_t new_blocks = (new_size + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint32_t old_blocks = (inode->size + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    
    for (uint32_t i = new_blocks; i < old_blocks && i < NXFS_DIRECT_BLOCKS; i++) {
        if (inode->direct_blocks[i] != 0) {
            nxfs_free_block(inode->direct_blocks[i] - NXFS_DATA_START);
            inode->direct_blocks[i] = 0;
        }
    }
    
    inode->size = new_size;
    
    (void)disk_id;  /* Used for indirect blocks in future */
    return 0;
}

/**
 * nxfs_file_create - Create new file inode
 */
int nxfs_file_create(uint32_t disk_id, const char *name, uint32_t mode)
{
    extern int nxfs_alloc_inode(void);
    
    int ino = nxfs_alloc_inode();
    if (ino < 0) {
        return -1;
    }
    
    nxfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_num = ino;
    inode.mode = mode;
    inode.size = 0;
    
    /* Copy name */
    if (name) {
        uint64_t len = 0;
        while (name[len] && len < NXFS_MAX_FILENAME - 1) {
            inode.name[len] = name[len];
            len++;
        }
        inode.name[len] = '\0';
    }
    
    int err = nxfs_write_inode(disk_id, ino, &inode);
    if (err) {
        extern int nxfs_free_inode(uint64_t inode_num);
        nxfs_free_inode(ino);
        return -1;
    }
    
    return ino;
}

/**
 * nxfs_file_delete - Delete file and free blocks
 */
int nxfs_file_delete(uint32_t disk_id, uint64_t inode_num)
{
    nxfs_inode_t inode;
    
    int err = nxfs_read_inode(disk_id, inode_num, &inode);
    if (err) {
        return err;
    }
    
    /* Free all data blocks */
    for (int i = 0; i < NXFS_DIRECT_BLOCKS; i++) {
        if (inode.direct_blocks[i] != 0) {
            nxfs_free_block(inode.direct_blocks[i] - NXFS_DATA_START);
        }
    }
    
    /* Free inode */
    extern int nxfs_free_inode(uint64_t inode_num);
    return nxfs_free_inode(inode_num);
}