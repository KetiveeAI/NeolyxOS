/*
 * NXZIP - NeolyxOS Compression Library
 * ZIP Archive Handler
 * 
 * Creates and extracts ZIP-compatible archives
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include "../include/nxzip.h"

/* ZIP format signatures */
#define ZIP_LOCAL_HEADER_SIG    0x04034B50
#define ZIP_CENTRAL_HEADER_SIG  0x02014B50
#define ZIP_END_RECORD_SIG      0x06054B50

/* Archive structure */
struct nxzip_archive {
    FILE* file;
    char path[256];
    int mode;   /* 0 = read, 1 = write */
    
    /* For reading */
    int file_count;
    nxzip_file_info_t* files;
    
    /* For writing */
    size_t* local_offsets;
    int local_count;
    int local_capacity;
};

/* Write little-endian values */
static void write_u16(FILE* f, uint16_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static void write_u32(FILE* f, uint32_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

/* Read little-endian values */
static uint16_t read_u16(FILE* f) {
    uint16_t v = fgetc(f);
    v |= fgetc(f) << 8;
    return v;
}

static uint32_t read_u32(FILE* f) {
    uint32_t v = fgetc(f);
    v |= fgetc(f) << 8;
    v |= fgetc(f) << 16;
    v |= fgetc(f) << 24;
    return v;
}

/* Create new archive for writing */
nxzip_archive_t* nxzip_create(const char* path) {
    nxzip_archive_t* archive = calloc(1, sizeof(nxzip_archive_t));
    if (!archive) return NULL;
    
    archive->file = fopen(path, "wb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }
    
    strncpy(archive->path, path, 255);
    archive->mode = 1;
    archive->local_capacity = 64;
    archive->local_offsets = malloc(64 * sizeof(size_t));
    archive->files = malloc(64 * sizeof(nxzip_file_info_t));
    
    return archive;
}

/* Add data to archive */
int nxzip_add_data(nxzip_archive_t* archive, const void* data, size_t size,
                   const char* dest_name, int method) {
    if (!archive || archive->mode != 1) return NXZIP_ERR_IO;
    
    /* Grow arrays if needed */
    if (archive->local_count >= archive->local_capacity) {
        archive->local_capacity *= 2;
        archive->local_offsets = realloc(archive->local_offsets, 
                                         archive->local_capacity * sizeof(size_t));
        archive->files = realloc(archive->files,
                                 archive->local_capacity * sizeof(nxzip_file_info_t));
    }
    
    /* Store file info */
    nxzip_file_info_t* info = &archive->files[archive->local_count];
    strncpy(info->name, dest_name, 255);
    info->uncompressed_size = size;
    info->crc32 = nxzip_crc32(data, size);
    info->method = method;
    info->offset = ftell(archive->file);
    
    archive->local_offsets[archive->local_count] = info->offset;
    
    /* Compress if requested */
    void* compressed = NULL;
    size_t compressed_size = 0;
    
    if (method == NXZIP_DEFLATE && size > 0) {
        if (nxzip_deflate(data, size, &compressed, &compressed_size) == NXZIP_OK) {
            info->compressed_size = compressed_size;
        } else {
            /* Fall back to store */
            method = NXZIP_STORE;
            info->method = NXZIP_STORE;
        }
    }
    
    if (method == NXZIP_STORE) {
        info->compressed_size = size;
    }
    
    /* Write local file header */
    write_u32(archive->file, ZIP_LOCAL_HEADER_SIG);
    write_u16(archive->file, 20);                    /* Version needed */
    write_u16(archive->file, 0);                     /* Flags */
    write_u16(archive->file, info->method);          /* Compression method */
    write_u16(archive->file, 0);                     /* Mod time */
    write_u16(archive->file, 0);                     /* Mod date */
    write_u32(archive->file, info->crc32);           /* CRC32 */
    write_u32(archive->file, info->compressed_size); /* Compressed size */
    write_u32(archive->file, info->uncompressed_size); /* Uncompressed size */
    write_u16(archive->file, strlen(dest_name));     /* Filename length */
    write_u16(archive->file, 0);                     /* Extra field length */
    fwrite(dest_name, 1, strlen(dest_name), archive->file);
    
    /* Write data */
    if (compressed) {
        fwrite(compressed, 1, compressed_size, archive->file);
        free(compressed);
    } else {
        fwrite(data, 1, size, archive->file);
    }
    
    archive->local_count++;
    return NXZIP_OK;
}

/* Add file from disk */
int nxzip_add_file(nxzip_archive_t* archive, const char* src_path, const char* dest_name) {
    FILE* f = fopen(src_path, "rb");
    if (!f) return NXZIP_ERR_IO;
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    void* data = malloc(size);
    if (!data) {
        fclose(f);
        return NXZIP_ERR_MEMORY;
    }
    
    fread(data, 1, size, f);
    fclose(f);
    
    int result = nxzip_add_data(archive, data, size, dest_name, NXZIP_DEFLATE);
    free(data);
    
    return result;
}

/* Add directory recursively */
int nxzip_add_directory(nxzip_archive_t* archive, const char* dir_path,
                        const char* prefix, nxzip_progress_fn progress, void* user_data) {
    DIR* dir = opendir(dir_path);
    if (!dir) return NXZIP_ERR_IO;
    
    struct dirent* entry;
    int file_num = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char src_path[512];
        char dest_name[512];
        
        snprintf(src_path, sizeof(src_path), "%s/%s", dir_path, entry->d_name);
        
        if (prefix && prefix[0]) {
            snprintf(dest_name, sizeof(dest_name), "%s/%s", prefix, entry->d_name);
        } else {
            snprintf(dest_name, sizeof(dest_name), "%s", entry->d_name);
        }
        
        struct stat st;
        if (stat(src_path, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory */
            nxzip_add_directory(archive, src_path, dest_name, progress, user_data);
        } else if (S_ISREG(st.st_mode)) {
            if (progress) {
                progress(dest_name, file_num * 10 % 100, user_data);
            }
            nxzip_add_file(archive, src_path, dest_name);
            file_num++;
        }
    }
    
    closedir(dir);
    return NXZIP_OK;
}

/* Finalize archive */
int nxzip_finalize(nxzip_archive_t* archive) {
    if (!archive || archive->mode != 1) return NXZIP_ERR_IO;
    
    size_t central_dir_start = ftell(archive->file);
    
    /* Write central directory */
    for (int i = 0; i < archive->local_count; i++) {
        nxzip_file_info_t* info = &archive->files[i];
        
        write_u32(archive->file, ZIP_CENTRAL_HEADER_SIG);
        write_u16(archive->file, 20);                     /* Version made by */
        write_u16(archive->file, 20);                     /* Version needed */
        write_u16(archive->file, 0);                      /* Flags */
        write_u16(archive->file, info->method);           /* Compression method */
        write_u16(archive->file, 0);                      /* Mod time */
        write_u16(archive->file, 0);                      /* Mod date */
        write_u32(archive->file, info->crc32);
        write_u32(archive->file, info->compressed_size);
        write_u32(archive->file, info->uncompressed_size);
        write_u16(archive->file, strlen(info->name));     /* Filename length */
        write_u16(archive->file, 0);                      /* Extra field length */
        write_u16(archive->file, 0);                      /* Comment length */
        write_u16(archive->file, 0);                      /* Disk number */
        write_u16(archive->file, 0);                      /* Internal attrs */
        write_u32(archive->file, 0);                      /* External attrs */
        write_u32(archive->file, archive->local_offsets[i]);  /* Local header offset */
        fwrite(info->name, 1, strlen(info->name), archive->file);
    }
    
    size_t central_dir_end = ftell(archive->file);
    size_t central_dir_size = central_dir_end - central_dir_start;
    
    /* Write end of central directory */
    write_u32(archive->file, ZIP_END_RECORD_SIG);
    write_u16(archive->file, 0);                          /* Disk number */
    write_u16(archive->file, 0);                          /* Central dir disk */
    write_u16(archive->file, archive->local_count);       /* Entries on disk */
    write_u16(archive->file, archive->local_count);       /* Total entries */
    write_u32(archive->file, central_dir_size);           /* Central dir size */
    write_u32(archive->file, central_dir_start);          /* Central dir offset */
    write_u16(archive->file, 0);                          /* Comment length */
    
    fclose(archive->file);
    archive->file = NULL;
    
    free(archive->local_offsets);
    free(archive->files);
    free(archive);
    
    return NXZIP_OK;
}

/* Open existing archive for reading */
nxzip_archive_t* nxzip_open(const char* path) {
    nxzip_archive_t* archive = calloc(1, sizeof(nxzip_archive_t));
    if (!archive) return NULL;
    
    archive->file = fopen(path, "rb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }
    
    strncpy(archive->path, path, 255);
    archive->mode = 0;
    
    /* Find end of central directory */
    fseek(archive->file, -22, SEEK_END);
    if (read_u32(archive->file) != ZIP_END_RECORD_SIG) {
        fclose(archive->file);
        free(archive);
        return NULL;
    }
    
    fseek(archive->file, 4, SEEK_CUR);  /* Skip to entry count */
    archive->file_count = read_u16(archive->file);
    fseek(archive->file, 2, SEEK_CUR);   /* Skip total */
    uint32_t central_dir_size = read_u32(archive->file);
    uint32_t central_dir_offset = read_u32(archive->file);
    (void)central_dir_size;
    
    /* Read central directory */
    archive->files = malloc(archive->file_count * sizeof(nxzip_file_info_t));
    if (!archive->files) {
        fclose(archive->file);
        free(archive);
        return NULL;
    }
    
    fseek(archive->file, central_dir_offset, SEEK_SET);
    
    for (int i = 0; i < archive->file_count; i++) {
        if (read_u32(archive->file) != ZIP_CENTRAL_HEADER_SIG) break;
        
        nxzip_file_info_t* info = &archive->files[i];
        
        fseek(archive->file, 6, SEEK_CUR);  /* Skip version, flags */
        info->method = read_u16(archive->file);
        fseek(archive->file, 4, SEEK_CUR);  /* Skip time/date */
        info->crc32 = read_u32(archive->file);
        info->compressed_size = read_u32(archive->file);
        info->uncompressed_size = read_u32(archive->file);
        uint16_t name_len = read_u16(archive->file);
        uint16_t extra_len = read_u16(archive->file);
        uint16_t comment_len = read_u16(archive->file);
        fseek(archive->file, 8, SEEK_CUR);  /* Skip disk, attrs */
        info->offset = read_u32(archive->file);
        
        fread(info->name, 1, name_len < 255 ? name_len : 255, archive->file);
        info->name[name_len < 255 ? name_len : 255] = '\0';
        
        fseek(archive->file, extra_len + comment_len, SEEK_CUR);
    }
    
    return archive;
}

/* Get file count */
int nxzip_file_count(nxzip_archive_t* archive) {
    return archive ? archive->file_count : 0;
}

/* Get file info */
int nxzip_get_file_info(nxzip_archive_t* archive, int index, nxzip_file_info_t* info) {
    if (!archive || index < 0 || index >= archive->file_count) return NXZIP_ERR_FORMAT;
    *info = archive->files[index];
    return NXZIP_OK;
}

/* Find file by name */
int nxzip_find_file(nxzip_archive_t* archive, const char* name) {
    if (!archive) return -1;
    for (int i = 0; i < archive->file_count; i++) {
        if (strcmp(archive->files[i].name, name) == 0) return i;
    }
    return -1;
}

/* Extract file to memory */
int nxzip_extract_to_memory(nxzip_archive_t* archive, int index,
                            void** out_data, size_t* out_size) {
    if (!archive || archive->mode != 0) return NXZIP_ERR_IO;
    if (index < 0 || index >= archive->file_count) return NXZIP_ERR_FORMAT;
    
    nxzip_file_info_t* info = &archive->files[index];
    
    /* Seek to local header and skip it */
    fseek(archive->file, info->offset + 26, SEEK_SET);
    uint16_t name_len = read_u16(archive->file);
    uint16_t extra_len = read_u16(archive->file);
    fseek(archive->file, name_len + extra_len, SEEK_CUR);
    
    /* Read compressed data */
    void* compressed = malloc(info->compressed_size);
    if (!compressed) return NXZIP_ERR_MEMORY;
    fread(compressed, 1, info->compressed_size, archive->file);
    
    /* Decompress */
    *out_size = info->uncompressed_size;
    *out_data = malloc(*out_size);
    if (!*out_data) {
        free(compressed);
        return NXZIP_ERR_MEMORY;
    }
    
    if (info->method == NXZIP_STORE) {
        memcpy(*out_data, compressed, *out_size);
    } else if (info->method == NXZIP_DEFLATE) {
        int result = nxzip_inflate(compressed, info->compressed_size, 
                                   *out_data, *out_size);
        if (result != NXZIP_OK) {
            free(compressed);
            free(*out_data);
            return result;
        }
    }
    
    /* Verify CRC */
    uint32_t crc = nxzip_crc32(*out_data, *out_size);
    free(compressed);
    
    if (crc != info->crc32) {
        free(*out_data);
        return NXZIP_ERR_CRC;
    }
    
    return NXZIP_OK;
}

/* Create directory recursively */
static int mkdir_p(const char* path) {
    char tmp[512];
    strncpy(tmp, path, 511);
    size_t len = strlen(tmp);
    if (tmp[len-1] == '/') tmp[len-1] = 0;
    
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

/* Extract file to disk */
int nxzip_extract_file(nxzip_archive_t* archive, int index, const char* dest_path) {
    void* data = NULL;
    size_t size = 0;
    
    int result = nxzip_extract_to_memory(archive, index, &data, &size);
    if (result != NXZIP_OK) return result;
    
    /* Create parent directories */
    char* dir = strdup(dest_path);
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = 0;
        mkdir_p(dir);
    }
    free(dir);
    
    FILE* f = fopen(dest_path, "wb");
    if (!f) {
        free(data);
        return NXZIP_ERR_IO;
    }
    
    fwrite(data, 1, size, f);
    fclose(f);
    free(data);
    
    return NXZIP_OK;
}

/* Extract all files */
int nxzip_extract_all(nxzip_archive_t* archive, const char* dest_dir,
                      nxzip_progress_fn progress, void* user_data) {
    if (!archive || archive->mode != 0) return NXZIP_ERR_IO;
    
    mkdir_p(dest_dir);
    
    for (int i = 0; i < archive->file_count; i++) {
        nxzip_file_info_t* info = &archive->files[i];
        
        char dest_path[512];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, info->name);
        
        if (progress) {
            int percent = (i + 1) * 100 / archive->file_count;
            progress(info->name, percent, user_data);
        }
        
        int result = nxzip_extract_file(archive, i, dest_path);
        if (result != NXZIP_OK) return result;
    }
    
    return NXZIP_OK;
}

/* Close archive */
void nxzip_close(nxzip_archive_t* archive) {
    if (!archive) return;
    if (archive->file) fclose(archive->file);
    free(archive->files);
    free(archive->local_offsets);
    free(archive);
}
