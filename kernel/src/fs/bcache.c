/*
 * NeolyxOS Block Cache
 * 
 * LRU cache for filesystem blocks.
 * Reduces disk I/O by caching recently accessed blocks.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void *kmalloc(uint64_t size);
extern void *kzalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ Cache Configuration ============ */

#define BCACHE_SIZE     128   /* Number of cached blocks */
#define BLOCK_SIZE      4096  /* 4KB blocks */

/* Block flags */
#define BCACHE_VALID    0x01  /* Block contains valid data */
#define BCACHE_DIRTY    0x02  /* Block has been modified */
#define BCACHE_LOCKED   0x04  /* Block is currently in use */

/* ============ Cache Entry ============ */

typedef struct bcache_entry {
    uint64_t device_id;       /* Device this block belongs to */
    uint64_t block_num;       /* Block number on device */
    uint32_t flags;           /* VALID, DIRTY, LOCKED */
    uint32_t ref_count;       /* Number of users holding this block */
    uint8_t  data[BLOCK_SIZE]; /* Actual block data */
    
    /* LRU linked list */
    struct bcache_entry *lru_prev;
    struct bcache_entry *lru_next;
    
    /* Hash chain for fast lookup */
    struct bcache_entry *hash_next;
} bcache_entry_t;

/* ============ Cache State ============ */

static bcache_entry_t *cache_entries = NULL;
static bcache_entry_t *lru_head = NULL;  /* Most recently used */
static bcache_entry_t *lru_tail = NULL;  /* Least recently used */

#define HASH_SIZE 64
static bcache_entry_t *hash_table[HASH_SIZE];

static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;

/* Block device operations (set by filesystem) */
typedef int (*block_read_fn)(void *device, uint64_t block, void *buffer);
typedef int (*block_write_fn)(void *device, uint64_t block, const void *buffer);

static block_read_fn  block_read = NULL;
static block_write_fn block_write = NULL;
static void *block_device = NULL;

/* ============ Hash Function ============ */

static uint32_t bcache_hash(uint64_t device_id, uint64_t block_num) {
    return (uint32_t)((device_id ^ block_num) % HASH_SIZE);
}

/* ============ LRU Operations ============ */

static void lru_remove(bcache_entry_t *entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        lru_head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        lru_tail = entry->lru_prev;
    }
    
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void lru_push_front(bcache_entry_t *entry) {
    entry->lru_prev = NULL;
    entry->lru_next = lru_head;
    
    if (lru_head) {
        lru_head->lru_prev = entry;
    }
    lru_head = entry;
    
    if (!lru_tail) {
        lru_tail = entry;
    }
}

/* ============ Hash Table Operations ============ */

static bcache_entry_t *hash_lookup(uint64_t device_id, uint64_t block_num) {
    uint32_t h = bcache_hash(device_id, block_num);
    bcache_entry_t *entry = hash_table[h];
    
    while (entry) {
        if (entry->device_id == device_id && 
            entry->block_num == block_num &&
            (entry->flags & BCACHE_VALID)) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return NULL;
}

static void hash_insert(bcache_entry_t *entry) {
    uint32_t h = bcache_hash(entry->device_id, entry->block_num);
    entry->hash_next = hash_table[h];
    hash_table[h] = entry;
}

static void hash_remove(bcache_entry_t *entry) {
    uint32_t h = bcache_hash(entry->device_id, entry->block_num);
    bcache_entry_t **pp = &hash_table[h];
    
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            entry->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* ============ Block Write-back ============ */

static int bcache_writeback(bcache_entry_t *entry) {
    if (!(entry->flags & BCACHE_DIRTY)) {
        return 0;  /* Not dirty, nothing to do */
    }
    
    if (!block_write) {
        return -1;  /* No write function */
    }
    
    if (block_write(block_device, entry->block_num, entry->data) != 0) {
        return -1;
    }
    
    entry->flags &= ~BCACHE_DIRTY;
    return 0;
}

/* ============ Eviction ============ */

static bcache_entry_t *bcache_evict(void) {
    /* Find LRU entry that's not locked */
    bcache_entry_t *victim = lru_tail;
    
    while (victim && (victim->flags & BCACHE_LOCKED)) {
        victim = victim->lru_prev;
    }
    
    if (!victim) {
        serial_puts("[BCACHE] WARN: All blocks locked, cannot evict\n");
        return NULL;
    }
    
    /* Write back if dirty */
    if (victim->flags & BCACHE_DIRTY) {
        bcache_writeback(victim);
    }
    
    /* Remove from hash and LRU */
    hash_remove(victim);
    lru_remove(victim);
    
    victim->flags = 0;
    return victim;
}

/* ============ Public API ============ */

/*
 * bcache_get - Get a cached block, reading from disk if needed
 * 
 * Returns: Pointer to cache entry, or NULL on error
 */
bcache_entry_t *bcache_get(uint64_t device_id, uint64_t block_num) {
    /* Check cache first */
    bcache_entry_t *entry = hash_lookup(device_id, block_num);
    
    if (entry) {
        cache_hits++;
        lru_remove(entry);
        lru_push_front(entry);
        entry->ref_count++;
        return entry;
    }
    
    cache_misses++;
    
    /* Not in cache - need to read from disk */
    entry = bcache_evict();
    if (!entry) {
        return NULL;
    }
    
    /* Read block from disk */
    if (block_read && block_read(block_device, block_num, entry->data) != 0) {
        serial_puts("[BCACHE] ERROR: Failed to read block\n");
        return NULL;
    }
    
    /* Set up entry */
    entry->device_id = device_id;
    entry->block_num = block_num;
    entry->flags = BCACHE_VALID;
    entry->ref_count = 1;
    
    /* Add to hash and LRU */
    hash_insert(entry);
    lru_push_front(entry);
    
    return entry;
}

/*
 * bcache_release - Release a block (decrement ref count)
 */
void bcache_release(bcache_entry_t *entry) {
    if (entry && entry->ref_count > 0) {
        entry->ref_count--;
    }
}

/*
 * bcache_mark_dirty - Mark block as modified
 */
void bcache_mark_dirty(bcache_entry_t *entry) {
    if (entry) {
        entry->flags |= BCACHE_DIRTY;
    }
}

/*
 * bcache_sync - Write all dirty blocks to disk
 */
void bcache_sync(void) {
    serial_puts("[BCACHE] Syncing dirty blocks...\n");
    
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache_entries[i].flags & BCACHE_DIRTY) {
            bcache_writeback(&cache_entries[i]);
        }
    }
}

/*
 * bcache_invalidate - Invalidate all cached blocks for a device
 */
void bcache_invalidate(uint64_t device_id) {
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache_entries[i].device_id == device_id) {
            if (cache_entries[i].flags & BCACHE_DIRTY) {
                bcache_writeback(&cache_entries[i]);
            }
            hash_remove(&cache_entries[i]);
            cache_entries[i].flags = 0;
        }
    }
}

/*
 * bcache_stats - Get cache statistics
 */
void bcache_stats(uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = cache_hits;
    if (misses) *misses = cache_misses;
}

/*
 * bcache_set_ops - Set block device operations
 */
void bcache_set_ops(void *device, block_read_fn read_fn, block_write_fn write_fn) {
    block_device = device;
    block_read = read_fn;
    block_write = write_fn;
}

/*
 * bcache_init - Initialize the block cache
 */
void bcache_init(void) {
    serial_puts("[BCACHE] Initializing block cache...\n");
    
    /* Allocate cache entries */
    cache_entries = (bcache_entry_t *)kzalloc(BCACHE_SIZE * sizeof(bcache_entry_t));
    if (!cache_entries) {
        serial_puts("[BCACHE] ERROR: Failed to allocate cache\n");
        return;
    }
    
    /* Initialize LRU list */
    lru_head = NULL;
    lru_tail = NULL;
    
    for (int i = 0; i < BCACHE_SIZE; i++) {
        cache_entries[i].flags = 0;
        lru_push_front(&cache_entries[i]);
    }
    
    /* Clear hash table */
    for (int i = 0; i < HASH_SIZE; i++) {
        hash_table[i] = NULL;
    }
    
    cache_hits = 0;
    cache_misses = 0;
    
    serial_puts("[BCACHE] Ready (128 blocks, 512KB)\n");
}
