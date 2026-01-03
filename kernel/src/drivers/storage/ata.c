#include <core/kernel.h>
#include <drivers/ata.h>
#include "../../include/drivers/drivers.h"
#include <io/io.h>
#include <utils/log.h>
#include <utils/string.h>
#include <mm/heap.h>

/* Memory functions */
extern void *memcpy(void *dest, const void *src, uint64_t n);
extern void *memset(void *s, int c, uint64_t n);

/* I/O port functions */
extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t val);
extern uint16_t inw(uint16_t port);
extern void outw(uint16_t port, uint16_t val);

// ATA I/O ports
#define ATA_DATA_PORT        0x1F0
#define ATA_FEATURES_PORT    0x1F1
#define ATA_SECTOR_COUNT     0x1F2
#define ATA_LBA_LOW          0x1F3
#define ATA_LBA_MID          0x1F4
#define ATA_LBA_HIGH         0x1F5
#define ATA_DRIVE_HEAD       0x1F6
#define ATA_STATUS_PORT      0x1F7
#define ATA_COMMAND_PORT     0x1F7

// ATA commands
#define ATA_CMD_READ_SECTORS     0x20
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_SET_FEATURES     0xEF

// ATA status bits
#define ATA_STATUS_ERR           0x01
#define ATA_STATUS_DRQ           0x08
#define ATA_STATUS_BSY           0x80
#define ATA_STATUS_RDY           0x40

// ATA driver data
typedef struct {
    ata_drive_info_t drives[4]; // Primary and secondary, master and slave
    int drive_count;
    void* buffer;
    size_t buffer_size;
} ata_driver_data_t;

// Global ATA driver instance
static ata_driver_data_t ata_data = {0};

// Wait for ATA drive to be ready
static int ata_wait_ready(int timeout) {
    for (int i = 0; i < timeout; i++) {
        uint8_t status = inb(ATA_STATUS_PORT);
        if (!(status & ATA_STATUS_BSY)) {
            return 0;
        }
        // Small delay
        for (int j = 0; j < 1000; j++) {
            __asm__ volatile("nop");
        }
    }
    return -1; // Timeout
}

// Wait for data request
static int ata_wait_drq(int timeout) {
    for (int i = 0; i < timeout; i++) {
        uint8_t status = inb(ATA_STATUS_PORT);
        if (status & ATA_STATUS_DRQ) {
            return 0;
        }
        if (status & ATA_STATUS_ERR) {
            return -1; // Error
        }
        // Small delay
        for (int j = 0; j < 1000; j++) {
            __asm__ volatile("nop");
        }
    }
    return -1; // Timeout
}

// Select drive
static void ata_select_drive(int drive) {
    uint8_t head = 0xA0 | (drive << 4);
    outb(ATA_DRIVE_HEAD, head);
    
    // Small delay after drive selection
    for (int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }
}

// Identify drive
static int ata_identify_drive(int drive, ata_drive_info_t* info) {
    if (!info) return -1;
    
    // Select drive
    ata_select_drive(drive);
    
    // Wait for drive to be ready
    if (ata_wait_ready(1000) != 0) {
        return -1;
    }
    
    // Send IDENTIFY command
    outb(ATA_COMMAND_PORT, ATA_CMD_IDENTIFY);
    
    // Wait for data request
    if (ata_wait_drq(1000) != 0) {
        return -1;
    }
    
    // Read identify data
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ATA_DATA_PORT);
    }
    
    // Parse drive information
    memset(info, 0, sizeof(ata_drive_info_t));
    
    // Check if drive exists
    if (identify_data[0] == 0) {
        info->type = ATA_DRIVE_NONE;
        return -1;
    }
    
    // Get drive type
    if (identify_data[83] & 0x4000) {
        info->type = ATA_DRIVE_SSD;
    } else {
        info->type = ATA_DRIVE_HDD;
    }
    
    // Get capacity (LBA28)
    uint32_t lba_sectors = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);
    info->capacity = (uint64_t)lba_sectors * 512;
    
    // Get sector size
    info->sector_size = 512;
    
    // Get model name
    for (int i = 0; i < 20; i++) {
        info->model[i*2] = (identify_data[27+i] >> 8) & 0xFF;
        info->model[i*2+1] = identify_data[27+i] & 0xFF;
    }
    info->model[40] = 0;
    
    // Get serial number
    for (int i = 0; i < 10; i++) {
        info->serial[i*2] = (identify_data[10+i] >> 8) & 0xFF;
        info->serial[i*2+1] = identify_data[10+i] & 0xFF;
    }
    info->serial[20] = 0;
    
    // Get firmware revision
    for (int i = 0; i < 4; i++) {
        info->firmware[i*2] = (identify_data[23+i] >> 8) & 0xFF;
        info->firmware[i*2+1] = identify_data[23+i] & 0xFF;
    }
    info->firmware[8] = 0;
    
    info->is_initialized = 1;
    return 0;
}

// Read sectors from drive (exported for nxstor_kdrv)
int ata_read_sectors(int drive, uint64_t lba, uint32_t count, void* buffer) {
    if (!buffer || count == 0 || drive < 0 || drive >= 4) return -1;
    
    ata_drive_info_t* info = &ata_data.drives[drive];
    if (!info->is_initialized) return -1;
    
    // Select drive
    ata_select_drive(drive);
    
    // Wait for drive to be ready
    if (ata_wait_ready(1000) != 0) {
        return -1;
    }
    
    // Set sector count
    outb(ATA_SECTOR_COUNT, count);
    
    // Set LBA address
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Send read command
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);
    
    // Read sectors
    for (uint32_t sector = 0; sector < count; sector++) {
        // Wait for data request
        if (ata_wait_drq(1000) != 0) {
            return -1;
        }
        
        // Read sector data
        uint16_t* sector_buffer = (uint16_t*)((char*)buffer + sector * 512);
        for (int i = 0; i < 256; i++) {
            sector_buffer[i] = inw(ATA_DATA_PORT);
        }
    }
    
    return count * 512;
}

// Write sectors to drive (exported for nxstor_kdrv)
int ata_write_sectors(int drive, uint64_t lba, uint32_t count, const void* buffer) {
    if (!buffer || count == 0 || drive < 0 || drive >= 4) return -1;
    
    ata_drive_info_t* info = &ata_data.drives[drive];
    if (!info->is_initialized) return -1;
    
    // Select drive
    ata_select_drive(drive);
    
    // Wait for drive to be ready
    if (ata_wait_ready(1000) != 0) {
        return -1;
    }
    
    // Set sector count
    outb(ATA_SECTOR_COUNT, count);
    
    // Set LBA address
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Send write command
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);
    
    // Write sectors
    for (uint32_t sector = 0; sector < count; sector++) {
        // Wait for data request
        if (ata_wait_drq(1000) != 0) {
            return -1;
        }
        
        // Write sector data
        uint16_t* sector_buffer = (uint16_t*)((char*)buffer + sector * 512);
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA_PORT, sector_buffer[i]);
        }
    }
    
    return count * 512;
}

// ATA driver initialization
int ata_driver_init(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    // Initialize driver data
    memset(&ata_data, 0, sizeof(ata_driver_data_t));
    ata_data.buffer_size = 8192;
    ata_data.buffer = kmalloc(ata_data.buffer_size);
    
    if (!ata_data.buffer) {
        klog("ATA driver: Failed to allocate buffer");
        return -1;
    }
    
    klog("ATA driver: Scanning for drives...");
    
    // Scan for drives on primary and secondary channels
    for (int drive = 0; drive < 4; drive++) {
        if (ata_identify_drive(drive, &ata_data.drives[drive]) == 0) {
            ata_data.drive_count++;
            
            klog("ATA drive found");
            klog("Drive type detected");
            klog("Drive capacity available");
            klog("Drive serial number available");
            klog("Drive firmware version available");
        }
    }
    
    klog("ATA driver: Drive scan completed");
    
    return 0;
}

// ATA driver deinitialization
int ata_driver_deinit(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    if (ata_data.buffer) {
        kfree(ata_data.buffer);
        ata_data.buffer = NULL;
    }
    
    memset(&ata_data, 0, sizeof(ata_driver_data_t));
    klog("ATA driver deinitialized");
    
    return 0;
}

// Read from drive
int ata_read(int drive, uint64_t offset, void* buffer, size_t size) {
    if (!buffer || size == 0 || drive < 0 || drive >= 4) return -1;
    
    ata_drive_info_t* info = &ata_data.drives[drive];
    if (!info->is_initialized) return -1;
    
    uint64_t lba = offset / 512;
    uint32_t sector_offset = offset % 512;
    uint32_t sectors_needed = (size + sector_offset + 511) / 512;
    
    // Read sectors
    int result = ata_read_sectors(drive, lba, sectors_needed, ata_data.buffer);
    if (result < 0) return -1;
    
    // Copy requested data
    memcpy(buffer, (char*)ata_data.buffer + sector_offset, size);
    
    return size;
}

// Write to drive
int ata_write(int drive, uint64_t offset, const void* buffer, size_t size) {
    if (!buffer || size == 0 || drive < 0 || drive >= 4) return -1;
    
    ata_drive_info_t* info = &ata_data.drives[drive];
    if (!info->is_initialized) return -1;
    
    uint64_t lba = offset / 512;
    uint32_t sector_offset = offset % 512;
    uint32_t sectors_needed = (size + sector_offset + 511) / 512;
    
    // If not aligned, read first sector
    if (sector_offset != 0) {
        if (ata_read_sectors(drive, lba, 1, ata_data.buffer) < 0) {
            return -1;
        }
    }
    
    // Copy data to buffer
    memcpy((char*)ata_data.buffer + sector_offset, buffer, size);
    
    // Write sectors
    int result = ata_write_sectors(drive, lba, sectors_needed, ata_data.buffer);
    if (result < 0) return -1;
    
    return size;
}

// Get drive information
int ata_get_drive_info(int drive, ata_drive_info_t* info) {
    if (!info || drive < 0 || drive >= 4) return -1;
    
    *info = ata_data.drives[drive];
    return 0;
}

// Get drive count
int ata_get_drive_count(void) {
    return ata_data.drive_count;
} 