/*
 * Rivee - User Data Manager
 * Handles preferences, recent files, autosave, and backups
 * 
 * Data is stored in:
 * - /Users/{username}/Library/Application Support/Rivee/  (user data)
 * - /Users/{username}/Documents/Rivee Backups/            (backups)
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef RIVEE_USERDATA_H
#define RIVEE_USERDATA_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Paths ============ */

/* Get user data directory (creates if not exists) */
const char *rivee_get_data_dir(void);

/* Get specific subdirectories */
const char *rivee_get_preferences_dir(void);
const char *rivee_get_backups_dir(void);
const char *rivee_get_cache_dir(void);
const char *rivee_get_autosave_dir(void);

/* ============ User Preferences ============ */

typedef struct {
    /* Window state */
    int window_x, window_y;
    int window_width, window_height;
    bool window_maximized;
    
    /* UI preferences */
    bool dark_mode;
    char workspace[64];
    char language[8];
    
    /* Default document settings */
    float default_width;
    float default_height;
    char default_units[16];
    
    /* Tool defaults */
    float default_stroke_width;
    uint32_t default_fill_color;
    uint32_t default_stroke_color;
    
    /* Grid/Guide settings */
    bool show_rulers;
    bool show_grid;
    bool snap_to_grid;
    bool snap_to_objects;
    float grid_spacing;
    
    /* Autosave */
    bool autosave_enabled;
    int autosave_interval_minutes;
    int max_backup_versions;
    
    /* Recent files */
    int max_recent_files;
    
    /* Performance */
    bool gpu_acceleration;
    int undo_levels;
    int cache_size_mb;
} rivee_preferences_t;

/* Load/save preferences */
int rivee_preferences_load(rivee_preferences_t *prefs);
int rivee_preferences_save(const rivee_preferences_t *prefs);
void rivee_preferences_reset_defaults(rivee_preferences_t *prefs);

/* ============ Recent Files ============ */

typedef struct {
    char path[256];
    char name[64];
    char thumbnail[256];    /* Thumbnail path */
    uint64_t last_opened;   /* Timestamp */
    uint64_t file_size;
} recent_file_t;

#define MAX_RECENT_FILES 20

/* Recent file management */
int rivee_recent_files_load(recent_file_t *files, int max);
void rivee_recent_files_add(const char *path);
void rivee_recent_files_remove(const char *path);
void rivee_recent_files_clear(void);

/* ============ Autosave & Backup ============ */

typedef struct {
    char document_path[256];
    char backup_path[256];
    uint64_t timestamp;
    int version;
} backup_info_t;

/* Autosave (temp files while working) */
void rivee_autosave_start(const char *document_path);
void rivee_autosave_save(void);
void rivee_autosave_stop(void);
bool rivee_autosave_exists(const char *document_path);
int rivee_autosave_recover(const char *document_path, char *recovered_path, int max_len);

/* Backups (versioned copies) */
int rivee_backup_create(const char *document_path);
int rivee_backup_list(const char *document_path, backup_info_t *backups, int max);
int rivee_backup_restore(const backup_info_t *backup, const char *restore_path);
void rivee_backup_cleanup(const char *document_path, int keep_versions);

/* ============ User Assets ============ */

/* User-created resources location */
const char *rivee_get_user_brushes_dir(void);
const char *rivee_get_user_palettes_dir(void);
const char *rivee_get_user_templates_dir(void);
const char *rivee_get_user_symbols_dir(void);
const char *rivee_get_user_workspaces_dir(void);

/* ============ Cloud Sync (Future) ============ */

typedef enum {
    SYNC_STATUS_OFF,
    SYNC_STATUS_SYNCING,
    SYNC_STATUS_SYNCED,
    SYNC_STATUS_ERROR
} sync_status_t;

sync_status_t rivee_sync_get_status(void);
void rivee_sync_enable(bool enable);
void rivee_sync_now(void);

/* ============ License Info ============ */

typedef enum {
    LICENSE_FREE,
    LICENSE_PRO,
    LICENSE_STUDIO
} license_type_t;

typedef struct {
    license_type_t type;
    char user_name[64];
    char user_email[128];
    char license_key[64];
    uint64_t expiry_date;
    bool is_valid;
} license_info_t;

int rivee_license_load(license_info_t *info);
int rivee_license_activate(const char *key);
bool rivee_license_is_feature_enabled(const char *feature);

#endif /* RIVEE_USERDATA_H */
