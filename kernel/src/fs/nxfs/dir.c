/*
 * NeolyxOS NXFS Directory Operations
 * 
 * Directory entry management
 * 
 * Copyright (c) 2024-2025 KetiveeAI and its branches.
 * Licensed under the KetiveeAI License.
 */

#include "nxfs.h"
#include "../../drivers/serial.h"

/* ============ Memory Functions ============ */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, uint64_t n);
extern void *memcpy(void *dest, const void *src, uint64_t n);
extern int strcmp(const char *s1, const char *s2);
extern uint64_t strlen(const char *s);

/* ============ External Functions ============ */
extern int nxfs_read_block(uint32_t disk_id, uint64_t block_num, void *buffer);
extern int nxfs_write_block(uint32_t disk_id, uint64_t block_num, const void *buffer);
extern int nxfs_read_inode(uint32_t disk_id, uint64_t inode_num, nxfs_inode_t *inode);
extern int nxfs_write_inode(uint32_t disk_id, uint64_t inode_num, const nxfs_inode_t *inode);
extern int nxfs_file_write(uint32_t disk_id, nxfs_inode_t *inode, 
                           uint64_t offset, const void *buffer, uint32_t size);
extern int nxfs_file_read(uint32_t disk_id, nxfs_inode_t *inode,
                          uint64_t offset, void *buffer, uint32_t size);

/* ============ Constants ============ */
#define NXFS_BLOCK_SIZE     4096
#define NXFS_DIRENTRY_SIZE  72   /* 64-byte name + 8-byte inode */
#define NXFS_ENTRIES_PER_BLOCK (NXFS_BLOCK_SIZE / NXFS_DIRENTRY_SIZE)

/* ============ Directory Entry Structure ============ */
typedef struct {
    char     name[NXFS_MAX_FILENAME];
    uint64_t inode_num;
} nxfs_dirent_t;

/* ============ Directory Operations ============ */

/**
 * nxfs_dir_lookup - Find entry in directory
 */
int nxfs_dir_lookup(uint32_t disk_id, nxfs_inode_t *dir_inode, 
                    const char *name, uint64_t *out_inode)
{
    if (!dir_inode || !name || !out_inode) {
        return -1;
    }
    
    /* Check if directory */
    if ((dir_inode->mode & 0170000) != 0040000) {
        serial_print("[NXFS] Not a directory\r\n");
        return -1;
    }
    
    nxfs_dirent_t entry;
    uint32_t num_entries = dir_inode->size / NXFS_DIRENTRY_SIZE;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t offset = i * NXFS_DIRENTRY_SIZE;
        
        int bytes = nxfs_file_read(disk_id, dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
        if (bytes != NXFS_DIRENTRY_SIZE) {
            continue;
        }
        
        if (entry.inode_num != 0 && strcmp(entry.name, name) == 0) {
            *out_inode = entry.inode_num;
            return 0;
        }
    }
    
    return -1;  /* Not found */
}

/**
 * nxfs_dir_add_entry - Add entry to directory
 */
int nxfs_dir_add_entry(uint32_t disk_id, nxfs_inode_t *dir_inode,
                       const char *name, uint64_t inode_num)
{
    if (!dir_inode || !name || inode_num == 0) {
        return -1;
    }
    
    /* Check if directory */
    if ((dir_inode->mode & 0170000) != 0040000) {
        return -1;
    }
    
    /* Check if entry already exists */
    uint64_t existing;
    if (nxfs_dir_lookup(disk_id, dir_inode, name, &existing) == 0) {
        serial_print("[NXFS] Entry already exists\r\n");
        return -1;
    }
    
    /* Find free slot or append */
    nxfs_dirent_t entry;
    uint32_t num_entries = dir_inode->size / NXFS_DIRENTRY_SIZE;
    int free_slot = -1;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t offset = i * NXFS_DIRENTRY_SIZE;
        
        int bytes = nxfs_file_read(disk_id, dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
        if (bytes != NXFS_DIRENTRY_SIZE) {
            continue;
        }
        
        if (entry.inode_num == 0) {
            free_slot = i;
            break;
        }
    }
    
    /* Create new entry */
    memset(&entry, 0, sizeof(entry));
    
    uint64_t len = strlen(name);
    if (len >= NXFS_MAX_FILENAME) {
        len = NXFS_MAX_FILENAME - 1;
    }
    memcpy(entry.name, name, len);
    entry.name[len] = '\0';
    entry.inode_num = inode_num;
    
    /* Write entry */
    uint64_t offset;
    if (free_slot >= 0) {
        offset = free_slot * NXFS_DIRENTRY_SIZE;
    } else {
        offset = dir_inode->size;
    }
    
    int bytes = nxfs_file_write(disk_id, dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
    if (bytes != NXFS_DIRENTRY_SIZE) {
        return -1;
    }
    
    /* Update directory inode on disk */
    nxfs_write_inode(disk_id, dir_inode->inode_num, dir_inode);
    
    return 0;
}

/**
 * nxfs_dir_remove_entry - Remove entry from directory
 */
int nxfs_dir_remove_entry(uint32_t disk_id, nxfs_inode_t *dir_inode, const char *name)
{
    if (!dir_inode || !name) {
        return -1;
    }
    
    nxfs_dirent_t entry;
    uint32_t num_entries = dir_inode->size / NXFS_DIRENTRY_SIZE;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t offset = i * NXFS_DIRENTRY_SIZE;
        
        int bytes = nxfs_file_read(disk_id, dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
        if (bytes != NXFS_DIRENTRY_SIZE) {
            continue;
        }
        
        if (entry.inode_num != 0 && strcmp(entry.name, name) == 0) {
            /* Clear entry */
            memset(&entry, 0, sizeof(entry));
            nxfs_file_write(disk_id, dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
            return 0;
        }
    }
    
    return -1;  /* Not found */
}

/**
 * nxfs_dir_create - Create new directory
 */
int nxfs_dir_create(uint32_t disk_id, nxfs_inode_t *parent_inode, const char *name)
{
    extern int nxfs_alloc_inode(void);
    
    if (!parent_inode || !name) {
        return -1;
    }
    
    /* Allocate inode for new directory */
    int ino = nxfs_alloc_inode();
    if (ino < 0) {
        return -1;
    }
    
    /* Create directory inode */
    nxfs_inode_t new_dir;
    memset(&new_dir, 0, sizeof(new_dir));
    new_dir.inode_num = ino;
    new_dir.mode = 0040755;  /* Directory with rwxr-xr-x */
    new_dir.size = 0;
    
    uint64_t len = strlen(name);
    if (len >= NXFS_MAX_FILENAME) {
        len = NXFS_MAX_FILENAME - 1;
    }
    memcpy(new_dir.name, name, len);
    new_dir.name[len] = '\0';
    
    /* Write inode */
    int err = nxfs_write_inode(disk_id, ino, &new_dir);
    if (err) {
        extern int nxfs_free_inode(uint64_t inode_num);
        nxfs_free_inode(ino);
        return -1;
    }
    
    /* Add . and .. entries */
    nxfs_dir_add_entry(disk_id, &new_dir, ".", ino);
    nxfs_dir_add_entry(disk_id, &new_dir, "..", parent_inode->inode_num);
    
    /* Write updated inode */
    nxfs_write_inode(disk_id, ino, &new_dir);
    
    /* Add to parent directory */
    err = nxfs_dir_add_entry(disk_id, parent_inode, name, ino);
    if (err) {
        extern int nxfs_free_inode(uint64_t inode_num);
        nxfs_free_inode(ino);
        return -1;
    }
    
    return ino;
}

/**
 * nxfs_dir_delete - Delete empty directory
 */
int nxfs_dir_delete(uint32_t disk_id, nxfs_inode_t *parent_inode, const char *name)
{
    if (!parent_inode || !name) {
        return -1;
    }
    
    /* Find directory */
    uint64_t dir_ino;
    if (nxfs_dir_lookup(disk_id, parent_inode, name, &dir_ino) != 0) {
        return -1;
    }
    
    /* Read directory inode */
    nxfs_inode_t dir_inode;
    if (nxfs_read_inode(disk_id, dir_ino, &dir_inode) != 0) {
        return -1;
    }
    
    /* Check if directory */
    if ((dir_inode.mode & 0170000) != 0040000) {
        serial_print("[NXFS] Not a directory\r\n");
        return -1;
    }
    
    /* Check if empty (only . and ..) */
    nxfs_dirent_t entry;
    uint32_t num_entries = dir_inode.size / NXFS_DIRENTRY_SIZE;
    int real_entries = 0;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t offset = i * NXFS_DIRENTRY_SIZE;
        
        int bytes = nxfs_file_read(disk_id, &dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
        if (bytes != NXFS_DIRENTRY_SIZE) {
            continue;
        }
        
        if (entry.inode_num != 0 && 
            strcmp(entry.name, ".") != 0 && 
            strcmp(entry.name, "..") != 0) {
            real_entries++;
        }
    }
    
    if (real_entries > 0) {
        serial_print("[NXFS] Directory not empty\r\n");
        return -1;
    }
    
    /* Remove from parent */
    nxfs_dir_remove_entry(disk_id, parent_inode, name);
    
    /* Free inode */
    extern int nxfs_free_inode(uint64_t inode_num);
    return nxfs_free_inode(dir_ino);
}

/**
 * nxfs_dir_list - List directory entries
 */
int nxfs_dir_list(uint32_t disk_id, nxfs_inode_t *dir_inode,
                  void (*callback)(const char *name, uint64_t inode_num, void *ctx),
                  void *ctx)
{
    if (!dir_inode || !callback) {
        return -1;
    }
    
    nxfs_dirent_t entry;
    uint32_t num_entries = dir_inode->size / NXFS_DIRENTRY_SIZE;
    int count = 0;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t offset = i * NXFS_DIRENTRY_SIZE;
        
        int bytes = nxfs_file_read(disk_id, dir_inode, offset, &entry, NXFS_DIRENTRY_SIZE);
        if (bytes != NXFS_DIRENTRY_SIZE) {
            continue;
        }
        
        if (entry.inode_num != 0) {
            callback(entry.name, entry.inode_num, ctx);
            count++;
        }
    }
    
    return count;
}