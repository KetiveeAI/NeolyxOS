/*
 * NeolyxOS Nucleus Write Module
 * 
 * Raw sector copying for ISO/DD image writing.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NUCLEUS_WRITE_H
#define NUCLEUS_WRITE_H

#include <stdint.h>
#include <stddef.h>

/* Write job descriptor */
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

/**
 * Copy sectors from source to destination.
 * @job: Write job descriptor
 * @progress: Progress callback (can be NULL)
 * @chunk_size: Sectors per chunk (for progress updates)
 * Returns 0 on success, negative on error.
 */
int nucleus_write_sectors(nucleus_write_job_t *job, nucleus_progress_cb progress, uint32_t chunk_size);

/**
 * Verify written data matches source.
 * @job: Write job (after writing)
 * Returns 0 if verified, negative on mismatch or error.
 */
int nucleus_verify_write(nucleus_write_job_t *job);

/**
 * Clone entire disk to another disk.
 * @src_port: Source AHCI port
 * @dst_port: Destination AHCI port
 * @progress: Progress callback
 * Returns 0 on success.
 */
int nucleus_clone_disk(int src_port, int dst_port, nucleus_progress_cb progress);

/**
 * Write image data to disk (for ISO/DD).
 */
int nucleus_write_image(int src_port, uint64_t src_offset,
                        int dst_port, uint64_t dst_offset,
                        uint64_t sector_count,
                        nucleus_progress_cb progress);

/**
 * Zero out a disk or partition.
 */
int nucleus_zero_disk(int port, uint64_t start_lba, uint64_t sector_count, 
                      nucleus_progress_cb progress);

/**
 * Default progress callback for serial output.
 */
void nucleus_default_progress(uint64_t done, uint64_t total, int percent);

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

/**
 * Detect ISO9660 filesystem on disk.
 */
int nucleus_detect_iso9660(int port, boot_source_t *info);

/**
 * Detect El Torito bootable CD.
 */
int nucleus_detect_eltorito(int port, boot_source_t *info);

/**
 * Detect NeolyxOS installation media.
 */
int nucleus_detect_neolyx_source(int port, boot_source_t *info);

/**
 * Find NeolyxOS installation source.
 * @exclude_port: Port to exclude (destination disk)
 * @info: Output info structure
 * Returns port number, or -1 if not found.
 */
int nucleus_find_install_source(int exclude_port, boot_source_t *info);

/**
 * Copy boot files from source to target.
 */
int nucleus_copy_boot_files(boot_source_t *source, int dest_port,
                            uint64_t dest_efi_lba, nucleus_progress_cb progress);

#endif /* NUCLEUS_WRITE_H */

