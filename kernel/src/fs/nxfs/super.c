// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <fs/nxfs/nxfs.h>
#include <fs/vfs.h>
#include <storage/storage.h>
#include <core/kernel.h>
#include <utils/log.h>

// NXFS superblock info structure
typedef struct {
    nxfs_superblock_t super;
    uint64_t root_inode_lba;
    uint64_t inode_table_lba;
    uint64_t data_blocks_lba;
    uint32_t inode_count;
    uint32_t free_inodes;
    uint32_t free_blocks;
} nxfs_sb_info_t;

// Implement full mount logic
int nxfs_mount(struct super_block *sb, void *data) {
    nxfs_superblock_t super;
    nxfs_sb_info_t *sb_info;
    char device_name[16];
    
    if (!sb || !data) {
        klog("NXFS: Invalid mount parameters");
        return -1; // EINVAL equivalent
    }
    
    // Extract device name from data
    if (strlen((char*)data) >= sizeof(device_name)) {
        klog("NXFS: Device name too long");
        return -1;
    }
    strcpy(device_name, (char*)data);
    
    // Read superblock from device
    if (storage_read_sectors(device_name, 0, 1, &super) != 0) {
        klogf("NXFS: Failed to read superblock from device %s", device_name);
        return -5; // EIO equivalent
    }
    
    // Validate magic number
    if (super.magic != NXFS_MAGIC) {
        klogf("NXFS: Invalid magic number 0x%08X, expected 0x%08X", 
              super.magic, NXFS_MAGIC);
        return -1; // EINVAL equivalent
    }
    
    // Allocate superblock info
    sb_info = kmalloc(sizeof(nxfs_sb_info_t));
    if (!sb_info) {
        klog("NXFS: Failed to allocate superblock info");
        return -2; // ENOMEM equivalent
    }
    
    // Initialize superblock info
    memset(sb_info, 0, sizeof(nxfs_sb_info_t));
    sb_info->super = super;
    
    // Calculate block layout (simplified)
    sb_info->root_inode_lba = 1; // Root inode at LBA 1
    sb_info->inode_table_lba = 2; // Inode table starts at LBA 2
    sb_info->data_blocks_lba = 1024; // Data blocks start at LBA 1024
    
    // Initialize superblock structure
    sb->s_type = FS_TYPE_NXFS;
    sb->s_flags = 0;
    sb->s_blocksize = super.block_size;
    sb->s_fs_info = sb_info;
    
    klogf("NXFS: Successfully mounted device %s (version %d, blocks: %lu)", 
          device_name, super.version, super.block_count);
    
    return 0; // Success
}

// Unmount function
int nxfs_umount(struct super_block *sb) {
    nxfs_sb_info_t *sb_info;
    
    if (!sb || !sb->s_fs_info) {
        klog("NXFS: Invalid unmount parameters");
        return -1;
    }
    
    sb_info = (nxfs_sb_info_t*)sb->s_fs_info;
    
    // Free superblock info
    kfree(sb_info);
    sb->s_fs_info = NULL;
    
    klog("NXFS: Successfully unmounted");
    return 0;
} 