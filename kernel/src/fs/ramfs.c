/*
 * NeolyxOS RAM Filesystem (ramfs)
 * 
 * Simple in-memory filesystem for initial userspace binaries.
 * Used to provide /bin/hello and other embedded programs before
 * a real disk filesystem is mounted.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fs/vfs.h"
#include "mm/kheap.h"
#include "ramfs_binaries.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Local hex64 implementation for debug output */
static void ramfs_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ Embedded Hello Binary ============ */

/*
 * Minimal ELF64 executable that prints "Hello from NeolyxOS userspace!"
 * and exits with code 0.
 * 
 * This is a pre-compiled binary created from userland/hello/hello.c
 * Entry point: 0x400000
 * 
 * For now, we use a minimal hand-crafted ELF that:
 * 1. Calls sys_write(1, "Hello...", len)
 * 2. Calls sys_exit(0)
 */

/* Minimal hello world ELF64 binary */
static const uint8_t hello_elf[] = {
    /* ELF Header (64 bytes) */
    0x7f, 'E', 'L', 'F',         /* Magic */
    0x02,                         /* 64-bit */
    0x01,                         /* Little endian */
    0x01,                         /* ELF version 1 */
    0x00,                         /* System V ABI */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Padding */
    0x02, 0x00,                   /* Executable */
    0x3e, 0x00,                   /* x86-64 */
    0x01, 0x00, 0x00, 0x00,       /* Version 1 */
    0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Entry: 0x400078 */
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* phoff: 64 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* shoff: 0 */
    0x00, 0x00, 0x00, 0x00,       /* Flags */
    0x40, 0x00,                   /* ehsize: 64 */
    0x38, 0x00,                   /* phentsize: 56 */
    0x01, 0x00,                   /* phnum: 1 */
    0x00, 0x00,                   /* shentsize */
    0x00, 0x00,                   /* shnum */
    0x00, 0x00,                   /* shstrndx */
    
    /* Program Header (56 bytes) at offset 64 */
    0x01, 0x00, 0x00, 0x00,       /* PT_LOAD */
    0x05, 0x00, 0x00, 0x00,       /* PF_R | PF_X */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Offset: 0 */
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,  /* VAddr: 0x400000 */
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,  /* PAddr: 0x400000 */
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* FileSz: 256 */
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* MemSz: 256 */
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Align: 0x1000 */
    
    /* Code starts at offset 0x78 (120) = entry point 0x400078 */
    /* _start: */
    
    /* mov rax, 24 (sys_write) */
    0x48, 0xc7, 0xc0, 0x18, 0x00, 0x00, 0x00,
    
    /* mov rdi, 1 (stdout) */
    0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00,
    
    /* lea rsi, [rip + message] - calculate address of message */
    0x48, 0x8d, 0x35, 0x23, 0x00, 0x00, 0x00,
    
    /* mov rdx, 31 (message length) */
    0x48, 0xc7, 0xc2, 0x1f, 0x00, 0x00, 0x00,
    
    /* syscall */
    0x0f, 0x05,
    
    /* mov rax, 1 (sys_exit) */
    0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00,
    
    /* mov rdi, 0 (exit code) */
    0x48, 0xc7, 0xc7, 0x00, 0x00, 0x00, 0x00,
    
    /* syscall */
    0x0f, 0x05,
    
    /* Message: "Hello from NeolyxOS userspace!\n" (31 bytes) */
    'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ',
    'N', 'e', 'o', 'l', 'y', 'x', 'O', 'S', ' ',
    'u', 's', 'e', 'r', 's', 'p', 'a', 'c', 'e', '!', '\n',
    
    /* Padding to 256 bytes */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#define HELLO_ELF_SIZE sizeof(hello_elf)

/* ============ RAMFS Structures ============ */

typedef struct ramfs_file {
    char name[64];
    const uint8_t *data;
    uint64_t size;
    vfs_type_t type;
} ramfs_file_t;

typedef struct ramfs_dir {
    char name[64];
    struct ramfs_file *files;
    int file_count;
    struct ramfs_dir *subdirs;
    int subdir_count;
    vfs_node_t *vnode;
} ramfs_dir_t;

/* File entries for /bin */
static ramfs_file_t bin_files[] = {
    { "hello", hello_elf, sizeof(hello_elf), VFS_FILE },
    { "init", init_elf_data, INIT_ELF_SIZE, VFS_FILE },
    { "nxsh", nxsh_elf_data, NXSH_ELF_SIZE, VFS_FILE },
    { "desktop", desktop_elf_data, DESKTOP_ELF_SIZE, VFS_FILE },
};

/* Directory: /bin */
static ramfs_dir_t bin_dir = {
    .name = "bin",
    .files = bin_files,
    .file_count = 4,  /* hello, init, nxsh, desktop */
    .subdirs = NULL,
    .subdir_count = 0,
    .vnode = NULL,
};

/* Root subdirectories */
static ramfs_dir_t *root_subdirs[] = { &bin_dir };

/* Root directory */
static ramfs_dir_t root_dir = {
    .name = "/",
    .files = NULL,
    .file_count = 0,
    .subdirs = NULL,  /* Will be set up in init */
    .subdir_count = 1,
    .vnode = NULL,
};

/* VFS node storage */
static vfs_node_t ramfs_root_node;
static vfs_node_t ramfs_bin_node;
static vfs_node_t ramfs_hello_node;
static vfs_node_t ramfs_init_node;
static vfs_node_t ramfs_nxsh_node;
static vfs_node_t ramfs_desktop_node;

/* ============ String Helpers ============ */

static int ramfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void ramfs_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ RAMFS VFS Operations ============ */

static int ramfs_open(vfs_node_t *node, uint32_t mode) {
    (void)node; (void)mode;
    return VFS_OK;
}

static int ramfs_close(vfs_node_t *node) {
    (void)node;
    return VFS_OK;
}

static int64_t ramfs_read(vfs_node_t *node, void *buffer, uint64_t offset, uint64_t size) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    
    ramfs_file_t *file = (ramfs_file_t *)node->private_data;
    
    if (offset >= file->size) return 0;
    if (offset + size > file->size) {
        size = file->size - offset;
    }
    
    /* Copy data */
    const uint8_t *src = file->data + offset;
    uint8_t *dst = (uint8_t *)buffer;
    for (uint64_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    
    return (int64_t)size;
}

static int64_t ramfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, uint64_t size) {
    (void)node; (void)buffer; (void)offset; (void)size;
    return VFS_ERR_READ_ONLY;  /* Read-only filesystem */
}

static vfs_node_t *ramfs_finddir(vfs_node_t *dir, const char *name);
static int ramfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read);

static vfs_operations_t ramfs_ops = {
    .open = ramfs_open,
    .close = ramfs_close,
    .read = ramfs_read,
    .write = ramfs_write,
    .readdir = ramfs_readdir,
    .finddir = ramfs_finddir,
    .create = NULL,
    .unlink = NULL,
    .rename = NULL,
    .truncate = NULL,
    .stat = NULL,
    .chmod = NULL,
    .chown = NULL,
};

static int ramfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read) {
    if (!dir || dir->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    ramfs_dir_t *rdir = (ramfs_dir_t *)dir->private_data;
    if (!rdir) return VFS_ERR_INVALID;
    
    *read = 0;
    
    /* List subdirectories */
    for (int i = 0; i < rdir->subdir_count && *read < count; i++) {
        ramfs_dir_t *sub = root_subdirs[i];
        entries[*read].inode = (uint64_t)(uintptr_t)sub;
        entries[*read].type = VFS_DIRECTORY;
        ramfs_strcpy(entries[*read].name, sub->name, VFS_NAME_MAX);
        (*read)++;
    }
    
    /* List files */
    for (int i = 0; i < rdir->file_count && *read < count; i++) {
        entries[*read].inode = (uint64_t)(uintptr_t)&rdir->files[i];
        entries[*read].type = rdir->files[i].type;
        ramfs_strcpy(entries[*read].name, rdir->files[i].name, VFS_NAME_MAX);
        (*read)++;
    }
    
    return VFS_OK;
}

static vfs_node_t *ramfs_finddir(vfs_node_t *dir, const char *name) {
    if (!dir || !name) {
        serial_puts("[RAMFS_FINDDIR] NULL dir or name!\n");
        return NULL;
    }
    
    serial_puts("[RAMFS_FINDDIR] Looking for: ");
    serial_puts(name);
    serial_puts(" in dir: ");
    serial_puts(dir->name);
    serial_puts("\n");
    
    ramfs_dir_t *rdir = (ramfs_dir_t *)dir->private_data;
    if (!rdir) {
        serial_puts("[RAMFS_FINDDIR] No private_data!\n");
        return NULL;
    }
    
    /* Debug: Show pointer addresses */
    serial_puts("[RAMFS_FINDDIR] dir ptr=0x");
    ramfs_hex64((uint64_t)dir);
    serial_puts(", root ptr=0x");
    ramfs_hex64((uint64_t)&ramfs_root_node);
    serial_puts(", bin ptr=0x");
    ramfs_hex64((uint64_t)&ramfs_bin_node);
    serial_puts("\n");
    
    /* Search subdirectories */
    if (dir == &ramfs_root_node) {
        serial_puts("[RAMFS_FINDDIR] At root, checking for 'bin'\n");
        if (ramfs_strcmp(name, "bin") == 0) {
            serial_puts("[RAMFS_FINDDIR] Found /bin directory!\n");
            return &ramfs_bin_node;
        }
    } else {
        serial_puts("[RAMFS_FINDDIR] Not at root node\n");
    }
    
    /* Search files in /bin */
    if (dir == &ramfs_bin_node) {
        serial_puts("[RAMFS_FINDDIR] At /bin, file_count=");
        char count_str[2] = {'0' + (rdir->file_count & 0xF), 0};
        serial_puts(count_str);
        serial_puts("\n");
        
        for (int i = 0; i < rdir->file_count; i++) {
            serial_puts("[RAMFS_FINDDIR] Checking file: ");
            serial_puts(rdir->files[i].name);
            serial_puts("\n");
            
            if (ramfs_strcmp(name, rdir->files[i].name) == 0) {
                serial_puts("[RAMFS_FINDDIR] Match! Returning node for: ");
                serial_puts(name);
                serial_puts("\n");
                
                /* Return the pre-allocated nodes */
                if (ramfs_strcmp(name, "hello") == 0) return &ramfs_hello_node;
                if (ramfs_strcmp(name, "init") == 0) return &ramfs_init_node;
                if (ramfs_strcmp(name, "nxsh") == 0) return &ramfs_nxsh_node;
                if (ramfs_strcmp(name, "desktop") == 0) return &ramfs_desktop_node;
            }
        }
    } else {
        serial_puts("[RAMFS_FINDDIR] Not at /bin node\n");
    }
    
    serial_puts("[RAMFS_FINDDIR] Not found: ");
    serial_puts(name);
    serial_puts("\n");
    return NULL;
}

/* ============ RAMFS Mount ============ */

static vfs_mount_t *ramfs_mount_ptr = NULL;

int ramfs_mount(vfs_mount_t *mount) {
    serial_puts("[RAMFS] Mounting RAM filesystem...\n");
    
    ramfs_mount_ptr = mount;
    
    /* Initialize root node */
    ramfs_strcpy(ramfs_root_node.name, "/", VFS_NAME_MAX);
    ramfs_root_node.type = VFS_DIRECTORY;
    ramfs_root_node.size = 0;
    ramfs_root_node.inode = 1;
    ramfs_root_node.permissions = 0755;
    ramfs_root_node.mount = mount;
    ramfs_root_node.ops = &ramfs_ops;
    ramfs_root_node.private_data = &root_dir;
    ramfs_root_node.parent = NULL;
    
    /* Initialize /bin node */
    ramfs_strcpy(ramfs_bin_node.name, "bin", VFS_NAME_MAX);
    ramfs_bin_node.type = VFS_DIRECTORY;
    ramfs_bin_node.size = 0;
    ramfs_bin_node.inode = 2;
    ramfs_bin_node.permissions = 0755;
    ramfs_bin_node.mount = mount;
    ramfs_bin_node.ops = &ramfs_ops;
    ramfs_bin_node.private_data = &bin_dir;
    ramfs_bin_node.parent = &ramfs_root_node;
    
    /* Initialize /bin/hello node */
    ramfs_strcpy(ramfs_hello_node.name, "hello", VFS_NAME_MAX);
    ramfs_hello_node.type = VFS_FILE;
    ramfs_hello_node.size = HELLO_ELF_SIZE;
    ramfs_hello_node.inode = 3;
    ramfs_hello_node.permissions = 0755;
    ramfs_hello_node.mount = mount;
    ramfs_hello_node.ops = &ramfs_ops;
    ramfs_hello_node.private_data = &bin_files[0];
    ramfs_hello_node.parent = &ramfs_bin_node;
    
    /* Initialize /bin/init node */
    ramfs_strcpy(ramfs_init_node.name, "init", VFS_NAME_MAX);
    ramfs_init_node.type = VFS_FILE;
    ramfs_init_node.size = INIT_ELF_SIZE;
    ramfs_init_node.inode = 4;
    ramfs_init_node.permissions = 0755;
    ramfs_init_node.mount = mount;
    ramfs_init_node.ops = &ramfs_ops;
    ramfs_init_node.private_data = &bin_files[1];
    ramfs_init_node.parent = &ramfs_bin_node;
    
    /* Initialize /bin/nxsh node */
    ramfs_strcpy(ramfs_nxsh_node.name, "nxsh", VFS_NAME_MAX);
    ramfs_nxsh_node.type = VFS_FILE;
    ramfs_nxsh_node.size = NXSH_ELF_SIZE;
    ramfs_nxsh_node.inode = 5;
    ramfs_nxsh_node.permissions = 0755;
    ramfs_nxsh_node.mount = mount;
    ramfs_nxsh_node.ops = &ramfs_ops;
    ramfs_nxsh_node.private_data = &bin_files[2];
    ramfs_nxsh_node.parent = &ramfs_bin_node;
    
    /* Initialize /bin/desktop node */
    ramfs_strcpy(ramfs_desktop_node.name, "desktop", VFS_NAME_MAX);
    ramfs_desktop_node.type = VFS_FILE;
    ramfs_desktop_node.size = DESKTOP_ELF_SIZE;
    ramfs_desktop_node.inode = 6;
    ramfs_desktop_node.permissions = 0755;
    ramfs_desktop_node.mount = mount;
    ramfs_desktop_node.ops = &ramfs_ops;
    ramfs_desktop_node.private_data = &bin_files[3];
    ramfs_desktop_node.parent = &ramfs_bin_node;
    
    mount->root = &ramfs_root_node;
    mount->private_data = &root_dir;
    
    serial_puts("[RAMFS] Mounted at /\n");
    serial_puts("[RAMFS] Available: /bin/hello, /bin/init, /bin/nxsh, /bin/desktop\n");
    
    return VFS_OK;
}

int ramfs_unmount(vfs_mount_t *mount) {
    (void)mount;
    ramfs_mount_ptr = NULL;
    return VFS_OK;
}

/* ============ RAMFS Filesystem Type ============ */

static vfs_fs_type_t ramfs_fs_type = {
    .name = "ramfs",
    .mount = ramfs_mount,
    .unmount = ramfs_unmount,
    .sync = NULL,
};

/* ============ RAMFS Initialization ============ */

void ramfs_init(void) {
    serial_puts("[RAMFS] Initializing RAM filesystem driver...\n");
    
    /* Set up root subdirs pointer */
    root_dir.subdirs = (ramfs_dir_t *)root_subdirs;
    
    /* WORKAROUND: Static initialization of .name fails in this kernel 
     * (likely .data section issue), so set it explicitly here */
    ramfs_fs_type.name = "ramfs";
    ramfs_fs_type.mount = ramfs_mount;
    ramfs_fs_type.unmount = ramfs_unmount;
    ramfs_fs_type.sync = NULL;
    
    /* Register with VFS */
    serial_puts("[RAMFS] DEBUG: ramfs_fs_type.name = '");
    if (ramfs_fs_type.name) {
        serial_puts(ramfs_fs_type.name);
    } else {
        serial_puts("(NULL)");
    }
    serial_puts("'\\n");
    vfs_register_fs(&ramfs_fs_type);
    
    serial_puts("[RAMFS] Driver registered\n");
}

/* Mount ramfs at /ramfs (not / to avoid FAT32 conflict) */
int ramfs_mount_root(void) {
    serial_puts("[RAMFS] Mounting ramfs at /ramfs...\n");
    return vfs_mount("/ramfs", NULL, "ramfs", 0);
}
