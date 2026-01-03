/*
 * NeolyxOS VFS Inode Implementation
 * 
 * Inode management with caching and reference counting
 * 
 * Copyright (c) 2024-2025 KetiveeAI and its branches.
 * Licensed under the KetiveeAI License.
 */

#include "vfs.h"
#include "../../drivers/serial.h"

/* ============ Memory Functions ============ */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, uint64_t n);
extern void *memcpy(void *dest, const void *src, uint64_t n);

/* ============ Inode Cache ============ */
#define INODE_CACHE_SIZE    256
#define INODE_HASH_SIZE     64

typedef struct inode_entry {
    struct inode        inode;
    int                 refcount;
    int                 dirty;
    int                 valid;
    struct inode_entry *hash_next;
    struct inode_entry *lru_next;
    struct inode_entry *lru_prev;
} inode_entry_t;

static inode_entry_t inode_cache[INODE_CACHE_SIZE];
static inode_entry_t *hash_table[INODE_HASH_SIZE];
static inode_entry_t lru_head;  /* Doubly-linked LRU list */
static int cache_initialized = 0;

/* ============ Hash Function ============ */
static inline uint32_t inode_hash(uint32_t ino, struct super_block *sb)
{
    return ((ino ^ (uint64_t)sb) >> 2) % INODE_HASH_SIZE;
}

/* ============ LRU Management ============ */
static void lru_init(void)
{
    lru_head.lru_next = &lru_head;
    lru_head.lru_prev = &lru_head;
}

static void lru_remove(inode_entry_t *entry)
{
    if (entry->lru_prev && entry->lru_next) {
        entry->lru_prev->lru_next = entry->lru_next;
        entry->lru_next->lru_prev = entry->lru_prev;
        entry->lru_next = entry->lru_prev = 0;
    }
}

static void lru_add_tail(inode_entry_t *entry)
{
    entry->lru_prev = lru_head.lru_prev;
    entry->lru_next = &lru_head;
    lru_head.lru_prev->lru_next = entry;
    lru_head.lru_prev = entry;
}

/* ============ Cache Initialization ============ */
int vfs_inode_cache_init(void)
{
    if (cache_initialized) {
        return 0;
    }
    
    memset(inode_cache, 0, sizeof(inode_cache));
    memset(hash_table, 0, sizeof(hash_table));
    lru_init();
    
    /* Add all entries to LRU (available) list */
    for (int i = 0; i < INODE_CACHE_SIZE; i++) {
        lru_add_tail(&inode_cache[i]);
    }
    
    cache_initialized = 1;
    serial_print("[VFS] Inode cache initialized\r\n");
    return 0;
}

/* ============ Find Inode in Cache ============ */
static inode_entry_t *cache_lookup(uint32_t ino, struct super_block *sb)
{
    uint32_t hash = inode_hash(ino, sb);
    inode_entry_t *entry = hash_table[hash];
    
    while (entry) {
        if (entry->valid && 
            entry->inode.i_ino == ino && 
            entry->inode.i_sb == sb) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return 0;
}

/* ============ Insert Inode in Hash ============ */
static void cache_insert(inode_entry_t *entry)
{
    uint32_t hash = inode_hash(entry->inode.i_ino, entry->inode.i_sb);
    entry->hash_next = hash_table[hash];
    hash_table[hash] = entry;
}

/* ============ Remove from Hash ============ */
static void cache_remove_hash(inode_entry_t *entry)
{
    uint32_t hash = inode_hash(entry->inode.i_ino, entry->inode.i_sb);
    inode_entry_t **pp = &hash_table[hash];
    
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            entry->hash_next = 0;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* ============ Allocate Cache Entry ============ */
static inode_entry_t *cache_alloc(void)
{
    /* Get least recently used entry with refcount 0 */
    inode_entry_t *entry = lru_head.lru_next;
    
    while (entry != &lru_head) {
        if (entry->refcount == 0) {
            /* Found one - remove from hash if valid */
            if (entry->valid) {
                /* Writeback if dirty */
                if (entry->dirty) {
                    /* Filesystem-specific writeback would go here */
                    entry->dirty = 0;
                }
                cache_remove_hash(entry);
                entry->valid = 0;
            }
            lru_remove(entry);
            return entry;
        }
        entry = entry->lru_next;
    }
    
    /* No free entries */
    serial_print("[VFS] ERROR: Inode cache full\r\n");
    return 0;
}

/* ============ Public API ============ */

/**
 * vfs_iget - Get inode by number, loading from disk if needed
 */
struct inode *vfs_iget(struct super_block *sb, uint32_t ino)
{
    if (!cache_initialized) {
        vfs_inode_cache_init();
    }
    
    if (!sb) {
        return 0;
    }
    
    /* Check cache first */
    inode_entry_t *entry = cache_lookup(ino, sb);
    if (entry) {
        entry->refcount++;
        lru_remove(entry);  /* Active inodes not in LRU */
        return &entry->inode;
    }
    
    /* Allocate new cache entry */
    entry = cache_alloc();
    if (!entry) {
        return 0;
    }
    
    /* Initialize inode */
    memset(&entry->inode, 0, sizeof(struct inode));
    entry->inode.i_ino = ino;
    entry->inode.i_sb = sb;
    entry->refcount = 1;
    entry->dirty = 0;
    entry->valid = 1;
    
    /* Insert into hash */
    cache_insert(entry);
    
    return &entry->inode;
}

/**
 * vfs_iput - Release inode reference
 */
void vfs_iput(struct inode *inode)
{
    if (!inode) {
        return;
    }
    
    /* Find cache entry */
    inode_entry_t *entry = (inode_entry_t *)((char *)inode - 
                           (uint64_t)&((inode_entry_t *)0)->inode);
    
    if (entry->refcount > 0) {
        entry->refcount--;
    }
    
    if (entry->refcount == 0) {
        /* Add back to LRU */
        lru_add_tail(entry);
    }
}

/**
 * vfs_inode_create - Allocate new inode
 */
struct inode *vfs_inode_create(struct super_block *sb)
{
    static uint32_t next_ino = 1;
    
    if (!cache_initialized) {
        vfs_inode_cache_init();
    }
    
    inode_entry_t *entry = cache_alloc();
    if (!entry) {
        return 0;
    }
    
    memset(&entry->inode, 0, sizeof(struct inode));
    entry->inode.i_ino = next_ino++;
    entry->inode.i_sb = sb;
    entry->refcount = 1;
    entry->dirty = 1;  /* New inode needs writeback */
    entry->valid = 1;
    
    cache_insert(entry);
    
    return &entry->inode;
}

/**
 * vfs_inode_destroy - Free inode
 */
void vfs_inode_destroy(struct inode *inode)
{
    if (!inode) {
        return;
    }
    
    inode_entry_t *entry = (inode_entry_t *)((char *)inode - 
                           (uint64_t)&((inode_entry_t *)0)->inode);
    
    /* Remove from hash */
    cache_remove_hash(entry);
    
    /* Clear and add to LRU */
    entry->valid = 0;
    entry->dirty = 0;
    entry->refcount = 0;
    lru_add_tail(entry);
}

/**
 * vfs_inode_mark_dirty - Mark inode as needing writeback
 */
void vfs_inode_mark_dirty(struct inode *inode)
{
    if (!inode) {
        return;
    }
    
    inode_entry_t *entry = (inode_entry_t *)((char *)inode - 
                           (uint64_t)&((inode_entry_t *)0)->inode);
    entry->dirty = 1;
}

/**
 * vfs_inode_sync - Write dirty inode to disk
 */
int vfs_inode_sync(struct inode *inode)
{
    if (!inode || !inode->i_sb) {
        return -1;
    }
    
    inode_entry_t *entry = (inode_entry_t *)((char *)inode - 
                           (uint64_t)&((inode_entry_t *)0)->inode);
    
    if (!entry->dirty) {
        return 0;  /* Nothing to do */
    }
    
    /* Filesystem-specific writeback would be called here via i_sb->s_op */
    entry->dirty = 0;
    return 0;
}

/**
 * vfs_inode_sync_all - Sync all dirty inodes
 */
int vfs_inode_sync_all(void)
{
    int synced = 0;
    
    for (int i = 0; i < INODE_CACHE_SIZE; i++) {
        if (inode_cache[i].valid && inode_cache[i].dirty) {
            vfs_inode_sync(&inode_cache[i].inode);
            synced++;
        }
    }
    
    return synced;
}