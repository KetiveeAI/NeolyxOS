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

static int fat_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
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
            /* Mark as end of chain */
            *(uint32_t *)(buffer + offset_in_sector) = FAT32_EOC;
            fat_write_sector(fat->device, fat_sector, buffer);
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
        *(uint32_t *)(buffer + offset_in_sector) = FAT32_FREE;
        fat_write_sector(fat->device, fat_sector, buffer);
        
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
    
    fat_mount_t *fat = (fat_mount_t *)node->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    
    /* Get or allocate first cluster */
    uint32_t first_cluster = (uint32_t)(uintptr_t)node->private_data;
    
    if (first_cluster < 2) {
        /* File has no clusters, allocate first one */
        first_cluster = fat_alloc_cluster(fat);
        if (first_cluster < 2) {
            serial_puts("[FAT] Write error: no free clusters\n");
            return VFS_ERR_NO_SPACE;
        }
        node->private_data = (void *)(uintptr_t)first_cluster;
    }
    
    /* Allocate cluster buffer */
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!cluster_buffer) return VFS_ERR_NO_MEM;
    
    /* Clear cluster buffer and copy data */
    fat_memset(cluster_buffer, 0, fat->bytes_per_cluster);
    
    uint64_t bytes_written = 0;
    uint32_t current_cluster = first_cluster;
    uint64_t cluster_offset = offset % fat->bytes_per_cluster;
    
    /* Skip to starting cluster */
    uint32_t skip_clusters = offset / fat->bytes_per_cluster;
    for (uint32_t i = 0; i < skip_clusters && current_cluster >= 2; i++) {
        uint32_t next = fat_get_next_cluster(fat, current_cluster);
        if (next < 2) {
            /* Allocate a new cluster */
            next = fat_alloc_cluster(fat);
            if (next < 2) {
                kfree(cluster_buffer);
                return VFS_ERR_NO_SPACE;
            }
            /* Link clusters */
            uint32_t fat_offset = current_cluster * 4;
            uint32_t fat_sector = fat->fat_start + (fat_offset / fat->bytes_per_sector);
            uint32_t off_in_sector = fat_offset % fat->bytes_per_sector;
            uint8_t fat_buf[512];
            fat_read_sector(fat->device, fat_sector, fat_buf);
            *(uint32_t *)(fat_buf + off_in_sector) = next;
            fat_write_sector(fat->device, fat_sector, fat_buf);
        }
        current_cluster = next;
    }
    
    /* Write data */
    const uint8_t *src = (const uint8_t *)buffer;
    
    while (bytes_written < size && current_cluster >= 2) {
        /* Read existing cluster data for partial write */
        if (cluster_offset > 0 || (size - bytes_written) < fat->bytes_per_cluster) {
            fat_read_cluster(fat, current_cluster, cluster_buffer);
        }
        
        /* Calculate how much to write to this cluster */
        uint64_t to_write = fat->bytes_per_cluster - cluster_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }
        
        /* Copy data into cluster buffer */
        fat_memcpy(cluster_buffer + cluster_offset, src + bytes_written, to_write);
        
        /* Write cluster to disk */
        if (fat_write_cluster(fat, current_cluster, cluster_buffer) != 0) {
            serial_puts("[FAT] Write error: cluster write failed\n");
            kfree(cluster_buffer);
            return VFS_ERR_IO;
        }
        
        bytes_written += to_write;
        cluster_offset = 0;
        
        /* Move to next cluster */
        if (bytes_written < size) {
            uint32_t next = fat_get_next_cluster(fat, current_cluster);
            if (next < 2) {
                /* Allocate new cluster */
                next = fat_alloc_cluster(fat);
                if (next < 2) {
                    kfree(cluster_buffer);
                    return bytes_written; /* Return partial write */
                }
                /* Link */
                uint32_t fat_offset = current_cluster * 4;
                uint32_t fat_sector = fat->fat_start + (fat_offset / fat->bytes_per_sector);
                uint32_t off_in_sector = fat_offset % fat->bytes_per_sector;
                uint8_t fat_buf[512];
                fat_read_sector(fat->device, fat_sector, fat_buf);
                *(uint32_t *)(fat_buf + off_in_sector) = next;
                fat_write_sector(fat->device, fat_sector, fat_buf);
            }
            current_cluster = next;
        }
    }
    
    /* Update file size */
    if (offset + bytes_written > node->size) {
        node->size = offset + bytes_written;
    }
    
    kfree(cluster_buffer);
    serial_puts("[FAT] Write complete\n");
    return bytes_written;
}

static vfs_node_t *fat_vfs_finddir(vfs_node_t *dir, const char *name);
static int fat_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read);

/* Convert filename to FAT 8.3 format */
static void fat_name_to_8_3(const char *name, char *name8, char *ext3) {
    int i;
    
    /* Initialize with spaces */
    for (i = 0; i < 8; i++) name8[i] = ' ';
    for (i = 0; i < 3; i++) ext3[i] = ' ';
    
    /* Find extension */
    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') dot = p;
    }
    
    /* Copy name part (up to 8 chars, uppercase) */
    i = 0;
    for (const char *p = name; *p && p != dot && i < 8; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            name8[i++] = c;
        }
    }
    
    /* Copy extension part (up to 3 chars, uppercase) */
    if (dot && dot[1]) {
        i = 0;
        for (const char *p = dot + 1; *p && i < 3; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                ext3[i++] = c;
            }
        }
    }
}

/* Create file or directory in FAT filesystem */
static int fat_vfs_create(vfs_node_t *parent, const char *name, uint8_t type, uint32_t mode) {
    if (!parent || !name || !parent->mount) return VFS_ERR_INVALID;
    if (parent->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    (void)mode; /* FAT doesn't use Unix modes */
    
    fat_mount_t *fat = (fat_mount_t *)parent->mount->private_data;
    if (!fat) return VFS_ERR_INVALID;
    
    uint32_t dir_cluster = (uint32_t)(uintptr_t)parent->private_data;
    if (dir_cluster < 2 && fat->fat_type == FAT_TYPE_FAT32) {
        dir_cluster = fat->root_cluster;
    }
    
    /* Convert name to 8.3 format */
    char name8[8], ext3[3];
    fat_name_to_8_3(name, name8, ext3);
    
    /* Allocate cluster buffer */
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!cluster_buffer) return VFS_ERR_NO_MEM;
    
    /* Search for empty slot in directory */
    uint32_t current_cluster = dir_cluster;
    int found_slot = 0;
    uint32_t slot_cluster = 0;
    uint32_t slot_index = 0;
    
    while (current_cluster >= 2 && !found_slot) {
        if (fat_read_cluster(fat, current_cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return VFS_ERR_IO;
        }
        
        uint32_t entries_per_cluster = fat->bytes_per_cluster / sizeof(fat_dirent_t);
        fat_dirent_t *de = (fat_dirent_t *)cluster_buffer;
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (de[i].name[0] == 0x00 || de[i].name[0] == 0xE5) {
                /* Empty or deleted entry - use it */
                slot_cluster = current_cluster;
                slot_index = i;
                found_slot = 1;
                break;
            }
        }
        
        if (!found_slot) {
            current_cluster = fat_get_next_cluster(fat, current_cluster);
        }
    }
    
    if (!found_slot) {
        /* Need to allocate new cluster for directory */
        uint32_t new_cluster = fat_alloc_cluster(fat);
        if (new_cluster < 2) {
            kfree(cluster_buffer);
            return VFS_ERR_NO_SPACE;
        }
        
        /* Link to directory chain */
        uint32_t fat_offset = current_cluster * 4;
        uint32_t fat_sector_num = fat->fat_start + (fat_offset / fat->bytes_per_sector);
        uint32_t off_in_sector = fat_offset % fat->bytes_per_sector;
        uint8_t fat_buf[512];
        fat_read_sector(fat->device, fat_sector_num, fat_buf);
        *(uint32_t *)(fat_buf + off_in_sector) = new_cluster;
        fat_write_sector(fat->device, fat_sector_num, fat_buf);
        
        /* Clear new cluster */
        fat_memset(cluster_buffer, 0, fat->bytes_per_cluster);
        fat_write_cluster(fat, new_cluster, cluster_buffer);
        
        slot_cluster = new_cluster;
        slot_index = 0;
    }
    
    /* Read cluster containing slot */
    if (fat_read_cluster(fat, slot_cluster, cluster_buffer) != 0) {
        kfree(cluster_buffer);
        return VFS_ERR_IO;
    }
    
    fat_dirent_t *de = (fat_dirent_t *)cluster_buffer;
    fat_dirent_t *entry = &de[slot_index];
    
    /* Clear and fill entry */
    fat_memset(entry, 0, sizeof(fat_dirent_t));
    fat_memcpy(entry->name, name8, 8);
    fat_memcpy(entry->ext, ext3, 3);
    
    if (type == VFS_DIRECTORY) {
        entry->attr = FAT_ATTR_DIRECTORY;
        /* Allocate cluster for directory contents */
        uint32_t dir_data_cluster = fat_alloc_cluster(fat);
        if (dir_data_cluster >= 2) {
            entry->first_cluster_lo = dir_data_cluster & 0xFFFF;
            entry->first_cluster_hi = (dir_data_cluster >> 16) & 0xFFFF;
        }
    } else {
        entry->attr = FAT_ATTR_ARCHIVE;
        /* Files start with no clusters (allocated on first write) */
        entry->first_cluster_lo = 0;
        entry->first_cluster_hi = 0;
    }
    
    entry->file_size = 0;
    
    /* Write directory cluster back */
    if (fat_write_cluster(fat, slot_cluster, cluster_buffer) != 0) {
        kfree(cluster_buffer);
        return VFS_ERR_IO;
    }
    
    kfree(cluster_buffer);
    serial_puts("[FAT] Created file: ");
    serial_puts(name);
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
    .unlink = NULL,
    .rename = NULL,
    .truncate = NULL,
    .stat = NULL,
    .chmod = NULL,
    .chown = NULL,
};

/* ============ Directory Operations ============ */

static int fat_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read) {
    if (!dir || dir->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    fat_mount_t *fat = (fat_mount_t *)dir->mount->private_data;
    uint32_t cluster = (uint32_t)(uintptr_t)dir->private_data;
    
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fat->bytes_per_cluster);
    if (!cluster_buffer) return VFS_ERR_NO_MEM;
    
    *read = 0;
    
    while (cluster >= 2 && *read < count) {
        if (fat_read_cluster(fat, cluster, cluster_buffer) != 0) {
            break;
        }
        
        uint32_t entries_per_cluster = fat->bytes_per_cluster / sizeof(fat_dirent_t);
        fat_dirent_t *de = (fat_dirent_t *)cluster_buffer;
        
        for (uint32_t i = 0; i < entries_per_cluster && *read < count; i++) {
            if (de[i].name[0] == 0x00) {
                /* No more entries */
                goto done;
            }
            if (de[i].name[0] == 0xE5) {
                /* Deleted entry */
                continue;
            }
            if (de[i].attr == FAT_ATTR_LFN) {
                /* Skip long filename entries for now */
                continue;
            }
            if (de[i].attr & FAT_ATTR_VOLUME_ID) {
                continue;
            }
            
            /* Valid entry */
            fat_8_3_to_name(&de[i], entries[*read].name);
            entries[*read].type = (de[i].attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
            entries[*read].inode = ((uint32_t)de[i].first_cluster_hi << 16) | de[i].first_cluster_lo;
            entries[*read].size = de[i].file_size;
            (*read)++;
        }
        
        cluster = fat_get_next_cluster(fat, cluster);
    }
    
done:
    kfree(cluster_buffer);
    return VFS_OK;
}

static vfs_node_t *fat_vfs_finddir(vfs_node_t *dir, const char *name) {
    vfs_dirent_t entries[32];
    uint32_t read = 0;
    
    if (fat_vfs_readdir(dir, entries, 32, &read) != VFS_OK) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < read; i++) {
        if (fat_strcmp(entries[i].name, name) == 0) {
            vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
            if (!node) return NULL;
            
            fat_strcpy(node->name, name, VFS_NAME_MAX);
            node->type = entries[i].type;
            node->size = entries[i].size;
            node->inode = entries[i].inode;
            node->mount = dir->mount;
            node->parent = dir;
            node->ops = &fat_ops;
            node->private_data = (void *)(uintptr_t)entries[i].inode;  /* First cluster */
            
            return node;
        }
    }
    
    return NULL;
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
