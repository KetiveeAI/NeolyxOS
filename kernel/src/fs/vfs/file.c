/*
 * NeolyxOS VFS File Operations
 * 
 * File descriptor management and I/O operations
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

/* ============ Dentry Functions (from dentry.c) ============ */
typedef struct dentry dentry_t;
extern dentry_t *vfs_path_resolve(const char *path);
extern void vfs_dentry_release(dentry_t *d);
extern struct inode *vfs_dentry_get_inode(dentry_t *d);

/* ============ File Descriptor Table ============ */
#define MAX_OPEN_FILES      256
#define FD_START            3    /* 0=stdin, 1=stdout, 2=stderr */

typedef struct {
    struct file     file;
    dentry_t       *dentry;
    int             in_use;
} file_entry_t;

static file_entry_t file_table[MAX_OPEN_FILES];
static int file_table_initialized = 0;

/* ============ Initialization ============ */
static void file_table_init(void)
{
    if (file_table_initialized) {
        return;
    }
    
    memset(file_table, 0, sizeof(file_table));
    file_table_initialized = 1;
    serial_print("[VFS] File table initialized\r\n");
}

/* ============ Allocate File Descriptor ============ */
static int alloc_fd(void)
{
    for (int i = FD_START; i < MAX_OPEN_FILES; i++) {
        if (!file_table[i].in_use) {
            return i;
        }
    }
    return -1;  /* No free descriptors */
}

/* ============ Validate File Descriptor ============ */
static file_entry_t *get_file_entry(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return 0;
    }
    if (!file_table[fd].in_use) {
        return 0;
    }
    return &file_table[fd];
}

/* ============ Error Codes ============ */
#define ENOENT      2
#define EBADF       9
#define EACCES      13
#define EINVAL      22
#define EMFILE      24
#define ENOSYS      38

/* ============ Public API ============ */

/**
 * vfs_open - Open file by path
 */
int vfs_open(const char *path, int flags)
{
    if (!file_table_initialized) {
        file_table_init();
    }
    
    if (!path) {
        return -EINVAL;
    }
    
    /* Resolve path */
    dentry_t *dentry = vfs_path_resolve(path);
    
    if (!dentry) {
        /* File not found */
        if (flags & O_CREAT) {
            /* TODO: Create file */
            serial_print("[VFS] vfs_open: O_CREAT not yet implemented\r\n");
            return -ENOSYS;
        }
        return -ENOENT;
    }
    
    struct inode *inode = vfs_dentry_get_inode(dentry);
    if (!inode) {
        vfs_dentry_release(dentry);
        return -ENOENT;
    }
    
    /* Allocate file descriptor */
    int fd = alloc_fd();
    if (fd < 0) {
        vfs_dentry_release(dentry);
        return -EMFILE;
    }
    
    /* Initialize file entry */
    file_entry_t *entry = &file_table[fd];
    entry->in_use = 1;
    entry->dentry = dentry;
    entry->file.f_inode = inode;
    entry->file.f_flags = flags;
    entry->file.f_pos = (flags & O_APPEND) ? inode->i_size : 0;
    entry->file.f_private = 0;
    
    /* Call filesystem open if available */
    if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->open) {
        int err = inode->i_sb->s_op->open(inode, &entry->file);
        if (err < 0) {
            entry->in_use = 0;
            vfs_dentry_release(dentry);
            return err;
        }
    }
    
    return fd;
}

/**
 * vfs_close - Close file descriptor
 */
int vfs_close(int fd)
{
    file_entry_t *entry = get_file_entry(fd);
    if (!entry) {
        return -EBADF;
    }
    
    struct inode *inode = entry->file.f_inode;
    
    /* Call filesystem close if available */
    if (inode && inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->close) {
        inode->i_sb->s_op->close(inode, &entry->file);
    }
    
    /* Release dentry */
    if (entry->dentry) {
        vfs_dentry_release(entry->dentry);
    }
    
    /* Clear entry */
    memset(entry, 0, sizeof(file_entry_t));
    
    return 0;
}

/**
 * vfs_read - Read from file
 */
ssize_t vfs_read(int fd, void *buf, size_t count)
{
    if (!buf || count == 0) {
        return 0;
    }
    
    file_entry_t *entry = get_file_entry(fd);
    if (!entry) {
        return -EBADF;
    }
    
    /* Check read permission */
    uint32_t flags = entry->file.f_flags & 0x03;
    if (flags != O_RDONLY && flags != O_RDWR) {
        return -EACCES;
    }
    
    struct inode *inode = entry->file.f_inode;
    if (!inode) {
        return -EBADF;
    }
    
    /* Check EOF */
    if (entry->file.f_pos >= inode->i_size) {
        return 0;
    }
    
    /* Limit to remaining bytes */
    if (entry->file.f_pos + count > inode->i_size) {
        count = inode->i_size - entry->file.f_pos;
    }
    
    /* Call filesystem read */
    if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->read) {
        ssize_t bytes = inode->i_sb->s_op->read(&entry->file, buf, count);
        if (bytes > 0) {
            entry->file.f_pos += bytes;
        }
        return bytes;
    }
    
    return -ENOSYS;
}

/**
 * vfs_write - Write to file
 */
ssize_t vfs_write(int fd, const void *buf, size_t count)
{
    if (!buf || count == 0) {
        return 0;
    }
    
    file_entry_t *entry = get_file_entry(fd);
    if (!entry) {
        return -EBADF;
    }
    
    /* Check write permission */
    uint32_t flags = entry->file.f_flags & 0x03;
    if (flags != O_WRONLY && flags != O_RDWR) {
        return -EACCES;
    }
    
    struct inode *inode = entry->file.f_inode;
    if (!inode) {
        return -EBADF;
    }
    
    /* Call filesystem write */
    if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->write) {
        ssize_t bytes = inode->i_sb->s_op->write(&entry->file, buf, count);
        if (bytes > 0) {
            entry->file.f_pos += bytes;
            /* Update inode size if needed */
            if (entry->file.f_pos > inode->i_size) {
                inode->i_size = entry->file.f_pos;
            }
            /* Mark inode dirty */
            extern void vfs_inode_mark_dirty(struct inode *);
            vfs_inode_mark_dirty(inode);
        }
        return bytes;
    }
    
    return -ENOSYS;
}

/**
 * vfs_seek - Seek in file
 */
off_t vfs_seek(int fd, off_t offset, int whence)
{
    file_entry_t *entry = get_file_entry(fd);
    if (!entry) {
        return -EBADF;
    }
    
    struct inode *inode = entry->file.f_inode;
    if (!inode) {
        return -EBADF;
    }
    
    int64_t new_pos;
    
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = entry->file.f_pos + offset;
        break;
    case SEEK_END:
        new_pos = inode->i_size + offset;
        break;
    default:
        return -EINVAL;
    }
    
    if (new_pos < 0) {
        return -EINVAL;
    }
    
    /* Call filesystem seek if available */
    if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->seek) {
        off_t result = inode->i_sb->s_op->seek(&entry->file, offset, whence);
        if (result >= 0) {
            entry->file.f_pos = result;
        }
        return result;
    }
    
    entry->file.f_pos = new_pos;
    return new_pos;
}

/**
 * vfs_stat - Get file status
 */
int vfs_stat(const char *path, struct inode *out_stat)
{
    if (!path || !out_stat) {
        return -EINVAL;
    }
    
    dentry_t *dentry = vfs_path_resolve(path);
    if (!dentry) {
        return -ENOENT;
    }
    
    struct inode *inode = vfs_dentry_get_inode(dentry);
    if (!inode) {
        vfs_dentry_release(dentry);
        return -ENOENT;
    }
    
    /* Copy inode info */
    memcpy(out_stat, inode, sizeof(struct inode));
    
    vfs_dentry_release(dentry);
    return 0;
}

/**
 * vfs_fstat - Get file status by fd
 */
int vfs_fstat(int fd, struct inode *out_stat)
{
    if (!out_stat) {
        return -EINVAL;
    }
    
    file_entry_t *entry = get_file_entry(fd);
    if (!entry) {
        return -EBADF;
    }
    
    struct inode *inode = entry->file.f_inode;
    if (!inode) {
        return -EBADF;
    }
    
    memcpy(out_stat, inode, sizeof(struct inode));
    return 0;
}

/**
 * vfs_dup - Duplicate file descriptor
 */
int vfs_dup(int oldfd)
{
    file_entry_t *old_entry = get_file_entry(oldfd);
    if (!old_entry) {
        return -EBADF;
    }
    
    int newfd = alloc_fd();
    if (newfd < 0) {
        return -EMFILE;
    }
    
    file_entry_t *new_entry = &file_table[newfd];
    memcpy(new_entry, old_entry, sizeof(file_entry_t));
    
    /* Increment dentry refcount */
    if (new_entry->dentry) {
        /* Just mark as used, dentry refcount handled separately */
        new_entry->dentry = old_entry->dentry;
    }
    
    return newfd;
}

/**
 * vfs_get_file_pos - Get current file position
 */
uint64_t vfs_get_file_pos(int fd)
{
    file_entry_t *entry = get_file_entry(fd);
    if (!entry) {
        return 0;
    }
    return entry->file.f_pos;
}

/**
 * vfs_get_file_size - Get file size
 */
uint64_t vfs_get_file_size(int fd)
{
    file_entry_t *entry = get_file_entry(fd);
    if (!entry || !entry->file.f_inode) {
        return 0;
    }
    return entry->file.f_inode->i_size;
}