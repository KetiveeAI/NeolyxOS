/*
 * NeolyxOS NTFS Filesystem Implementation (Read-Only)
 * 
 * Read-only support for Windows NTFS volumes.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fs/ntfs.h"
#include "fs/nxfs.h"  /* For block_ops */
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern nxfs_block_ops_t *block_ops;

/* ============ Helpers ============ */

static void *ntfs_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void *ntfs_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static int ntfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void ntfs_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* UTF-16LE to ASCII (simple conversion) */
static void ntfs_utf16_to_ascii(const uint16_t *src, uint8_t len, char *dst) {
    for (int i = 0; i < len && i < 254; i++) {
        dst[i] = (src[i] < 128) ? (char)src[i] : '?';
    }
    dst[len] = '\0';
}

/* ============ Sector I/O ============ */

static nxfs_block_ops_t *ntfs_block_ops = NULL;

static int ntfs_read_sector(void *device, uint64_t sector, void *buffer) {
    if (!ntfs_block_ops || !ntfs_block_ops->read_block) return -1;
    return ntfs_block_ops->read_block(device, sector, buffer);
}

static int ntfs_read_cluster(ntfs_mount_t *ntfs, uint64_t lcn, void *buffer) {
    uint64_t sector = lcn * ntfs->sectors_per_cluster;
    uint8_t *dst = (uint8_t *)buffer;
    
    for (uint32_t i = 0; i < ntfs->sectors_per_cluster; i++) {
        if (ntfs_read_sector(ntfs->device, sector + i, dst + i * ntfs->bytes_per_sector) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ============ MFT Operations ============ */

int ntfs_read_mft_record(ntfs_mount_t *ntfs, uint64_t mft_num, void *buffer) {
    uint64_t records_per_cluster = ntfs->bytes_per_cluster / ntfs->bytes_per_file_record;
    uint64_t cluster = ntfs->mft_lcn + (mft_num / records_per_cluster);
    uint64_t offset = (mft_num % records_per_cluster) * ntfs->bytes_per_file_record;
    
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(ntfs->bytes_per_cluster);
    if (!cluster_buffer) return -1;
    
    if (ntfs_read_cluster(ntfs, cluster, cluster_buffer) != 0) {
        kfree(cluster_buffer);
        return -1;
    }
    
    ntfs_memcpy(buffer, cluster_buffer + offset, ntfs->bytes_per_file_record);
    kfree(cluster_buffer);
    
    /* Verify FILE signature */
    ntfs_file_record_t *record = (ntfs_file_record_t *)buffer;
    if (record->signature[0] != 'F' || record->signature[1] != 'I' ||
        record->signature[2] != 'L' || record->signature[3] != 'E') {
        return -1;
    }
    
    return 0;
}

ntfs_attr_header_t *ntfs_find_attr(void *record, uint32_t type) {
    ntfs_file_record_t *file = (ntfs_file_record_t *)record;
    uint8_t *ptr = (uint8_t *)record + file->attrs_offset;
    
    while (1) {
        ntfs_attr_header_t *attr = (ntfs_attr_header_t *)ptr;
        
        if (attr->type == NTFS_ATTR_END || attr->type == 0) {
            break;
        }
        
        if (attr->type == type) {
            return attr;
        }
        
        ptr += attr->length;
        if (ptr > (uint8_t *)record + file->bytes_used) break;
    }
    
    return NULL;
}

int64_t ntfs_read_attr(ntfs_mount_t *ntfs, void *record, uint32_t type,
                       void *buffer, uint64_t offset, uint64_t size) {
    ntfs_attr_header_t *attr = ntfs_find_attr(record, type);
    if (!attr) return -1;
    
    if (!attr->non_resident) {
        /* Resident attribute */
        uint8_t *data = (uint8_t *)attr + attr->resident.value_offset;
        uint32_t len = attr->resident.value_length;
        
        if (offset >= len) return 0;
        if (offset + size > len) size = len - offset;
        
        ntfs_memcpy(buffer, data + offset, size);
        return size;
    }
    
    /* Non-resident attribute - parse runlist */
    /* TODO: Implement runlist parsing */
    return -1;
}

/* ============ VFS Operations ============ */

static int ntfs_vfs_open(vfs_node_t *node, uint32_t mode) {
    if (mode & (VFS_O_WRONLY | VFS_O_RDWR)) {
        return VFS_ERR_READ_ONLY;
    }
    return VFS_OK;
}

static int ntfs_vfs_close(vfs_node_t *node) {
    return VFS_OK;
}

static int64_t ntfs_vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, uint64_t size) {
    if (!node || !node->mount) return VFS_ERR_INVALID;
    
    ntfs_mount_t *ntfs = (ntfs_mount_t *)node->mount->private_data;
    uint64_t mft_num = node->inode;
    
    /* Read MFT record */
    uint8_t *record = (uint8_t *)kmalloc(ntfs->bytes_per_file_record);
    if (!record) return VFS_ERR_NO_MEM;
    
    if (ntfs_read_mft_record(ntfs, mft_num, record) != 0) {
        kfree(record);
        return VFS_ERR_IO;
    }
    
    /* Read $DATA attribute */
    int64_t result = ntfs_read_attr(ntfs, record, NTFS_ATTR_DATA, buffer, offset, size);
    
    kfree(record);
    return result;
}

static int64_t ntfs_vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, uint64_t size) {
    return VFS_ERR_READ_ONLY;  /* NTFS is read-only */
}

static vfs_node_t *ntfs_vfs_finddir(vfs_node_t *dir, const char *name);
static int ntfs_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read);

static vfs_operations_t ntfs_ops = {
    .open = ntfs_vfs_open,
    .close = ntfs_vfs_close,
    .read = ntfs_vfs_read,
    .write = ntfs_vfs_write,
    .readdir = ntfs_vfs_readdir,
    .finddir = ntfs_vfs_finddir,
    .create = NULL,  /* Read-only */
    .unlink = NULL,
    .rename = NULL,
    .truncate = NULL,
    .stat = NULL,
    .chmod = NULL,
    .chown = NULL,
};

/* ============ Directory Operations ============ */

static int ntfs_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read) {
    if (!dir || dir->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    ntfs_mount_t *ntfs = (ntfs_mount_t *)dir->mount->private_data;
    uint64_t mft_num = dir->inode;
    
    /* Read MFT record */
    uint8_t *record = (uint8_t *)kmalloc(ntfs->bytes_per_file_record);
    if (!record) return VFS_ERR_NO_MEM;
    
    if (ntfs_read_mft_record(ntfs, mft_num, record) != 0) {
        kfree(record);
        return VFS_ERR_IO;
    }
    
    /* Find $INDEX_ROOT attribute */
    ntfs_attr_header_t *index = ntfs_find_attr(record, NTFS_ATTR_INDEX_ROOT);
    if (!index) {
        kfree(record);
        return VFS_ERR_IO;
    }
    
    /* TODO: Parse index entries properly */
    /* For now, return empty directory */
    *read = 0;
    
    kfree(record);
    return VFS_OK;
}

static vfs_node_t *ntfs_vfs_finddir(vfs_node_t *dir, const char *name) {
    /* TODO: Implement directory search */
    return NULL;
}

/* ============ NTFS Mount ============ */

int ntfs_mount(vfs_mount_t *mount) {
    serial_puts("[NTFS] Mounting filesystem (read-only)...\n");
    
    if (!ntfs_block_ops) {
        serial_puts("[NTFS] ERROR: No block operations set\n");
        return VFS_ERR_IO;
    }
    
    /* Read boot sector */
    uint8_t boot_sector[512];
    if (ntfs_read_sector(mount->device, 0, boot_sector) != 0) {
        return VFS_ERR_IO;
    }
    
    ntfs_boot_t *boot = (ntfs_boot_t *)boot_sector;
    
    /* Validate NTFS signature */
    if (boot->oem_id[0] != 'N' || boot->oem_id[1] != 'T' ||
        boot->oem_id[2] != 'F' || boot->oem_id[3] != 'S') {
        serial_puts("[NTFS] ERROR: Not an NTFS volume\n");
        return VFS_ERR_INVALID;
    }
    
    /* Allocate mount info */
    ntfs_mount_t *ntfs = (ntfs_mount_t *)kzalloc(sizeof(ntfs_mount_t));
    if (!ntfs) return VFS_ERR_NO_MEM;
    
    ntfs->device = mount->device;
    ntfs->bytes_per_sector = boot->bytes_per_sector;
    ntfs->sectors_per_cluster = boot->sectors_per_cluster;
    ntfs->bytes_per_cluster = ntfs->bytes_per_sector * ntfs->sectors_per_cluster;
    ntfs->mft_lcn = boot->mft_lcn;
    ntfs->total_sectors = boot->total_sectors;
    
    /* Calculate file record size */
    if (boot->clusters_per_file_record > 0) {
        ntfs->bytes_per_file_record = boot->clusters_per_file_record * ntfs->bytes_per_cluster;
    } else {
        ntfs->bytes_per_file_record = 1 << (-boot->clusters_per_file_record);
    }
    
    serial_puts("[NTFS] MFT at cluster ");
    /* Print MFT LCN */
    uint64_t lcn = ntfs->mft_lcn;
    char buf[20];
    int i = 19;
    buf[i--] = '\0';
    if (lcn == 0) { buf[i--] = '0'; }
    else { while (lcn > 0) { buf[i--] = '0' + (lcn % 10); lcn /= 10; } }
    serial_puts(&buf[i + 1]);
    serial_puts("\n");
    
    /* Allocate MFT buffer */
    ntfs->mft_buffer = (uint8_t *)kmalloc(ntfs->bytes_per_file_record);
    if (!ntfs->mft_buffer) {
        kfree(ntfs);
        return VFS_ERR_NO_MEM;
    }
    
    /* Create root vnode (MFT entry 5 = root directory) */
    vfs_node_t *root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(ntfs->mft_buffer);
        kfree(ntfs);
        return VFS_ERR_NO_MEM;
    }
    
    ntfs_strcpy(root->name, "/", VFS_NAME_MAX);
    root->type = VFS_DIRECTORY;
    root->inode = NTFS_MFT_ROOT;
    root->mount = mount;
    root->ops = &ntfs_ops;
    
    ntfs->root = root;
    mount->root = root;
    mount->private_data = ntfs;
    mount->flags |= VFS_O_RDONLY;  /* Mark as read-only */
    
    serial_puts("[NTFS] Mounted successfully (read-only)\n");
    return VFS_OK;
}

int ntfs_unmount(vfs_mount_t *mount) {
    ntfs_mount_t *ntfs = (ntfs_mount_t *)mount->private_data;
    if (ntfs) {
        if (ntfs->mft_buffer) kfree(ntfs->mft_buffer);
        kfree(ntfs);
    }
    mount->private_data = NULL;
    mount->root = NULL;
    return VFS_OK;
}

/* ============ Filesystem Registration ============ */

static vfs_fs_type_t ntfs_fs_type = {
    .name = "ntfs",
    .mount = ntfs_mount,
    .unmount = ntfs_unmount,
    .sync = NULL,  /* Read-only */
};

void ntfs_init(void) {
    serial_puts("[NTFS] Initializing NTFS filesystem driver (read-only)...\n");
    ntfs_block_ops = block_ops;
    vfs_register_fs(&ntfs_fs_type);
    serial_puts("[NTFS] Ready\n");
}
