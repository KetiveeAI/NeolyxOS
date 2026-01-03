/*
 * NXStorage Kernel Driver (nxstor.kdrv)
 * 
 * Unified Storage Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxstor_kdrv.h"
#include "../src/drivers/storage/nvme.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ATA driver (legacy) */
extern int ata_init(void);
extern int ata_read_sectors(int drive, uint64_t lba, uint32_t count, void *buffer);
extern int ata_write_sectors(int drive, uint64_t lba, uint32_t count, const void *buffer);

/* ============ Constants ============ */

#define MAX_STORAGE_DEVICES    16

/* ============ Driver State ============ */

static struct {
    int                     initialized;
    int                     device_count;
    nxstor_device_info_t    devices[MAX_STORAGE_DEVICES];
    nxstor_stats_t          stats[MAX_STORAGE_DEVICES];
} g_nxstor = {0};

/* ============ Helpers ============ */

static inline void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static inline void nx_memset(void *p, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)p;
    while (n--) *ptr++ = (uint8_t)c;
}

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

static void serial_size(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    uint64_t val = bytes;
    
    while (val >= 1024 && unit < 4) {
        val /= 1024;
        unit++;
    }
    
    serial_dec((int)val);
    serial_puts(" ");
    serial_puts(units[unit]);
}

/* ============ API Implementation ============ */

int nxstor_kdrv_init(void) {
    if (g_nxstor.initialized) {
        return g_nxstor.device_count;
    }
    
    serial_puts("[NXStor] Initializing kernel driver v" NXSTOR_KDRV_VERSION "\n");
    
    nx_memset(&g_nxstor, 0, sizeof(g_nxstor));
    
    /* Initialize NVMe */
    int nvme_count = nvme_init();
    if (nvme_count > 0) {
        serial_puts("[NXStor] Found ");
        serial_dec(nvme_count);
        serial_puts(" NVMe device(s)\n");
        
        for (int i = 0; i < nvme_count && g_nxstor.device_count < MAX_STORAGE_DEVICES; i++) {
            nvme_controller_t *ctrl = nvme_get_controller(i);
            if (!ctrl || !ctrl->initialized) continue;
            
            /* Add each namespace as a device */
            for (uint32_t ns = 0; ns < ctrl->namespace_count; ns++) {
                if (!ctrl->namespaces[ns].active) continue;
                
                nxstor_device_info_t *dev = &g_nxstor.devices[g_nxstor.device_count];
                dev->id = g_nxstor.device_count;
                dev->type = NXSTOR_TYPE_NVME;
                dev->media = NXSTOR_MEDIA_SSD;
                dev->status = NXSTOR_STATUS_ONLINE;
                
                nx_strcpy(dev->model, ctrl->model, 48);
                nx_strcpy(dev->serial, ctrl->serial, 24);
                nx_strcpy(dev->firmware, ctrl->firmware, 12);
                
                dev->total_sectors = ctrl->namespaces[ns].blocks;
                dev->sector_size = ctrl->namespaces[ns].block_size;
                dev->total_bytes = dev->total_sectors * dev->sector_size;
                
                dev->max_transfer_kb = 256; /* Typical NVMe max */
                dev->supports_trim = 1;
                dev->supports_ncq = 1;
                dev->is_rotational = 0;
                
                dev->controller_id = i;
                dev->port = ns;
                
                g_nxstor.device_count++;
            }
        }
    }
    
    /* Initialize ATA/AHCI (TODO: Enhanced detection) */
    /* For now, the existing ATA driver handles this */
    
    g_nxstor.initialized = 1;
    
    serial_puts("[NXStor] Total ");
    serial_dec(g_nxstor.device_count);
    serial_puts(" storage device(s) available\n");
    
    return g_nxstor.device_count;
}

void nxstor_kdrv_shutdown(void) {
    if (!g_nxstor.initialized) return;
    
    serial_puts("[NXStor] Shutting down...\n");
    
    /* Sync all devices */
    for (int i = 0; i < g_nxstor.device_count; i++) {
        nxstor_kdrv_sync(i);
    }
    
    g_nxstor.initialized = 0;
    g_nxstor.device_count = 0;
}

int nxstor_kdrv_device_count(void) {
    return g_nxstor.device_count;
}

int nxstor_kdrv_device_info(int index, nxstor_device_info_t *info) {
    if (index < 0 || index >= g_nxstor.device_count) return -1;
    if (!info) return -2;
    
    *info = g_nxstor.devices[index];
    return 0;
}

int nxstor_kdrv_read(int device_id, uint64_t lba, uint32_t count, void *buffer) {
    if (device_id < 0 || device_id >= g_nxstor.device_count) return -1;
    if (!buffer) return -2;
    
    nxstor_device_info_t *dev = &g_nxstor.devices[device_id];
    int result = 0;
    
    switch (dev->type) {
        case NXSTOR_TYPE_NVME: {
            nvme_controller_t *ctrl = nvme_get_controller(dev->controller_id);
            if (!ctrl) return -3;
            result = nvme_read(ctrl, dev->port + 1, lba, count, buffer);
            break;
        }
        
        case NXSTOR_TYPE_ATA:
        case NXSTOR_TYPE_AHCI:
            result = ata_read_sectors(dev->port, lba, count, buffer);
            break;
            
        default:
            return -4;
    }
    
    if (result >= 0) {
        g_nxstor.stats[device_id].reads++;
        g_nxstor.stats[device_id].bytes_read += count * dev->sector_size;
    } else {
        g_nxstor.stats[device_id].errors++;
    }
    
    return result;
}

int nxstor_kdrv_write(int device_id, uint64_t lba, uint32_t count, const void *buffer) {
    if (device_id < 0 || device_id >= g_nxstor.device_count) return -1;
    if (!buffer) return -2;
    
    nxstor_device_info_t *dev = &g_nxstor.devices[device_id];
    int result = 0;
    
    switch (dev->type) {
        case NXSTOR_TYPE_NVME: {
            nvme_controller_t *ctrl = nvme_get_controller(dev->controller_id);
            if (!ctrl) return -3;
            result = nvme_write(ctrl, dev->port + 1, lba, count, buffer);
            break;
        }
        
        case NXSTOR_TYPE_ATA:
        case NXSTOR_TYPE_AHCI:
            result = ata_write_sectors(dev->port, lba, count, (void*)buffer);
            break;
            
        default:
            return -4;
    }
    
    if (result >= 0) {
        g_nxstor.stats[device_id].writes++;
        g_nxstor.stats[device_id].bytes_written += count * dev->sector_size;
    } else {
        g_nxstor.stats[device_id].errors++;
    }
    
    return result;
}

int nxstor_kdrv_sync(int device_id) {
    if (device_id < 0 || device_id >= g_nxstor.device_count) return -1;
    
    nxstor_device_info_t *dev = &g_nxstor.devices[device_id];
    int result = 0;
    
    serial_puts("[NXStor] Sync device ");
    serial_dec(device_id);
    serial_puts("...");
    
    switch (dev->type) {
        case NXSTOR_TYPE_NVME: {
            /* NVMe flush via controller (weak - may not be implemented) */
            extern int nvme_flush(nvme_controller_t *, uint32_t) __attribute__((weak));
            nvme_controller_t *ctrl = nvme_get_controller(dev->controller_id);
            if (ctrl && nvme_flush) {
                result = nvme_flush(ctrl, dev->port + 1);
            }
            break;
        }
        
        case NXSTOR_TYPE_ATA:
        case NXSTOR_TYPE_AHCI: {
            /* SATA flush via AHCI */
            extern int ahci_flush(int port);
            result = ahci_flush(dev->port);
            break;
        }
        
        default:
            /* No flush needed for virtual/other devices */
            result = 0;
            break;
    }
    
    if (result == 0) {
        serial_puts(" OK\n");
    } else {
        serial_puts(" FAILED\n");
        g_nxstor.stats[device_id].errors++;
    }
    
    return result;
}

int nxstor_kdrv_trim(int device_id, uint64_t lba, uint64_t count) {
    if (device_id < 0 || device_id >= g_nxstor.device_count) return -1;
    
    nxstor_device_info_t *dev = &g_nxstor.devices[device_id];
    if (!dev->supports_trim) return -2;
    
    /* TODO: Issue TRIM/Deallocate command */
    return 0;
}

int nxstor_kdrv_get_stats(int device_id, nxstor_stats_t *stats) {
    if (device_id < 0 || device_id >= g_nxstor.device_count) return -1;
    if (!stats) return -2;
    
    *stats = g_nxstor.stats[device_id];
    return 0;
}

int nxstor_kdrv_reset(int device_id) {
    if (device_id < 0 || device_id >= g_nxstor.device_count) return -1;
    
    serial_puts("[NXStor] Reset device ");
    serial_dec(device_id);
    serial_puts("\n");
    
    /* TODO: Issue reset to hardware */
    return 0;
}

const char *nxstor_kdrv_type_name(nxstor_type_t type) {
    switch (type) {
        case NXSTOR_TYPE_ATA:     return "ATA";
        case NXSTOR_TYPE_AHCI:    return "SATA";
        case NXSTOR_TYPE_NVME:    return "NVMe";
        case NXSTOR_TYPE_USB:     return "USB";
        case NXSTOR_TYPE_SCSI:    return "SCSI";
        case NXSTOR_TYPE_VIRTUAL: return "Virtual";
        default:                  return "Unknown";
    }
}

const char *nxstor_kdrv_media_name(nxstor_media_t media) {
    switch (media) {
        case NXSTOR_MEDIA_HDD:       return "HDD";
        case NXSTOR_MEDIA_SSD:       return "SSD";
        case NXSTOR_MEDIA_OPTICAL:   return "Optical";
        case NXSTOR_MEDIA_REMOVABLE: return "Removable";
        case NXSTOR_MEDIA_VIRTUAL:   return "Virtual";
        default:                     return "Unknown";
    }
}

void nxstor_kdrv_debug(void) {
    serial_puts("\n=== NXStor Debug Info ===\n");
    serial_puts("Version: " NXSTOR_KDRV_VERSION "\n");
    serial_puts("Devices: ");
    serial_dec(g_nxstor.device_count);
    serial_puts("\n\n");
    
    for (int i = 0; i < g_nxstor.device_count; i++) {
        nxstor_device_info_t *dev = &g_nxstor.devices[i];
        
        serial_puts("[");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(dev->model);
        serial_puts("\n    Type: ");
        serial_puts(nxstor_kdrv_type_name(dev->type));
        serial_puts(" ");
        serial_puts(nxstor_kdrv_media_name(dev->media));
        serial_puts("\n    Size: ");
        serial_size(dev->total_bytes);
        serial_puts("\n    Sector: ");
        serial_dec(dev->sector_size);
        serial_puts(" bytes\n");
        
        nxstor_stats_t *st = &g_nxstor.stats[i];
        serial_puts("    I/O: ");
        serial_dec((int)st->reads);
        serial_puts(" reads, ");
        serial_dec((int)st->writes);
        serial_puts(" writes\n");
    }
    
    serial_puts("=========================\n\n");
}
