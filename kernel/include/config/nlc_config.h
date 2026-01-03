/*
 * NeolyxOS NLC Configuration Parser/Writer
 * 
 * Parses and writes .nlc (NeoLyx Config) files
 * Format: INI-style with [sections] and key = "value"
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NLC_CONFIG_H
#define NLC_CONFIG_H

#include <stdint.h>

/* ============ Constants ============ */

#define NLC_MAX_SECTIONS     32
#define NLC_MAX_KEYS         64
#define NLC_MAX_KEY_LEN      64
#define NLC_MAX_VALUE_LEN    256
#define NLC_MAX_SECTION_LEN  32

/* ============ System Configuration Constants ============ */

/* These are read from system.nlc at boot */
#define NEOLYX_OS_NAME       "NeolyxOS"
#define NEOLYX_VERSION       "1.0.0"
#define NEOLYX_CODENAME      "Alpha"
#define NEOLYX_ARCH          "x86_64"

/* ============ Data Structures ============ */

/* Single key-value pair */
typedef struct {
    char key[NLC_MAX_KEY_LEN];
    char value[NLC_MAX_VALUE_LEN];
} nlc_entry_t;

/* Section with key-value pairs */
typedef struct {
    char name[NLC_MAX_SECTION_LEN];
    nlc_entry_t entries[NLC_MAX_KEYS];
    int entry_count;
} nlc_section_t;

/* Full config file */
typedef struct {
    char filepath[256];
    nlc_section_t sections[NLC_MAX_SECTIONS];
    int section_count;
    int loaded;
} nlc_config_t;

/* ============ Global System Config ============ */

/* System configuration (from system.nlc) */
extern nlc_config_t g_system_config;

/* Boot configuration (from boot.nlc) */
extern nlc_config_t g_boot_config;

/* ============ Functions ============ */

/* Initialize config system */
int nlc_init(void);

/* Load config from file */
int nlc_load(nlc_config_t *config, const char *path);

/* Save config to file */
int nlc_save(nlc_config_t *config);

/* Get value from config */
const char* nlc_get(nlc_config_t *config, const char *section, const char *key);

/* Set value in config */
int nlc_set(nlc_config_t *config, const char *section, const char *key, const char *value);

/* Get integer value */
int nlc_get_int(nlc_config_t *config, const char *section, const char *key, int default_val);

/* Get boolean value */
int nlc_get_bool(nlc_config_t *config, const char *section, const char *key, int default_val);

/* ============ Convenience Functions ============ */

/* Get OS name from system config */
const char* nlc_get_os_name(void);

/* Get OS version from system config */
const char* nlc_get_os_version(void);

/* Get OS codename from system config */
const char* nlc_get_os_codename(void);

/* Get full version string (e.g., "NeolyxOS 1.0.0 Alpha") */
void nlc_get_full_version(char *buffer, int max_len);

/* Check if OS is installed */
int nlc_is_installed(void);

/* Get boot disk device */
const char* nlc_get_boot_device(void);

/* Get edition (desktop/server) */
const char* nlc_get_edition(void);

#endif /* NLC_CONFIG_H */
