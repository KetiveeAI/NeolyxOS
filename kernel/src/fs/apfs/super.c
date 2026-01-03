#include <fs/apfs/apfs.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>
#include <string.h>
#include <crypto/crc.h>

static int apfs_read_super(struct super_block *sb, apfs_nx_superblock_t **nx_sb)
{
    struct block_device *dev = sb->s_dev;
    apfs_nx_superblock_t *super = kmalloc(dev->block_size);
    
    if (!super)
        return -ENOMEM;
    
    // Read container superblock (first block)
    if (block_read(dev, 0, 1, super) < 0) {
        kfree(super);
        return -EIO;
    }
    
    // Verify magic
    if (super->nx_magic != APFS_MAGIC) {
        kfree(super);
        return -EINVAL;
    }
    
    // Verify checksum
    uint64_t saved_cksum = *(uint64_t*)super->nx_o.o_cksum;
    memset(super->nx_o.o_cksum, 0, 8);
    uint64_t calc_cksum = crc64(super, dev->block_size);
    
    if (calc_cksum != saved_cksum) {
        kfree(super);
        return -EINVAL;
    }
    
    *nx_sb = super;
    return 0;
}

int apfs_mount(struct super_block *sb, void *data)
{
    apfs_nx_superblock_t *nx_sb;
    int err;
    
    if ((err = apfs_read_super(sb, &nx_sb)) != 0)
        return err;
    
    apfs_fs_t *fs = kmalloc(sizeof(apfs_fs_t));
    if (!fs) {
        kfree(nx_sb);
        return -ENOMEM;
    }
    
    fs->sb = sb;
    fs->dev = sb->s_dev;
    fs->nx_sb = nx_sb;
    
    // Initialize space manager
    if ((err = apfs_init_spaceman(fs)) != 0) {
        kfree(fs);
        kfree(nx_sb);
        return err;
    }
    
    // Initialize object map
    if ((err = apfs_init_omap(fs)) != 0) {
        apfs_free_spaceman(fs);
        kfree(fs);
        kfree(nx_sb);
        return err;
    }
    
    sb->s_fs_info = fs;
    sb->s_blocksize = nx_sb->nx_block_size;
    sb->s_op = &apfs_ops;
    
    return 0;
}

int apfs_umount(struct super_block *sb)
{
    apfs_fs_t *fs = sb->s_fs_info;
    
    if (fs) {
        apfs_free_spaceman(fs);
        apfs_free_omap(fs);
        kfree(fs->nx_sb);
        kfree(fs);
    }
    
    return 0;
} 