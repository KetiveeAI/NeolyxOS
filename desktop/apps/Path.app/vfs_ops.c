/*
 * NeolyxOS Path.app - VFS Operations
 * 
 * Filesystem operation wrappers using NeolyxOS VFS syscalls.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* ============ Forward Declarations ============ */

typedef struct file_entry file_entry_t;

/* ============ VFS Syscall Numbers ============ */

#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_STAT        40
#define SYS_OPENDIR     50
#define SYS_READDIR     51
#define SYS_CLOSEDIR    52
#define SYS_MKDIR       53
#define SYS_RMDIR       54
#define SYS_UNLINK      55
#define SYS_RENAME      56

/* ============ Directory Entry Structure ============ */

typedef struct {
    char name[256];
    uint64_t size;
    uint64_t mtime;
    uint32_t mode;
    uint8_t is_dir;
    uint8_t is_hidden;
    uint8_t is_symlink;
    uint8_t reserved;
} vfs_dirent_t;

/* ============ Stat Structure ============ */

typedef struct {
    uint64_t size;
    uint64_t mtime;
    uint64_t atime;
    uint64_t ctime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint8_t is_dir;
    uint8_t is_symlink;
    uint8_t reserved[6];
} vfs_stat_t;

/* ============ Syscall Wrapper ============ */

static inline int64_t syscall3(int num, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t result;
    __asm__ volatile (
        "movq %1, %%rax\n"
        "movq %2, %%rdi\n"
        "movq %3, %%rsi\n"
        "movq %4, %%rdx\n"
        "syscall\n"
        "movq %%rax, %0\n"
        : "=r"(result)
        : "r"((uint64_t)num), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return result;
}

static inline int64_t syscall2(int num, uint64_t a1, uint64_t a2) {
    return syscall3(num, a1, a2, 0);
}

static inline int64_t syscall1(int num, uint64_t a1) {
    return syscall3(num, a1, 0, 0);
}

/* ============ String Helpers ============ */

static int vfs_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void vfs_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void vfs_strcat(char *dst, const char *src, int max) {
    int dlen = vfs_strlen(dst);
    int i = 0;
    while (src[i] && dlen + i < max - 1) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
}

/* ============ VFS Operations ============ */

/*
 * List directory contents
 * Returns number of entries read, or -1 on error
 */
int path_vfs_list_dir(const char *path, file_entry_t *entries, int max_entries) {
    if (!path || !entries || max_entries <= 0) return -1;
    
    /* Open directory */
    int64_t dir_fd = syscall1(SYS_OPENDIR, (uint64_t)path);
    if (dir_fd < 0) return -1;
    
    int count = 0;
    vfs_dirent_t dirent;
    
    /* Read entries */
    while (count < max_entries) {
        int64_t result = syscall2(SYS_READDIR, dir_fd, (uint64_t)&dirent);
        if (result <= 0) break;
        
        file_entry_t *entry = &entries[count];
        
        /* Copy name */
        vfs_strcpy(entry->name, dirent.name, 256);
        
        /* Build full path */
        vfs_strcpy(entry->path, path, 1024);
        if (path[vfs_strlen(path) - 1] != '/') {
            vfs_strcat(entry->path, "/", 1024);
        }
        vfs_strcat(entry->path, dirent.name, 1024);
        
        /* Copy attributes */
        entry->size = dirent.size;
        entry->mtime = dirent.mtime;
        entry->mode = dirent.mode;
        entry->is_dir = dirent.is_dir;
        entry->is_hidden = dirent.is_hidden || (dirent.name[0] == '.');
        entry->is_symlink = dirent.is_symlink;
        entry->selected = 0;
        entry->icon_id = entry->is_dir ? 2 : 4;  /* NXI_ICON_FOLDER or NXI_ICON_FILE */
        
        count++;
    }
    
    /* Close directory */
    syscall1(SYS_CLOSEDIR, dir_fd);
    
    return count;
}

/*
 * Get file/directory info
 * Returns 0 on success, -1 on error
 */
int path_vfs_stat(const char *path, file_entry_t *entry) {
    if (!path || !entry) return -1;
    
    vfs_stat_t stat;
    int64_t result = syscall2(SYS_STAT, (uint64_t)path, (uint64_t)&stat);
    if (result < 0) return -1;
    
    entry->size = stat.size;
    entry->mtime = stat.mtime;
    entry->mode = stat.mode;
    entry->is_dir = stat.is_dir;
    entry->is_symlink = stat.is_symlink;
    
    return 0;
}

/*
 * Create a new directory
 * Returns 0 on success, -1 on error
 */
int path_vfs_mkdir(const char *path) {
    if (!path) return -1;
    return syscall1(SYS_MKDIR, (uint64_t)path) < 0 ? -1 : 0;
}

/*
 * Delete a file
 * Returns 0 on success, -1 on error
 */
int path_vfs_unlink(const char *path) {
    if (!path) return -1;
    return syscall1(SYS_UNLINK, (uint64_t)path) < 0 ? -1 : 0;
}

/*
 * Delete a directory (must be empty)
 * Returns 0 on success, -1 on error
 */
int path_vfs_rmdir(const char *path) {
    if (!path) return -1;
    return syscall1(SYS_RMDIR, (uint64_t)path) < 0 ? -1 : 0;
}

/*
 * Delete file or directory
 * Returns 0 on success, -1 on error
 */
int path_vfs_delete(const char *path) {
    if (!path) return -1;
    
    /* Try unlink first (for files) */
    if (syscall1(SYS_UNLINK, (uint64_t)path) >= 0) return 0;
    
    /* Try rmdir (for directories) */
    return syscall1(SYS_RMDIR, (uint64_t)path) < 0 ? -1 : 0;
}

/*
 * Rename/move file or directory
 * Returns 0 on success, -1 on error
 */
int path_vfs_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return -1;
    return syscall2(SYS_RENAME, (uint64_t)old_path, (uint64_t)new_path) < 0 ? -1 : 0;
}

/*
 * Create an empty file
 * Returns 0 on success, -1 on error
 */
int path_vfs_touch(const char *path) {
    if (!path) return -1;
    
    /* Open with create flag, then close */
    int64_t fd = syscall3(SYS_OPEN, (uint64_t)path, 0x201, 0644);  /* O_CREAT | O_WRONLY */
    if (fd < 0) return -1;
    
    syscall1(SYS_CLOSE, fd);
    return 0;
}

/*
 * Copy a file
 * Returns 0 on success, -1 on error
 */
int path_vfs_copy(const char *src, const char *dst) {
    if (!src || !dst) return -1;
    
    /* Open source file */
    int64_t src_fd = syscall3(SYS_OPEN, (uint64_t)src, 0, 0);  /* O_RDONLY */
    if (src_fd < 0) return -1;
    
    /* Open destination file (create/truncate) */
    int64_t dst_fd = syscall3(SYS_OPEN, (uint64_t)dst, 0x601, 0644);  /* O_CREAT | O_TRUNC | O_WRONLY */
    if (dst_fd < 0) {
        syscall1(SYS_CLOSE, src_fd);
        return -1;
    }
    
    /* Copy in chunks */
    char buffer[4096];
    int64_t bytes_read;
    
    while ((bytes_read = syscall3(SYS_READ, src_fd, (uint64_t)buffer, 4096)) > 0) {
        int64_t bytes_written = syscall3(SYS_WRITE, dst_fd, (uint64_t)buffer, bytes_read);
        if (bytes_written != bytes_read) {
            syscall1(SYS_CLOSE, src_fd);
            syscall1(SYS_CLOSE, dst_fd);
            return -1;
        }
    }
    
    syscall1(SYS_CLOSE, src_fd);
    syscall1(SYS_CLOSE, dst_fd);
    
    return 0;
}

/*
 * Move a file (copy + delete)
 * Returns 0 on success, -1 on error
 */
int path_vfs_move(const char *src, const char *dst) {
    /* Try rename first (same filesystem) */
    if (path_vfs_rename(src, dst) == 0) return 0;
    
    /* Fall back to copy + delete */
    if (path_vfs_copy(src, dst) != 0) return -1;
    return path_vfs_delete(src);
}

/* ============ Path Utilities ============ */

/*
 * Get filename from path
 */
const char* path_basename(const char *path) {
    if (!path) return "";
    
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p + 1;
    }
    return last_slash;
}

/*
 * Get file extension
 */
const char* path_extension(const char *filename) {
    if (!filename) return "";
    
    const char *dot = 0;
    for (const char *p = filename; *p; p++) {
        if (*p == '.') dot = p;
    }
    return dot ? dot + 1 : "";
}

/*
 * Check if extension matches image types
 */
int path_is_image(const char *filename) {
    const char *ext = path_extension(filename);
    if (!ext[0]) return 0;
    
    /* Simple case-insensitive check */
    char lower[8] = {0};
    for (int i = 0; ext[i] && i < 7; i++) {
        lower[i] = (ext[i] >= 'A' && ext[i] <= 'Z') ? ext[i] + 32 : ext[i];
    }
    
    return (lower[0] == 'p' && lower[1] == 'n' && lower[2] == 'g' && !lower[3]) ||
           (lower[0] == 'j' && lower[1] == 'p' && lower[2] == 'g' && !lower[3]) ||
           (lower[0] == 'j' && lower[1] == 'p' && lower[2] == 'e' && lower[3] == 'g' && !lower[4]) ||
           (lower[0] == 'g' && lower[1] == 'i' && lower[2] == 'f' && !lower[3]) ||
           (lower[0] == 'b' && lower[1] == 'm' && lower[2] == 'p' && !lower[3]) ||
           (lower[0] == 'w' && lower[1] == 'e' && lower[2] == 'b' && lower[3] == 'p' && !lower[4]);
}

/*
 * Check if extension matches text types
 */
int path_is_text(const char *filename) {
    const char *ext = path_extension(filename);
    if (!ext[0]) return 0;
    
    char lower[8] = {0};
    for (int i = 0; ext[i] && i < 7; i++) {
        lower[i] = (ext[i] >= 'A' && ext[i] <= 'Z') ? ext[i] + 32 : ext[i];
    }
    
    return (lower[0] == 't' && lower[1] == 'x' && lower[2] == 't' && !lower[3]) ||
           (lower[0] == 'm' && lower[1] == 'd' && !lower[2]) ||
           (lower[0] == 'c' && !lower[1]) ||
           (lower[0] == 'h' && !lower[1]) ||
           (lower[0] == 'j' && lower[1] == 's' && !lower[2]) ||
           (lower[0] == 'p' && lower[1] == 'y' && !lower[2]) ||
           (lower[0] == 'r' && lower[1] == 's' && !lower[2]);
}
