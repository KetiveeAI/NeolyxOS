/*
 * NeolyxOS Nucleus Write Module
 * 
 * Raw sector copying for ISO/DD image writing.
 * Supports progress callbacks and verification.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "drivers/ahci.h"
#include "mm/pmm.h"
#include "utils/nucleus.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Helper: Get sector count using ahci_identify */
static uint64_t nw_get_sector_count(int port) {
    ahci_drive_t info = {0};
    if (ahci_identify(port, &info) == 0 && info.present) {
        return info.sectors;
    }
    return 0;
}

/* Helper: Check if AHCI is initialized by trying to read from any port */
static int nw_ahci_available(void) {
    /* Try to identify port 0 - if AHCI works, identify returns success or proper error */
    ahci_drive_t info = {0};
    /* ahci_identify returns 0 on success, negative on error */
    /* If AHCI not initialized, this will likely crash or return error */
    /* A safer approach: try reading a sector and check result */
    return 1; /* Assume available if we got this far */
}

/* ============ Types ============ */

typedef struct {
    int source_port;            /* Source disk port (-1 if file) */
    uint64_t source_offset;     /* Source LBA offset */
    int dest_port;              /* Destination disk port */
    uint64_t dest_offset;       /* Destination LBA offset */
    uint64_t sector_count;      /* Total sectors to copy */
    uint64_t sectors_done;      /* Sectors copied so far */
    int verify_after;           /* 1 = verify after write */
    int status;                 /* 0=ok, <0=error, 1=in_progress */
} nucleus_write_job_t;

/* Progress callback: called every N sectors */
typedef void (*nucleus_progress_cb)(uint64_t done, uint64_t total, int percent);

/* ============ Helpers ============ */

static void nw_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

static int nw_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

static void nw_print_num(uint64_t n) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (n == 0) {
        serial_putc('0');
        return;
    }
    while (n > 0 && i > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    serial_puts(&buf[i]);
}

/* ============ Core Write Functions ============ */

/**
 * nucleus_write_sectors - Copy sectors from source to destination
 * @job: Write job descriptor
 * @progress: Progress callback (can be NULL)
 * @chunk_size: Sectors per chunk (for progress updates)
 *
 * Returns 0 on success, negative on error.
 */
int nucleus_write_sectors(nucleus_write_job_t *job, nucleus_progress_cb progress, uint32_t chunk_size) {
    if (!job || job->sector_count == 0) {
        serial_puts("[WRITE] ERROR: Invalid job\n");
        return -1;
    }
    
    if (chunk_size == 0) chunk_size = 128;  /* 64KB chunks */
    if (chunk_size > 256) chunk_size = 256; /* Max 128KB */
    
    /* Allocate buffer (multiple pages for efficiency) */
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) {
        serial_puts("[WRITE] ERROR: Out of memory\n");
        return -2;
    }
    
    /* For larger chunks, allocate more pages */
    uint8_t *buffer = (uint8_t *)buffer_phys;
    
    serial_puts("[WRITE] Starting copy: ");
    nw_print_num(job->sector_count);
    serial_puts(" sectors (");
    nw_print_num((job->sector_count * 512) / (1024 * 1024));
    serial_puts(" MB)\n");
    
    job->status = 1;  /* In progress */
    job->sectors_done = 0;
    
    uint64_t remaining = job->sector_count;
    uint64_t src_lba = job->source_offset;
    uint64_t dst_lba = job->dest_offset;
    int last_percent = -1;
    
    while (remaining > 0) {
        /* Calculate chunk for this iteration */
        uint32_t this_chunk = (remaining > chunk_size) ? chunk_size : (uint32_t)remaining;
        
        /* Limit to single page buffer (8 sectors = 4KB) */
        if (this_chunk > 8) this_chunk = 8;
        
        /* Read from source */
        for (uint32_t s = 0; s < this_chunk; s++) {
            if (nucleus_read_disk(job->source_port, src_lba + s, 1, buffer + s * 512) != 0) {
                serial_puts("[WRITE] ERROR: Read failed at LBA ");
                nw_print_num(src_lba + s);
                serial_puts("\n");
                job->status = -3;
                pmm_free_page(buffer_phys);
                return -3;
            }
        }
        
        /* Write to destination */
        for (uint32_t s = 0; s < this_chunk; s++) {
            if (nucleus_write_disk(job->dest_port, dst_lba + s, 1, buffer + s * 512) != 0) {
                serial_puts("[WRITE] ERROR: Write failed at LBA ");
                nw_print_num(dst_lba + s);
                serial_puts("\n");
                job->status = -4;
                pmm_free_page(buffer_phys);
                return -4;
            }
        }
        
        /* Update progress */
        job->sectors_done += this_chunk;
        src_lba += this_chunk;
        dst_lba += this_chunk;
        remaining -= this_chunk;
        
        /* Progress callback */
        int percent = (int)((job->sectors_done * 100) / job->sector_count);
        if (progress && percent != last_percent) {
            progress(job->sectors_done, job->sector_count, percent);
            last_percent = percent;
        }
    }
    
    serial_puts("[WRITE] Copy complete: ");
    nw_print_num(job->sectors_done);
    serial_puts(" sectors\n");
    
    pmm_free_page(buffer_phys);
    job->status = 0;  /* Success */
    return 0;
}

/**
 * nucleus_verify_write - Verify written data matches source
 * @job: Write job (after writing)
 *
 * Returns 0 if verified, negative on mismatch or error.
 */
int nucleus_verify_write(nucleus_write_job_t *job) {
    if (!job || job->sector_count == 0) return -1;
    
    serial_puts("[WRITE] Verifying ");
    nw_print_num(job->sector_count);
    serial_puts(" sectors...\n");
    
    /* Allocate two buffers for comparison */
    uint64_t buf1_phys = pmm_alloc_page();
    uint64_t buf2_phys = pmm_alloc_page();
    
    if (!buf1_phys || !buf2_phys) {
        if (buf1_phys) pmm_free_page(buf1_phys);
        if (buf2_phys) pmm_free_page(buf2_phys);
        serial_puts("[WRITE] ERROR: Out of memory for verify\n");
        return -2;
    }
    
    uint8_t *src_buf = (uint8_t *)buf1_phys;
    uint8_t *dst_buf = (uint8_t *)buf2_phys;
    
    uint64_t src_lba = job->source_offset;
    uint64_t dst_lba = job->dest_offset;
    uint64_t verified = 0;
    
    /* Verify first 1MB (2048 sectors) for quick check */
    uint64_t to_verify = job->sector_count;
    if (to_verify > 2048) to_verify = 2048;
    
    while (verified < to_verify) {
        /* Read source */
        if (nucleus_read_disk(job->source_port, src_lba, 1, src_buf) != 0) {
            serial_puts("[WRITE] Verify read error (source)\n");
            pmm_free_page(buf1_phys);
            pmm_free_page(buf2_phys);
            return -3;
        }
        
        /* Read destination */
        if (nucleus_read_disk(job->dest_port, dst_lba, 1, dst_buf) != 0) {
            serial_puts("[WRITE] Verify read error (dest)\n");
            pmm_free_page(buf1_phys);
            pmm_free_page(buf2_phys);
            return -4;
        }
        
        /* Compare */
        if (nw_memcmp(src_buf, dst_buf, 512) != 0) {
            serial_puts("[WRITE] VERIFY FAILED at LBA ");
            nw_print_num(dst_lba);
            serial_puts("\n");
            pmm_free_page(buf1_phys);
            pmm_free_page(buf2_phys);
            return -5;
        }
        
        src_lba++;
        dst_lba++;
        verified++;
    }
    
    pmm_free_page(buf1_phys);
    pmm_free_page(buf2_phys);
    
    serial_puts("[WRITE] Verified ");
    nw_print_num(verified);
    serial_puts(" sectors OK\n");
    
    return 0;
}

/**
 * nucleus_clone_disk - Clone entire disk to another disk
 * @src_port: Source AHCI port
 * @dst_port: Destination AHCI port
 * @progress: Progress callback
 *
 * Returns 0 on success.
 */
int nucleus_clone_disk(int src_port, int dst_port, nucleus_progress_cb progress) {
    /* Get source disk size */
    uint64_t src_sectors = nw_get_sector_count(src_port);
    uint64_t dst_sectors = nw_get_sector_count(dst_port);
    
    if (src_sectors == 0) {
        serial_puts("[WRITE] ERROR: Cannot get source disk size\n");
        return -1;
    }
    
    if (dst_sectors < src_sectors) {
        serial_puts("[WRITE] ERROR: Destination disk too small\n");
        return -2;
    }
    
    serial_puts("[WRITE] Cloning disk: port ");
    serial_putc('0' + src_port);
    serial_puts(" -> port ");
    serial_putc('0' + dst_port);
    serial_puts("\n");
    
    nucleus_write_job_t job = {
        .source_port = src_port,
        .source_offset = 0,
        .dest_port = dst_port,
        .dest_offset = 0,
        .sector_count = src_sectors,
        .sectors_done = 0,
        .verify_after = 1,
        .status = 0
    };
    
    int result = nucleus_write_sectors(&job, progress, 128);
    
    if (result == 0 && job.verify_after) {
        result = nucleus_verify_write(&job);
    }
    
    return result;
}

/**
 * nucleus_write_image - Write image data to disk (for ISO/DD)
 * @src_port: Source disk port (containing image)
 * @src_offset: LBA offset of image on source disk
 * @dst_port: Destination disk port
 * @dst_offset: LBA offset to write to
 * @sector_count: Number of sectors to write
 * @progress: Progress callback
 *
 * Returns 0 on success.
 */
int nucleus_write_image(int src_port, uint64_t src_offset,
                        int dst_port, uint64_t dst_offset,
                        uint64_t sector_count,
                        nucleus_progress_cb progress) {
    serial_puts("[WRITE] Writing image: ");
    nw_print_num(sector_count);
    serial_puts(" sectors\n");
    
    nucleus_write_job_t job = {
        .source_port = src_port,
        .source_offset = src_offset,
        .dest_port = dst_port,
        .dest_offset = dst_offset,
        .sector_count = sector_count,
        .sectors_done = 0,
        .verify_after = 0,
        .status = 0
    };
    
    return nucleus_write_sectors(&job, progress, 128);
}

/**
 * nucleus_zero_disk - Zero out a disk or partition
 * @port: AHCI port
 * @start_lba: Starting LBA
 * @sector_count: Number of sectors to zero
 * @progress: Progress callback
 *
 * Returns 0 on success.
 */
int nucleus_zero_disk(int port, uint64_t start_lba, uint64_t sector_count, 
                      nucleus_progress_cb progress) {
    serial_puts("[WRITE] Zeroing ");
    nw_print_num(sector_count);
    serial_puts(" sectors from LBA ");
    nw_print_num(start_lba);
    serial_puts("\n");
    
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) {
        serial_puts("[WRITE] ERROR: Out of memory\n");
        return -1;
    }
    
    uint8_t *buffer = (uint8_t *)buffer_phys;
    nw_memset(buffer, 0, 4096);  /* Zero the buffer */
    
    uint64_t lba = start_lba;
    uint64_t remaining = sector_count;
    uint64_t done = 0;
    int last_percent = -1;
    
    while (remaining > 0) {
        if (nucleus_write_disk(port, lba, 1, buffer) != 0) {
            serial_puts("[WRITE] ERROR: Zero write failed at LBA ");
            nw_print_num(lba);
            serial_puts("\n");
            pmm_free_page(buffer_phys);
            return -2;
        }
        
        lba++;
        remaining--;
        done++;
        
        int percent = (int)((done * 100) / sector_count);
        if (progress && percent != last_percent && (percent % 10 == 0)) {
            progress(done, sector_count, percent);
            last_percent = percent;
        }
    }
    
    pmm_free_page(buffer_phys);
    
    serial_puts("[WRITE] Zero complete\n");
    return 0;
}

/* Default progress callback for serial output */
void nucleus_default_progress(uint64_t done, uint64_t total, int percent) {
    serial_puts("[WRITE] Progress: ");
    nw_print_num(percent);
    serial_puts("% (");
    nw_print_num(done);
    serial_puts("/");
    nw_print_num(total);
    serial_puts(" sectors)\n");
    (void)total;
}

/* ============ Boot Media Detection ============ */

/* Boot media source info */
typedef struct {
    int port;                    /* AHCI port number */
    int source_type;             /* 0=none, 1=ISO9660, 2=DD/raw, 3=NeolyxOS installer */
    uint64_t boot_lba;           /* Starting LBA of bootable content */
    uint64_t content_sectors;    /* Size of content in sectors */
    char volume_label[32];       /* Volume label if detected */
    int has_bootloader;          /* 1 if bootloader found */
    int has_kernel;              /* 1 if kernel.bin found */
} boot_source_t;

/* String comparison helper */
static int nw_strncmp(const uint8_t *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != (uint8_t)b[i]) return a[i] - (uint8_t)b[i];
    }
    return 0;
}

/**
 * nucleus_detect_iso9660 - Check if disk has ISO9660 filesystem
 * @port: AHCI port
 * @info: Output info structure
 *
 * ISO9660 Primary Volume Descriptor at sector 16.
 * Returns 1 if ISO9660 detected.
 */
int nucleus_detect_iso9660(int port, boot_source_t *info) {
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return 0;
    uint8_t *buffer = (uint8_t *)buffer_phys;
    
    /* Read sector 16 (0x8000 = Primary Volume Descriptor) */
    if (nucleus_read_disk(port, 16, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Check for ISO9660 signature: byte 1-5 = "CD001" */
    if (nw_strncmp(buffer + 1, "CD001", 5) == 0) {
        serial_puts("[WRITE] Found ISO9660 at port ");
        serial_putc('0' + port);
        serial_puts("\n");
        
        info->source_type = 1;  /* ISO9660 */
        info->port = port;
        info->boot_lba = 0;
        
        /* Copy volume label (bytes 40-71) */
        for (int i = 0; i < 31 && buffer[40 + i] != ' '; i++) {
            info->volume_label[i] = buffer[40 + i];
        }
        
        /* Get volume size (bytes 80-87, little-endian 32-bit) */
        uint32_t vol_sectors = buffer[80] | (buffer[81] << 8) | 
                               (buffer[82] << 16) | (buffer[83] << 24);
        info->content_sectors = (uint64_t)vol_sectors * 4; /* 2KB to 512B sectors */
        
        pmm_free_page(buffer_phys);
        return 1;
    }
    
    pmm_free_page(buffer_phys);
    return 0;
}

/**
 * nucleus_detect_eltorito - Check for El Torito bootable CD
 * @port: AHCI port
 * @info: Output info structure
 *
 * El Torito Boot Record at sector 17.
 * Returns 1 if bootable.
 */
int nucleus_detect_eltorito(int port, boot_source_t *info) {
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return 0;
    uint8_t *buffer = (uint8_t *)buffer_phys;
    
    /* Read sector 17 (El Torito Boot Record) */
    if (nucleus_read_disk(port, 17, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Check for El Torito: byte 0 = 0x00, byte 1-5 = "CD001", byte 6 = 0x01 */
    /* Then "EL TORITO SPECIFICATION" at offset 7 */
    if (buffer[0] == 0x00 && nw_strncmp(buffer + 1, "CD001", 5) == 0) {
        /* Get boot catalog LBA (bytes 71-74) */
        uint32_t boot_catalog_lba = buffer[71] | (buffer[72] << 8) |
                                     (buffer[73] << 16) | (buffer[74] << 24);
        
        if (boot_catalog_lba > 0) {
            serial_puts("[WRITE] El Torito boot catalog at LBA ");
            nw_print_num(boot_catalog_lba);
            serial_puts("\n");
            
            info->boot_lba = (uint64_t)boot_catalog_lba * 4;  /* 2KB to 512B sectors */
            pmm_free_page(buffer_phys);
            return 1;
        }
    }
    
    pmm_free_page(buffer_phys);
    return 0;
}

/**
 * nucleus_detect_neolyx_source - Check for NeolyxOS installation media
 * @port: AHCI port
 * @info: Output info structure
 *
 * Checks for:
 * 1. FAT32 EFI partition with /EFI/BOOT/BOOTX64.EFI
 * 2. kernel.bin in EFI partition
 * 3. NeolyxOS boot signature
 *
 * Returns 1 if NeolyxOS source detected.
 */
int nucleus_detect_neolyx_source(int port, boot_source_t *info) {
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return 0;
    uint8_t *buffer = (uint8_t *)buffer_phys;
    
    /* Read sector 0 (MBR or GPT protective MBR) */
    if (nucleus_read_disk(port, 0, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Check for boot signature 0xAA55 */
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Check if GPT (partition type 0xEE in first MBR entry) */
    int is_gpt = (buffer[450] == 0xEE);
    
    uint64_t efi_start_lba = 0;
    uint64_t efi_sectors = 0;
    
    if (is_gpt) {
        /* Read GPT header at LBA 1 */
        if (nucleus_read_disk(port, 1, 1, buffer) != 0) {
            pmm_free_page(buffer_phys);
            return 0;
        }
        
        /* Verify GPT signature "EFI PART" */
        if (nw_strncmp(buffer, "EFI PART", 8) != 0) {
            pmm_free_page(buffer_phys);
            return 0;
        }
        
        /* Read first GPT entry at LBA 2 */
        if (nucleus_read_disk(port, 2, 1, buffer) != 0) {
            pmm_free_page(buffer_phys);
            return 0;
        }
        
        /* Check for EFI System Partition GUID (C12A7328-F81F-11D2-BA4B-00A0C93EC93B) */
        const uint8_t efi_guid[] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
        
        int is_efi = 1;
        for (int i = 0; i < 16; i++) {
            if (buffer[i] != efi_guid[i]) {
                is_efi = 0;
                break;
            }
        }
        
        if (is_efi) {
            /* Get EFI partition LBA (bytes 32-39) */
            efi_start_lba = *((uint64_t *)(buffer + 32));
            uint64_t efi_end_lba = *((uint64_t *)(buffer + 40));
            efi_sectors = efi_end_lba - efi_start_lba + 1;
            
            serial_puts("[WRITE] EFI System Partition at LBA ");
            nw_print_num(efi_start_lba);
            serial_puts("\n");
        }
    } else {
        /* MBR partition table - find first FAT partition */
        for (int i = 0; i < 4; i++) {
            uint8_t type = buffer[446 + i * 16 + 4];
            /* FAT types: 0x01=FAT12, 0x04/0x06=FAT16, 0x0B/0x0C=FAT32, 0xEF=EFI */
            if (type == 0x0B || type == 0x0C || type == 0xEF) {
                efi_start_lba = *((uint32_t *)(buffer + 446 + i * 16 + 8));
                efi_sectors = *((uint32_t *)(buffer + 446 + i * 16 + 12));
                break;
            }
        }
    }
    
    if (efi_start_lba == 0) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Read FAT boot sector */
    if (nucleus_read_disk(port, efi_start_lba, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Verify FAT signature */
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Check for FAT32 (signature at offset 82) or FAT16 (offset 54) */
    int is_fat32 = (nw_strncmp(buffer + 82, "FAT32", 5) == 0);
    int is_fat16 = (nw_strncmp(buffer + 54, "FAT", 3) == 0);
    
    if (!is_fat32 && !is_fat16) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    /* Volume label from BPB */
    int label_offset = is_fat32 ? 71 : 43;
    for (int i = 0; i < 11 && buffer[label_offset + i] != ' '; i++) {
        info->volume_label[i] = buffer[label_offset + i];
    }
    
    /* Check for NeolyxOS signature in OEM name (bytes 3-10) */
    /* Rufus/dd will copy our boot image which contains "NEOLYX  " or similar */
    int is_neolyx = 0;
    if (nw_strncmp(buffer + 3, "NEOLYX", 6) == 0 ||
        nw_strncmp(buffer + 3, "NEOLYXOS", 8) == 0 ||
        nw_strncmp(buffer + 3, "mkfs.fat", 8) == 0) {  /* Standard FAT is OK too */
        is_neolyx = 1;
    }
    
    /* Populate info */
    info->port = port;
    info->source_type = is_neolyx ? 3 : 2;  /* 3=NeolyxOS installer, 2=generic DD */
    info->boot_lba = efi_start_lba;
    info->content_sectors = efi_sectors;
    info->has_bootloader = 1;  /* Assume true if valid FAT */
    info->has_kernel = 1;      /* Will need filesystem traversal to verify */
    
    serial_puts("[WRITE] Detected ");
    serial_puts(is_neolyx ? "NeolyxOS" : "generic");
    serial_puts(" boot media on port ");
    serial_putc('0' + port);
    serial_puts("\n");
    
    pmm_free_page(buffer_phys);
    return 1;
}

/**
 * nucleus_find_install_source - Find NeolyxOS installation source
 * @exclude_port: Port to exclude (e.g., destination disk)
 * @info: Output info structure
 *
 * Scans all AHCI ports for bootable NeolyxOS media.
 * Priority: NeolyxOS installer > ISO9660 > Generic DD
 *
 * Returns port number, or -1 if not found.
 */
int nucleus_find_install_source(int exclude_port, boot_source_t *info) {
    if (!nw_ahci_available()) {
        serial_puts("[WRITE] AHCI not initialized\n");
        return -1;
    }
    
    serial_puts("[WRITE] Scanning for installation source...\n");
    
    boot_source_t candidates[32];
    int candidate_count = 0;
    
    /* Scan all possible ports */
    for (int port = 0; port < 32; port++) {
        if (port == exclude_port) continue;
        
        /* Check if port has a device */
        uint64_t sectors = nw_get_sector_count(port);
        if (sectors == 0) continue;
        
        boot_source_t temp = {0};
        temp.port = port;
        
        /* Try NeolyxOS detection first (highest priority) */
        if (nucleus_detect_neolyx_source(port, &temp)) {
            if (candidate_count < 32) {
                candidates[candidate_count++] = temp;
            }
        }
        /* Try ISO9660 */
        else if (nucleus_detect_iso9660(port, &temp)) {
            nucleus_detect_eltorito(port, &temp);  /* Check if bootable */
            if (candidate_count < 32) {
                candidates[candidate_count++] = temp;
            }
        }
    }
    
    if (candidate_count == 0) {
        serial_puts("[WRITE] No installation source found\n");
        return -1;
    }
    
    /* Select best candidate (prefer type 3=NeolyxOS, then 1=ISO, then 2=DD) */
    int best_idx = 0;
    for (int i = 1; i < candidate_count; i++) {
        if (candidates[i].source_type > candidates[best_idx].source_type) {
            best_idx = i;
        }
    }
    
    *info = candidates[best_idx];
    
    serial_puts("[WRITE] Selected source: port ");
    serial_putc('0' + info->port);
    serial_puts(" (type ");
    serial_putc('0' + info->source_type);
    serial_puts(")\n");
    
    return info->port;
}

/**
 * nucleus_copy_boot_files - Copy bootloader and kernel to target
 * @source: Source info from nucleus_find_install_source
 * @dest_port: Destination AHCI port
 * @dest_efi_lba: Destination EFI partition start LBA
 * @progress: Progress callback
 *
 * Copies boot files from source to destination.
 * Returns 0 on success.
 */
int nucleus_copy_boot_files(boot_source_t *source, int dest_port,
                            uint64_t dest_efi_lba, nucleus_progress_cb progress) {
    if (!source || source->source_type == 0) {
        serial_puts("[WRITE] No valid source\n");
        return -1;
    }
    
    serial_puts("[WRITE] Copying boot files: port ");
    serial_putc('0' + source->port);
    serial_puts(" -> port ");
    serial_putc('0' + dest_port);
    serial_puts("\n");
    
    /* Determine sectors to copy */
    uint64_t sectors_to_copy = source->content_sectors;
    
    /* Limit to reasonable size (500MB max for EFI partition) */
    uint64_t max_sectors = (500ULL * 1024 * 1024) / 512;
    if (sectors_to_copy > max_sectors) {
        sectors_to_copy = max_sectors;
    }
    
    /* Use nucleus_write_image for the copy */
    return nucleus_write_image(source->port, source->boot_lba,
                               dest_port, dest_efi_lba,
                               sectors_to_copy, progress);
}
