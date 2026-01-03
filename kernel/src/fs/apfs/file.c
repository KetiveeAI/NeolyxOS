#include <fs/apfs/apfs.h>
#include <fs/vfs.h>

static int apfs_file_read(struct file *file, char *buf, size_t count)
{
    apfs_inode_t *inode = file->f_inode->i_private;
    apfs_fs_t *fs = file->f_inode->i_sb->s_fs_info;
    uint64_t pos = file->f_pos;
    size_t read = 0;
    
    // Handle encrypted files
    if (inode->i_crypto_id != 0) {
        if (!fs->crypto_ops || !fs->crypto_ops->decrypt) {
            return -ENOTSUP;
        }
    }
    
    // Read using extent records
    while (read < count) {
        apfs_extent_t extent;
        if (apfs_get_extent(fs, inode, pos, &extent) != 0) {
            break;
        }
        
        size_t to_read = min(count - read, extent.len - (pos - extent.offset));
        if (block_read(fs->dev, extent.phys_start, 
                      (to_read + fs->sb->s_blocksize - 1) / fs->sb->s_blocksize,
                      buf + read) < 0) {
            break;
        }
        
        // Decrypt if needed
        if (inode->i_crypto_id != 0) {
            fs->crypto_ops->decrypt(buf + read, to_read, inode->i_crypto_id);
        }
        
        read += to_read;
        pos += to_read;
    }
    
    file->f_pos = pos;
    return read;
}

static struct file_operations apfs_file_ops = {
    .read = apfs_file_read,
    .write = apfs_file_write,
    .seek = apfs_file_seek,
    // ... other operations
}; 