/*
 * NeolyxOS Installer - App Registry
 * Manages /System/Library/Registry/apps.json
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "../include/installer.h"

#define REGISTRY_PATH "/System/Library/Registry"
#define APPS_REGISTRY REGISTRY_PATH "/apps.json"
#define MAX_APPS 256

/* Registry entry */
typedef struct {
    char bundle_id[MAX_NAME_LEN];
    char name[MAX_NAME_LEN];
    char version[16];
    char path[MAX_PATH_LEN];
    char category[32];
    uint64_t install_time;
} registry_entry_t;

/* In-memory registry */
static registry_entry_t g_registry[MAX_APPS];
static int g_registry_count = 0;
static bool g_registry_loaded = false;

/* Initialize registry directory */
bool registry_init(void) {
    /* Create registry directory if needed */
    struct stat st;
    if (stat(REGISTRY_PATH, &st) != 0) {
        if (mkdir(REGISTRY_PATH, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "Warning: Cannot create registry directory\n");
            return false;
        }
    }
    
    /* Load existing registry */
    FILE* fp = fopen(APPS_REGISTRY, "r");
    if (!fp) {
        /* No registry file yet, that's OK */
        g_registry_loaded = true;
        return true;
    }
    
    /* Read and parse JSON array */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size <= 2) {
        fclose(fp);
        g_registry_loaded = true;
        return true;
    }
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return false;
    }
    
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    
    /* Parse entries (simplified - look for bundle_id patterns) */
    const char* pos = content;
    while ((pos = strstr(pos, "\"bundle_id\":")) != NULL && g_registry_count < MAX_APPS) {
        registry_entry_t* entry = &g_registry[g_registry_count];
        
        /* Find bundle_id value */
        pos = strchr(pos, ':') + 1;
        while (*pos == ' ' || *pos == '"') pos++;
        
        int i = 0;
        while (*pos && *pos != '"' && i < MAX_NAME_LEN - 1) {
            entry->bundle_id[i++] = *pos++;
        }
        entry->bundle_id[i] = '\0';
        
        /* Find name */
        const char* name_pos = strstr(pos, "\"name\":");
        if (name_pos) {
            name_pos = strchr(name_pos, ':') + 1;
            while (*name_pos == ' ' || *name_pos == '"') name_pos++;
            i = 0;
            while (*name_pos && *name_pos != '"' && i < MAX_NAME_LEN - 1) {
                entry->name[i++] = *name_pos++;
            }
            entry->name[i] = '\0';
        }
        
        /* Find path */
        const char* path_pos = strstr(pos, "\"path\":");
        if (path_pos) {
            path_pos = strchr(path_pos, ':') + 1;
            while (*path_pos == ' ' || *path_pos == '"') path_pos++;
            i = 0;
            while (*path_pos && *path_pos != '"' && i < MAX_PATH_LEN - 1) {
                entry->path[i++] = *path_pos++;
            }
            entry->path[i] = '\0';
        }
        
        g_registry_count++;
    }
    
    free(content);
    g_registry_loaded = true;
    return true;
}

/* Save registry to disk */
static bool registry_save(void) {
    FILE* fp = fopen(APPS_REGISTRY, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot write to registry file\n");
        return false;
    }
    
    fprintf(fp, "[\n");
    
    for (int i = 0; i < g_registry_count; i++) {
        registry_entry_t* e = &g_registry[i];
        
        fprintf(fp, "  {\n");
        fprintf(fp, "    \"bundle_id\": \"%s\",\n", e->bundle_id);
        fprintf(fp, "    \"name\": \"%s\",\n", e->name);
        fprintf(fp, "    \"version\": \"%s\",\n", e->version);
        fprintf(fp, "    \"path\": \"%s\",\n", e->path);
        fprintf(fp, "    \"category\": \"%s\",\n", e->category);
        fprintf(fp, "    \"install_time\": %lu\n", e->install_time);
        fprintf(fp, "  }%s\n", (i < g_registry_count - 1) ? "," : "");
    }
    
    fprintf(fp, "]\n");
    fclose(fp);
    
    return true;
}

/* Check if app is already registered */
bool registry_app_exists(const char* bundle_id) {
    if (!g_registry_loaded) registry_init();
    
    for (int i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].bundle_id, bundle_id) == 0) {
            return true;
        }
    }
    return false;
}

/* Add app to registry */
bool registry_add_app(const app_manifest_t* manifest, const char* path) {
    if (!manifest || !path) return false;
    if (!g_registry_loaded) registry_init();
    
    /* Check if already exists */
    if (registry_app_exists(manifest->bundle_id)) {
        /* Update existing entry */
        for (int i = 0; i < g_registry_count; i++) {
            if (strcmp(g_registry[i].bundle_id, manifest->bundle_id) == 0) {
                strncpy(g_registry[i].name, manifest->name, MAX_NAME_LEN - 1);
                strncpy(g_registry[i].version, manifest->version, 15);
                strncpy(g_registry[i].path, path, MAX_PATH_LEN - 1);
                strncpy(g_registry[i].category, manifest->category, 31);
                break;
            }
        }
    } else {
        /* Add new entry */
        if (g_registry_count >= MAX_APPS) {
            fprintf(stderr, "Error: Registry full\n");
            return false;
        }
        
        registry_entry_t* entry = &g_registry[g_registry_count++];
        strncpy(entry->bundle_id, manifest->bundle_id, MAX_NAME_LEN - 1);
        strncpy(entry->name, manifest->name, MAX_NAME_LEN - 1);
        strncpy(entry->version, manifest->version, 15);
        strncpy(entry->path, path, MAX_PATH_LEN - 1);
        strncpy(entry->category, manifest->category, 31);
        entry->install_time = 0; /* Would use time() in real implementation */
    }
    
    return registry_save();
}

/* Remove app from registry */
bool registry_remove_app(const char* bundle_id) {
    if (!bundle_id) return false;
    if (!g_registry_loaded) registry_init();
    
    for (int i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].bundle_id, bundle_id) == 0) {
            /* Shift remaining entries */
            for (int j = i; j < g_registry_count - 1; j++) {
                g_registry[j] = g_registry[j + 1];
            }
            g_registry_count--;
            return registry_save();
        }
    }
    
    return false;
}

/* Register app in system */
bool register_app(const app_manifest_t* manifest, const char* install_path) {
    return registry_add_app(manifest, install_path);
}

/* Unregister app from system */
bool unregister_app(const char* bundle_id) {
    return registry_remove_app(bundle_id);
}

/* Add to dock (placeholder - would integrate with desktop shell) */
bool add_to_dock(const char* app_path) {
    if (!app_path) return false;
    printf("[Dock] Would add: %s\n", app_path);
    return true;
}

/* Create desktop shortcut (placeholder) */
bool create_desktop_shortcut(const char* app_path, const char* name) {
    if (!app_path || !name) return false;
    printf("[Desktop] Would create shortcut for: %s\n", name);
    return true;
}
