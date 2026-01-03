/*
 * Rivee - User Data Manager Implementation
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "../include/userdata.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* External VFS functions */
extern int vfs_mkdir(const char *path);
extern int vfs_exists(const char *path);
extern int vfs_read(const char *path, char *buf, uint32_t max_len);
extern int vfs_write(const char *path, const char *data, uint32_t len);
extern int vfs_list(const char *path, void (*callback)(const char *name, void *ctx), void *ctx);
extern int vfs_delete(const char *path);
extern int vfs_copy(const char *src, const char *dst);

/* ============ Path Management ============ */

static char data_dir[256] = "";
static char prefs_dir[256] = "";
static char backup_dir[256] = "";
static char cache_dir[256] = "";
static char autosave_dir[256] = "";

static void init_paths(void) {
    if (data_dir[0] != '\0') return; /* Already initialized */
    
    /* Base: /Users/{user}/Library/Application Support/Rivee */
    const char *home = "/Users/default"; /* Would get actual user home */
    
    snprintf(data_dir, sizeof(data_dir), 
             "%s/Library/Application Support/Rivee", home);
    snprintf(prefs_dir, sizeof(prefs_dir), "%s/Preferences", data_dir);
    snprintf(backup_dir, sizeof(backup_dir), "%s/Backups", data_dir);
    snprintf(cache_dir, sizeof(cache_dir), "%s/Cache", data_dir);
    snprintf(autosave_dir, sizeof(autosave_dir), "%s/Autosave", data_dir);
    
    /* Create directories if needed */
    vfs_mkdir(data_dir);
    vfs_mkdir(prefs_dir);
    vfs_mkdir(backup_dir);
    vfs_mkdir(cache_dir);
    vfs_mkdir(autosave_dir);
}

const char *rivee_get_data_dir(void) {
    init_paths();
    return data_dir;
}

const char *rivee_get_preferences_dir(void) {
    init_paths();
    return prefs_dir;
}

const char *rivee_get_backups_dir(void) {
    init_paths();
    return backup_dir;
}

const char *rivee_get_cache_dir(void) {
    init_paths();
    return cache_dir;
}

const char *rivee_get_autosave_dir(void) {
    init_paths();
    return autosave_dir;
}

/* ============ Preferences ============ */

void rivee_preferences_reset_defaults(rivee_preferences_t *prefs) {
    if (!prefs) return;
    
    memset(prefs, 0, sizeof(rivee_preferences_t));
    
    prefs->window_width = 1280;
    prefs->window_height = 800;
    prefs->window_maximized = false;
    
    prefs->dark_mode = true;
    strcpy(prefs->workspace, "default");
    strcpy(prefs->language, "en");
    
    prefs->default_width = 800;
    prefs->default_height = 600;
    strcpy(prefs->default_units, "pixels");
    
    prefs->default_stroke_width = 2.0f;
    prefs->default_fill_color = 0xFFFFFFFF;
    prefs->default_stroke_color = 0xFF000000;
    
    prefs->show_rulers = true;
    prefs->show_grid = false;
    prefs->snap_to_grid = true;
    prefs->snap_to_objects = true;
    prefs->grid_spacing = 10.0f;
    
    prefs->autosave_enabled = true;
    prefs->autosave_interval_minutes = 5;
    prefs->max_backup_versions = 10;
    
    prefs->max_recent_files = 20;
    
    prefs->gpu_acceleration = true;
    prefs->undo_levels = 100;
    prefs->cache_size_mb = 256;
}

int rivee_preferences_load(rivee_preferences_t *prefs) {
    if (!prefs) return -1;
    
    init_paths();
    
    char path[512];
    snprintf(path, sizeof(path), "%s/preferences.json", prefs_dir);
    
    char buffer[4096] = {0};
    if (vfs_read(path, buffer, sizeof(buffer)) <= 0) {
        /* No prefs file, use defaults */
        rivee_preferences_reset_defaults(prefs);
        return 0;
    }
    
    /* TODO: Parse JSON properly */
    /* For now, just use defaults */
    rivee_preferences_reset_defaults(prefs);
    
    return 0;
}

int rivee_preferences_save(const rivee_preferences_t *prefs) {
    if (!prefs) return -1;
    
    init_paths();
    
    char path[512];
    snprintf(path, sizeof(path), "%s/preferences.json", prefs_dir);
    
    /* Build JSON */
    char json[4096];
    int len = snprintf(json, sizeof(json),
        "{\n"
        "  \"window\": {\n"
        "    \"x\": %d, \"y\": %d,\n"
        "    \"width\": %d, \"height\": %d,\n"
        "    \"maximized\": %s\n"
        "  },\n"
        "  \"ui\": {\n"
        "    \"dark_mode\": %s,\n"
        "    \"workspace\": \"%s\",\n"
        "    \"language\": \"%s\"\n"
        "  },\n"
        "  \"autosave\": {\n"
        "    \"enabled\": %s,\n"
        "    \"interval_minutes\": %d,\n"
        "    \"max_backups\": %d\n"
        "  }\n"
        "}\n",
        prefs->window_x, prefs->window_y,
        prefs->window_width, prefs->window_height,
        prefs->window_maximized ? "true" : "false",
        prefs->dark_mode ? "true" : "false",
        prefs->workspace,
        prefs->language,
        prefs->autosave_enabled ? "true" : "false",
        prefs->autosave_interval_minutes,
        prefs->max_backup_versions
    );
    
    return vfs_write(path, json, len);
}

/* ============ Recent Files ============ */

static recent_file_t g_recent_files[MAX_RECENT_FILES];
static int g_recent_count = 0;

int rivee_recent_files_load(recent_file_t *files, int max) {
    if (!files) return -1;
    
    init_paths();
    
    char path[512];
    snprintf(path, sizeof(path), "%s/recent_files.json", data_dir);
    
    char buffer[8192] = {0};
    if (vfs_read(path, buffer, sizeof(buffer)) <= 0) {
        return 0; /* No recent files */
    }
    
    /* TODO: Parse JSON properly */
    
    memcpy(files, g_recent_files, sizeof(recent_file_t) * (max < g_recent_count ? max : g_recent_count));
    return g_recent_count < max ? g_recent_count : max;
}

void rivee_recent_files_add(const char *path) {
    if (!path) return;
    
    /* Remove if already exists */
    rivee_recent_files_remove(path);
    
    /* Shift existing entries down */
    if (g_recent_count >= MAX_RECENT_FILES) {
        g_recent_count = MAX_RECENT_FILES - 1;
    }
    
    for (int i = g_recent_count; i > 0; i--) {
        g_recent_files[i] = g_recent_files[i - 1];
    }
    
    /* Add new entry at front */
    memset(&g_recent_files[0], 0, sizeof(recent_file_t));
    strncpy(g_recent_files[0].path, path, sizeof(g_recent_files[0].path) - 1);
    
    /* Extract filename */
    const char *slash = strrchr(path, '/');
    strncpy(g_recent_files[0].name, slash ? slash + 1 : path, 
            sizeof(g_recent_files[0].name) - 1);
    
    g_recent_files[0].last_opened = (uint64_t)time(NULL);
    g_recent_count++;
}

void rivee_recent_files_remove(const char *path) {
    if (!path) return;
    
    for (int i = 0; i < g_recent_count; i++) {
        if (strcmp(g_recent_files[i].path, path) == 0) {
            /* Shift remaining entries up */
            for (int j = i; j < g_recent_count - 1; j++) {
                g_recent_files[j] = g_recent_files[j + 1];
            }
            g_recent_count--;
            return;
        }
    }
}

void rivee_recent_files_clear(void) {
    g_recent_count = 0;
    memset(g_recent_files, 0, sizeof(g_recent_files));
}

/* ============ Autosave ============ */

static char g_autosave_path[256] = "";
static char g_document_path[256] = "";

void rivee_autosave_start(const char *document_path) {
    if (!document_path) return;
    
    init_paths();
    
    strncpy(g_document_path, document_path, sizeof(g_document_path) - 1);
    
    /* Create autosave filename from document hash */
    unsigned int hash = 0;
    for (const char *p = document_path; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }
    
    snprintf(g_autosave_path, sizeof(g_autosave_path),
             "%s/%08X.autosave", autosave_dir, hash);
}

void rivee_autosave_save(void) {
    if (g_autosave_path[0] == '\0') return;
    
    /* Would save current document state to autosave file */
    /* This is called periodically by the app */
}

void rivee_autosave_stop(void) {
    /* Delete autosave file if it exists */
    if (g_autosave_path[0] != '\0') {
        vfs_delete(g_autosave_path);
        g_autosave_path[0] = '\0';
        g_document_path[0] = '\0';
    }
}

bool rivee_autosave_exists(const char *document_path) {
    if (!document_path) return false;
    
    init_paths();
    
    unsigned int hash = 0;
    for (const char *p = document_path; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }
    
    char path[256];
    snprintf(path, sizeof(path), "%s/%08X.autosave", autosave_dir, hash);
    
    return vfs_exists(path);
}

int rivee_autosave_recover(const char *document_path, char *recovered_path, int max_len) {
    if (!document_path || !recovered_path) return -1;
    
    if (!rivee_autosave_exists(document_path)) return -1;
    
    unsigned int hash = 0;
    for (const char *p = document_path; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }
    
    snprintf(recovered_path, max_len, "%s/%08X.autosave", autosave_dir, hash);
    return 0;
}

/* ============ Backups ============ */

int rivee_backup_create(const char *document_path) {
    if (!document_path) return -1;
    
    init_paths();
    
    /* Get filename without path */
    const char *filename = strrchr(document_path, '/');
    filename = filename ? filename + 1 : document_path;
    
    /* Create backup filename with timestamp */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    char backup_path[512];
    snprintf(backup_path, sizeof(backup_path), 
             "%s/%s_%04d%02d%02d_%02d%02d%02d.bak",
             backup_dir, filename,
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    
    return vfs_copy(document_path, backup_path);
}

int rivee_backup_list(const char *document_path, backup_info_t *backups, int max) {
    if (!document_path || !backups) return -1;
    
    init_paths();
    
    /* TODO: Scan backup directory for matching files */
    (void)max;
    
    return 0;
}

int rivee_backup_restore(const backup_info_t *backup, const char *restore_path) {
    if (!backup || !restore_path) return -1;
    
    return vfs_copy(backup->backup_path, restore_path);
}

void rivee_backup_cleanup(const char *document_path, int keep_versions) {
    if (!document_path) return;
    
    /* TODO: Delete old backups keeping only 'keep_versions' most recent */
    (void)keep_versions;
}

/* ============ User Asset Directories ============ */

const char *rivee_get_user_brushes_dir(void) {
    static char path[256] = "";
    if (path[0] == '\0') {
        init_paths();
        snprintf(path, sizeof(path), "%s/Brushes", data_dir);
        vfs_mkdir(path);
    }
    return path;
}

const char *rivee_get_user_palettes_dir(void) {
    static char path[256] = "";
    if (path[0] == '\0') {
        init_paths();
        snprintf(path, sizeof(path), "%s/Palettes", data_dir);
        vfs_mkdir(path);
    }
    return path;
}

const char *rivee_get_user_templates_dir(void) {
    static char path[256] = "";
    if (path[0] == '\0') {
        init_paths();
        snprintf(path, sizeof(path), "%s/Templates", data_dir);
        vfs_mkdir(path);
    }
    return path;
}

const char *rivee_get_user_symbols_dir(void) {
    static char path[256] = "";
    if (path[0] == '\0') {
        init_paths();
        snprintf(path, sizeof(path), "%s/Symbols", data_dir);
        vfs_mkdir(path);
    }
    return path;
}

const char *rivee_get_user_workspaces_dir(void) {
    static char path[256] = "";
    if (path[0] == '\0') {
        init_paths();
        snprintf(path, sizeof(path), "%s/Workspaces", data_dir);
        vfs_mkdir(path);
    }
    return path;
}
