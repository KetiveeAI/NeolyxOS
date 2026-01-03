#include "nxfs.h"
#include <stdint.h>
#include <stddef.h>
#include <drivers/ahci.h>
#include <fs/nxfs.h>
#include <fs/vfs.h>

// Kernel-space string functions
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern char *strncpy(char *dest, const char *src, size_t n);
extern int snprintf(char *str, size_t size, const char *format, ...);

// Serial output for debug
extern void serial_puts(const char *s);


// Disk Manager Logic
void nxfs_disk_manager_init(nxfs_disk_manager_t* mgr) {
    if (!mgr) return;
    memset(mgr, 0, sizeof(nxfs_disk_manager_t));
    mgr->selected_disk = -1;
    mgr->selected_partition = -1;
}

void nxfs_scan_disks(nxfs_disk_manager_t* mgr) {
    if (!mgr) return;
    
    // Use AHCI driver to probe for connected drives
    ahci_drive_t ahci_drives[AHCI_MAX_PORTS];
    int drive_count = ahci_probe_drives(ahci_drives, AHCI_MAX_PORTS);
    
    if (drive_count <= 0) {
        // No drives found - leave disk_count at 0
        mgr->disk_count = 0;
        return;
    }
    
    mgr->disk_count = 0;
    
    for (int i = 0; i < drive_count && mgr->disk_count < NXFS_MAX_DISKS; i++) {
        if (!ahci_drives[i].present) continue;
        
        nxfs_disk_t* disk = &mgr->disks[mgr->disk_count];
        disk->disk_id = ahci_drives[i].port;
        disk->size_bytes = ahci_drives[i].sectors * 512ULL;
        disk->sector_size = 512;
        disk->partition_count = 0;  // Will be populated by partition scan
        
        // Copy model name (trim trailing spaces)
        strncpy(disk->name, ahci_drives[i].model, sizeof(disk->name) - 1);
        disk->name[sizeof(disk->name) - 1] = '\0';
        
        // Trim trailing spaces from model name
        int len = strlen(disk->name);
        while (len > 0 && disk->name[len - 1] == ' ') {
            disk->name[--len] = '\0';
        }
        
        disk->is_virtual = 0;
        disk->is_encrypted = 0;
        
        mgr->disk_count++;
    }
}


int nxfs_create_partition(nxfs_disk_t* disk, uint64_t start_lba, uint64_t end_lba, uint8_t type, const char* label) {
    if (!disk || disk->partition_count >= NXFS_MAX_PARTITIONS) return -1;
    nxfs_partition_t* part = &disk->partitions[disk->partition_count++];
    part->type = type;
    part->status = 1;
    part->start_lba = start_lba;
    part->end_lba = end_lba;
    strncpy(part->label, label, sizeof(part->label)-1);
    return 0;
}

int nxfs_delete_partition(nxfs_disk_t* disk, int part_idx) {
    if (!disk || part_idx < 0 || part_idx >= disk->partition_count) return -1;
    for (int i = part_idx; i < disk->partition_count-1; ++i) {
        disk->partitions[i] = disk->partitions[i+1];
    }
    disk->partition_count--;
    return 0;
}

int nxfs_format_partition(nxfs_disk_t* disk, int part_idx, uint8_t fs_type, int encrypted) {
    if (!disk || part_idx < 0 || part_idx >= disk->partition_count) return -1;
    
    nxfs_partition_t* part = &disk->partitions[part_idx];
    
    // Calculate total blocks in this partition
    // Each NXFS block is 4096 bytes = 8 sectors (512 bytes each)
    uint64_t partition_sectors = part->end_lba - part->start_lba;
    uint64_t total_blocks = partition_sectors / 8;
    
    if (total_blocks < 128) {
        // Partition too small for NXFS (minimum 512KB)
        return -2;
    }
    
    // Use AHCI port as device identifier for block operations
    // The NXFS format function uses block_ops which are configured at init
    // For now, format directly writes to AHCI via the configured block_ops
    int result = nxfs_format((void*)(uintptr_t)disk->disk_id, total_blocks, 
                              part->label, encrypted, NULL);
    
    if (result == 0) {
        part->type = fs_type;
        disk->is_encrypted = encrypted;
    }
    
    return result;
}



int nxfs_resize_partition(nxfs_disk_t* disk, int part_idx, uint64_t new_end_lba) {
    if (!disk || part_idx < 0 || part_idx >= disk->partition_count) return -1;
    disk->partitions[part_idx].end_lba = new_end_lba;
    return 0;
}

int nxfs_mount_partition(nxfs_disk_t* disk, int part_idx) {
    if (!disk || part_idx < 0 || part_idx >= disk->partition_count) return -1;
    
    nxfs_partition_t* part = &disk->partitions[part_idx];
    
    // Build mount path: /mnt/disk<id>p<part>
    char mount_path[64];
    snprintf(mount_path, sizeof(mount_path), "/mnt/disk%up%d", disk->disk_id, part_idx);
    
    // Determine filesystem type
    const char* fs_type;
    switch (part->type) {
        case NXFS_PARTITION_TYPE_NXFS:
        case NXFS_PARTITION_TYPE_ENCRYPTED:
            fs_type = "nxfs";
            break;
        case NXFS_PARTITION_TYPE_FAT32:
            fs_type = "fat";
            break;
        default:
            return -2;  // Unsupported filesystem
    }
    
    // Use AHCI port as device identifier (cast to void* for API)
    void* device = (void*)(uintptr_t)disk->disk_id;
    
    // Call VFS mount
    int result = vfs_mount(mount_path, device, fs_type, 0);
    
    if (result == 0) {
        part->status = 1;  // Mark as mounted
    }
    
    return result;
}


int nxfs_unmount_partition(nxfs_disk_t* disk, int part_idx) {
    if (!disk || part_idx < 0 || part_idx >= disk->partition_count) return -1;
    
    nxfs_partition_t* part = &disk->partitions[part_idx];
    
    // Build mount path
    char mount_path[64];
    snprintf(mount_path, sizeof(mount_path), "/mnt/disk%up%d", disk->disk_id, part_idx);
    
    // Call VFS unmount
    int result = vfs_unmount(mount_path);
    
    if (result == 0) {
        part->status = 0;  // Mark as unmounted
    }
    
    return result;
}


int nxfs_create_virtual_disk(nxfs_disk_manager_t* mgr, uint64_t size_bytes, const char* name) {
    if (!mgr || mgr->disk_count >= NXFS_MAX_DISKS) return -1;
    nxfs_disk_t* disk = &mgr->disks[mgr->disk_count++];
    disk->disk_id = mgr->disk_count-1;
    disk->size_bytes = size_bytes;
    disk->sector_size = 512;
    disk->partition_count = 0;
    strncpy(disk->name, name, sizeof(disk->name)-1);
    disk->is_virtual = 1;
    disk->is_encrypted = 0;
    return 0;
}

int nxfs_delete_virtual_disk(nxfs_disk_manager_t* mgr, int disk_idx) {
    if (!mgr || disk_idx < 0 || disk_idx >= mgr->disk_count) return -1;
    for (int i = disk_idx; i < mgr->disk_count-1; ++i) {
        mgr->disks[i] = mgr->disks[i+1];
    }
    mgr->disk_count--;
    return 0;
}

int nxfs_get_disk_info(nxfs_disk_manager_t* mgr, int disk_idx, nxfs_disk_t* out_disk) {
    if (!mgr || disk_idx < 0 || disk_idx >= mgr->disk_count || !out_disk) return -1;
    *out_disk = mgr->disks[disk_idx];
    return 0;
}

int nxfs_get_partition_info(nxfs_disk_t* disk, int part_idx, nxfs_partition_t* out_part) {
    if (!disk || part_idx < 0 || part_idx >= disk->partition_count || !out_part) return -1;
    *out_part = disk->partitions[part_idx];
    return 0;
}

// --- GUI Disk Manager ---
// Helper to print integer as string
static void print_uint(uint64_t n) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (n == 0) {
        serial_puts("0");
        return;
    }
    while (n > 0 && i > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    serial_puts(&buf[i]);
}

void nxfs_show_disk_manager_gui(nxfs_disk_manager_t* mgr) {
    serial_puts("\n====================[ Disk Manager ]====================\n");
    serial_puts("[NAV] | [Help] | [Privacy] | [Instructions]\n");
    serial_puts("------------------------------------------------------\n");
    
    for (uint32_t i = 0; i < mgr->disk_count; ++i) {
        nxfs_disk_t* disk = &mgr->disks[i];
        serial_puts("Disk ");
        print_uint(i);
        serial_puts(": ");
        serial_puts(disk->name);
        serial_puts(disk->is_virtual ? " (Virtual, " : " (Physical, ");
        print_uint(disk->size_bytes / (1024*1024*1024));
        serial_puts(" GB)\n");
        
        for (uint32_t j = 0; j < disk->partition_count; ++j) {
            nxfs_partition_t* part = &disk->partitions[j];
            serial_puts("  Partition ");
            print_uint(j);
            serial_puts(": ");
            serial_puts(part->label);
            serial_puts(" [");
            serial_puts(
                part->type == NXFS_PARTITION_TYPE_NXFS ? "NXFS" :
                part->type == NXFS_PARTITION_TYPE_FAT32 ? "FAT32" :
                part->type == NXFS_PARTITION_TYPE_ENCRYPTED ? "Encrypted" :
                part->type == NXFS_PARTITION_TYPE_VIRTUAL ? "Virtual" : "Other"
            );
            serial_puts("] ");
            print_uint(part->start_lba);
            serial_puts(" - ");
            print_uint(part->end_lba);
            serial_puts("\n");
        }
    }
    
    serial_puts("------------------------------------------------------\n");
    serial_puts("[F] Format | [M] Mount | [U] Unmount | [D] Delete | [C] Create\n");
    serial_puts("======================================================\n");
}
 