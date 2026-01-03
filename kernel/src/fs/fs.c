// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <fs/fs.h>
#include <fs/vfs.h>
#include <string.h>

static struct file_system_type {
    const char *name;
    int type;
    struct fs_operations *ops;
} fs_types[] = {
    {"nxfs", FS_TYPE_NXFS, &nxfs_ops},
    {"ext4", FS_TYPE_EXT4, &ext4_ops},
    {"fat32", FS_TYPE_FAT32, &fat32_ops},
    {"ntfs", FS_TYPE_NTFS, &ntfs_ops},
    {"iso9660", FS_TYPE_ISO9660, &iso9660_ops},
    {NULL, 0, NULL}
};

int register_filesystem(const char *name, struct fs_operations *ops)
{
    for (int i = 0; fs_types[i].name; i++) {
        if (!fs_types[i].name) {
            fs_types[i].name = name;
            fs_types[i].ops = ops;
            return 0;
        }
    }
    return -ENOSPC;
}

int unregister_filesystem(const char *name)
{
    for (int i = 0; fs_types[i].name; i++) {
        if (strcmp(fs_types[i].name, name) == 0) {
            fs_types[i].name = NULL;
            fs_types[i].ops = NULL;
            return 0;
        }
    }
    return -ENOENT;
}

struct fs_operations *get_fs_ops(const char *name)
{
    for (int i = 0; fs_types[i].name; i++) {
        if (strcmp(fs_types[i].name, name) == 0) {
            return fs_types[i].ops;
        }
    }
    return NULL;
} 