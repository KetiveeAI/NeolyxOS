/*
 * NeolyxOS VFS Superblock and Mount Operations
 * 
 * Filesystem mounting and superblock management
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
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dest, const char *src);
extern uint64_t strlen(const char *s);

/* ============ External Functions ============ */
extern struct fs_operations *get_fs_ops(const char *name);

/* ============ Dentry Functions ============ */
typedef struct dentry dentry_t;
extern dentry_t *vfs_dentry_get_root(void);
extern void vfs_dentry_set_root(struct inode *root_inode);
extern dentry_t *vfs_dentry_create(const char *name, dentry_t *parent, struct inode *inode);
extern dentry_t *vfs_path_resolve(const char *path);
extern void vfs_dentry_release(dentry_t *d);
extern struct inode *vfs_dentry_get_inode(dentry_t *d);

/* ============ Inode Functions ============ */
extern struct inode *vfs_inode_create(struct super_block *sb);

/* ============ Mount Table ============ */
#define MAX_MOUNTS          32
#define PATH_MAX            256

typedef struct mount_point {
    char                source[PATH_MAX];
    char                target[PATH_MAX];
    char                fstype[32];
    struct super_block  sb;
    dentry_t           *mount_dentry;
    int                 in_use;
    unsigned long       flags;
} mount_point_t;

static mount_point_t mount_table[MAX_MOUNTS];
static int mount_table_initialized = 0;
static struct super_block *root_sb = 0;

/* ============ Error Codes ============ */
#define ENOENT      2
#define ENODEV      19
#define EINVAL      22
#define EBUSY       16
#define ENOSPC      28
#define ENOSYS      38

/* ============ Initialization ============ */
static void mount_table_init(void)
{
    if (mount_table_initialized) {
        return;
    }
    
    memset(mount_table, 0, sizeof(mount_table));
    mount_table_initialized = 1;
    serial_print("[VFS] Mount table initialized\r\n");
}

/* ============ Find Mount Point ============ */
static mount_point_t *find_mount_by_target(const char *target)
{
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].in_use && strcmp(mount_table[i].target, target) == 0) {
            return &mount_table[i];
        }
    }
    return 0;
}

static mount_point_t *alloc_mount(void)
{
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].in_use) {
            return &mount_table[i];
        }
    }
    return 0;
}

/* ============ Public API ============ */

/**
 * vfs_mount - Mount filesystem
 */
int vfs_mount(const char *source, const char *target, const char *fstype, 
              unsigned long flags, void *data)
{
    if (!mount_table_initialized) {
        mount_table_init();
    }
    
    if (!target || !fstype) {
        return -EINVAL;
    }
    
    /* Check if already mounted */
    if (find_mount_by_target(target)) {
        serial_print("[VFS] Mount target already in use: ");
        serial_print(target);
        serial_print("\r\n");
        return -EBUSY;
    }
    
    /* Get filesystem operations */
    struct fs_operations *ops = get_fs_ops(fstype);
    if (!ops) {
        serial_print("[VFS] Unknown filesystem type: ");
        serial_print(fstype);
        serial_print("\r\n");
        return -ENODEV;
    }
    
    /* Allocate mount point */
    mount_point_t *mp = alloc_mount();
    if (!mp) {
        return -ENOSPC;
    }
    
    /* Initialize mount point */
    memset(mp, 0, sizeof(mount_point_t));
    
    if (source) {
        uint64_t len = strlen(source);
        if (len >= PATH_MAX) len = PATH_MAX - 1;
        memcpy(mp->source, source, len);
    }
    
    uint64_t tlen = strlen(target);
    if (tlen >= PATH_MAX) tlen = PATH_MAX - 1;
    memcpy(mp->target, target, tlen);
    
    uint64_t flen = strlen(fstype);
    if (flen >= 32) flen = 31;
    memcpy(mp->fstype, fstype, flen);
    
    mp->flags = flags;
    
    /* Initialize superblock */
    mp->sb.s_type = FS_TYPE_NXFS;  /* Default */
    mp->sb.s_flags = flags;
    mp->sb.s_blocksize = 4096;     /* Default 4KB blocks */
    mp->sb.s_op = ops;
    mp->sb.s_fs_info = 0;
    
    /* Call filesystem mount */
    if (ops->mount) {
        int err = ops->mount(&mp->sb, data);
        if (err < 0) {
            memset(mp, 0, sizeof(mount_point_t));
            return err;
        }
    }
    
    /* Create root inode for this filesystem */
    struct inode *root_inode = vfs_inode_create(&mp->sb);
    if (!root_inode) {
        if (ops->umount) {
            ops->umount(&mp->sb);
        }
        memset(mp, 0, sizeof(mount_point_t));
        return -ENOSPC;
    }
    
    /* Set root inode properties */
    root_inode->i_ino = 1;  /* Root inode is typically 1 */
    root_inode->i_mode = 0040755;  /* Directory */
    
    /* Handle root mount specially */
    if (strcmp(target, "/") == 0) {
        root_sb = &mp->sb;
        vfs_dentry_set_root(root_inode);
        mp->mount_dentry = vfs_dentry_get_root();
        serial_print("[VFS] Root filesystem mounted\r\n");
    } else {
        /* Mount at target path */
        dentry_t *target_dentry = vfs_path_resolve(target);
        if (!target_dentry) {
            /* Create mount point dentry */
            dentry_t *parent = vfs_dentry_get_root();
            target_dentry = vfs_dentry_create(target + 1, parent, root_inode);
        } else {
            /* Update existing dentry */
            vfs_dentry_release(target_dentry);
        }
        mp->mount_dentry = target_dentry;
    }
    
    mp->in_use = 1;
    
    serial_print("[VFS] Mounted ");
    serial_print(fstype);
    serial_print(" on ");
    serial_print(target);
    serial_print("\r\n");
    
    return 0;
}

/**
 * vfs_umount - Unmount filesystem
 */
int vfs_umount(const char *target)
{
    if (!target) {
        return -EINVAL;
    }
    
    mount_point_t *mp = find_mount_by_target(target);
    if (!mp) {
        return -ENOENT;
    }
    
    /* Check if root - can't unmount root */
    if (strcmp(target, "/") == 0) {
        serial_print("[VFS] Cannot unmount root filesystem\r\n");
        return -EBUSY;
    }
    
    /* Call filesystem unmount */
    if (mp->sb.s_op && mp->sb.s_op->umount) {
        int err = mp->sb.s_op->umount(&mp->sb);
        if (err < 0) {
            return err;
        }
    }
    
    /* Release mount dentry */
    if (mp->mount_dentry) {
        vfs_dentry_release(mp->mount_dentry);
    }
    
    /* Clear mount point */
    memset(mp, 0, sizeof(mount_point_t));
    
    serial_print("[VFS] Unmounted ");
    serial_print(target);
    serial_print("\r\n");
    
    return 0;
}

/**
 * vfs_sync - Sync all filesystems
 */
int vfs_sync(void)
{
    extern int vfs_inode_sync_all(void);
    int synced = vfs_inode_sync_all();
    
    serial_print("[VFS] Synced inodes\r\n");
    return synced;
}

/**
 * vfs_get_root_sb - Get root superblock
 */
struct super_block *vfs_get_root_sb(void)
{
    return root_sb;
}

/**
 * vfs_statfs - Get filesystem statistics
 */
int vfs_statfs(const char *path, struct super_block *out_sb)
{
    if (!path || !out_sb) {
        return -EINVAL;
    }
    
    /* Find mount point for path */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].in_use) {
            continue;
        }
        
        /* Simple prefix match */
        uint64_t tlen = strlen(mount_table[i].target);
        int match = 1;
        for (uint64_t j = 0; j < tlen; j++) {
            if (path[j] != mount_table[i].target[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            memcpy(out_sb, &mount_table[i].sb, sizeof(struct super_block));
            return 0;
        }
    }
    
    return -ENOENT;
}

/**
 * vfs_init - Initialize VFS subsystem
 */
int vfs_init(void)
{
    extern int vfs_inode_cache_init(void);
    extern int vfs_dentry_cache_init(void);
    
    mount_table_init();
    vfs_inode_cache_init();
    vfs_dentry_cache_init();
    
    serial_print("[VFS] Virtual Filesystem initialized\r\n");
    return 0;
}

/**
 * vfs_list_mounts - List all mount points
 */
int vfs_list_mounts(void)
{
    int count = 0;
    
    serial_print("[VFS] Mount points:\r\n");
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].in_use) {
            serial_print("  ");
            serial_print(mount_table[i].source[0] ? mount_table[i].source : "(none)");
            serial_print(" -> ");
            serial_print(mount_table[i].target);
            serial_print(" (");
            serial_print(mount_table[i].fstype);
            serial_print(")\r\n");
            count++;
        }
    }
    
    return count;
}