/*
 * NeolyxOS Virtual File System
 * 
 * VFS abstraction layer for all filesystem types.
 * Supports mounting multiple filesystems.
 * 
 * Design:
 *   - VFS nodes (vnodes) for all files/directories
 *   - Mount points for different filesystems
 *   - File operations abstraction
 *   - Path resolution
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_VFS_H
#define NEOLYX_VFS_H

#include <stdint.h>
#include <stddef.h>

/* ============ VFS Constants ============ */

#define VFS_NAME_MAX        255
#define VFS_PATH_MAX        1024
#define VFS_MAX_MOUNTS      16
#define VFS_MAX_OPEN_FILES  64

/* File types */
typedef enum {
    VFS_FILE        = 1,
    VFS_DIRECTORY   = 2,
    VFS_SYMLINK     = 3,
    VFS_DEVICE      = 4,
    VFS_PIPE        = 5,
    VFS_SOCKET      = 6,
} vfs_type_t;

/* File open modes */
#define VFS_O_RDONLY    0x0001
#define VFS_O_WRONLY    0x0002
#define VFS_O_RDWR      0x0003
#define VFS_O_CREATE    0x0100
#define VFS_O_TRUNC     0x0200
#define VFS_O_APPEND    0x0400

/* Seek modes */
#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

/* ============ Error Codes ============ */

#define VFS_OK              0
#define VFS_ERR_NOT_FOUND   (-1)
#define VFS_ERR_PERM        (-2)
#define VFS_ERR_IO          (-3)
#define VFS_ERR_NO_MEM      (-4)
#define VFS_ERR_EXIST       (-5)
#define VFS_ERR_NOT_DIR     (-6)
#define VFS_ERR_IS_DIR      (-7)
#define VFS_ERR_NOT_EMPTY   (-8)
#define VFS_ERR_BUSY        (-9)
#define VFS_ERR_INVALID     (-10)
#define VFS_ERR_NO_SPACE    (-11)
#define VFS_ERR_READ_ONLY   (-12)

/* ============ VFS Structures ============ */

/* Forward declarations */
struct vfs_node;
struct vfs_mount;
struct vfs_operations;

/* VFS Node (file/directory/device) */
typedef struct vfs_node {
    char name[VFS_NAME_MAX + 1];
    vfs_type_t type;
    uint32_t permissions;           /* rwxrwxrwx */
    uint32_t uid;                   /* Owner user ID */
    uint32_t gid;                   /* Owner group ID */
    uint64_t size;                  /* File size */
    uint64_t created;               /* Creation time */
    uint64_t modified;              /* Modification time */
    uint64_t accessed;              /* Access time */
    
    uint64_t inode;                 /* Inode number */
    uint32_t ref_count;             /* Reference count */
    uint32_t flags;                 /* Internal flags */
    
    struct vfs_mount *mount;        /* Mount point this node belongs to */
    struct vfs_node *parent;        /* Parent directory */
    struct vfs_operations *ops;     /* File operations */
    
    void *private_data;             /* Filesystem-specific data */
} vfs_node_t;

/* Directory entry for listing */
typedef struct {
    char name[VFS_NAME_MAX + 1];
    vfs_type_t type;
    uint64_t inode;
    uint64_t size;
} vfs_dirent_t;

/* Open file handle */
typedef struct {
    vfs_node_t *node;
    uint64_t offset;
    uint32_t mode;
    uint32_t flags;
} vfs_file_t;

/* File operations (per filesystem) */
typedef struct vfs_operations {
    /* File operations */
    int (*open)(vfs_node_t *node, uint32_t mode);
    int (*close)(vfs_node_t *node);
    int64_t (*read)(vfs_node_t *node, void *buffer, uint64_t offset, uint64_t size);
    int64_t (*write)(vfs_node_t *node, const void *buffer, uint64_t offset, uint64_t size);
    
    /* Directory operations */
    int (*readdir)(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read);
    vfs_node_t *(*finddir)(vfs_node_t *dir, const char *name);
    
    /* File management */
    int (*create)(vfs_node_t *parent, const char *name, vfs_type_t type, uint32_t perms);
    int (*unlink)(vfs_node_t *parent, const char *name);
    int (*rename)(vfs_node_t *old_parent, const char *old_name, 
                  vfs_node_t *new_parent, const char *new_name);
    int (*truncate)(vfs_node_t *node, uint64_t size);
    
    /* Attributes */
    int (*stat)(vfs_node_t *node);
    int (*chmod)(vfs_node_t *node, uint32_t mode);
    int (*chown)(vfs_node_t *node, uint32_t uid, uint32_t gid);
} vfs_operations_t;

/* Filesystem type */
typedef struct {
    const char *name;               /* e.g., "nxfs", "nxfs_enc" */
    int (*mount)(struct vfs_mount *mount);
    int (*unmount)(struct vfs_mount *mount);
    int (*sync)(struct vfs_mount *mount);
} vfs_fs_type_t;

/* Mount point */
typedef struct vfs_mount {
    char path[VFS_PATH_MAX];        /* Mount path */
    vfs_node_t *root;               /* Root node of mounted filesystem */
    vfs_fs_type_t *fs_type;         /* Filesystem type */
    void *device;                   /* Block device */
    uint32_t flags;                 /* Mount flags (read-only, etc.) */
    void *private_data;             /* Filesystem-specific mount data */
} vfs_mount_t;

/* ============ VFS API ============ */

/**
 * Initialize the VFS.
 */
void vfs_init(void);

/**
 * Mount a filesystem.
 * 
 * @param path      Mount point path
 * @param device    Block device
 * @param fs_type   Filesystem type name (e.g., "nxfs")
 * @param flags     Mount flags
 * @return VFS_OK on success, error code otherwise
 */
int vfs_mount(const char *path, void *device, const char *fs_type, uint32_t flags);

/**
 * Unmount a filesystem.
 * 
 * @param path Mount point path
 * @return VFS_OK on success
 */
int vfs_unmount(const char *path);

/**
 * Open a file.
 * 
 * @param path  File path
 * @param mode  Open mode (VFS_O_RDONLY, etc.)
 * @return File handle or NULL on error
 */
vfs_file_t *vfs_open(const char *path, uint32_t mode);

/**
 * Close a file.
 * 
 * @param file File handle
 * @return VFS_OK on success
 */
int vfs_close(vfs_file_t *file);

/**
 * Read from a file.
 * 
 * @param file   File handle
 * @param buffer Buffer to read into
 * @param size   Number of bytes to read
 * @return Number of bytes read, or negative error
 */
int64_t vfs_read(vfs_file_t *file, void *buffer, uint64_t size);

/**
 * Write to a file.
 * 
 * @param file   File handle
 * @param buffer Data to write
 * @param size   Number of bytes to write
 * @return Number of bytes written, or negative error
 */
int64_t vfs_write(vfs_file_t *file, const void *buffer, uint64_t size);

/**
 * Seek in a file.
 * 
 * @param file   File handle
 * @param offset Offset
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New offset or negative error
 */
int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence);

/**
 * Get file information.
 * 
 * @param path Path to file
 * @param node Node structure to fill
 * @return VFS_OK on success
 */
int vfs_stat(const char *path, vfs_node_t *node);

/**
 * List directory contents.
 * 
 * @param path    Directory path
 * @param entries Array to fill
 * @param count   Maximum entries to return
 * @param read    Actual entries read
 * @return VFS_OK on success
 */
int vfs_readdir(const char *path, vfs_dirent_t *entries, uint32_t count, uint32_t *read);

/**
 * Create a file or directory.
 * 
 * @param path  Path to create
 * @param type  VFS_FILE or VFS_DIRECTORY
 * @param perms Permissions
 * @return VFS_OK on success
 */
int vfs_create(const char *path, vfs_type_t type, uint32_t perms);

/**
 * Delete a file or empty directory.
 * 
 * @param path Path to delete
 * @return VFS_OK on success
 */
int vfs_unlink(const char *path);

/**
 * Create a directory and all parent directories.
 * Like 'mkdir -p' - creates intermediate directories as needed.
 * 
 * @param path Full path to create
 * @param perms Permissions for directories
 * @return VFS_OK on success
 */
int vfs_makedirs(const char *path, uint32_t perms);

/**
 * Resolve a path to a VFS node.
 * 
 * @param path Full path
 * @return VFS node or NULL
 */
vfs_node_t *vfs_resolve(const char *path);

/**
 * Register a filesystem type.
 * 
 * @param fs_type Filesystem type structure
 */
void vfs_register_fs(vfs_fs_type_t *fs_type);

#endif /* NEOLYX_VFS_H */
