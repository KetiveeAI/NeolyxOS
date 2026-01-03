/*
 * NeolyxOS Configuration System API
 * 
 * Reads/writes .nlc (NeoLyx Config) files using INI-style format.
 * Used by desktop shell, Settings.app, and other components.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXCONFIG_H
#define NXCONFIG_H

#include <stdint.h>
#include <stddef.h>

/* ============ Constants ============ */

#define NXC_MAX_SECTIONS     32
#define NXC_MAX_KEYS         64
#define NXC_MAX_KEY_LEN      64
#define NXC_MAX_VALUE_LEN    256
#define NXC_MAX_LINE_LEN     512

/* Config file paths */
#define NXC_SYSTEM_CONFIG    "/System/Config/system.nlc"
#define NXC_USER_DEFAULTS    "/System/Config/user_defaults.nlc"
#define NXC_USER_PREFS       "/Users/%s/Library/Preferences/user.nlc"

/* ============ Structures ============ */

/* Key-value pair */
typedef struct {
    char key[NXC_MAX_KEY_LEN];
    char value[NXC_MAX_VALUE_LEN];
} nxc_entry_t;

/* Section with key-value pairs */
typedef struct {
    char name[NXC_MAX_KEY_LEN];
    nxc_entry_t entries[NXC_MAX_KEYS];
    int entry_count;
} nxc_section_t;

/* Config file handle */
typedef struct {
    char filepath[256];
    nxc_section_t sections[NXC_MAX_SECTIONS];
    int section_count;
    int modified;  /* 1 if changes need saving */
} nxc_config_t;

/* ============ API Functions ============ */

/**
 * Load a .nlc config file
 * @param path Path to .nlc file
 * @return Config handle or NULL on error
 */
nxc_config_t* nxc_load(const char *path);

/**
 * Save config to file
 * @param config Config handle
 * @return 0 on success, -1 on error
 */
int nxc_save(nxc_config_t *config);

/**
 * Free config handle
 * @param config Config to free
 */
void nxc_free(nxc_config_t *config);

/**
 * Get string value from config
 * @param config Config handle
 * @param section Section name (e.g., "desktop")
 * @param key Key name (e.g., "theme")
 * @param default_val Default if not found
 * @return Value string or default_val
 */
const char* nxc_get_string(nxc_config_t *config, const char *section, 
                            const char *key, const char *default_val);

/**
 * Get integer value from config
 * @param config Config handle
 * @param section Section name
 * @param key Key name
 * @param default_val Default if not found
 * @return Integer value or default_val
 */
int nxc_get_int(nxc_config_t *config, const char *section, 
                const char *key, int default_val);

/**
 * Get boolean value from config
 * @param config Config handle
 * @param section Section name
 * @param key Key name
 * @param default_val Default if not found
 * @return 1 for true, 0 for false
 */
int nxc_get_bool(nxc_config_t *config, const char *section, 
                 const char *key, int default_val);

/**
 * Get hex color value (e.g., "0x0000B4D8")
 * @param config Config handle
 * @param section Section name
 * @param key Key name
 * @param default_val Default color
 * @return 32-bit ARGB color
 */
uint32_t nxc_get_color(nxc_config_t *config, const char *section, 
                       const char *key, uint32_t default_val);

/**
 * Set string value in config
 * @param config Config handle
 * @param section Section name (created if not exists)
 * @param key Key name
 * @param value Value to set
 * @return 0 on success, -1 on error
 */
int nxc_set_string(nxc_config_t *config, const char *section, 
                   const char *key, const char *value);

/**
 * Set integer value in config
 */
int nxc_set_int(nxc_config_t *config, const char *section, 
                const char *key, int value);

/**
 * Set boolean value in config
 */
int nxc_set_bool(nxc_config_t *config, const char *section, 
                 const char *key, int value);

/**
 * Set hex color value
 */
int nxc_set_color(nxc_config_t *config, const char *section, 
                  const char *key, uint32_t color);

/* ============ Desktop Config Helpers ============ */

/* Quick access to common desktop settings */
typedef struct {
    /* Desktop */
    char theme[32];           /* "dark" or "light" */
    uint32_t accent_color;    /* ARGB color */
    char wallpaper[256];      /* Path to wallpaper */
    char dock_position[16];   /* "bottom", "left", "right" */
    int menubar_transparent;
    
    /* Dock */
    int dock_icon_size;       /* 32-128 */
    int dock_autohide;
    int dock_magnification;
    char minimize_effect[16]; /* "scale", "genie" */
    int bounce_on_launch;
    
    /* Window */
    int animations_enabled;
    char animation_speed[16]; /* "slow", "normal", "fast" */
    int snap_enabled;
    
    /* Mouse */
    int cursor_speed;
    int natural_scrolling;
    char cursor_size[16];     /* "small", "normal", "large" */
} nxc_desktop_settings_t;

/**
 * Load desktop settings from user config
 * @param settings Output settings struct
 * @return 0 on success, -1 on error
 */
int nxc_load_desktop_settings(nxc_desktop_settings_t *settings);

/**
 * Save desktop settings to user config
 * @param settings Settings to save
 * @return 0 on success, -1 on error
 */
int nxc_save_desktop_settings(const nxc_desktop_settings_t *settings);

/**
 * Apply desktop settings immediately
 * Publishes events for all components to update
 * @param settings Settings to apply
 */
void nxc_apply_desktop_settings(const nxc_desktop_settings_t *settings);

#endif /* NXCONFIG_H */
