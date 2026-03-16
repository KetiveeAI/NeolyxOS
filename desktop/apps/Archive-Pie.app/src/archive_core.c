/*
 * Archive.app - Core Functions
 * Archive operations implementation
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nxzip.h>
#include "archive.h"

/* Format file size for display */
const char* format_file_size(uint64_t bytes) {
    static char buf[32];
    
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    
    return buf;
}

/* Calculate compression ratio */
int calculate_ratio(uint64_t original, uint64_t compressed) {
    if (original == 0) return 0;
    return (int)((compressed * 100) / original);
}

/* Open an archive */
bool archive_open(archive_state_t* state, const char* path) {
    if (!state || !path) return false;
    
    /* Close existing archive */
    archive_close(state);
    
    /* Open new archive */
    state->archive = nxzip_open(path);
    if (!state->archive) {
        return false;
    }
    
    /* Copy path info */
    strncpy(state->archive_path, path, MAX_PATH_LEN - 1);
    const char* name = strrchr(path, '/');
    strncpy(state->archive_name, name ? name + 1 : path, 255);
    
    /* Load entries */
    state->entry_count = nxzip_file_count(state->archive);
    if (state->entry_count > MAX_ENTRIES) {
        state->entry_count = MAX_ENTRIES;
    }
    
    state->total_size = 0;
    state->compressed_size = 0;
    
    for (int i = 0; i < state->entry_count; i++) {
        nxzip_file_info_t info;
        if (nxzip_get_file_info(state->archive, i, &info) == NXZIP_OK) {
            archive_entry_t* entry = &state->entries[i];
            strncpy(entry->name, info.name, 255);
            entry->size = info.uncompressed_size;
            entry->compressed_size = info.compressed_size;
            entry->is_directory = (info.name[strlen(info.name) - 1] == '/');
            entry->index = i;
            
            state->total_size += info.uncompressed_size;
            state->compressed_size += info.compressed_size;
        }
    }
    
    return true;
}

/* Create a new archive */
bool archive_create(archive_state_t* state, const char* path, int format) {
    if (!state || !path) return false;
    (void)format;
    
    archive_close(state);
    
    state->archive = nxzip_create(path);
    if (!state->archive) {
        return false;
    }
    
    strncpy(state->archive_path, path, MAX_PATH_LEN - 1);
    const char* name = strrchr(path, '/');
    strncpy(state->archive_name, name ? name + 1 : path, 255);
    
    state->entry_count = 0;
    state->total_size = 0;
    state->compressed_size = 0;
    state->is_modified = true;
    
    return true;
}

/* Close archive */
void archive_close(archive_state_t* state) {
    if (!state) return;
    
    if (state->archive) {
        nxzip_close(state->archive);
        state->archive = NULL;
    }
    
    state->archive_path[0] = '\0';
    state->archive_name[0] = '\0';
    state->entry_count = 0;
    state->total_size = 0;
    state->compressed_size = 0;
    state->is_modified = false;
}

/* Get entries in a folder */
int archive_get_entries(archive_state_t* state, const char* folder,
                        archive_entry_t* out_entries, int max_count) {
    if (!state || !out_entries) return 0;
    
    int count = 0;
    size_t folder_len = folder ? strlen(folder) : 0;
    
    for (int i = 0; i < state->entry_count && count < max_count; i++) {
        archive_entry_t* entry = &state->entries[i];
        
        /* Check if entry is in the requested folder */
        if (folder_len > 0) {
            if (strncmp(entry->name, folder, folder_len) != 0) continue;
            if (entry->name[folder_len] != '/') continue;
        }
        
        /* Check if it's a direct child (not nested deeper) */
        const char* remainder = entry->name + folder_len;
        if (*remainder == '/') remainder++;
        if (strchr(remainder, '/') != NULL && 
            strchr(remainder, '/') != remainder + strlen(remainder) - 1) {
            continue;
        }
        
        out_entries[count++] = *entry;
    }
    
    return count;
}

/* Extract all files */
bool archive_extract_all(archive_state_t* state, const char* dest,
                         progress_fn progress, void* user_data) {
    if (!state || !state->archive || !dest) return false;
    
    int result = nxzip_extract_all(state->archive, dest, 
                                   (nxzip_progress_fn)progress, user_data);
    return result == NXZIP_OK;
}

/* Extract single file */
bool archive_extract_file(archive_state_t* state, int index, const char* dest) {
    if (!state || !state->archive || index < 0) return false;
    
    int result = nxzip_extract_file(state->archive, index, dest);
    return result == NXZIP_OK;
}

/* Add a file to archive (for creation mode) */
bool archive_add_file(archive_state_t* state, const char* path) {
    if (!state || !state->archive || !path) return false;
    
    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;
    
    int result = nxzip_add_file(state->archive, path, name);
    if (result == NXZIP_OK) {
        state->is_modified = true;
        return true;
    }
    
    return false;
}

/* Delete file from archive (would need archive rebuild) */
bool archive_delete_file(archive_state_t* state, int index) {
    if (!state || !state->archive || index < 0) return false;
    /* ZIP format doesn't support in-place deletion */
    /* Would need to rebuild archive without this entry */
    (void)index;
    return false;
}
