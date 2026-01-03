/*
 * NeolyxOS Path.app - Search
 * 
 * File search functionality with incremental results.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* ============ Configuration ============ */

#define SEARCH_MAX_RESULTS  256
#define SEARCH_MAX_DEPTH    10
#define SEARCH_PATH_MAX     1024

/* ============ Forward Declarations ============ */

typedef struct file_entry file_entry_t;

/* VFS operations from vfs_ops.c */
extern int path_vfs_list_dir(const char *path, file_entry_t *entries, int max);

/* ============ Search State ============ */

typedef enum {
    SEARCH_IDLE,
    SEARCH_RUNNING,
    SEARCH_COMPLETE,
    SEARCH_CANCELLED
} search_status_t;

typedef struct {
    /* Query */
    char query[256];
    char base_path[SEARCH_PATH_MAX];
    int case_sensitive;
    int search_contents;  /* Search file contents too */
    
    /* Results */
    file_entry_t results[SEARCH_MAX_RESULTS];
    int result_count;
    
    /* State */
    search_status_t status;
    
    /* Progress tracking (for incremental search) */
    char pending_dirs[64][SEARCH_PATH_MAX];
    int pending_count;
    int current_depth;
    int files_scanned;
} search_state_t;

/* ============ String Helpers ============ */

static int search_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void search_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char search_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/*
 * Case-insensitive substring search
 */
static int search_contains(const char *haystack, const char *needle, int case_sens) {
    if (!needle[0]) return 1;  /* Empty query matches all */
    
    int nlen = search_strlen(needle);
    
    for (int i = 0; haystack[i]; i++) {
        int match = 1;
        for (int j = 0; j < nlen && haystack[i + j]; j++) {
            char hc = case_sens ? haystack[i + j] : search_tolower(haystack[i + j]);
            char nc = case_sens ? needle[j] : search_tolower(needle[j]);
            if (hc != nc) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    
    return 0;
}

/* ============ Initialization ============ */

void search_init(search_state_t *search) {
    search->query[0] = '\0';
    search->base_path[0] = '\0';
    search->case_sensitive = 0;
    search->search_contents = 0;
    search->result_count = 0;
    search->status = SEARCH_IDLE;
    search->pending_count = 0;
    search->current_depth = 0;
    search->files_scanned = 0;
}

/* ============ Search Control ============ */

/*
 * Start a new search
 */
void search_start(search_state_t *search, const char *path, const char *query) {
    search_init(search);
    
    search_strcpy(search->base_path, path, SEARCH_PATH_MAX);
    search_strcpy(search->query, query, 256);
    
    /* Add base path as first directory to scan */
    search_strcpy(search->pending_dirs[0], path, SEARCH_PATH_MAX);
    search->pending_count = 1;
    
    search->status = SEARCH_RUNNING;
}

/*
 * Process one step of the search (non-blocking)
 * Call this repeatedly until status changes from SEARCH_RUNNING
 * Returns number of results found in this step
 */
int search_step(search_state_t *search) {
    if (search->status != SEARCH_RUNNING) return 0;
    if (search->pending_count == 0) {
        search->status = SEARCH_COMPLETE;
        return 0;
    }
    if (search->result_count >= SEARCH_MAX_RESULTS) {
        search->status = SEARCH_COMPLETE;
        return 0;
    }
    
    /* Pop a directory from the pending list */
    search->pending_count--;
    char current_dir[SEARCH_PATH_MAX];
    search_strcpy(current_dir, search->pending_dirs[search->pending_count], SEARCH_PATH_MAX);
    
    /* List directory contents */
    file_entry_t entries[64];
    int count = path_vfs_list_dir(current_dir, entries, 64);
    if (count < 0) return 0;
    
    int found = 0;
    
    for (int i = 0; i < count && search->result_count < SEARCH_MAX_RESULTS; i++) {
        file_entry_t *entry = &entries[i];
        search->files_scanned++;
        
        /* Skip . and .. */
        if (entry->name[0] == '.' && 
            (entry->name[1] == '\0' || 
             (entry->name[1] == '.' && entry->name[2] == '\0'))) {
            continue;
        }
        
        /* Check if name matches query */
        if (search_contains(entry->name, search->query, search->case_sensitive)) {
            /* Add to results */
            search->results[search->result_count] = *entry;
            search->result_count++;
            found++;
        }
        
        /* Add subdirectories to pending list */
        if (entry->is_dir && 
            search->pending_count < 64 && 
            search->current_depth < SEARCH_MAX_DEPTH) {
            search_strcpy(search->pending_dirs[search->pending_count], 
                          entry->path, SEARCH_PATH_MAX);
            search->pending_count++;
        }
    }
    
    return found;
}

/*
 * Run search to completion (blocking)
 */
void search_run_all(search_state_t *search) {
    while (search->status == SEARCH_RUNNING) {
        search_step(search);
    }
}

/*
 * Cancel an in-progress search
 */
void search_cancel(search_state_t *search) {
    if (search->status == SEARCH_RUNNING) {
        search->status = SEARCH_CANCELLED;
    }
}

/*
 * Clear search results
 */
void search_clear(search_state_t *search) {
    search->result_count = 0;
    search->query[0] = '\0';
    search->status = SEARCH_IDLE;
    search->pending_count = 0;
    search->files_scanned = 0;
}

/* ============ Query ============ */

int search_is_active(search_state_t *search) {
    return search->status == SEARCH_RUNNING;
}

int search_is_complete(search_state_t *search) {
    return search->status == SEARCH_COMPLETE;
}

int search_get_result_count(search_state_t *search) {
    return search->result_count;
}

file_entry_t* search_get_results(search_state_t *search) {
    return search->results;
}

int search_get_files_scanned(search_state_t *search) {
    return search->files_scanned;
}
