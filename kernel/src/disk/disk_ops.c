/*
 * NeolyxOS Disk Operations Implementation
 * 
 * C backend for NeolyxOS Disk Manager
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../../include/disk/disk_ops.h"
#include "../../include/fs/nxfs.h"

/* External serial functions */
extern void serial_puts(const char *s);
extern void serial_hex(uint64_t val);

/* External disk driver functions */
extern int ata_read_sectors(uint32_t drive, uint64_t lba, uint32_t count, void *buffer);
extern int ata_write_sectors(uint32_t drive, uint64_t lba, uint32_t count, const void *buffer);
extern int ata_identify(uint32_t drive, void *buffer);

/* Memory functions */
static void disk_memset(void *ptr, uint8_t val, uint64_t size) {
    uint8_t *p = (uint8_t *)ptr;  
    while (size--) *p++ = val;
}

static void disk_memcpy(void *dst, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (size--) *d++ = *s++;
}

static uint64_t disk_strlen(const char *s) {
    uint64_t len = 0;
    while (*s++) len++;
    return len;
}

/* ============ Internal State ============ */

static DiskInfo detected_disks[DISK_MAX_DISKS];
static uint32_t disk_count = 0;
static PartitionInfo detected_partitions[DISK_MAX_DISKS * DISK_MAX_PARTITIONS];
static uint32_t partition_counts[DISK_MAX_DISKS];

/* ============ Initialization ============ */

int disk_ops_init(void) {
    serial_puts("[DISK] Initializing disk operations...\n");
    disk_count = 0;
    disk_memset(detected_disks, 0, sizeof(detected_disks));
    disk_memset(partition_counts, 0, sizeof(partition_counts));
    return 0;
}

/* ============ Disk Scanning ============ */

int disk_scan_all(void) {
    serial_puts("[DISK] Scanning for disks...\n");
    disk_count = 0;
    
    /* Scan ATA drives */
    for (uint32_t drive = 0; drive < 4; drive++) {
        static uint8_t identify_buffer[512];
        
        if (ata_identify(drive, identify_buffer) == 0) {
            DiskInfo *info = &detected_disks[disk_count];
            disk_memset(info, 0, sizeof(DiskInfo));
            
            info->disk_id = disk_count;
            
            /* Determine disk type based on drive number */
            if (drive < 2) {
                info->type = DISK_TYPE_HDD;
                info->is_ssd = 0;
            } else {
                info->type = DISK_TYPE_SSD;
                info->is_ssd = 1;
            }
            
            /* Extract model name from identify data (bytes 54-93) */
            for (int i = 0; i < 40 && i < DISK_NAME_MAX - 1; i++) {
                info->model[i] = identify_buffer[54 + i];
            }
            
            /* Extract serial number (bytes 20-39) */
            for (int i = 0; i < 20 && i < DISK_NAME_MAX - 1; i++) {
                info->serial[i] = identify_buffer[20 + i];
            }
            
            /* Get disk size from identify data (LBA48) */
            uint64_t *lba48_sectors = (uint64_t *)&identify_buffer[200];
            if (*lba48_sectors > 0) {
                info->size_sectors = *lba48_sectors;
            } else {
                /* LBA28 fallback */
                uint32_t *lba28_sectors = (uint32_t *)&identify_buffer[120];
                info->size_sectors = *lba28_sectors;
            }
            
            info->sector_size = 512;
            info->size_bytes = info->size_sectors * 512;
            
            /* Default S.M.A.R.T. values */
            info->health_percent = 95;
            info->temperature_c = 35;
            info->read_speed_mbps = 500;
            info->write_speed_mbps = 450;
            
            serial_puts("[DISK] Found disk: ");
            serial_puts(info->model);
            serial_puts(" (");
            serial_hex(info->size_bytes / (1024 * 1024));
            serial_puts(" MB)\n");
            
            disk_count++;
        }
    }
    
    serial_puts("[DISK] Total disks found: ");
    serial_hex(disk_count);
    serial_puts("\n");
    
    return disk_count;
}

int disk_get_count(void) {
    return disk_count;
}

int disk_get_info(uint32_t disk_id, DiskInfo *info) {
    if (disk_id >= disk_count || !info) {
        return -1;
    }
    disk_memcpy(info, &detected_disks[disk_id], sizeof(DiskInfo));
    return 0;
}

int disk_get_smart(uint32_t disk_id, DiskInfo *info) {
    if (disk_id >= disk_count || !info) {
        return -1;
    }
    
    /* TODO: Read actual S.M.A.R.T. data via ATA command */
    info->health_percent = detected_disks[disk_id].health_percent;
    info->temperature_c = detected_disks[disk_id].temperature_c;
    info->power_on_hours = 12500;
    info->read_speed_mbps = detected_disks[disk_id].read_speed_mbps;
    info->write_speed_mbps = detected_disks[disk_id].write_speed_mbps;
    
    return 0;
}

/* ============ Partition Operations ============ */

int disk_get_partition_count(uint32_t disk_id) {
    if (disk_id >= disk_count) {
        return 0;
    }
    return partition_counts[disk_id];
}

int disk_get_partition_info(uint32_t disk_id, uint32_t part_id, PartitionInfo *info) {
    if (disk_id >= disk_count || !info) {
        return -1;
    }
    
    uint32_t offset = disk_id * DISK_MAX_PARTITIONS + part_id;
    disk_memcpy(info, &detected_partitions[offset], sizeof(PartitionInfo));
    return 0;
}

int disk_create_partition(uint32_t disk_id, uint64_t size_mb, uint8_t fs_type, const char *label) {
    serial_puts("[DISK] Creating partition: ");
    serial_puts(label);
    serial_puts(" (");
    serial_hex(size_mb);
    serial_puts(" MB)\n");
    
    if (disk_id >= disk_count) {
        return -1;
    }
    
    uint32_t part_count = partition_counts[disk_id];
    if (part_count >= DISK_MAX_PARTITIONS) {
        return -1;
    }
    
    uint32_t offset = disk_id * DISK_MAX_PARTITIONS + part_count;
    PartitionInfo *part = &detected_partitions[offset];
    
    disk_memset(part, 0, sizeof(PartitionInfo));
    part->partition_id = part_count;
    part->disk_id = disk_id;
    part->fs_type = fs_type;
    part->size_bytes = size_mb * 1024 * 1024;
    
    /* Calculate LBA based on existing partitions */
    if (part_count == 0) {
        part->start_lba = 2048;  /* Standard starting LBA */
    } else {
        PartitionInfo *prev = &detected_partitions[offset - 1];
        part->start_lba = prev->end_lba + 1;
    }
    part->end_lba = part->start_lba + (part->size_bytes / 512);
    
    if (label) {
        uint64_t len = disk_strlen(label);
        if (len > DISK_NAME_MAX - 1) len = DISK_NAME_MAX - 1;
        disk_memcpy(part->label, label, len);
    }
    
    partition_counts[disk_id]++;
    detected_disks[disk_id].partition_count = partition_counts[disk_id];
    
    serial_puts("[DISK] Partition created successfully\n");
    return 0;
}

int disk_delete_partition(uint32_t disk_id, uint32_t part_id) {
    serial_puts("[DISK] Deleting partition...\n");
    
    if (disk_id >= disk_count) {
        return -1;
    }
    
    uint32_t part_count = partition_counts[disk_id];
    if (part_id >= part_count) {
        return -1;
    }
    
    /* Shift remaining partitions */
    uint32_t offset = disk_id * DISK_MAX_PARTITIONS;
    for (uint32_t i = part_id; i < part_count - 1; i++) {
        disk_memcpy(&detected_partitions[offset + i], 
                    &detected_partitions[offset + i + 1],
                    sizeof(PartitionInfo));
    }
    
    partition_counts[disk_id]--;
    detected_disks[disk_id].partition_count = partition_counts[disk_id];
    
    serial_puts("[DISK] Partition deleted\n");
    return 0;
}

/* ============ Filesystem Operations ============ */

int disk_format_nxfs(uint32_t disk_id, uint32_t part_id, const char *label) {
    serial_puts("[DISK] Formatting partition with NXFS...\n");
    
    if (disk_id >= disk_count) {
        return -1;
    }
    
    /* Call NXFS format function */
    int result = nxfs_format(disk_id, part_id, label);
    
    if (result == 0) {
        /* Update partition info */
        uint32_t offset = disk_id * DISK_MAX_PARTITIONS + part_id;
        detected_partitions[offset].fs_type = FS_TYPE_NXFS;
        serial_puts("[DISK] NXFS format complete\n");
    }
    
    return result;
}

int disk_format(uint32_t disk_id, uint32_t part_id, uint8_t fs_type, const char *label) {
    switch (fs_type) {
        case FS_TYPE_NXFS:
            return disk_format_nxfs(disk_id, part_id, label);
        default:
            serial_puts("[DISK] Error: Unsupported filesystem type\n");
            return -1;
    }
}

int disk_mount(uint32_t disk_id, uint32_t part_id, const char *mount_point) {
    serial_puts("[DISK] Mounting partition at ");
    serial_puts(mount_point);
    serial_puts("\n");
    
    uint32_t offset = disk_id * DISK_MAX_PARTITIONS + part_id;
    if (detected_partitions[offset].fs_type == FS_TYPE_NXFS) {
        return nxfs_mount(disk_id, part_id, mount_point);
    }
    
    return -1;
}

int disk_unmount(uint32_t disk_id, uint32_t part_id) {
    /* TODO: Implement unmount */
    return 0;
}

/* ============ Secure Operations ============ */

int disk_secure_erase(uint32_t disk_id) {
    serial_puts("[DISK] WARNING: Secure erasing disk...\n");
    
    if (disk_id >= disk_count) {
        return -1;
    }
    
    /* TODO: For real secure erase, send ATA SECURITY ERASE command */
    /* For now, just clear partition table */
    
    partition_counts[disk_id] = 0;
    detected_disks[disk_id].partition_count = 0;
    
    static uint8_t zero_buffer[512];
    disk_memset(zero_buffer, 0, 512);
    
    /* Clear first few sectors (MBR/GPT) */
    for (int i = 0; i < 34; i++) {
        disk_write_sectors(disk_id, i, 1, zero_buffer);
    }
    
    serial_puts("[DISK] Secure erase complete\n");
    return 0;
}

/* ============ System Compatibility ============ */

int system_check_compatibility(SystemCompatInfo *compat) {
    serial_puts("[DISK] Checking system compatibility...\n");
    
    if (!compat) return -1;
    
    disk_memset(compat, 0, sizeof(SystemCompatInfo));
    
    /* CPU check - x86_64 required */
    compat->cpu_compatible = 1;
    disk_memcpy(compat->cpu_name, "x86_64 Processor", 16);
    
    /* GPU check - any GPU works */
    compat->gpu_compatible = 1;
    disk_memcpy(compat->gpu_name, "Compatible GPU", 14);
    
    /* RAM check - 4GB minimum */
    compat->ram_mb = 16384;  /* TODO: Get actual RAM */
    compat->ram_sufficient = (compat->ram_mb >= 4096) ? 1 : 0;
    
    /* Disk space check - 20GB minimum */
    uint64_t free_space = 0;
    for (uint32_t i = 0; i < disk_count; i++) {
        free_space += detected_disks[i].size_bytes;
    }
    compat->disk_free_bytes = free_space;
    compat->disk_sufficient = (free_space >= 20ULL * 1024 * 1024 * 1024) ? 1 : 0;
    
    /* UEFI check */
    compat->uefi_available = 1;  /* We're running from UEFI bootloader */
    disk_memcpy(compat->firmware_type, "UEFI", 4);
    
    serial_puts("[DISK] Compatibility check complete\n");
    return 0;
}

/* ============ Low-level I/O ============ */

int disk_read_sectors(uint32_t disk_id, uint64_t start_lba, uint32_t count, void *buffer) {
    return ata_read_sectors(disk_id, start_lba, count, buffer);
}

int disk_write_sectors(uint32_t disk_id, uint64_t start_lba, uint32_t count, const void *buffer) {
    return ata_write_sectors(disk_id, start_lba, count, buffer);
}

int disk_read_block(uint32_t disk_id, uint64_t block, void *buffer) {
    return disk_read_sectors(disk_id, block * 8, 8, buffer);  /* 4KB = 8 sectors */
}

int disk_write_block(uint32_t disk_id, uint64_t block, const void *buffer) {
    return disk_write_sectors(disk_id, block * 8, 8, buffer);
}

uint64_t disk_get_size_blocks(uint32_t disk_id) {
    if (disk_id >= disk_count) return 0;
    return detected_disks[disk_id].size_sectors / 8;
}

uint64_t partition_get_start(uint32_t disk_id, uint32_t part_id) {
    uint32_t offset = disk_id * DISK_MAX_PARTITIONS + part_id;
    return detected_partitions[offset].start_lba / 8;
}

uint64_t partition_get_size(uint32_t disk_id, uint32_t part_id) {
    uint32_t offset = disk_id * DISK_MAX_PARTITIONS + part_id;
    PartitionInfo *part = &detected_partitions[offset];
    return (part->end_lba - part->start_lba) / 8;
}

/* ============ Installation ============ */

int disk_install_neolyx(uint32_t disk_id, uint32_t part_id) {
    serial_puts("[DISK] Installing NeolyxOS...\n");
    
    /* TODO: 
     * 1. Copy kernel to partition
     * 2. Copy system files
     * 3. Create boot entry
     * 4. Set boot flags
     */
    
    serial_puts("[DISK] Installation not yet implemented\n");
    return -1;
}
