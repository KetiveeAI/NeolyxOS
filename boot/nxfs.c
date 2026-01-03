#include "inc/nxfs.h"

EFI_STATUS nxfs_mount(EFI_FILE_HANDLE root, nxfs_superblock_t* sb) {
    // TODO: Read superblock from disk (implement real logic)
    Print(L"[NXFS] Mounting filesystem...\n");
    // For now, just zero out the superblock
    if (sb) {
        __builtin_memset(sb, 0, sizeof(nxfs_superblock_t));
        sb->magic = NXFS_MAGIC;
        sb->version = 1;
    }
    return EFI_SUCCESS;
}

EFI_STATUS nxfs_open(EFI_FILE_HANDLE root, const CHAR16* filename, nxfs_inode_t* inode) {
    // TODO: Locate file in NXFS, fill inode struct
    Print(L"[NXFS] Opening file: %s\n", filename);
    if (inode) {
        __builtin_memset(inode, 0, sizeof(nxfs_inode_t));
    }
    return EFI_NOT_FOUND;
}

EFI_STATUS nxfs_read(EFI_FILE_HANDLE root, nxfs_inode_t* inode, void* buffer, UINTN* size) {
    // TODO: Read file data from disk into buffer
    Print(L"[NXFS] Reading file...\n");
    return EFI_UNSUPPORTED;
} 