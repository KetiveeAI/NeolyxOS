/*
 * NeolyxOS NXFS Block I/O
 * 
 * Low-level block read/write operations with caching
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

/* ============ Block Cache ============ */
#define BLOCK_CACHE_SIZE    64
#define BLOCK_SIZE          4096

typedef struct block_buffer {
    uint64_t            block_num;
    uint32_t            disk_id;
    uint8_t             data[BLOCK_SIZE];
    int                 valid;
    int                 dirty;
    int                 refcount;
    struct block_buffer *hash_next;
    struct block_buffer *lru_next;
    struct block_buffer *lru_prev;
} block_buffer_t;

static block_buffer_t block_cache[BLOCK_CACHE_SIZE];
static block_buffer_t *block_hash[32];
static block_buffer_t block_lru_head;
static int block_cache_initialized = 0;

/* ============ Storage Driver Interface ============ */
/* These would normally call the actual disk driver */
extern int storage_read_sectors(uint32_t disk_id, uint64_t lba, 
                                void *buffer, uint32_t count);
extern int storage_write_sectors(uint32_t disk_id, uint64_t lba, 
                                 const void *buffer, uint32_t count);

/* Fallback implementations if driver not available */
__attribute__((weak))
int storage_read_sectors(uint32_t disk_id, uint64_t lba, 
                         void *buffer, uint32_t count)
{
    (void)disk_id; (void)lba; (void)count;
    memset(buffer, 0, count * 512);
    return 0;
}

__attribute__((weak))
int storage_write_sectors(uint32_t disk_id, uint64_t lba, 
                          const void *buffer, uint32_t count)
{
    (void)disk_id; (void)lba; (void)buffer; (void)count;
    return 0;
}

/* ============ Hash Function ============ */
static inline uint32_t block_hash_fn(uint64_t block_num, uint32_t disk_id)
{
    return ((block_num ^ disk_id) >> 2) % 32;
}

/* ============ LRU Management ============ */
static void block_lru_init(void)
{
    block_lru_head.lru_next = &block_lru_head;
    block_lru_head.lru_prev = &block_lru_head;
}

static void block_lru_remove(block_buffer_t *b)
{
    if (b->lru_prev && b->lru_next) {
        b->lru_prev->lru_next = b->lru_next;
        b->lru_next->lru_prev = b->lru_prev;
        b->lru_next = b->lru_prev = 0;
    }
}

static void block_lru_add_tail(block_buffer_t *b)
{
    b->lru_prev = block_lru_head.lru_prev;
    b->lru_next = &block_lru_head;
    block_lru_head.lru_prev->lru_next = b;
    block_lru_head.lru_prev = b;
}

/* ============ Cache Initialization ============ */
int nxfs_io_init(void)
{
    if (block_cache_initialized) {
        return 0;
    }
    
    memset(block_cache, 0, sizeof(block_cache));
    memset(block_hash, 0, sizeof(block_hash));
    block_lru_init();
    
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        block_lru_add_tail(&block_cache[i]);
    }
    
    block_cache_initialized = 1;
    serial_print("[NXFS] Block I/O cache initialized\r\n");
    return 0;
}

/* ============ Cache Lookup ============ */
static block_buffer_t *block_cache_lookup(uint64_t block_num, uint32_t disk_id)
{
    uint32_t hash = block_hash_fn(block_num, disk_id);
    block_buffer_t *b = block_hash[hash];
    
    while (b) {
        if (b->valid && b->block_num == block_num && b->disk_id == disk_id) {
            return b;
        }
        b = b->hash_next;
    }
    return 0;
}

/* ============ Cache Insert ============ */
static void block_cache_insert(block_buffer_t *b)
{
    uint32_t hash = block_hash_fn(b->block_num, b->disk_id);
    b->hash_next = block_hash[hash];
    block_hash[hash] = b;
}

/* ============ Cache Remove ============ */
static void block_cache_remove_hash(block_buffer_t *b)
{
    uint32_t hash = block_hash_fn(b->block_num, b->disk_id);
    block_buffer_t **pp = &block_hash[hash];
    
    while (*pp) {
        if (*pp == b) {
            *pp = b->hash_next;
            b->hash_next = 0;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* ============ Writeback ============ */
static int block_writeback(block_buffer_t *b)
{
    if (!b->valid || !b->dirty) {
        return 0;
    }
    
    /* Convert block to sectors (assuming 512-byte sectors) */
    uint64_t lba = b->block_num * (BLOCK_SIZE / 512);
    uint32_t sectors = BLOCK_SIZE / 512;
    
    int err = storage_write_sectors(b->disk_id, lba, b->data, sectors);
    if (err == 0) {
        b->dirty = 0;
    }
    return err;
}

/* ============ Allocate Block Buffer ============ */
static block_buffer_t *block_alloc(void)
{
    block_buffer_t *b = block_lru_head.lru_next;
    
    while (b != &block_lru_head) {
        if (b->refcount == 0) {
            if (b->valid) {
                if (b->dirty) {
                    block_writeback(b);
                }
                block_cache_remove_hash(b);
            }
            block_lru_remove(b);
            memset(b, 0, sizeof(block_buffer_t));
            return b;
        }
        b = b->lru_next;
    }
    
    serial_print("[NXFS] ERROR: Block cache full\r\n");
    return 0;
}

/* ============ Public API ============ */

/**
 * nxfs_read_block - Read a block from disk
 */
int nxfs_read_block(uint32_t disk_id, uint64_t block_num, void *buffer)
{
    if (!block_cache_initialized) {
        nxfs_io_init();
    }
    
    if (!buffer) {
        return -1;
    }
    
    /* Check cache */
    block_buffer_t *b = block_cache_lookup(block_num, disk_id);
    if (b) {
        memcpy(buffer, b->data, BLOCK_SIZE);
        return 0;
    }
    
    /* Allocate and read */
    b = block_alloc();
    if (!b) {
        /* Direct read without caching */
        uint64_t lba = block_num * (BLOCK_SIZE / 512);
        return storage_read_sectors(disk_id, lba, buffer, BLOCK_SIZE / 512);
    }
    
    b->block_num = block_num;
    b->disk_id = disk_id;
    
    /* Read from disk */
    uint64_t lba = block_num * (BLOCK_SIZE / 512);
    int err = storage_read_sectors(disk_id, lba, b->data, BLOCK_SIZE / 512);
    if (err) {
        block_lru_add_tail(b);
        return err;
    }
    
    b->valid = 1;
    b->dirty = 0;
    b->refcount = 0;
    
    block_cache_insert(b);
    block_lru_add_tail(b);
    
    memcpy(buffer, b->data, BLOCK_SIZE);
    return 0;
}

/**
 * nxfs_write_block - Write a block to disk (with caching)
 */
int nxfs_write_block(uint32_t disk_id, uint64_t block_num, const void *buffer)
{
    if (!block_cache_initialized) {
        nxfs_io_init();
    }
    
    if (!buffer) {
        return -1;
    }
    
    /* Check cache */
    block_buffer_t *b = block_cache_lookup(block_num, disk_id);
    if (b) {
        memcpy(b->data, buffer, BLOCK_SIZE);
        b->dirty = 1;
        return 0;
    }
    
    /* Allocate new block */
    b = block_alloc();
    if (!b) {
        /* Direct write without caching */
        uint64_t lba = block_num * (BLOCK_SIZE / 512);
        return storage_write_sectors(disk_id, lba, buffer, BLOCK_SIZE / 512);
    }
    
    b->block_num = block_num;
    b->disk_id = disk_id;
    memcpy(b->data, buffer, BLOCK_SIZE);
    b->valid = 1;
    b->dirty = 1;
    b->refcount = 0;
    
    block_cache_insert(b);
    block_lru_add_tail(b);
    
    return 0;
}

/**
 * nxfs_sync_blocks - Write all dirty blocks to disk
 */
int nxfs_sync_blocks(uint32_t disk_id)
{
    int synced = 0;
    
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (block_cache[i].valid && block_cache[i].dirty) {
            if (disk_id == (uint32_t)-1 || block_cache[i].disk_id == disk_id) {
                block_writeback(&block_cache[i]);
                synced++;
            }
        }
    }
    
    return synced;
}

/**
 * nxfs_invalidate_blocks - Invalidate cached blocks for a disk
 */
int nxfs_invalidate_blocks(uint32_t disk_id)
{
    int invalidated = 0;
    
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (block_cache[i].valid && block_cache[i].disk_id == disk_id) {
            if (block_cache[i].dirty) {
                block_writeback(&block_cache[i]);
            }
            block_cache_remove_hash(&block_cache[i]);
            block_cache[i].valid = 0;
            invalidated++;
        }
    }
    
    return invalidated;
}

/**
 * nxfs_zero_block - Write zeros to a block
 */
int nxfs_zero_block(uint32_t disk_id, uint64_t block_num)
{
    uint8_t zeros[BLOCK_SIZE];
    memset(zeros, 0, BLOCK_SIZE);
    return nxfs_write_block(disk_id, block_num, zeros);
}