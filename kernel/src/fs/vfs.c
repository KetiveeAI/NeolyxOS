/*
 * NeolyxOS VFS Implementation
 * 
 * Virtual filesystem layer - mount, path resolution, file ops.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fs/vfs.h"
#include "fs/sip.h"
#include "mm/kheap.h"
#include "core/panic.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ VFS State ============ */

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;

static vfs_file_t open_files[VFS_MAX_OPEN_FILES];
static int vfs_initialized = 0;

/* Registered filesystem types */
#define MAX_FS_TYPES 8
static vfs_fs_type_t *fs_types[MAX_FS_TYPES];
static int fs_type_count = 0;

/* Root filesystem */
static vfs_mount_t *root_mount = NULL;

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

static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static int vfs_strncmp(const char *a, const char *b, int n) {
    while (n > 0 && *a && *a == *b) { a++; b++; n--; }
    return n == 0 ? 0 : *a - *b;
}

static void *vfs_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

/* ============ Path Helpers ============ */

/* Get parent directory path */
static int vfs_dirname(const char *path, char *dir) {
    int len = vfs_strlen(path);
    int i = len - 1;
    
    /* Skip trailing slashes */
    while (i > 0 && path[i] == '/') i--;
    
    /* Find last slash */
    while (i > 0 && path[i] != '/') i--;
    
    if (i == 0) {
        dir[0] = '/';
        dir[1] = '\0';
    } else {
        for (int j = 0; j < i; j++) dir[j] = path[j];
        dir[i] = '\0';
    }
    return 0;
}

/* Get filename from path */
static const char *vfs_basename(const char *path) {
    int len = vfs_strlen(path);
    int i = len - 1;
    
    /* Skip trailing slashes */
    while (i > 0 && path[i] == '/') i--;
    
    /* Find last slash */
    while (i > 0 && path[i - 1] != '/') i--;
    
    return &path[i];
}

/* ============ VFS Implementation ============ */

void vfs_init(void) {
    serial_puts("[VFS] Initializing virtual filesystem...\n");
    
    vfs_memset(mounts, 0, sizeof(mounts));
    vfs_memset(open_files, 0, sizeof(open_files));
    vfs_memset(fs_types, 0, sizeof(fs_types));
    
    mount_count = 0;
    fs_type_count = 0;
    vfs_initialized = 1;
    
    serial_puts("[VFS] Ready\n");
}

void vfs_register_fs(vfs_fs_type_t *fs_type) {
    if (fs_type_count < MAX_FS_TYPES) {
        fs_types[fs_type_count++] = fs_type;
        serial_puts("[VFS] Registered filesystem: ");
        serial_puts(fs_type->name);
        serial_puts("\n");
    }
}

static vfs_fs_type_t *vfs_find_fs(const char *name) {
    for (int i = 0; i < fs_type_count; i++) {
        if (vfs_strcmp(fs_types[i]->name, name) == 0) {
            return fs_types[i];
        }
    }
    return NULL;
}

int vfs_mount(const char *path, void *device, const char *fs_name, uint32_t flags) {
    if (!vfs_initialized) {
        serial_puts("[VFS] ERROR: Not initialized\n");
        return VFS_ERR_INVALID;
    }
    
    if (mount_count >= VFS_MAX_MOUNTS) {
        serial_puts("[VFS] ERROR: Max mounts reached\n");
        return VFS_ERR_NO_MEM;
    }
    
    vfs_fs_type_t *fs = vfs_find_fs(fs_name);
    if (!fs) {
        serial_puts("[VFS] ERROR: Unknown filesystem: ");
        serial_puts(fs_name);
        serial_puts("\n");
        return VFS_ERR_INVALID;
    }
    
    /* Find free mount slot */
    vfs_mount_t *mount = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].root == NULL) {
            mount = &mounts[i];
            break;
        }
    }
    
    if (!mount) return VFS_ERR_NO_MEM;
    
    /* Initialize mount */
    vfs_strcpy(mount->path, path, VFS_PATH_MAX);
    mount->device = device;
    mount->fs_type = fs;
    mount->flags = flags;
    
    /* Call filesystem mount */
    int ret = fs->mount(mount);
    if (ret != VFS_OK) {
        vfs_memset(mount, 0, sizeof(*mount));
        return ret;
    }
    
    mount_count++;
    
    /* First mount is root */
    if (vfs_strcmp(path, "/") == 0) {
        root_mount = mount;
    }
    
    serial_puts("[VFS] Mounted ");
    serial_puts(fs_name);
    serial_puts(" at ");
    serial_puts(path);
    serial_puts("\n");
    
    return VFS_OK;
}

int vfs_unmount(const char *path) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].root && vfs_strcmp(mounts[i].path, path) == 0) {
            if (mounts[i].fs_type->unmount) {
                mounts[i].fs_type->unmount(&mounts[i]);
            }
            vfs_memset(&mounts[i], 0, sizeof(mounts[i]));
            mount_count--;
            return VFS_OK;
        }
    }
    return VFS_ERR_NOT_FOUND;
}

/* Find mount point for a path */
static vfs_mount_t *vfs_find_mount(const char *path) {
    serial_puts("[VFS_FIND_MOUNT] Looking for: ");
    serial_puts(path);
    serial_puts("\n");
    
    serial_puts("[VFS_FIND_MOUNT] Available mounts:\n");
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].root != NULL) {
            serial_puts("  Mount ");
            char num = '0' + i;
            serial_putc(num);
            serial_puts(": ");
            serial_puts(mounts[i].path);
            serial_puts("\n");
        }
    }
    
    vfs_mount_t *best = root_mount;
    int best_len = 1;
    
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].root == NULL) continue;
        
        int len = vfs_strlen(mounts[i].path);
        if (len > best_len && vfs_strncmp(path, mounts[i].path, len) == 0) {
            if (path[len] == '/' || path[len] == '\0') {
                best = &mounts[i];
                best_len = len;
            }
        }
    }
    
    if (best) {
        serial_puts("[VFS_FIND_MOUNT] Best match: ");
        serial_puts(best->path);
        serial_puts("\n");
    } else {
        serial_puts("[VFS_FIND_MOUNT] No mount found!\n");
    }
    
    return best;
}

vfs_node_t *vfs_resolve(const char *path) {
    if (!path || path[0] != '/') return NULL;
    
    vfs_mount_t *mount = vfs_find_mount(path);
    if (!mount || !mount->root) {
        serial_puts("[VFS_RESOLVE] No mount found\n");
        return NULL;
    }
    
    /* Skip mount path prefix */
    const char *rel_path = path + vfs_strlen(mount->path);
    while (*rel_path == '/') rel_path++;
    
    serial_puts("[VFS_RESOLVE] Relative path: '");
    serial_puts(rel_path);
    serial_puts("'\n");
    
    /* If empty path, return root */
    if (*rel_path == '\0') {
        serial_puts("[VFS_RESOLVE] Empty rel_path, returning root\n");
        return mount->root;
    }
    
    /* Walk path components */
    vfs_node_t *node = mount->root;
    char component[VFS_NAME_MAX + 1];
    int i = 0;
    
    while (*rel_path) {
        /* Extract component */
        i = 0;
        while (*rel_path && *rel_path != '/') {
            if (i < VFS_NAME_MAX) component[i++] = *rel_path;
            rel_path++;
        }
        component[i] = '\0';
        while (*rel_path == '/') rel_path++;
        
        if (i == 0) continue;
        
        serial_puts("[VFS_RESOLVE] Looking up: '");
        serial_puts(component);
        serial_puts("'\n");
        
        /* Look up component */
        if (node->type != VFS_DIRECTORY) {
            serial_puts("[VFS_RESOLVE] Node is not a directory!\n");
            return NULL;
        }
        
        if (!node->ops) {
            serial_puts("[VFS_RESOLVE] Node ops is NULL!\n");
            return NULL;
        }
        if (!node->ops->finddir) {
            serial_puts("[VFS_RESOLVE] ops->finddir is NULL!\n");
            return NULL;
        }
        
        node = node->ops->finddir(node, component);
        if (!node) {
            serial_puts("[VFS_RESOLVE] finddir returned NULL!\n");
            return NULL;
        }
        serial_puts("[VFS_RESOLVE] Found component\n");
    }
    
    return node;
}

vfs_file_t *vfs_open(const char *path, uint32_t mode) {
    serial_puts("[VFS_OPEN] Opening: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* SIP check for write modes */
    if ((mode & VFS_O_WRONLY) || (mode & VFS_O_RDWR) || (mode & VFS_O_CREATE)) {
        if (sip_check_path(path, SIP_OP_WRITE) != 0) {
            serial_puts("[VFS] SIP: Write denied to ");
            serial_puts(path);
            serial_puts("\n");
            return NULL;
        }
    }
    
    serial_puts("[VFS_OPEN] Calling vfs_resolve...\n");
    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        serial_puts("[VFS_OPEN] vfs_resolve returned NULL!\n");
        if (mode & VFS_O_CREATE) {
            /* TODO: Create file */
        }
        return NULL;
    }
    serial_puts("[VFS_OPEN] vfs_resolve found node\n");
    
    /* Find free file handle */
    vfs_file_t *file = NULL;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (open_files[i].node == NULL) {
            file = &open_files[i];
            break;
        }
    }
    
    if (!file) {
        serial_puts("[VFS_OPEN] No free file handles!\n");
        return NULL;
    }
    
    /* Call filesystem open */
    if (node->ops && node->ops->open) {
        if (node->ops->open(node, mode) != VFS_OK) {
            serial_puts("[VFS_OPEN] Filesystem open() failed!\n");
            return NULL;
        }
    }
    
    file->node = node;
    file->offset = 0;
    file->mode = mode;
    file->flags = 0;
    node->ref_count++;
    
    serial_puts("[VFS_OPEN] Success!\n");
    return file;
}

int vfs_close(vfs_file_t *file) {
    if (!file || !file->node) return VFS_ERR_INVALID;
    
    if (file->node->ops && file->node->ops->close) {
        file->node->ops->close(file->node);
    }
    
    file->node->ref_count--;
    file->node = NULL;
    file->offset = 0;
    
    return VFS_OK;
}

int64_t vfs_read(vfs_file_t *file, void *buffer, uint64_t size) {
    if (!file || !file->node) return VFS_ERR_INVALID;
    if (!(file->mode & VFS_O_RDONLY) && !(file->mode & VFS_O_RDWR)) {
        return VFS_ERR_PERM;
    }
    
    if (!file->node->ops || !file->node->ops->read) {
        return VFS_ERR_IO;
    }
    
    int64_t read = file->node->ops->read(file->node, buffer, file->offset, size);
    if (read > 0) {
        file->offset += read;
    }
    
    return read;
}

int64_t vfs_write(vfs_file_t *file, const void *buffer, uint64_t size) {
    if (!file || !file->node) return VFS_ERR_INVALID;
    if (!(file->mode & VFS_O_WRONLY) && !(file->mode & VFS_O_RDWR)) {
        return VFS_ERR_PERM;
    }
    
    if (!file->node->ops || !file->node->ops->write) {
        return VFS_ERR_IO;
    }
    
    int64_t written = file->node->ops->write(file->node, buffer, file->offset, size);
    if (written > 0) {
        file->offset += written;
        if (file->offset > file->node->size) {
            file->node->size = file->offset;
        }
    }
    
    return written;
}

int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (!file || !file->node) return VFS_ERR_INVALID;
    
    int64_t new_offset;
    switch (whence) {
        case VFS_SEEK_SET:
            new_offset = offset;
            break;
        case VFS_SEEK_CUR:
            new_offset = file->offset + offset;
            break;
        case VFS_SEEK_END:
            new_offset = file->node->size + offset;
            break;
        default:
            return VFS_ERR_INVALID;
    }
    
    if (new_offset < 0) return VFS_ERR_INVALID;
    file->offset = new_offset;
    return new_offset;
}

int vfs_stat(const char *path, vfs_node_t *node) {
    vfs_node_t *found = vfs_resolve(path);
    if (!found) return VFS_ERR_NOT_FOUND;
    
    /* Copy node info */
    *node = *found;
    return VFS_OK;
}

int vfs_readdir(const char *path, vfs_dirent_t *entries, uint32_t count, uint32_t *read) {
    vfs_node_t *dir = vfs_resolve(path);
    if (!dir) return VFS_ERR_NOT_FOUND;
    if (dir->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    if (!dir->ops || !dir->ops->readdir) {
        return VFS_ERR_IO;
    }
    
    return dir->ops->readdir(dir, entries, count, read);
}

int vfs_create(const char *path, vfs_type_t type, uint32_t perms) {
    /* SIP check before creating */
    if (sip_check_path(path, SIP_OP_CREATE) != 0) {
        serial_puts("[VFS] SIP: Create denied at ");
        serial_puts(path);
        serial_puts("\n");
        return VFS_ERR_PERM;
    }
    
    char parent_path[VFS_PATH_MAX];
    vfs_dirname(path, parent_path);
    
    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent) return VFS_ERR_NOT_FOUND;
    if (parent->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    if (!parent->ops || !parent->ops->create) {
        return VFS_ERR_IO;
    }
    
    const char *name = vfs_basename(path);
    return parent->ops->create(parent, name, type, perms);
}

int vfs_unlink(const char *path) {
    /* SIP check before deleting */
    if (sip_check_path(path, SIP_OP_DELETE) != 0) {
        serial_puts("[VFS] SIP: Delete denied for ");
        serial_puts(path);
        serial_puts("\n");
        return VFS_ERR_PERM;
    }
    
    char parent_path[VFS_PATH_MAX];
    vfs_dirname(path, parent_path);
    
    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent) return VFS_ERR_NOT_FOUND;
    
    if (!parent->ops || !parent->ops->unlink) {
        return VFS_ERR_IO;
    }
    
    const char *name = vfs_basename(path);
    return parent->ops->unlink(parent, name);
}

/*
 * vfs_makedirs - Create directory and all parent directories
 * Like 'mkdir -p' - creates /a, /a/b, /a/b/c for path "/a/b/c"
 */
int vfs_makedirs(const char *path, uint32_t perms) {
    if (!path || path[0] != '/') return VFS_ERR_INVALID;
    
    /* Work buffer for building partial paths */
    char partial[VFS_PATH_MAX];
    int idx = 0;
    const char *p = path + 1;  /* Skip leading slash */
    
    /* Create each component */
    while (*p) {
        /* Build partial path up to next component */
        partial[idx++] = '/';
        while (*p && *p != '/') {
            if (idx < VFS_PATH_MAX - 1) {
                partial[idx++] = *p;
            }
            p++;
        }
        partial[idx] = '\0';
        
        /* Skip duplicate slashes */
        while (*p == '/') p++;
        
        /* Try to create this directory */
        int result = vfs_create(partial, VFS_DIRECTORY, perms);
        
        if (result != VFS_OK && result != -2) {
            /* -2 is "already exists", which is ok */
            /* Log the failure but continue anyway */
            serial_puts("[VFS] makedirs: failed at ");
            serial_puts(partial);
            serial_puts("\n");
        }
    }
    
    /* Return success if final directory exists or was created */
    vfs_node_t *check = vfs_resolve(path);
    return check ? VFS_OK : VFS_ERR_NOT_FOUND;
}
