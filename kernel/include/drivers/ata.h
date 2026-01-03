#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>
#include "drivers.h"

// ATA drive types
typedef enum {
    ATA_DRIVE_NONE,
    ATA_DRIVE_HDD,
    ATA_DRIVE_SSD,
    ATA_DRIVE_CDROM
} ata_drive_type_t;

// ATA drive information
typedef struct {
    ata_drive_type_t type;
    uint64_t capacity;
    uint32_t sector_size;
    char model[41];
    char serial[21];
    char firmware[9];
    int is_initialized;
} ata_drive_info_t;

// ATA driver functions
int ata_driver_init(kernel_driver_t* drv);
int ata_driver_deinit(kernel_driver_t* drv);

// ATA operations
int ata_read(int drive, uint64_t offset, void* buffer, size_t size);
int ata_write(int drive, uint64_t offset, const void* buffer, size_t size);
int ata_get_drive_info(int drive, ata_drive_info_t* info);
int ata_get_drive_count(void);

// ATA constants
#define ATA_SECTOR_SIZE       512
#define ATA_MAX_DRIVES        4

#endif // ATA_H 