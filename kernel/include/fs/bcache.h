/*
 * NeolyxOS Block Cache Header
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYXOS_BCACHE_H
#define NEOLYXOS_BCACHE_H

#include <stdint.h>

#define BLOCK_SIZE 4096

/* Block flags */
#define BCACHE_VALID    0x01
#define BCACHE_DIRTY    0x02
#define BCACHE_LOCKED   0x04

/* Forward declaration */
typedef struct bcache_entry bcache_entry_t;

/* Block device operation types */
typedef int (*block_read_fn)(void *device, uint64_t block, void *buffer);
typedef int (*block_write_fn)(void *device, uint64_t block, const void *buffer);

/* Initialize block cache */
void bcache_init(void);

/* Set block device operations */
void bcache_set_ops(void *device, block_read_fn read_fn, block_write_fn write_fn);

/* Get a cached block (reads from disk if not cached) */
bcache_entry_t *bcache_get(uint64_t device_id, uint64_t block_num);

/* Release a block (decrement ref count) */
void bcache_release(bcache_entry_t *entry);

/* Mark block as modified */
void bcache_mark_dirty(bcache_entry_t *entry);

/* Sync all dirty blocks to disk */
void bcache_sync(void);

/* Invalidate all blocks for a device */
void bcache_invalidate(uint64_t device_id);

/* Get cache statistics */
void bcache_stats(uint32_t *hits, uint32_t *misses);

/* Access block data (returns pointer to data buffer) */
static inline uint8_t *bcache_data(bcache_entry_t *entry) {
    /* Data is at offset 32 bytes into entry struct */
    return (uint8_t *)entry + 32;
}

#endif /* NEOLYXOS_BCACHE_H */
