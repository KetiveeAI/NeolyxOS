/*
 * NXZIP - NeolyxOS Compression Library
 * Custom ZIP-compatible archive format
 * 
 * Features:
 *   - Store (no compression) and DEFLATE support
 *   - ZIP-compatible file format
 *   - Simple archive creation and extraction
 *   - CRC32 checksum validation
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#ifndef NXZIP_H
#define NXZIP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes */
#define NXZIP_OK             0
#define NXZIP_ERR_MEMORY    -1
#define NXZIP_ERR_IO        -2
#define NXZIP_ERR_FORMAT    -3
#define NXZIP_ERR_CRC       -4
#define NXZIP_ERR_COMPRESS  -5

/* Compression methods */
#define NXZIP_STORE          0   /* No compression */
#define NXZIP_DEFLATE        8   /* DEFLATE (compatible with ZIP) */

/* Archive handle */
typedef struct nxzip_archive nxzip_archive_t;

/* File info structure */
typedef struct {
    char name[256];
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t crc32;
    uint16_t method;
    uint32_t offset;
} nxzip_file_info_t;

/* Progress callback */
typedef void (*nxzip_progress_fn)(const char* filename, int percent, void* user_data);

/* ========================================================================== */
/* Archive Creation API                                                        */
/* ========================================================================== */

/* Create a new archive for writing */
nxzip_archive_t* nxzip_create(const char* path);

/* Add a file to the archive */
int nxzip_add_file(nxzip_archive_t* archive, const char* src_path, const char* dest_name);

/* Add data from memory to the archive */
int nxzip_add_data(nxzip_archive_t* archive, const void* data, size_t size, 
                   const char* dest_name, int method);

/* Add an entire directory recursively */
int nxzip_add_directory(nxzip_archive_t* archive, const char* dir_path, 
                        const char* prefix, nxzip_progress_fn progress, void* user_data);

/* Finalize and close the archive */
int nxzip_finalize(nxzip_archive_t* archive);

/* ========================================================================== */
/* Archive Extraction API                                                      */
/* ========================================================================== */

/* Open an existing archive for reading */
nxzip_archive_t* nxzip_open(const char* path);

/* Get number of files in archive */
int nxzip_file_count(nxzip_archive_t* archive);

/* Get info about a file by index */
int nxzip_get_file_info(nxzip_archive_t* archive, int index, nxzip_file_info_t* info);

/* Find a file by name, returns index or -1 */
int nxzip_find_file(nxzip_archive_t* archive, const char* name);

/* Extract a file to disk */
int nxzip_extract_file(nxzip_archive_t* archive, int index, const char* dest_path);

/* Extract a file to memory (caller frees) */
int nxzip_extract_to_memory(nxzip_archive_t* archive, int index, 
                            void** out_data, size_t* out_size);

/* Extract all files to a directory */
int nxzip_extract_all(nxzip_archive_t* archive, const char* dest_dir,
                      nxzip_progress_fn progress, void* user_data);

/* Close archive */
void nxzip_close(nxzip_archive_t* archive);

/* ========================================================================== */
/* Compression Utilities                                                       */
/* ========================================================================== */

/* Compress data using DEFLATE */
int nxzip_deflate(const void* src, size_t src_size, 
                  void** dst, size_t* dst_size);

/* Decompress DEFLATE data */
int nxzip_inflate(const void* src, size_t src_size,
                  void* dst, size_t dst_size);

/* Calculate CRC32 */
uint32_t nxzip_crc32(const void* data, size_t size);

/* Update CRC32 incrementally */
uint32_t nxzip_crc32_update(uint32_t crc, const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NXZIP_H */
