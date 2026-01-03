// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <fs/fs.h>

// File system types
#define FS_TYPE_NXFS   0x01
#define FS_TYPE_EXT4   0x02
#define FS_TYPE_FAT32  0x03
#define FS_TYPE_NTFS   0x04
#define FS_TYPE_ISO9660 0x05

// File open flags
#define O_RDONLY   0x01
#define O_WRONLY   0x02
#define O_RDWR     0x03
#define O_CREAT    0x04
#define O_TRUNC    0x08
#define O_APPEND   0x10

// File seek whence
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

// File system operations structure
struct fs_operations {
    int (*mount)(struct super_block *sb, void *data);
    int (*umount)(struct super_block *sb);
    // File operations
    int (*open)(struct inode *inode, struct file *file);
    int (*close)(struct inode *inode, struct file *file);
    ssize_t (*read)(struct file *file, char *buf, size_t count);
    ssize_t (*write)(struct file *file, const char *buf, size_t count);
    off_t (*seek)(struct file *file, off_t offset, int whence);
    // Directory operations
    int (*mkdir)(struct inode *dir, const char *name, int mode);
    int (*rmdir)(struct inode *dir, const char *name);
    int (*readdir)(struct inode *dir, struct dirent *dirent, unsigned int count);
    // Inode operations
    int (*create)(struct inode *dir, const char *name, int mode);
    int (*lookup)(struct inode *dir, const char *name, struct inode **result);
    int (*unlink)(struct inode *dir, const char *name);
};

// Superblock structure
struct super_block {
    uint32_t s_type;
    uint32_t s_flags;
    uint32_t s_blocksize;
    struct fs_operations *s_op;
    void *s_fs_info; // Filesystem-specific info
};

// Inode structure
struct inode {
    uint32_t i_ino;
    uint32_t i_mode;
    uint32_t i_uid;
    uint32_t i_gid;
    uint64_t i_size;
    uint64_t i_atime;
    uint64_t i_mtime;
    uint64_t i_ctime;
    struct super_block *i_sb;
    void *i_private; // Filesystem-specific data
};

// File structure
struct file {
    struct inode *f_inode;
    uint32_t f_flags;
    uint64_t f_pos;
    void *f_private;
};

// Directory entry structure
struct dirent {
    uint32_t d_ino;
    char d_name[256];
    uint32_t d_type;
};

// VFS API
int vfs_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data);
int vfs_umount(const char *target);
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
off_t vfs_seek(int fd, off_t offset, int whence);

#endif // VFS_H 