/*
 * Archive-Pie.app - NeolyxOS Native Archive Manager
 * Header file
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declare nxzip types */
typedef struct nxzip_archive nxzip_archive_t;
#define MAX_ENTRIES 10000
#define MAX_PATH_LEN 512

/* Archive entry */
typedef struct {
    char name[256];
    char path[MAX_PATH_LEN];
    uint32_t size;
    uint32_t compressed_size;
    bool is_directory;
    int index;
} archive_entry_t;

/* App state */
typedef struct {
    nxzip_archive_t* archive;
    char archive_path[MAX_PATH_LEN];
    char archive_name[256];
    archive_entry_t entries[MAX_ENTRIES];
    int entry_count;
    uint64_t total_size;
    uint64_t compressed_size;
    char current_folder[MAX_PATH_LEN];
    int selected_count;
    bool is_modified;
} archive_state_t;

/* Progress callback */
typedef void (*progress_fn)(int percent, void* user_data);

/* Archive operations */
bool archive_open(archive_state_t* state, const char* path);
bool archive_create(archive_state_t* state, const char* path, int format);
void archive_close(archive_state_t* state);

/* File operations */
int archive_get_entries(archive_state_t* state, const char* folder, 
                        archive_entry_t* out_entries, int max_count);
bool archive_extract_all(archive_state_t* state, const char* dest, 
                         progress_fn progress, void* user_data);
bool archive_extract_file(archive_state_t* state, int index, const char* dest);
bool archive_add_file(archive_state_t* state, const char* path);
bool archive_delete_file(archive_state_t* state, int index);

/* Utilities */
const char* format_file_size(uint64_t bytes);
int calculate_ratio(uint64_t original, uint64_t compressed);

#endif /* ARCHIVE_H */
