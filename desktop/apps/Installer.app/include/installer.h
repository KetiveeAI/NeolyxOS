/*
 * NeolyxOS Installer - Header
 * Native application installer for NeolyxOS
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#ifndef INSTALLER_H
#define INSTALLER_H

#include <stdint.h>
#include <stdbool.h>

#define INSTALLER_VERSION "1.0.0"

#define MAX_PERMISSIONS 16
#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 64

/* Install steps */
typedef enum {
    STEP_WELCOME = 0,
    STEP_PERMISSIONS,
    STEP_LOCATION,
    STEP_PROGRESS,
    STEP_COMPLETE,
    STEP_COUNT
} install_step_t;

/* Permission flags */
typedef enum {
    PERM_FILESYSTEM_READ  = (1 << 0),
    PERM_FILESYSTEM_WRITE = (1 << 1),
    PERM_NETWORK          = (1 << 2),
    PERM_CAMERA           = (1 << 3),
    PERM_MICROPHONE       = (1 << 4),
    PERM_LOCATION         = (1 << 5),
    PERM_BLUETOOTH        = (1 << 6),
    PERM_SYSTEM_REGISTRY  = (1 << 7)
} permission_t;

/* App manifest structure */
typedef struct {
    char name[MAX_NAME_LEN];
    char version[16];
    char bundle_id[MAX_NAME_LEN];
    char category[32];
    char description[256];
    char author[MAX_NAME_LEN];
    char binary[MAX_PATH_LEN];
    char icon[MAX_PATH_LEN];
    uint32_t permissions;
    uint64_t size_bytes;
} app_manifest_t;

/* Package info */
typedef struct {
    char path[MAX_PATH_LEN];
    app_manifest_t manifest;
    bool is_valid;
    int file_count;
    uint64_t total_size;
} package_t;

/* Installer state */
typedef struct {
    install_step_t current_step;
    package_t* package;
    char install_path[MAX_PATH_LEN];
    int install_progress;
    bool add_to_dock;
    bool create_shortcut;
    bool install_success;
    char status_message[256];
} installer_state_t;

/* Package functions */
package_t* package_open(const char* path);
void package_close(package_t* pkg);
bool package_extract(package_t* pkg, const char* dest_path, 
                     void (*progress_cb)(int percent, const char* file));

/* Manifest functions */
bool manifest_parse(const char* json, app_manifest_t* manifest);
const char* permission_to_string(uint32_t perm);

/* Install functions */
bool install_app(package_t* pkg, const char* install_path);
bool register_app(const app_manifest_t* manifest, const char* install_path);
bool unregister_app(const char* bundle_id);
bool add_to_dock(const char* app_path);
bool create_desktop_shortcut(const char* app_path, const char* name);

/* Registry functions */
bool registry_init(void);
bool registry_add_app(const app_manifest_t* manifest, const char* path);
bool registry_remove_app(const char* bundle_id);
bool registry_app_exists(const char* bundle_id);

#endif /* INSTALLER_H */
