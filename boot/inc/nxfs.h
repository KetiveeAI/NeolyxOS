#ifndef NXFS_H
#define NXFS_H

#include <efi.h>
#include <efilib.h>

#define NXFS_MAGIC 0x4E584653  // 'NXFS'

typedef struct {
    uint32_t magic;
    uint32_t version;
    // Add other fields as needed
} nxfs_superblock_t;

typedef struct {
    // Your inode structure
} nxfs_inode_t;

EFI_STATUS nxfs_mount(EFI_FILE_HANDLE root, nxfs_superblock_t* sb);
EFI_STATUS nxfs_open(EFI_FILE_HANDLE root, const CHAR16* name, nxfs_inode_t* inode);
EFI_STATUS nxfs_read(EFI_FILE_HANDLE root, nxfs_inode_t* inode, void* buffer, UINTN* size);

#endif 