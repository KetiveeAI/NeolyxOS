/*
 * NeolyxOS NPA Archive Format
 * 
 * NPA (Neolyx Package Archive) - Binary package format for NeolyxOS.
 * 
 * File Structure:
 * +----------------+
 * | NPA Header     |  (magic, version, file_count, manifest_size)
 * +----------------+
 * | Manifest JSON  |  (package metadata as JSON string)
 * +----------------+
 * | File Entries[] |  (array of file entry headers)
 * +----------------+
 * | File Data      |  (concatenated file contents)
 * +----------------+
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NPA_ARCHIVE_H
#define NEOLYX_NPA_ARCHIVE_H

#include <stdint.h>

/* ============ NPA Format Constants ============ */

#define NPA_MAGIC           0x4E504131   /* "NPA1" in little-endian */
#define NPA_VERSION         1
#define NPA_MAX_PATH        256
#define NPA_MAX_FILES       1024
#define NPA_MAX_MANIFEST    8192
#define NPA_BLOCK_SIZE      512

/* File types */
#define NPA_TYPE_FILE       0
#define NPA_TYPE_DIRECTORY  1
#define NPA_TYPE_SYMLINK    2

/* ============ NPA Header Structure ============ */

typedef struct {
    uint32_t magic;           /* Must be NPA_MAGIC */
    uint32_t version;         /* Archive format version */
    uint32_t file_count;      /* Number of file entries */
    uint32_t manifest_size;   /* Size of manifest JSON in bytes */
    uint32_t total_size;      /* Total archive size */
    uint32_t header_checksum; /* CRC32 of header (excluding this field) */
    uint32_t data_checksum;   /* CRC32 of all file data */
    uint32_t flags;           /* Reserved for future use */
} __attribute__((packed)) npa_header_t;

/* ============ File Entry Structure ============ */

typedef struct {
    char     path[NPA_MAX_PATH];  /* Relative path within package */
    uint32_t size;                /* Uncompressed file size */
    uint32_t offset;              /* Offset from start of data section */
    uint32_t checksum;            /* CRC32 of file data */
    uint16_t type;                /* NPA_TYPE_FILE, DIRECTORY, etc. */
    uint16_t permissions;         /* Unix-style permissions (0755, etc.) */
    uint32_t reserved;            /* Alignment padding */
} __attribute__((packed)) npa_file_entry_t;

/* ============ Manifest Structure (Parsed) ============ */

typedef struct {
    char name[64];            /* Package name */
    char version[32];         /* Version string (semver) */
    char description[256];    /* Package description */
    char author[64];          /* Author name */
    char license[32];         /* License identifier */
    char homepage[128];       /* Homepage URL */
    char install_path[128];   /* Target install path */
    int  dependency_count;    /* Number of dependencies */
    char dependencies[16][64];/* Dependency names */
} npa_manifest_t;

/* ============ Archive Handle ============ */

typedef struct {
    void            *device;        /* VFS file handle or device */
    npa_header_t     header;        /* Parsed header */
    npa_manifest_t   manifest;      /* Parsed manifest */
    npa_file_entry_t *entries;      /* File entry array (allocated) */
    uint64_t         data_offset;   /* Offset to file data section */
    int              valid;         /* 1 if archive is valid */
} npa_archive_t;

/* ============ Archive API ============ */

/**
 * npa_open - Open and validate an NPA archive
 * @path: Path to .npa file
 * @archive: Output archive handle
 * @return: 0 on success, negative on error
 */
int npa_open(const char *path, npa_archive_t *archive);

/**
 * npa_close - Close archive and free resources
 * @archive: Archive handle to close
 */
void npa_close(npa_archive_t *archive);

/**
 * npa_get_manifest - Get parsed manifest
 * @archive: Open archive
 * @return: Pointer to manifest structure
 */
const npa_manifest_t* npa_get_manifest(const npa_archive_t *archive);

/**
 * npa_get_file_count - Get number of files in archive
 * @archive: Open archive
 * @return: File count
 */
int npa_get_file_count(const npa_archive_t *archive);

/**
 * npa_get_entry - Get file entry by index
 * @archive: Open archive
 * @index: File index (0 to file_count-1)
 * @return: Pointer to file entry, NULL if invalid
 */
const npa_file_entry_t* npa_get_entry(const npa_archive_t *archive, int index);

/**
 * npa_find_entry - Find file entry by path
 * @archive: Open archive
 * @path: Relative path to search for
 * @return: Pointer to file entry, NULL if not found
 */
const npa_file_entry_t* npa_find_entry(const npa_archive_t *archive, const char *path);

/**
 * npa_extract_file - Extract single file to destination
 * @archive: Open archive
 * @entry: File entry to extract
 * @dest_path: Destination file path
 * @return: 0 on success, negative on error
 */
int npa_extract_file(npa_archive_t *archive, const npa_file_entry_t *entry, 
                     const char *dest_path);

/**
 * npa_extract_all - Extract all files to destination directory
 * @archive: Open archive
 * @dest_dir: Destination directory path
 * @return: Number of files extracted, negative on error
 */
int npa_extract_all(npa_archive_t *archive, const char *dest_dir);

/**
 * npa_archive_verify - Verify archive integrity (all checksums)
 * @archive: Open archive
 * @return: 0 if valid, negative if corrupted
 */
int npa_archive_verify(npa_archive_t *archive);

/* ============ CRC32 Checksum ============ */

/**
 * npa_crc32 - Calculate CRC32 checksum
 * @data: Data buffer
 * @size: Buffer size
 * @return: CRC32 value
 */
uint32_t npa_crc32(const void *data, uint32_t size);

#endif /* NEOLYX_NPA_ARCHIVE_H */
