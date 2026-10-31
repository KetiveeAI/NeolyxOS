/*
 * NeolyxOS FAT32 Filesystem Implementation
 * 
 * Full read/write support for FAT32.
 * Universal compatibility with USB drives, SD cards.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fs/fat.h"
#include "fs/nxfs.h"  /* For block_ops */
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Helpers ============ */

static void *fat_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void *fat_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void fat_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ Sector I/O ============ */

/* Use raw AHCI for 512-byte sector access */
extern int ahci_read(int port, uint64_t sector, uint32_t count, void *buffer);
extern int ahci_write(int port, uint64_t sector, uint32_t count, const void *buffer);

/* ahci_block_dev_t structure (must match ahci_block.h) */
typedef struct {
    int port;
    uint64_t sector_count;
    uint64_t block_count;
    uint64_t partition_offset;  /* LBA offset for partition */
} fat_ahci_dev_t;

static int fat_read_sector(void *device, uint32_t sector, void *buffer) {
    if (!device) {
        serial_puts("[FAT] read_sector: null device\n");
        return -1;
    }
    
    /* Use partition offset from device structure */
    fat_ahci_dev_t *dev = (fat_ahci_dev_t *)device;
    uint64_t absolute_sector = dev->partition_offset + sector;
    
    int result = ahci_read(dev->port, absolute_sector, 1, buffer);
    if (result != 0) {
        serial_puts("[FAT] AHCI read failed at sector\n");
    }
    return result;
}

static int fat_write_sector(void *device, uint32_t sector, const void *buffer) {
    if (!device) return -1;
    
    /* Use partition offset from device structure */
    fat_ahci_dev_t *dev = (fat_ahci_dev_t *)device;
    uint64_t absolute_sector = dev->partition_offset + sector;
    
    return ahci_write(dev->port, absolute_sector, 1, buffer);
}

/* For compatibility - can be used to set block ops if needed */
static nxfs_block_ops_t *fat_block_ops = NULL;

void fat_set_block_ops(nxfs_block_ops_t *ops) {
    fat_block_ops = ops;
}

/* ============ FAT Table Operations ============ */

static int fat_set_entry(fat_mount_t *fat, uint32_t cluster, uint32_t value);

uint32_t fat_get_next_cluster(fat_mount_t *fat, uint32_t cluster) {
    if (cluster < 2) return 0;
    
    uint32_t fat_offset = cluster * 4;  /* FAT32 = 4 bytes per entry */
    uint32_t fat_sector = fat->fat_start + (fat_offset / fat->bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % fat->bytes_per_sector;
    
    uint8_t buffer[FAT_SECTOR_SIZE];
    if (fat_read_sector(fat->device, fat_sector, buffer) != 0) {
        return 0;
    }
    
    uint32_t entry = *(uint32_t *)(buffer + offset_in_sector);
    entry &= 0x0FFFFFFF;  /* Mask reserved bits */
    
    if (entry >= FAT32_EOC) return 0;  /* End of chain */
    return entry;
}

uint32_t fat_alloc_cluster(fat_mount_t *fat) {
    uint8_t buffer[FAT_SECTOR_SIZE];
    
    for (uint32_t cluster = 2; cluster < fat->total_clusters + 2; cluster++) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fat->fat_start + (fat_offset / fat->bytes_per_sector);
        uint32_t offset_in_sector = fat_offset % fat->bytes_per_sector;
        
        if (fat_read_sector(fat->device, fat_sector, buffer) != 0) {
            continue;
        }
        
        uint32_t entry = *(uint32_t *)(buffer + offset_in_sector) & 0x0FFFFFFF;
        if (entry == FAT32_FREE) {
            /* Mark as end of chain in every FAT copy */
            if (fat_set_entry(fat, cluster, 0x0FFFFFFF) != 0) return 0;
            return cluster;
        }
    }
    
    return 0;  /* No free clusters */
}

int fat_free_chain(fat_mount_t *fat, uint32_t start) {
    uint8_t buffer[FAT_SECTOR_SIZE];
    uint32_t cluster = start;
    
    while (cluster >= 2 && cluster < fat->total_clusters + 2) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fat->fat_start + (fat_offset / fat->bytes_per_sector);
        uint32_t offset_in_sector = fat_offset % fat->bytes_per_sector;
        
        if (fat_read_sector(fat->device, fat_sector, buffer) != 0) {
            return -1;
        }
        
        uint32_t next = *(uint32_t *)(buffer + offset_in_sector) & 0x0FFFFFFF;
        if (fat_set_entry(fat, cluster, FAT32_FREE) != 0) return -1;
        
        if (next >= FAT32_EOC) break;
        cluster = next;
    }
    
    return 0;
}

/* ============ Cluster I/O ============ */

int fat_read_cluster(fat_mount_t *fat, uint32_t cluster, void *buffer) {
    if (cluster < 2) return -1;
    
    uint32_t first_sector = fat->data_start + (cluster - 2) * fat->sectors_per_cluster;
    uint8_t *dst = (uint8_t *)buffer;
    
    for (uint32_t i = 0; i < fat->sectors_per_cluster; i++) {
        if (fat_read_sector(fat->device, first_sector + i, dst + i * fat->bytes_per_sector) != 0) {
            return -1;
        }
    }
    
    return 0;
}

int fat_write_cluster(fat_mount_t *fat, uint32_t cluster, const void *buffer) {
    if (cluster < 2) return -1;
    
    uint32_t first_sector = fat->data_start + (cluster - 2) * fat->sectors_per_cluster;
    const uint8_t *src = (const uint8_t *)buffer;
    
    for (uint32_t i = 0; i < fat->sectors_per_cluster; i++) {
        if (fat_write_sector(fat->device, first_sector + i, src + i * fat->bytes_per_sector) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/* ============ Name Conversion ============ */

static void fat_8_3_to_name(const fat_dirent_t *entry, char *name) {
    int i, j = 0;
    
    /* Copy filename, trim spaces */
    for (i = 0; i < 8 && entry->name[i] != ' '; i++) {
        name[j++] = entry->name[i];
    }
    
    /* Add extension if present */
    if (entry->ext[0] != ' ') {
        name[j++] = '.';
        for (i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            name[j++] = entry->ext[i];
        }
    }
    
    name[j] = '\0';
    
    /* Convert to lowercase */
    for (i = 0; name[i]; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') {
            name[i] = name[i] - 'A' + 'a';
        }
    }
}

/* ============ FAT Table Updates ============ */

/*
 * fat_set_entry - Set one FAT32 table entry in every FAT copy.
 * Keeping all copies in step is what other FAT implementations expect;
 * a stale second FAT is reported as corruption by disk checkers.
 * Returns 0 on success, -1 on I/O error or unsupported geometry.
 */
static int fat_set_entry(fat_mount_t *fat, uint32_t cluster, uint32_t value) {
    if (!fat || cluster < 2 || cluster >= fat->total_clusters + 2) return -1;
    if (fat->bytes_per_sector > FAT_SECTOR_SIZE) return -1;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_in_fat = fat_offset / fat->bytes_per_sector;
    uint32_t off_in_sector = fat_offset % fat->bytes_per_sector;
    uint32_t copies = fat->num_fats ? fat->num_fats : 1;
    uint8_t buf[FAT_SECTOR_SIZE];
    
    for (uint32_t c = 0; c < copies; c++) {
        uint32_t sector = fat->fat_start + c * fat->fat_sectors + sector_in_fat;
        if (fat_read_sector(fat->device, sector, buf) != 0) return -1;
        uint32_t old = *(uint32_t *)(buf + off_in_sector);
        /* The top 4 bits of a FAT32 entry are reserved and must be kept */
        *(uint32_t *)(buf + off_in_sector) = (old & 0xF0000000) | (value & 0x0FFFFFFF);
        if (fat_write_sector(fat->device, sector, buf) != 0) return -1;
    }
    return 0;
}

/* Allocate a cluster, link it after @prev (0 = none) and zero it. */
static uint32_t fat_alloc_linked(fat_mount_t *fat, uint32_t prev, uint8_t *scratch) {
    uint32_t c = fat_alloc_cluster(fat);
    if (c < 2) return 0;
    if (scratch) {
        fat_memset(scratch, 0, fat->bytes_per_cluster);
        if (fat_write_cluster(fat, c, scratch) != 0) {
            fat_set_entry(fat, c, FAT32_FREE);
            return 0;
        }
    }
    if (prev >= 2 && fat_set_entry(fat, prev, c) != 0) {
        fat_set_entry(fat, c, FAT32_FREE);
        return 0;
    }
    return c;
}

/* ============ Node Helpers ============ */

/*
 * A FAT node keeps its first cluster in private_data and the location of
 * its own directory entry in inode: directory cluster in the high 32
 * bits, entry index + 1 in the low 32 bits. 0 means the node has no
 * directory entry (the root directory).
 */
static uint64_t fat_loc(uint32_t cluster, uint32_t index) {
    return ((uint64_t)cluster << 32) | ((uint64_t)index + 1);
}

static uint32_t fat_node_cluster(fat_mount_t *fat, vfs_node_t *node) {
    uint32_t c = (uint32_t)(uintptr_t)node->private_data;
    if (c < 2 && node == fat->root && fat->fat_type == FAT_TYPE_FAT32) c = fat->root_cluster;
    return c;
}

static int fat_ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == 0 && *b == 0;
}

static uint8_t fat_lfn_checksum(const fat_dirent_t *de) {
    const uint8_t *p = (const uint8_t *)de;   /* name[8] + ext[3] */
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + p[i]);
    return sum;
}

/* 1980-01-01 in FAT date format (day 1, month 1, year 0) */
#define FAT_DATE_EPOCH 0x0021

/* Byte offsets of the 13 UCS-2 characters inside an LFN entry */
static const uint8_t k_lfn_off[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

/* Write the directory entry of @node back with its first cluster and size. */
static int fat_update_dirent(fat_mount_t *fat, vfs_node_t *node, uint8_t *scratch) {
    if (node->inode == 0) return VFS_OK;                 /* root has no entry */
    if (node->size > 0xFFFFFFFFULL) return VFS_ERR_NO_SPACE;
    uint32_t dcl = (uint32_t)(node->inode >> 32);
    uint32_t idx = (uint32_t)(node->inode & 0xFFFFFFFF) - 1;
    if (idx >= fat->bytes_per_cluster / sizeof(fat_dirent_t)) return VFS_ERR_INVALID;
    if (fat_read_cluster(fat, dcl, scratch) != 0) return VFS_ERR_IO;
    fat_dirent_t *de = &((fat_dirent_t *)scratch)[idx];
    uint32_t first = (uint32_t)(uintptr_t)node->private_data;
    de->first_cluster_lo = first & 0xFFFF;
    de->first_cluster_hi = (first >> 16) & 0xFFFF;
    if (node->type != VFS_DIRECTORY) de->file_size = (uint32_t)node->size;
    return fat_write_cluster(fat, dcl, scratch) == 0 ? VFS_OK : VFS_ERR_IO;
}

/* ============ Directory Iteration ============ */

/* Directory slots of one name: long-name slots first, the 8.3 entry last */
#define FAT_MAX_SLOTS 21
typedef struct {
    uint32_t count;
    uint32_t cl[FAT_MAX_SLOTS];
    uint32_t ix[FAT_MAX_SLOTS];
} fat_slots_t;

typedef struct {
    fat_mount_t *fat;
    uint8_t     *buf;           /* one cluster of directory entries */
    uint32_t     cluster;       /* cluster held in buf, 0 at end */
    uint32_t     index;         /* next entry to examine */
    uint32_t     per_cluster;
    char         lfn[VFS_NAME_MAX + 1];
    uint32_t     lfn_len;
    uint8_t      lfn_sum;
    uint8_t      lfn_next;      /* next expected LFN sequence, 0 = none pending */
    uint8_t      lfn_ok;        /* a complete LFN precedes the next entry */
    fat_slots_t  lfn_slots;     /* where the pending long-name entries are */
} fat_dir_iter_t;

typedef struct {
    fat_dirent_t de;
    char         name[VFS_NAME_MAX + 1];   /* long name if present */
    char         short_name[13];           /* 8.3 name, lower case */
    uint32_t     cluster, index;           /* location of the 8.3 entry */
    fat_slots_t  slots;                    /* every slot of this name */
} fat_dir_item_t;

static int fat_dir_iter_open(fat_dir_iter_t *it, fat_mount_t *fat, uint32_t dir_cluster) {
    fat_memset(it, 0, sizeof(*it));
    it->fat = fat;
    if (dir_cluster < 2) return VFS_ERR_INVALID;      /* FAT12/16 fixed root: unsupported */
    it->per_cluster = fat->bytes_per_cluster / sizeof(fat_dirent_t);
    it->buf = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!it->buf) return VFS_ERR_NO_MEM;
    if (fat_read_cluster(fat, dir_cluster, it->buf) != 0) {
        kfree(it->buf);
        it->buf = NULL;
        return VFS_ERR_IO;
    }
    it->cluster = dir_cluster;
    return VFS_OK;
}

static void fat_dir_iter_close(fat_dir_iter_t *it) {
    if (it->buf) kfree(it->buf);
    it->buf = NULL;
}

static void fat_lfn_reset(fat_dir_iter_t *it) {
    it->lfn_next = 0;
    it->lfn_ok = 0;
    it->lfn_len = 0;
    it->lfn_slots.count = 0;
}

/* Fold one LFN entry into the pending long name. */
static void fat_lfn_take(fat_dir_iter_t *it, const uint8_t *raw, uint32_t cl, uint32_t ix) {
    uint8_t ord = raw[0];
    uint8_t seq = ord & 0x1F;
    if (ord & 0x40) {
        if (seq == 0 || seq > 20) { fat_lfn_reset(it); return; }
        it->lfn_sum = raw[13];
        it->lfn_len = (uint32_t)seq * 13;
        if (it->lfn_len > VFS_NAME_MAX) it->lfn_len = VFS_NAME_MAX;
        it->lfn_next = seq;
        it->lfn_ok = 0;
        it->lfn_slots.count = 0;
    }
    if (it->lfn_next == 0 || seq != it->lfn_next || raw[13] != it->lfn_sum) {
        fat_lfn_reset(it);
        return;
    }
    if (it->lfn_slots.count < FAT_MAX_SLOTS - 1) {
        it->lfn_slots.cl[it->lfn_slots.count] = cl;
        it->lfn_slots.ix[it->lfn_slots.count] = ix;
        it->lfn_slots.count++;
    }
    for (int k = 0; k < 13; k++) {
        uint32_t pos = (uint32_t)(seq - 1) * 13 + (uint32_t)k;
        uint16_t ch = (uint16_t)(raw[k_lfn_off[k]] | (raw[k_lfn_off[k] + 1] << 8));
        if (ch == 0x0000) { if (pos < it->lfn_len) it->lfn_len = pos; break; }
        if (pos >= VFS_NAME_MAX) break;
        it->lfn[pos] = (ch == 0xFFFF) ? 0 : (ch < 0x20 || ch > 0x7E) ? '?' : (char)ch;
    }
    it->lfn_next = (uint8_t)(seq - 1);
    if (it->lfn_next == 0) it->lfn_ok = 1;
}

/* Returns 1 with the next entry, 0 at the end of the directory, <0 on error. */
static int fat_dir_iter_next(fat_dir_iter_t *it, fat_dir_item_t *out) {
    while (it->cluster >= 2) {
        if (it->index >= it->per_cluster) {
            uint32_t next = fat_get_next_cluster(it->fat, it->cluster);
            if (next < 2) { it->cluster = 0; return 0; }
            if (fat_read_cluster(it->fat, next, it->buf) != 0) return VFS_ERR_IO;
            it->cluster = next;
            it->index = 0;
        }
        uint32_t idx = it->index++;
        fat_dirent_t *de = &((fat_dirent_t *)it->buf)[idx];
        uint8_t first = (uint8_t)de->name[0];
        if (first == 0x00) { it->cluster = 0; return 0; }
        if (first == 0xE5) { fat_lfn_reset(it); continue; }
        if (de->attr == FAT_ATTR_LFN) { fat_lfn_take(it, (const uint8_t *)de, it->cluster, idx); continue; }
        if (de->attr & FAT_ATTR_VOLUME_ID) { fat_lfn_reset(it); continue; }
        
        out->de = *de;
        out->cluster = it->cluster;
        out->index = idx;
        fat_8_3_to_name(de, out->short_name);
        out->slots.count = 0;
        if (it->lfn_ok && it->lfn_len > 0 && fat_lfn_checksum(de) == it->lfn_sum) {
            fat_memcpy(out->name, it->lfn, it->lfn_len);
            out->name[it->lfn_len] = '\0';
            out->slots = it->lfn_slots;
        } else {
            fat_strcpy(out->name, out->short_name, sizeof(out->name));
        }
        out->slots.cl[out->slots.count] = it->cluster;
        out->slots.ix[out->slots.count] = idx;
        out->slots.count++;
        fat_lfn_reset(it);
        return 1;
    }
    return 0;
}

/* Find @name (long or 8.3, case-insensitive) in a directory. */
static int fat_dir_lookup(fat_mount_t *fat, uint32_t dir_cluster, const char *name, fat_dir_item_t *out) {
    fat_dir_iter_t it;
    int rc = fat_dir_iter_open(&it, fat, dir_cluster);
    if (rc != VFS_OK) return rc;
    while ((rc = fat_dir_iter_next(&it, out)) > 0) {
        if (fat_ci_eq(out->name, name) || fat_ci_eq(out->short_name, name)) {
            fat_dir_iter_close(&it);
            return VFS_OK;
        }
    }
    fat_dir_iter_close(&it);
    return rc < 0 ? rc : VFS_ERR_NOT_FOUND;
}

/* ============ VFS Operations ============ */

static int fat_vfs_open(vfs_node_t *node, uint32_t mode) {
    (void)mode;
    return VFS_OK;
}

static int fat_vfs_close(vfs_node_t *node) {
    return VFS_OK;
}

static int64_t fat_vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, uint64_t size) {
    if (!node || !node->mount) return VFS_ERR_INVALID;
    
    fat_mount_t *fat = (fat_mount_t *)node->mount->private_data;
    uint32_t first_cluster = (uint32_t)(uintptr_t)node->private_data;
    
    if (offset >= node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;
    
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!cluster_buffer) return VFS_ERR_NO_MEM;
    
    uint64_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t cluster = first_cluster;
    
    /* Skip to starting cluster */
    uint32_t skip_clusters = offset / fat->bytes_per_cluster;
    for (uint32_t i = 0; i < skip_clusters && cluster >= 2; i++) {
        cluster = fat_get_next_cluster(fat, cluster);
    }
    
    uint32_t cluster_offset = offset % fat->bytes_per_cluster;
    
    while (bytes_read < size && cluster >= 2) {
        if (fat_read_cluster(fat, cluster, cluster_buffer) != 0) {
            break;
        }
        
        uint32_t to_copy = fat->bytes_per_cluster - cluster_offset;
        if (to_copy > size - bytes_read) to_copy = size - bytes_read;
        
        fat_memcpy(dst + bytes_read, cluster_buffer + cluster_offset, to_copy);
        bytes_read += to_copy;
        cluster_offset = 0;
        
        cluster = fat_get_next_cluster(fat, cluster);
    }
    
    kfree(cluster_buffer);
    return bytes_read;
}

static int64_t fat_vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, uint64_t size) {
    if (!node || !node->mount || !buffer || size == 0) return VFS_ERR_INVALID;
    if (node->type == VFS_DIRECTORY) return VFS_ERR_IS_DIR;
    
    fat_mount_t *fat = (fat_mount_t *)node->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    if (fat->fat_type != FAT_TYPE_FAT32) return VFS_ERR_READ_ONLY;
    if (offset > 0xFFFFFFFFULL || size > 0xFFFFFFFFULL - offset) return VFS_ERR_NO_SPACE;
    
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!cluster_buffer) return VFS_ERR_NO_MEM;
    
    /* Get or allocate first cluster */
    uint32_t first_cluster = (uint32_t)(uintptr_t)node->private_data;
    if (first_cluster < 2) {
        first_cluster = fat_alloc_linked(fat, 0, cluster_buffer);
        if (first_cluster < 2) {
            serial_puts("[FAT] Write error: no free clusters\n");
            kfree(cluster_buffer);
            return VFS_ERR_NO_SPACE;
        }
        node->private_data = (void *)(uintptr_t)first_cluster;
    }
    
    uint64_t bytes_written = 0;
    uint32_t current_cluster = first_cluster;
    uint64_t cluster_offset = offset % fat->bytes_per_cluster;
    int64_t result = 0;
    
    /* Skip to starting cluster, extending the chain with zeroed clusters */
    uint32_t skip_clusters = (uint32_t)(offset / fat->bytes_per_cluster);
    for (uint32_t i = 0; i < skip_clusters; i++) {
        uint32_t next = fat_get_next_cluster(fat, current_cluster);
        if (next < 2) next = fat_alloc_linked(fat, current_cluster, cluster_buffer);
        if (next < 2) { result = VFS_ERR_NO_SPACE; goto out; }
        current_cluster = next;
    }
    
    const uint8_t *src = (const uint8_t *)buffer;
    while (bytes_written < size) {
        uint64_t to_write = fat->bytes_per_cluster - cluster_offset;
        if (to_write > size - bytes_written) to_write = size - bytes_written;
        
        /* Partial cluster: keep the bytes around the written range */
        if (to_write < fat->bytes_per_cluster &&
            fat_read_cluster(fat, current_cluster, cluster_buffer) != 0) {
            result = VFS_ERR_IO;
            break;
        }
        fat_memcpy(cluster_buffer + cluster_offset, src + bytes_written, to_write);
        if (fat_write_cluster(fat, current_cluster, cluster_buffer) != 0) {
            serial_puts("[FAT] Write error: cluster write failed\n");
            result = VFS_ERR_IO;
            break;
        }
        bytes_written += to_write;
        cluster_offset = 0;
        
        if (bytes_written < size) {
            uint32_t next = fat_get_next_cluster(fat, current_cluster);
            if (next < 2) next = fat_alloc_linked(fat, current_cluster, cluster_buffer);
            if (next < 2) { result = VFS_ERR_NO_SPACE; break; }
            current_cluster = next;
        }
    }
    
out:
    if (offset + bytes_written > node->size) node->size = offset + bytes_written;
    /* Persist first cluster and size so the data survives a remount */
    if (fat_update_dirent(fat, node, cluster_buffer) != VFS_OK) {
        serial_puts("[FAT] Write error: directory entry update failed\n");
        if (result == 0) result = VFS_ERR_IO;
    }
    kfree(cluster_buffer);
    if (bytes_written > 0) return (int64_t)bytes_written;   /* partial write */
    return result;
}

/* Shrink a file to zero length. Other sizes are not supported yet. */
static int fat_vfs_truncate(vfs_node_t *node, uint64_t size) {
    if (!node || !node->mount) return VFS_ERR_INVALID;
    if (node->type == VFS_DIRECTORY) return VFS_ERR_IS_DIR;
    if (size != 0) return VFS_ERR_INVALID;
    fat_mount_t *fat = (fat_mount_t *)node->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    if (fat->fat_type != FAT_TYPE_FAT32) return VFS_ERR_READ_ONLY;
    
    uint8_t *scratch = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!scratch) return VFS_ERR_NO_MEM;
    uint32_t first = (uint32_t)(uintptr_t)node->private_data;
    node->private_data = (void *)(uintptr_t)0;
    node->size = 0;
    /* Detach the chain from the entry first: a crash then leaks clusters
     * instead of leaving an entry that points at freed ones. */
    int rc = fat_update_dirent(fat, node, scratch);
    kfree(scratch);
    if (rc != VFS_OK) return rc;
    if (first >= 2 && fat_free_chain(fat, first) != 0) {
        serial_puts("[FAT] Truncate: freeing cluster chain failed (clusters leaked)\n");
    }
    return VFS_OK;
}

static vfs_node_t *fat_vfs_finddir(vfs_node_t *dir, const char *name);
static int fat_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read);

/* ============ Name Handling ============ */

static int fat_short_char_ok(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return 1;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '(': case ')':
        case '-': case '@': case '^': case '_': case '`': case '{': case '}': case '~':
            return 1;
    }
    return 0;
}

/*
 * Validate a name for a new directory entry. Printable ASCII only, no
 * path separators or characters FAT forbids, no trailing dot or space.
 */
static int fat_name_valid(const char *name) {
    int len = 0;
    for (const char *p = name; *p; p++, len++) {
        char c = *p;
        if (c < 0x20 || c > 0x7E) return 0;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') return 0;
    }
    if (len == 0 || len > VFS_NAME_MAX) return 0;
    if (name[len - 1] == '.' || name[len - 1] == ' ') return 0;
    if (fat_ci_eq(name, ".") || fat_ci_eq(name, "..")) return 0;
    return 1;
}

/*
 * If @name (upper-cased) is a valid 8.3 name, fill name8/ext3 and return 1.
 * *exact is set when the name has no lower-case letters, so the 8.3 entry
 * alone reproduces it and no long-name entries are needed.
 */
static int fat_as_short(const char *name, char *name8, char *ext3, int *exact) {
    const char *dot = NULL;
    int base = 0, ext = 0;
    *exact = 1;
    for (int i = 0; i < 8; i++) name8[i] = ' ';
    for (int i = 0; i < 3; i++) ext3[i] = ' ';
    for (const char *p = name; *p; p++) {
        if (*p == '.') { if (dot) return 0; dot = p; continue; }
        char c = *p;
        if (c >= 'a' && c <= 'z') { c = (char)(c - 'a' + 'A'); *exact = 0; }
        if (!fat_short_char_ok(c)) return 0;
        if (!dot) { if (base >= 8) return 0; name8[base++] = c; }
        else      { if (ext >= 3) return 0; ext3[ext++] = c; }
    }
    if (base == 0) return 0;
    if (dot && ext == 0) return 0;
    if ((uint8_t)name8[0] == 0xE5) name8[0] = 0x05;
    return 1;
}

/* Does an entry with this raw 8.3 name exist in the directory? */
static int fat_short_exists(fat_mount_t *fat, uint32_t dir_cluster, const char *name8, const char *ext3) {
    fat_dir_iter_t it;
    fat_dir_item_t item;
    if (fat_dir_iter_open(&it, fat, dir_cluster) != VFS_OK) return -1;
    int rc, found = 0;
    while ((rc = fat_dir_iter_next(&it, &item)) > 0) {
        int same = 1;
        for (int i = 0; i < 8 && same; i++) if (item.de.name[i] != name8[i]) same = 0;
        for (int i = 0; i < 3 && same; i++) if (item.de.ext[i] != ext3[i]) same = 0;
        if (same) { found = 1; break; }
    }
    fat_dir_iter_close(&it);
    return rc < 0 ? -1 : found;
}

/* Build a unique BASE~N.EXT alias for a long name. */
static int fat_make_alias(fat_mount_t *fat, uint32_t dir_cluster, const char *name, char *name8, char *ext3) {
    const char *dot = NULL;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (dot == name) dot = NULL;                        /* ".profile": no extension */
    
    char base[8];
    int nb = 0;
    for (const char *p = name; *p && p != dot && nb < 8; p++) {
        char c = *p;
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        base[nb++] = fat_short_char_ok(c) ? c : '_';
    }
    if (nb == 0) base[nb++] = '_';
    
    for (int i = 0; i < 3; i++) ext3[i] = ' ';
    if (dot) {
        int ne = 0;
        for (const char *p = dot + 1; *p && ne < 3; p++) {
            char c = *p;
            if (c == ' ' || c == '.') continue;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            ext3[ne++] = fat_short_char_ok(c) ? c : '_';
        }
    }
    
    for (int n = 1; n <= 9999; n++) {
        char tail[6];
        int nt = 0;
        char digits[5];
        int nd = 0;
        for (int v = n; v > 0; v /= 10) digits[nd++] = (char)('0' + v % 10);
        tail[nt++] = '~';
        while (nd > 0) tail[nt++] = digits[--nd];
        int keep = nb < 8 - nt ? nb : 8 - nt;
        for (int i = 0; i < 8; i++) name8[i] = ' ';
        for (int i = 0; i < keep; i++) name8[i] = base[i];
        for (int i = 0; i < nt; i++) name8[keep + i] = tail[i];
        if ((uint8_t)name8[0] == 0xE5) name8[0] = 0x05;
        int ex = fat_short_exists(fat, dir_cluster, name8, ext3);
        if (ex < 0) return VFS_ERR_IO;
        if (!ex) return VFS_OK;
    }
    return VFS_ERR_EXIST;
}

/*
 * Find @count consecutive free entries in a directory, extending it with
 * zeroed clusters when needed. Slot positions go to cl[]/ix[].
 */
static int fat_dir_find_slots(fat_mount_t *fat, uint32_t dir_cluster, uint32_t count,
                              uint32_t *cl, uint32_t *ix, uint8_t *buf) {
    uint32_t per = fat->bytes_per_cluster / sizeof(fat_dirent_t);
    uint32_t run = 0;
    uint32_t cluster = dir_cluster;
    uint32_t guard = 0;
    
    while (guard++ < 65536) {
        if (fat_read_cluster(fat, cluster, buf) != 0) return VFS_ERR_IO;
        fat_dirent_t *de = (fat_dirent_t *)buf;
        for (uint32_t i = 0; i < per; i++) {
            uint8_t f = (uint8_t)de[i].name[0];
            if (f == 0x00 || f == 0xE5) {
                cl[run] = cluster;
                ix[run] = i;
                if (++run == count) return VFS_OK;
            } else {
                run = 0;
            }
        }
        uint32_t next = fat_get_next_cluster(fat, cluster);
        if (next < 2) {
            next = fat_alloc_linked(fat, cluster, buf);
            if (next < 2) return VFS_ERR_NO_SPACE;
        }
        cluster = next;
    }
    return VFS_ERR_NO_SPACE;
}

static int fat_dir_put_entry(fat_mount_t *fat, uint32_t cluster, uint32_t index,
                             const void *entry, uint8_t *buf) {
    if (fat_read_cluster(fat, cluster, buf) != 0) return VFS_ERR_IO;
    fat_memcpy(buf + index * sizeof(fat_dirent_t), entry, sizeof(fat_dirent_t));
    return fat_write_cluster(fat, cluster, buf) == 0 ? VFS_OK : VFS_ERR_IO;
}

/* Mark every slot of a name deleted, the 8.3 entry first. */
static int fat_dir_erase(fat_mount_t *fat, const fat_slots_t *slots, uint8_t *buf) {
    int rc = VFS_OK;
    for (uint32_t k = slots->count; k > 0; k--) {
        uint32_t i = k - 1;
        if (fat_read_cluster(fat, slots->cl[i], buf) != 0) { rc = VFS_ERR_IO; continue; }
        buf[slots->ix[i] * sizeof(fat_dirent_t)] = 0xE5;
        if (fat_write_cluster(fat, slots->cl[i], buf) != 0) rc = VFS_ERR_IO;
    }
    return rc;
}

/*
 * fat_dir_add_entry - Write @name into a directory.
 * @tmpl:  the 8.3 entry to store (attr, first cluster, size, dates); its
 *         name and ext are filled in here.
 * @skip_cl/@skip_ix: an existing 8.3 entry that may carry the same name
 *         (a case-only rename); 0/0 for none.
 * @out:   where the new slots went, so the caller can undo the write.
 */
static int fat_dir_add_entry(fat_mount_t *fat, uint32_t dir_cluster, const char *name,
                             fat_dirent_t *tmpl, uint32_t skip_cl, uint32_t skip_ix,
                             fat_slots_t *out, uint8_t *buf) {
    if (!fat_name_valid(name)) return VFS_ERR_INVALID;
    out->count = 0;
    
    fat_dir_item_t existing;
    int rc = fat_dir_lookup(fat, dir_cluster, name, &existing);
    if (rc == VFS_OK && !(skip_cl >= 2 && existing.cluster == skip_cl && existing.index == skip_ix)) {
        return VFS_ERR_EXIST;
    }
    if (rc != VFS_OK && rc != VFS_ERR_NOT_FOUND) return rc;
    
    /* Short entry: the name itself when it fits 8.3, else a BASE~N alias */
    char name8[8], ext3[3];
    int exact = 0;
    int need_lfn = 1;
    if (fat_as_short(name, name8, ext3, &exact)) {
        int ex = fat_short_exists(fat, dir_cluster, name8, ext3);
        if (ex < 0) return VFS_ERR_IO;
        if (!ex) need_lfn = !exact;
        else if ((rc = fat_make_alias(fat, dir_cluster, name, name8, ext3)) != VFS_OK) return rc;
    } else if ((rc = fat_make_alias(fat, dir_cluster, name, name8, ext3)) != VFS_OK) {
        return rc;
    }
    
    uint32_t len = 0;
    while (name[len]) len++;
    uint32_t nlfn = need_lfn ? (len + 12) / 13 : 0;
    if (nlfn + 1 > FAT_MAX_SLOTS) return VFS_ERR_INVALID;
    
    rc = fat_dir_find_slots(fat, dir_cluster, nlfn + 1, out->cl, out->ix, buf);
    if (rc != VFS_OK) return rc;
    
    fat_memcpy(tmpl->name, name8, 8);
    fat_memcpy(tmpl->ext, ext3, 3);
    
    /* Long-name entries, highest sequence first, then the 8.3 entry */
    uint8_t sum = fat_lfn_checksum(tmpl);
    uint32_t written = 0;
    for (uint32_t k = 0; k < nlfn && rc == VFS_OK; k++) {
        uint32_t seq = nlfn - k;
        uint8_t raw[32];
        fat_memset(raw, 0, sizeof(raw));
        raw[0] = (uint8_t)(seq | (k == 0 ? 0x40 : 0));
        raw[11] = FAT_ATTR_LFN;
        raw[13] = sum;
        for (int c = 0; c < 13; c++) {
            uint32_t pos = (seq - 1) * 13 + (uint32_t)c;
            uint16_t ch = pos < len ? (uint8_t)name[pos] : (pos == len ? 0x0000 : 0xFFFF);
            raw[k_lfn_off[c]] = (uint8_t)(ch & 0xFF);
            raw[k_lfn_off[c] + 1] = (uint8_t)(ch >> 8);
        }
        rc = fat_dir_put_entry(fat, out->cl[k], out->ix[k], raw, buf);
        if (rc == VFS_OK) written++;
    }
    if (rc == VFS_OK) rc = fat_dir_put_entry(fat, out->cl[nlfn], out->ix[nlfn], tmpl, buf);
    
    if (rc != VFS_OK) {
        /* Leave nothing half-made: mark any written long-name slots deleted */
        out->count = written;
        fat_dir_erase(fat, out, buf);
        out->count = 0;
        return rc;
    }
    out->count = nlfn + 1;
    return VFS_OK;
}

/* Create file or directory in FAT filesystem (long names supported) */
static int fat_vfs_create(vfs_node_t *parent, const char *name, vfs_type_t type, uint32_t mode) {
    if (!parent || !name || !parent->mount) return VFS_ERR_INVALID;
    if (parent->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    (void)mode; /* FAT doesn't use Unix modes */
    
    fat_mount_t *fat = (fat_mount_t *)parent->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    if (fat->fat_type != FAT_TYPE_FAT32) return VFS_ERR_READ_ONLY;
    if (!fat_name_valid(name)) return VFS_ERR_INVALID;
    
    uint32_t dir_cluster = fat_node_cluster(fat, parent);
    if (dir_cluster < 2) return VFS_ERR_INVALID;
    
    uint8_t *buf = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!buf) return VFS_ERR_NO_MEM;
    
    fat_dirent_t entry;
    fat_memset(&entry, 0, sizeof(entry));
    /* No RTC date plumbing here yet: use the FAT epoch, 1980-01-01, which
     * unlike an all-zero date is a valid FAT date. */
    entry.create_date = entry.modify_date = entry.access_date = FAT_DATE_EPOCH;
    
    /* Refuse an existing name before allocating a directory cluster */
    fat_dir_item_t existing;
    int rc = fat_dir_lookup(fat, dir_cluster, name, &existing);
    if (rc == VFS_OK) { kfree(buf); return VFS_ERR_EXIST; }
    if (rc != VFS_ERR_NOT_FOUND) { kfree(buf); return rc; }
    
    uint32_t data_cluster = 0;
    if (type == VFS_DIRECTORY) {
        entry.attr = FAT_ATTR_DIRECTORY;
        data_cluster = fat_alloc_linked(fat, 0, buf);
        if (data_cluster < 2) { kfree(buf); return VFS_ERR_NO_SPACE; }
        /* "." and ".." entries; ".." is 0 when the parent is the root */
        fat_dirent_t *de = (fat_dirent_t *)buf;
        fat_memset(buf, 0, fat->bytes_per_cluster);
        fat_memset(de[0].name, ' ', 11);
        de[0].name[0] = '.';
        de[0].attr = FAT_ATTR_DIRECTORY;
        de[0].create_date = de[0].modify_date = de[0].access_date = FAT_DATE_EPOCH;
        de[0].first_cluster_lo = data_cluster & 0xFFFF;
        de[0].first_cluster_hi = (data_cluster >> 16) & 0xFFFF;
        fat_memset(de[1].name, ' ', 11);
        de[1].name[0] = '.';
        de[1].name[1] = '.';
        de[1].attr = FAT_ATTR_DIRECTORY;
        de[1].create_date = de[1].modify_date = de[1].access_date = FAT_DATE_EPOCH;
        uint32_t up = (dir_cluster == fat->root_cluster) ? 0 : dir_cluster;
        de[1].first_cluster_lo = up & 0xFFFF;
        de[1].first_cluster_hi = (up >> 16) & 0xFFFF;
        if (fat_write_cluster(fat, data_cluster, buf) != 0) {
            fat_free_chain(fat, data_cluster);
            kfree(buf);
            return VFS_ERR_IO;
        }
        entry.first_cluster_lo = data_cluster & 0xFFFF;
        entry.first_cluster_hi = (data_cluster >> 16) & 0xFFFF;
    } else {
        /* Files start with no clusters (allocated on first write) */
        entry.attr = FAT_ATTR_ARCHIVE;
    }
    
    fat_slots_t slots;
    rc = fat_dir_add_entry(fat, dir_cluster, name, &entry, 0, 0, &slots, buf);
    if (rc != VFS_OK) {
        if (data_cluster >= 2) fat_free_chain(fat, data_cluster);
        kfree(buf);
        serial_puts("[FAT] Create failed: ");
        serial_puts(name);
        serial_puts("\n");
        return rc;
    }
    
    kfree(buf);
    serial_puts(type == VFS_DIRECTORY ? "[FAT] Created directory: " : "[FAT] Created file: ");
    serial_puts(name);
    serial_puts("\n");
    return VFS_OK;
}

static int fat_is_dot_entry(const fat_dirent_t *de) {
    return de->name[0] == '.' &&
           (de->name[1] == ' ' || (de->name[1] == '.' && de->name[2] == ' '));
}

static uint32_t fat_entry_cluster(const fat_dirent_t *de) {
    return ((uint32_t)de->first_cluster_hi << 16) | de->first_cluster_lo;
}

/* 1 if the directory holds nothing but "." and "..", 0 if not, <0 on error. */
static int fat_dir_is_empty(fat_mount_t *fat, uint32_t dir_cluster) {
    fat_dir_iter_t it;
    fat_dir_item_t item;
    int rc = fat_dir_iter_open(&it, fat, dir_cluster);
    if (rc != VFS_OK) return rc;
    int empty = 1;
    while ((rc = fat_dir_iter_next(&it, &item)) > 0) {
        if (!fat_is_dot_entry(&item.de)) { empty = 0; break; }
    }
    fat_dir_iter_close(&it);
    return rc < 0 ? rc : empty;
}

/* Cluster of the parent of directory @dir_cluster, read from its ".." entry. */
static uint32_t fat_dir_parent(fat_mount_t *fat, uint32_t dir_cluster, uint8_t *buf) {
    if (fat_read_cluster(fat, dir_cluster, buf) != 0) return 0;
    fat_dirent_t *de = (fat_dirent_t *)buf;
    if (de[1].name[0] != '.' || de[1].name[1] != '.') return 0;
    uint32_t up = fat_entry_cluster(&de[1]);
    return up < 2 ? fat->root_cluster : up;
}

/* Point the ".." entry of directory @dir_cluster at @parent_cluster. */
static int fat_dir_set_parent(fat_mount_t *fat, uint32_t dir_cluster, uint32_t parent_cluster,
                              uint8_t *buf) {
    if (fat_read_cluster(fat, dir_cluster, buf) != 0) return VFS_ERR_IO;
    fat_dirent_t *de = (fat_dirent_t *)buf;
    if (de[1].name[0] != '.' || de[1].name[1] != '.') return VFS_ERR_IO;
    uint32_t up = (parent_cluster == fat->root_cluster) ? 0 : parent_cluster;
    de[1].first_cluster_lo = up & 0xFFFF;
    de[1].first_cluster_hi = (up >> 16) & 0xFFFF;
    return fat_write_cluster(fat, dir_cluster, buf) == 0 ? VFS_OK : VFS_ERR_IO;
}

/*
 * fat_vfs_unlink - Delete a file or an empty directory.
 * The name's slots are marked deleted before the data clusters are freed,
 * so a crash in between leaks space instead of leaving an entry that
 * points at free clusters. Read-only files are refused.
 */
static int fat_vfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !name || !parent->mount) return VFS_ERR_INVALID;
    if (parent->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    fat_mount_t *fat = (fat_mount_t *)parent->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    if (fat->fat_type != FAT_TYPE_FAT32) return VFS_ERR_READ_ONLY;
    
    uint32_t dir_cluster = fat_node_cluster(fat, parent);
    if (dir_cluster < 2) return VFS_ERR_INVALID;
    
    fat_dir_item_t item;
    int rc = fat_dir_lookup(fat, dir_cluster, name, &item);
    if (rc != VFS_OK) return rc;
    if (fat_is_dot_entry(&item.de)) return VFS_ERR_INVALID;
    if (item.de.attr & FAT_ATTR_READ_ONLY) return VFS_ERR_READ_ONLY;
    
    uint32_t first = fat_entry_cluster(&item.de);
    if (item.de.attr & FAT_ATTR_DIRECTORY) {
        if (first < 2 || first == fat->root_cluster) return VFS_ERR_INVALID;
        int empty = fat_dir_is_empty(fat, first);
        if (empty < 0) return empty;
        if (!empty) return VFS_ERR_NOT_EMPTY;
    }
    
    uint8_t *buf = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!buf) return VFS_ERR_NO_MEM;
    rc = fat_dir_erase(fat, &item.slots, buf);
    kfree(buf);
    if (rc != VFS_OK) {
        serial_puts("[FAT] Delete failed while erasing entry: ");
        serial_puts(name);
        serial_puts("\n");
        return rc;
    }
    if (first >= 2 && fat_free_chain(fat, first) != 0) {
        serial_puts("[FAT] Delete: freeing cluster chain failed (clusters leaked)\n");
    }
    serial_puts("[FAT] Deleted: ");
    serial_puts(name);
    serial_puts("\n");
    return VFS_OK;
}

/*
 * fat_vfs_rename - Rename or move a name within one FAT volume.
 * The new entry is written before the old one is erased, so a crash
 * leaves the file reachable (possibly under both names) rather than lost.
 * A moved directory gets its ".." entry pointed at the new parent.
 * Refuses an existing target and moving a directory into itself.
 */
static int fat_vfs_rename(vfs_node_t *old_parent, const char *old_name,
                          vfs_node_t *new_parent, const char *new_name) {
    if (!old_parent || !new_parent || !old_name || !new_name) return VFS_ERR_INVALID;
    if (!old_parent->mount || old_parent->mount != new_parent->mount) return VFS_ERR_INVALID;
    if (old_parent->type != VFS_DIRECTORY || new_parent->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    fat_mount_t *fat = (fat_mount_t *)old_parent->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    if (fat->fat_type != FAT_TYPE_FAT32) return VFS_ERR_READ_ONLY;
    if (!fat_name_valid(new_name)) return VFS_ERR_INVALID;
    
    uint32_t src_dir = fat_node_cluster(fat, old_parent);
    uint32_t dst_dir = fat_node_cluster(fat, new_parent);
    if (src_dir < 2 || dst_dir < 2) return VFS_ERR_INVALID;
    
    fat_dir_item_t item;
    int rc = fat_dir_lookup(fat, src_dir, old_name, &item);
    if (rc != VFS_OK) return rc;
    if (fat_is_dot_entry(&item.de)) return VFS_ERR_INVALID;
    if (item.de.attr & FAT_ATTR_READ_ONLY) return VFS_ERR_READ_ONLY;
    
    int is_dir = (item.de.attr & FAT_ATTR_DIRECTORY) != 0;
    uint32_t first = fat_entry_cluster(&item.de);
    
    uint8_t *buf = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!buf) return VFS_ERR_NO_MEM;
    
    /* A directory cannot move into itself or anything below it */
    if (is_dir && src_dir != dst_dir) {
        if (first < 2) { kfree(buf); return VFS_ERR_INVALID; }
        uint32_t c = dst_dir;
        for (uint32_t guard = 0; c >= 2 && c != fat->root_cluster; guard++) {
            if (c == first || guard > 4096) { kfree(buf); return VFS_ERR_INVALID; }
            c = fat_dir_parent(fat, c, buf);
        }
        if (c < 2) { kfree(buf); return VFS_ERR_IO; }
    }
    
    /* Same dates, attributes and data; only the name and place change */
    fat_dirent_t entry = item.de;
    fat_slots_t new_slots;
    uint32_t skip_cl = (src_dir == dst_dir) ? item.cluster : 0;
    uint32_t skip_ix = (src_dir == dst_dir) ? item.index : 0;
    rc = fat_dir_add_entry(fat, dst_dir, new_name, &entry, skip_cl, skip_ix, &new_slots, buf);
    if (rc != VFS_OK) { kfree(buf); return rc; }
    
    int parent_moved = 0;
    if (is_dir && src_dir != dst_dir) {
        rc = fat_dir_set_parent(fat, first, dst_dir, buf);
        if (rc != VFS_OK) {
            fat_dir_erase(fat, &new_slots, buf);
            kfree(buf);
            serial_puts("[FAT] Rename failed updating '..' of: ");
            serial_puts(old_name);
            serial_puts("\n");
            return rc;
        }
        parent_moved = 1;
    }
    
    rc = fat_dir_erase(fat, &item.slots, buf);
    if (rc != VFS_OK) {
        /* Undo, so two entries never share one cluster chain */
        fat_dir_erase(fat, &new_slots, buf);
        if (parent_moved) fat_dir_set_parent(fat, first, src_dir, buf);
        kfree(buf);
        serial_puts("[FAT] Rename failed erasing old entry: ");
        serial_puts(old_name);
        serial_puts("\n");
        return rc;
    }
    
    kfree(buf);
    serial_puts("[FAT] Renamed: ");
    serial_puts(old_name);
    serial_puts(" -> ");
    serial_puts(new_name);
    serial_puts("\n");
    return VFS_OK;
}

static vfs_operations_t fat_ops = {
    .open = fat_vfs_open,
    .close = fat_vfs_close,
    .read = fat_vfs_read,
    .write = fat_vfs_write,
    .readdir = fat_vfs_readdir,
    .finddir = fat_vfs_finddir,
    .create = fat_vfs_create,
    .unlink = fat_vfs_unlink,
    .rename = fat_vfs_rename,
    .truncate = fat_vfs_truncate,
    .stat = NULL,
    .chmod = NULL,
    .chown = NULL,
};

/* ============ Directory Operations ============ */

static int fat_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read) {
    if (!dir || !entries || !read) return VFS_ERR_INVALID;
    if (dir->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    *read = 0;
    
    fat_mount_t *fat = (fat_mount_t *)dir->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    
    fat_dir_iter_t it;
    fat_dir_item_t item;
    int rc = fat_dir_iter_open(&it, fat, fat_node_cluster(fat, dir));
    if (rc != VFS_OK) return rc;
    
    while (*read < count && (rc = fat_dir_iter_next(&it, &item)) > 0) {
        vfs_dirent_t *e = &entries[*read];
        fat_strcpy(e->name, item.name, sizeof(e->name));
        e->type = (item.de.attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
        e->inode = ((uint32_t)item.de.first_cluster_hi << 16) | item.de.first_cluster_lo;
        e->size = item.de.file_size;
        (*read)++;
    }
    fat_dir_iter_close(&it);
    return rc < 0 ? rc : VFS_OK;
}

static vfs_node_t *fat_vfs_finddir(vfs_node_t *dir, const char *name) {
    if (!dir || !name || !dir->mount || dir->type != VFS_DIRECTORY) return NULL;
    fat_mount_t *fat = (fat_mount_t *)dir->mount->private_data;
    if (!fat) return NULL;
    
    fat_dir_item_t item;
    if (fat_dir_lookup(fat, fat_node_cluster(fat, dir), name, &item) != VFS_OK) return NULL;
    
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    
    uint32_t first = ((uint32_t)item.de.first_cluster_hi << 16) | item.de.first_cluster_lo;
    fat_strcpy(node->name, item.name, VFS_NAME_MAX);
    node->type = (item.de.attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    node->size = item.de.file_size;
    node->inode = fat_loc(item.cluster, item.index);    /* where the entry lives */
    node->mount = dir->mount;
    node->parent = dir;
    node->ops = &fat_ops;
    node->private_data = (void *)(uintptr_t)first;      /* first cluster */
    /* ".." of a top-level directory is stored as cluster 0 */
    if (node->type == VFS_DIRECTORY && first < 2 && fat->fat_type == FAT_TYPE_FAT32) {
        node->private_data = (void *)(uintptr_t)fat->root_cluster;
    }
    return node;
}

/* ============ FAT Mount ============ */

int fat_mount(vfs_mount_t *mount) {
    serial_puts("[FAT] Mounting filesystem...\n");
    
    /* Read boot sector */
    uint8_t boot_sector[FAT_SECTOR_SIZE];
    if (fat_read_sector(mount->device, 0, boot_sector) != 0) {
        serial_puts("[FAT] ERROR: Failed to read boot sector\n");
        return VFS_ERR_IO;
    }
    
    fat32_bpb_t *bpb = (fat32_bpb_t *)boot_sector;
    
    /* ============ STEP 1: Validate boot signature ============ */
    if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
        serial_puts("[FAT] Not a FAT filesystem (no 55AA signature)\n");
        return VFS_ERR_INVALID;
    }
    
    /* ============ STEP 2: Check FAT type signature ============ */
    /* FAT32 has "FAT32   " at offset 82 */
    /* FAT16/12 has "FAT16   " or "FAT12   " at offset 54 */
    int is_fat32 = (boot_sector[82] == 'F' && boot_sector[83] == 'A' && 
                    boot_sector[84] == 'T' && boot_sector[85] == '3' && 
                    boot_sector[86] == '2');
    int is_fat16 = (boot_sector[54] == 'F' && boot_sector[55] == 'A' && 
                    boot_sector[56] == 'T' && boot_sector[57] == '1');
    int is_fat12 = (boot_sector[54] == 'F' && boot_sector[55] == 'A' && 
                    boot_sector[56] == 'T' && boot_sector[57] == '1' &&
                    boot_sector[58] == '2');
    
    if (!is_fat32 && !is_fat16 && !is_fat12) {
        serial_puts("[FAT] Not a FAT filesystem (no FAT signature found)\n");
        return VFS_ERR_INVALID;
    }
    
    /* ============ STEP 3: HARD BPB SANITY CHECK (before ANY math) ============ */
    /* This prevents divide-by-zero and overflow errors */
    if (bpb->bpb.bytes_per_sector < 512 ||
        bpb->bpb.sectors_per_cluster == 0 ||
        bpb->bpb.num_fats == 0 ||
        (bpb->bpb.bytes_per_sector & (bpb->bpb.bytes_per_sector - 1)) != 0) {
        serial_puts("[FAT] ERROR: Invalid BPB - not a FAT filesystem\n");
        return VFS_ERR_INVALID;
    }
    
    /* ============ STEP 4: Now safe to allocate and do math ============ */
    fat_mount_t *fat = (fat_mount_t *)kzalloc(sizeof(fat_mount_t));
    if (!fat) {
        serial_puts("[FAT] ERROR: Failed to allocate mount info\n");
        return VFS_ERR_NO_MEM;
    }
    
    fat->device = mount->device;
    fat->bytes_per_sector = bpb->bpb.bytes_per_sector;
    fat->sectors_per_cluster = bpb->bpb.sectors_per_cluster;
    fat->bytes_per_cluster = fat->bytes_per_sector * fat->sectors_per_cluster;
    fat->num_fats = bpb->bpb.num_fats;
    
    /* Calculate FAT locations */
    fat->fat_start = bpb->bpb.reserved_sectors;
    
    if (bpb->bpb.fat_size_16 != 0) {
        fat->fat_sectors = bpb->bpb.fat_size_16;
        fat->fat_type = FAT_TYPE_FAT16;
    } else {
        fat->fat_sectors = bpb->fat_size_32;
        fat->fat_type = FAT_TYPE_FAT32;
    }
    
    fat->root_start = fat->fat_start + bpb->bpb.num_fats * fat->fat_sectors;
    fat->root_sectors = (bpb->bpb.root_entry_count * 32 + fat->bytes_per_sector - 1) / fat->bytes_per_sector;
    fat->data_start = fat->root_start + fat->root_sectors;
    
    uint32_t total_sectors = bpb->bpb.total_sectors_16 ? bpb->bpb.total_sectors_16 : bpb->bpb.total_sectors_32;
    uint32_t data_sectors = total_sectors - fat->data_start;
    fat->total_clusters = data_sectors / fat->sectors_per_cluster;
    
    /* Determine FAT type by cluster count */
    if (fat->total_clusters < 4085) {
        fat->fat_type = FAT_TYPE_FAT12;
    } else if (fat->total_clusters < 65525) {
        fat->fat_type = FAT_TYPE_FAT16;
    } else {
        fat->fat_type = FAT_TYPE_FAT32;
        fat->root_cluster = bpb->root_cluster;
    }
    
    /* Copy volume label */
    fat_memcpy(fat->volume_label, bpb->volume_label, 11);
    fat->volume_label[11] = '\0';
    
    serial_puts("[FAT] Type: FAT");
    serial_putc('0' + (fat->fat_type / 10) % 10);
    serial_putc('0' + fat->fat_type % 10);
    serial_puts(", Volume: ");
    serial_puts(fat->volume_label);
    serial_puts("\n");
    
    /* Create root vnode */
    vfs_node_t *root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(fat);
        return VFS_ERR_NO_MEM;
    }
    
    fat_strcpy(root->name, "/", VFS_NAME_MAX);
    root->type = VFS_DIRECTORY;
    root->mount = mount;
    root->ops = &fat_ops;
    
    if (fat->fat_type == FAT_TYPE_FAT32) {
        root->private_data = (void *)(uintptr_t)fat->root_cluster;
    } else {
        /* FAT12/16: root is special (fixed sectors) */
        root->private_data = (void *)(uintptr_t)0;  /* Special marker */
    }
    
    fat->root = root;
    mount->root = root;
    mount->private_data = fat;
    
    serial_puts("[FAT] Mounted successfully\n");
    return VFS_OK;
}

int fat_unmount(vfs_mount_t *mount) {
    fat_mount_t *fat = (fat_mount_t *)mount->private_data;
    if (fat) {
        kfree(fat);
    }
    mount->private_data = NULL;
    mount->root = NULL;
    return VFS_OK;
}

int fat_sync(vfs_mount_t *mount) {
    /* FAT has no delayed writes in this implementation */
    return VFS_OK;
}

/* ============ FAT32 Format ============ */

/**
 * fat_validate_label - Validate and normalize a FAT volume label
 * @label: Input label (can be any string)
 * @out: Output buffer (11 chars + null)
 * 
 * FAT labels: max 11 chars, uppercase, no special chars except space
 * Returns 1 if valid, 0 if modified/truncated
 */
static int fat_validate_label(const char *label, char *out) {
    int valid = 1;
    int i = 0, o = 0;
    
    /* Fill with spaces first */
    for (int j = 0; j < 11; j++) out[j] = ' ';
    out[11] = '\0';
    
    if (!label || !label[0]) {
        /* Default label */
        out[0] = 'N'; out[1] = 'O'; out[2] = ' '; out[3] = 'N';
        out[4] = 'A'; out[5] = 'M'; out[6] = 'E';
        return 1;
    }
    
    while (label[i] && o < 11) {
        char c = label[i++];
        
        /* Convert to uppercase */
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        
        /* Valid chars: A-Z, 0-9, space, $%'-_@~`!(){}^#& */
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
            c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
            c == '@' || c == '~' || c == '!' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '^' || c == '#' || c == '&') {
            out[o++] = c;
        } else {
            /* Replace invalid with underscore */
            out[o++] = '_';
            valid = 0;
        }
    }
    
    if (label[i]) valid = 0;  /* Truncated */
    
    return valid;
}

/**
 * fat_calc_geometry - Calculate FAT filesystem geometry
 * @total_sectors: Total sectors in volume
 * @out_fat_type: Output FAT type (12, 16, or 32)
 * @out_sectors_per_cluster: Output cluster size in sectors
 * @out_fat_sectors: Output FAT size in sectors
 * @out_reserved: Output reserved sector count
 *
 * Based on Microsoft FAT specification:
 * - FAT12: < 4,085 clusters (< ~8MB)
 * - FAT16: 4,085 - 65,524 clusters (~8MB - ~512MB)
 * - FAT32: > 65,524 clusters (> ~512MB)
 */
static void fat_calc_geometry(uint64_t total_sectors, 
                               int *out_fat_type, 
                               uint32_t *out_sectors_per_cluster,
                               uint32_t *out_fat_sectors,
                               uint32_t *out_reserved) {
    uint32_t sectors_per_cluster;
    int fat_type;
    uint32_t reserved;
    
    uint64_t size_mb = (total_sectors * 512) / (1024 * 1024);
    
    /* Determine FAT type and cluster size based on volume size */
    if (size_mb < 8) {
        /* FAT12 for very small volumes */
        fat_type = 12;
        reserved = 1;
        sectors_per_cluster = 1;  /* 512 bytes */
        if (size_mb >= 4) sectors_per_cluster = 2;  /* 1KB */
    } else if (size_mb < 512) {
        /* FAT16 for medium volumes */
        fat_type = 16;
        reserved = 1;
        if (size_mb < 16) sectors_per_cluster = 1;       /* 512B */
        else if (size_mb < 32) sectors_per_cluster = 2;  /* 1KB */
        else if (size_mb < 64) sectors_per_cluster = 4;  /* 2KB */
        else if (size_mb < 128) sectors_per_cluster = 8; /* 4KB */
        else if (size_mb < 256) sectors_per_cluster = 16;/* 8KB */
        else sectors_per_cluster = 32;                    /* 16KB */
    } else {
        /* FAT32 for large volumes */
        fat_type = 32;
        reserved = 32;
        if (size_mb < 8192) sectors_per_cluster = 8;      /* 4KB */
        else if (size_mb < 16384) sectors_per_cluster = 16; /* 8KB */
        else if (size_mb < 32768) sectors_per_cluster = 32; /* 16KB */
        else sectors_per_cluster = 64;                     /* 32KB max */
    }
    
    /* Calculate total clusters and FAT size */
    uint32_t data_sectors = (uint32_t)total_sectors - reserved;
    uint32_t total_clusters = data_sectors / sectors_per_cluster;
    
    uint32_t fat_entry_bits;
    if (fat_type == 12) fat_entry_bits = 12;
    else if (fat_type == 16) fat_entry_bits = 16;
    else fat_entry_bits = 32;
    
    /* FAT size calculation */
    uint32_t fat_bytes;
    if (fat_type == 12) {
        fat_bytes = ((total_clusters + 2) * 3 + 1) / 2;  /* 1.5 bytes per entry */
    } else {
        fat_bytes = (total_clusters + 2) * (fat_entry_bits / 8);
    }
    uint32_t fat_sectors = (fat_bytes + 511) / 512;
    
    *out_fat_type = fat_type;
    *out_sectors_per_cluster = sectors_per_cluster;
    *out_fat_sectors = fat_sectors;
    *out_reserved = reserved;
    
    (void)fat_entry_bits;  /* Suppress warning */
}

/**
 * fat_format - Format a partition as FAT12/16/32 (auto-selected)
 * @device: Pointer to fat_ahci_dev_t (contains port and partition offset)
 * @total_sectors: Total sectors in the partition
 * @volume_label: Volume label (will be validated and normalized)
 *
 * Returns 0 on success, negative on error.
 * 
 * Automatically selects FAT type based on volume size:
 * - FAT12: < 8MB
 * - FAT16: 8MB - 512MB
 * - FAT32: > 512MB
 */
int fat_format(void *device, uint64_t total_sectors, const char *volume_label) {
    if (!device || total_sectors < 128) {
        serial_puts("[FAT] Format error: invalid device or too small\n");
        return -1;
    }
    
    fat_ahci_dev_t *dev = (fat_ahci_dev_t *)device;
    int port = dev->port;
    uint64_t part_offset = dev->partition_offset;
    
    /* Calculate geometry */
    int fat_type;
    uint32_t sectors_per_cluster, fat_sectors, reserved_sectors;
    fat_calc_geometry(total_sectors, &fat_type, &sectors_per_cluster, 
                      &fat_sectors, &reserved_sectors);
    
    /* Validate label */
    char valid_label[12];
    fat_validate_label(volume_label, valid_label);
    
    serial_puts("[FAT] Formatting as FAT");
    if (fat_type == 12) serial_puts("12");
    else if (fat_type == 16) serial_puts("16");
    else serial_puts("32");
    serial_puts(" (cluster size: ");
    uint32_t cluster_kb = (sectors_per_cluster * 512) / 1024;
    if (cluster_kb == 0) serial_puts("512B");
    else {
        serial_putc('0' + (cluster_kb / 10) % 10);
        serial_putc('0' + cluster_kb % 10);
        serial_puts("KB");
    }
    serial_puts(")...\n");
    
    uint32_t bytes_per_sector = 512;
    uint32_t num_fats = 2;
    uint32_t root_cluster = 2;
    
    /* For FAT12/16, we need root directory entries count */
    uint32_t root_dir_entries = (fat_type == 32) ? 0 : 512;  /* 16KB root dir for FAT12/16 */
    uint32_t root_dir_sectors = (root_dir_entries * 32 + 511) / 512;
    
    uint32_t data_start_sector;
    if (fat_type == 32) {
        data_start_sector = reserved_sectors + (num_fats * fat_sectors);
    } else {
        data_start_sector = reserved_sectors + (num_fats * fat_sectors) + root_dir_sectors;
    }
    
    /* Allocate sector buffer */
    uint8_t buffer[FAT_SECTOR_SIZE];
    
    /* === STEP 1: Zero reserved sectors === */
    serial_puts("[FAT] Step 1: Zeroing reserved sectors...\n");
    fat_memset(buffer, 0, FAT_SECTOR_SIZE);
    for (uint32_t s = 0; s < reserved_sectors; s++) {
        if (ahci_write(port, part_offset + s, 1, buffer) != 0) {
            serial_puts("[FAT] Error: Failed to zero reserved sector\n");
            return -2;
        }
    }
    
    /* === STEP 2: Write BPB (Boot Sector at sector 0) === */
    serial_puts("[FAT] Step 2: Writing BPB...\n");
    fat_memset(buffer, 0, FAT_SECTOR_SIZE);
    
    /* Jump instruction */
    buffer[0] = 0xEB; 
    buffer[1] = (fat_type == 32) ? 0x58 : 0x3C;  /* Different jump for FAT12/16 */
    buffer[2] = 0x90;
    
    /* OEM Name */
    buffer[3] = 'N'; buffer[4] = 'E'; buffer[5] = 'O'; buffer[6] = 'L';
    buffer[7] = 'Y'; buffer[8] = 'X'; buffer[9] = 'O'; buffer[10] = 'S';
    
    /* BPB at offset 11 */
    buffer[11] = (bytes_per_sector) & 0xFF;
    buffer[12] = (bytes_per_sector >> 8) & 0xFF;
    buffer[13] = (uint8_t)sectors_per_cluster;
    buffer[14] = (reserved_sectors) & 0xFF;
    buffer[15] = (reserved_sectors >> 8) & 0xFF;
    buffer[16] = (uint8_t)num_fats;
    buffer[17] = (root_dir_entries) & 0xFF;
    buffer[18] = (root_dir_entries >> 8) & 0xFF;
    
    /* Total sectors 16 (for small volumes) */
    if (total_sectors < 0x10000 && fat_type != 32) {
        buffer[19] = total_sectors & 0xFF;
        buffer[20] = (total_sectors >> 8) & 0xFF;
    } else {
        buffer[19] = 0; buffer[20] = 0;
    }
    
    buffer[21] = 0xF8;  /* Media type: fixed disk */
    
    /* FAT size 16 (for FAT12/16), 0 for FAT32 */
    if (fat_type != 32) {
        buffer[22] = fat_sectors & 0xFF;
        buffer[23] = (fat_sectors >> 8) & 0xFF;
    } else {
        buffer[22] = 0; buffer[23] = 0;
    }
    
    buffer[24] = 63; buffer[25] = 0;   /* Sectors per track */
    buffer[26] = 255; buffer[27] = 0;  /* Number of heads */
    buffer[28] = 0; buffer[29] = 0; buffer[30] = 0; buffer[31] = 0; /* Hidden sectors */
    
    /* Total sectors 32 */
    if (total_sectors >= 0x10000 || fat_type == 32) {
        uint32_t total32 = (uint32_t)total_sectors;
        buffer[32] = total32 & 0xFF;
        buffer[33] = (total32 >> 8) & 0xFF;
        buffer[34] = (total32 >> 16) & 0xFF;
        buffer[35] = (total32 >> 24) & 0xFF;
    }
    
    if (fat_type == 32) {
        /* FAT32 specific fields at offset 36 */
        buffer[36] = fat_sectors & 0xFF;
        buffer[37] = (fat_sectors >> 8) & 0xFF;
        buffer[38] = (fat_sectors >> 16) & 0xFF;
        buffer[39] = (fat_sectors >> 24) & 0xFF;
        buffer[40] = 0; buffer[41] = 0;  /* Flags */
        buffer[42] = 0; buffer[43] = 0;  /* Version 0.0 */
        buffer[44] = root_cluster & 0xFF;
        buffer[45] = (root_cluster >> 8) & 0xFF;
        buffer[46] = (root_cluster >> 16) & 0xFF;
        buffer[47] = (root_cluster >> 24) & 0xFF;
        buffer[48] = 1; buffer[49] = 0;  /* FSInfo sector */
        buffer[50] = 6; buffer[51] = 0;  /* Backup boot sector */
        /* Reserved 52-63 already zero */
        buffer[64] = 0x80;               /* Drive number */
        buffer[65] = 0;                  /* Reserved */
        buffer[66] = 0x29;               /* Extended boot sig */
        buffer[67] = 0x12; buffer[68] = 0x34; buffer[69] = 0x56; buffer[70] = 0x78;
        
        /* Volume label */
        for (int i = 0; i < 11; i++) buffer[71 + i] = valid_label[i];
        
        /* Filesystem type */
        buffer[82] = 'F'; buffer[83] = 'A'; buffer[84] = 'T'; buffer[85] = '3';
        buffer[86] = '2'; buffer[87] = ' '; buffer[88] = ' '; buffer[89] = ' ';
    } else {
        /* FAT12/16 extended BPB at offset 36 */
        buffer[36] = 0x80;  /* Drive number */
        buffer[37] = 0;     /* Reserved */
        buffer[38] = 0x29;  /* Extended boot sig */
        buffer[39] = 0x12; buffer[40] = 0x34; buffer[41] = 0x56; buffer[42] = 0x78;
        
        /* Volume label */
        for (int i = 0; i < 11; i++) buffer[43 + i] = valid_label[i];
        
        /* Filesystem type */
        if (fat_type == 12) {
            buffer[54] = 'F'; buffer[55] = 'A'; buffer[56] = 'T'; buffer[57] = '1';
            buffer[58] = '2'; buffer[59] = ' '; buffer[60] = ' '; buffer[61] = ' ';
        } else {
            buffer[54] = 'F'; buffer[55] = 'A'; buffer[56] = 'T'; buffer[57] = '1';
            buffer[58] = '6'; buffer[59] = ' '; buffer[60] = ' '; buffer[61] = ' ';
        }
    }
    
    /* Boot signature */
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
    
    if (ahci_write(port, part_offset + 0, 1, buffer) != 0) {
        serial_puts("[FAT] Error: Failed to write boot sector\n");
        return -3;
    }
    
    /* Backup boot sector for FAT32 */
    if (fat_type == 32) {
        if (ahci_write(port, part_offset + 6, 1, buffer) != 0) {
            serial_puts("[FAT] Error: Failed to write backup boot sector\n");
            return -3;
        }
        
        /* Write FSInfo at sector 1 */
        fat_memset(buffer, 0, FAT_SECTOR_SIZE);
        buffer[0] = 0x52; buffer[1] = 0x52; buffer[2] = 0x61; buffer[3] = 0x41;
        buffer[484] = 0x72; buffer[485] = 0x72; buffer[486] = 0x41; buffer[487] = 0x61;
        
        uint32_t data_clusters = ((uint32_t)total_sectors - data_start_sector) / sectors_per_cluster;
        uint32_t free_clusters = data_clusters - 1;
        buffer[488] = free_clusters & 0xFF;
        buffer[489] = (free_clusters >> 8) & 0xFF;
        buffer[490] = (free_clusters >> 16) & 0xFF;
        buffer[491] = (free_clusters >> 24) & 0xFF;
        buffer[492] = 3; buffer[493] = 0; buffer[494] = 0; buffer[495] = 0;
        buffer[510] = 0x55; buffer[511] = 0xAA;
        
        if (ahci_write(port, part_offset + 1, 1, buffer) != 0) {
            serial_puts("[FAT] Error: Failed to write FSInfo\n");
            return -4;
        }
        if (ahci_write(port, part_offset + 7, 1, buffer) != 0) {
            serial_puts("[FAT] Error: Failed to write backup FSInfo\n");
            return -4;
        }
    }
    
    /* === STEP 3: Initialize FAT tables === */
    serial_puts("[FAT] Step 3: Initializing FAT tables...\n");
    
    fat_memset(buffer, 0, FAT_SECTOR_SIZE);
    
    if (fat_type == 32) {
        /* FAT32: 4 bytes per entry */
        buffer[0] = 0xF8; buffer[1] = 0xFF; buffer[2] = 0xFF; buffer[3] = 0x0F;
        buffer[4] = 0xFF; buffer[5] = 0xFF; buffer[6] = 0xFF; buffer[7] = 0x0F;
        buffer[8] = 0xFF; buffer[9] = 0xFF; buffer[10] = 0xFF; buffer[11] = 0x0F;
    } else if (fat_type == 16) {
        /* FAT16: 2 bytes per entry */
        buffer[0] = 0xF8; buffer[1] = 0xFF;
        buffer[2] = 0xFF; buffer[3] = 0xFF;
    } else {
        /* FAT12: 1.5 bytes per entry (first 3 entries = 4.5 bytes) */
        buffer[0] = 0xF8; buffer[1] = 0xFF; buffer[2] = 0xFF;
    }
    
    uint32_t fat1_start = reserved_sectors;
    if (ahci_write(port, part_offset + fat1_start, 1, buffer) != 0) {
        serial_puts("[FAT] Error: Failed to write FAT1\n");
        return -5;
    }
    
    uint32_t fat2_start = reserved_sectors + fat_sectors;
    if (ahci_write(port, part_offset + fat2_start, 1, buffer) != 0) {
        serial_puts("[FAT] Error: Failed to write FAT2\n");
        return -5;
    }
    
    /* Zero remaining FAT sectors (quick format) */
    fat_memset(buffer, 0, FAT_SECTOR_SIZE);
    uint32_t fat_clear = (fat_sectors < 16) ? fat_sectors : 16;
    for (uint32_t s = 1; s < fat_clear; s++) {
        ahci_write(port, part_offset + fat1_start + s, 1, buffer);
        ahci_write(port, part_offset + fat2_start + s, 1, buffer);
    }
    
    /* === STEP 4: Zero root directory === */
    serial_puts("[FAT] Step 4: Zeroing root directory...\n");
    fat_memset(buffer, 0, FAT_SECTOR_SIZE);
    
    if (fat_type == 32) {
        /* Root cluster for FAT32 */
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ahci_write(port, part_offset + data_start_sector + s, 1, buffer) != 0) {
                serial_puts("[FAT] Error: Failed to zero root directory\n");
                return -6;
            }
        }
    } else {
        /* Fixed root directory for FAT12/16 */
        uint32_t root_start = reserved_sectors + (num_fats * fat_sectors);
        for (uint32_t s = 0; s < root_dir_sectors; s++) {
            if (ahci_write(port, part_offset + root_start + s, 1, buffer) != 0) {
                serial_puts("[FAT] Error: Failed to zero root directory\n");
                return -6;
            }
        }
    }
    
    serial_puts("[FAT] Format complete!\n");
    return 0;
}

/* ============ Filesystem Registration ============ */

static vfs_fs_type_t fat_fs_type = {
    .name = "fat",
    .mount = fat_mount,
    .unmount = fat_unmount,
    .sync = fat_sync,
};

static vfs_fs_type_t fat32_fs_type = {
    .name = "fat32",
    .mount = fat_mount,
    .unmount = fat_unmount,
    .sync = fat_sync,
};

void fat_init(void) {
    serial_puts("[FAT] Initializing FAT filesystem driver...\n");
    /* FAT uses direct AHCI access - block_ops will be set by fat_mount if needed */
    vfs_register_fs(&fat_fs_type);
    vfs_register_fs(&fat32_fs_type);
    serial_puts("[FAT] Ready (FAT12/16/32)\n");
}
