/*
 * NeolyxOS - nxpkg-pack Tool
 * Creates .nxpkg packages from .app directories
 * 
 * Usage: nxpkg-pack AppName.app [-o output_dir]
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#include "miniz.h"

#define NXPKG_MAGIC "NXPK"
#define NXPKG_VERSION 1

/* Package header */
typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    uint32_t manifest_offset;
    uint32_t payload_offset;
} nxpkg_header_t;

/* Forward declarations */
static int read_manifest(const char* app_path, char** out_json, size_t* out_size);
static int compress_directory(const char* dir_path, void** out_data, size_t* out_size);
static int write_package(const char* output_path, const char* manifest, size_t manifest_size,
                         const void* payload, size_t payload_size);

/* Print usage */
static void print_usage(const char* prog) {
    printf("Usage: %s <AppName.app> [-o output_dir]\n", prog);
    printf("\nCreates an .nxpkg package from an .app directory.\n");
    printf("\nOptions:\n");
    printf("  -o DIR    Output directory (default: current)\n");
    printf("\nExample:\n");
    printf("  %s Calculator.app -o /tmp/\n", prog);
}

/* Extract app name from path */
static void get_app_name(const char* path, char* name, size_t name_size) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    
    size_t len = strlen(base);
    if (len > 4 && strcmp(base + len - 4, ".app") == 0) {
        len -= 4;
    }
    
    if (len >= name_size) len = name_size - 1;
    strncpy(name, base, len);
    name[len] = '\0';
}

/* Check if path is a directory */
static int is_directory(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Read file contents */
static int read_file(const char* path, char** out_data, size_t* out_size) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    
    fseek(fp, 0, SEEK_END);
    *out_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    *out_data = malloc(*out_size + 1);
    if (!*out_data) {
        fclose(fp);
        return -1;
    }
    
    fread(*out_data, 1, *out_size, fp);
    (*out_data)[*out_size] = '\0';
    fclose(fp);
    return 0;
}

/* Read manifest from .app directory */
static int read_manifest(const char* app_path, char** out_json, size_t* out_size) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.npa", app_path);
    
    if (read_file(manifest_path, out_json, out_size) != 0) {
        fprintf(stderr, "Error: Cannot read %s\n", manifest_path);
        return -1;
    }
    
    return 0;
}

/* Simple ZIP creation using miniz memory functions */
static int compress_directory(const char* dir_path, void** out_data, size_t* out_size) {
    /* For simplicity, we'll create a tar-like archive */
    /* In production, use full miniz mz_zip_* APIs */
    
    DIR* dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open directory %s\n", dir_path);
        return -1;
    }
    
    /* Allocate buffer for compressed output */
    size_t buffer_size = 1024 * 1024 * 10; /* 10MB max */
    *out_data = malloc(buffer_size);
    if (!*out_data) {
        closedir(dir);
        return -1;
    }
    
    mz_ulong compressed_size = buffer_size;
    
    /* Read entire directory into buffer, then compress */
    size_t raw_size = 0;
    char* raw_data = malloc(buffer_size);
    if (!raw_data) {
        free(*out_data);
        closedir(dir);
        return -1;
    }
    
    /* Write a simple header with file list */
    /* Format: [count:4][name:256][size:4][data...]... */
    uint32_t* file_count = (uint32_t*)raw_data;
    *file_count = 0;
    raw_size = 4;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) != 0) continue;
        
        if (S_ISREG(st.st_mode)) {
            /* Write filename */
            memset(raw_data + raw_size, 0, 256);
            strncpy(raw_data + raw_size, entry->d_name, 255);
            raw_size += 256;
            
            /* Write file size */
            uint32_t fsize = (uint32_t)st.st_size;
            memcpy(raw_data + raw_size, &fsize, 4);
            raw_size += 4;
            
            /* Write file data */
            FILE* fp = fopen(filepath, "rb");
            if (fp) {
                fread(raw_data + raw_size, 1, fsize, fp);
                fclose(fp);
                raw_size += fsize;
            }
            
            (*file_count)++;
        }
    }
    closedir(dir);
    
    /* Compress the raw data */
    int result = mz_compress(*out_data, &compressed_size, 
                             (const unsigned char*)raw_data, raw_size);
    free(raw_data);
    
    if (result != MZ_OK) {
        fprintf(stderr, "Error: Compression failed\n");
        free(*out_data);
        return -1;
    }
    
    *out_size = compressed_size;
    return 0;
}

/* Write the final .nxpkg file */
static int write_package(const char* output_path, const char* manifest, size_t manifest_size,
                         const void* payload, size_t payload_size) {
    FILE* fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create %s\n", output_path);
        return -1;
    }
    
    /* Create header */
    nxpkg_header_t header;
    memcpy(header.magic, NXPKG_MAGIC, 4);
    header.version = NXPKG_VERSION;
    header.flags = 0;
    header.manifest_offset = sizeof(nxpkg_header_t);
    header.payload_offset = sizeof(nxpkg_header_t) + 4 + manifest_size;
    
    /* Write header */
    fwrite(&header, sizeof(header), 1, fp);
    
    /* Write manifest size + data */
    uint32_t msize = (uint32_t)manifest_size;
    fwrite(&msize, 4, 1, fp);
    fwrite(manifest, manifest_size, 1, fp);
    
    /* Write payload */
    fwrite(payload, payload_size, 1, fp);
    
    fclose(fp);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* app_path = NULL;
    const char* output_dir = ".";
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (!app_path) {
            app_path = argv[i];
        }
    }
    
    if (!app_path) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Validate input */
    if (!is_directory(app_path)) {
        fprintf(stderr, "Error: %s is not a directory\n", app_path);
        return 1;
    }
    
    /* Get app name */
    char app_name[128];
    get_app_name(app_path, app_name, sizeof(app_name));
    
    printf("Packaging: %s\n", app_name);
    
    /* Read manifest */
    char* manifest = NULL;
    size_t manifest_size = 0;
    if (read_manifest(app_path, &manifest, &manifest_size) != 0) {
        return 1;
    }
    printf("  Manifest: %zu bytes\n", manifest_size);
    
    /* Compress directory */
    void* payload = NULL;
    size_t payload_size = 0;
    if (compress_directory(app_path, &payload, &payload_size) != 0) {
        free(manifest);
        return 1;
    }
    printf("  Payload: %zu bytes\n", payload_size);
    
    /* Build output path */
    char output_path[512];
    snprintf(output_path, sizeof(output_path), "%s/%s.nxpkg", output_dir, app_name);
    
    /* Write package */
    if (write_package(output_path, manifest, manifest_size, payload, payload_size) != 0) {
        free(manifest);
        free(payload);
        return 1;
    }
    
    printf("Created: %s\n", output_path);
    
    free(manifest);
    free(payload);
    return 0;
}
