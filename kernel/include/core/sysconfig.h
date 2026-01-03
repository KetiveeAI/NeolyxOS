/*
 * NeolyxOS System Configuration
 * 
 * Defines system profiles and their feature sets.
 * 
 * Profile differences:
 * 
 * DESKTOP MODE:
 *   - Full graphical desktop environment
 *   - All apps (ReoLab, browser, file manager, etc.)
 *   - WiFi enabled
 *   - GPU accelerated
 *   - Mouse + keyboard + touch
 * 
 * SERVER MODE:
 *   - Shell/terminal only (no desktop at boot)
 *   - Essential apps only (for OS to boot)
 *   - LAN networking (WiFi disabled by default)
 *   - No ReoLab IDE, no browser
 *   - GPU still enabled (for AI workloads)
 *   - Keyboard only (mouse optional/legacy)
 *   - Can upgrade to Desktop via recovery menu
 *     (in-place, all data preserved)
 * 
 * BOTH MODES:
 *   - GPU enabled (modern AI requires it)
 *   - Graphics drivers auto-installed
 *   - Recovery partition available
 *   - Core system services
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_SYSCONFIG_H
#define NEOLYX_SYSCONFIG_H

#include <stdint.h>

/* ============ System Profile ============ */

typedef enum {
    SYSPROFILE_DESKTOP = 0,
    SYSPROFILE_SERVER = 1,
} sysprofile_t;

/* ============ Feature Flags ============ */

#define FEATURE_GUI             0x0001  /* Desktop environment */
#define FEATURE_WIFI            0x0002  /* WiFi networking */
#define FEATURE_LAN             0x0004  /* Wired networking */
#define FEATURE_GPU             0x0008  /* GPU acceleration */
#define FEATURE_AUDIO           0x0010  /* Audio subsystem */
#define FEATURE_MOUSE           0x0020  /* Mouse input */
#define FEATURE_TOUCH           0x0040  /* Touch input */
#define FEATURE_BROWSER         0x0100  /* Web browser */
#define FEATURE_REOLAB          0x0200  /* ReoLab IDE */
#define FEATURE_FILEMANAGER     0x0400  /* File manager */
#define FEATURE_TERMINAL        0x0800  /* Terminal app */
#define FEATURE_SETTINGS        0x1000  /* Settings app */

/* Profile feature sets */
#define PROFILE_DESKTOP_FEATURES \
    (FEATURE_GUI | FEATURE_WIFI | FEATURE_LAN | FEATURE_GPU | \
     FEATURE_AUDIO | FEATURE_MOUSE | FEATURE_TOUCH | \
     FEATURE_BROWSER | FEATURE_REOLAB | FEATURE_FILEMANAGER | \
     FEATURE_TERMINAL | FEATURE_SETTINGS)

#define PROFILE_SERVER_FEATURES \
    (FEATURE_LAN | FEATURE_GPU | FEATURE_TERMINAL)
    /* No GUI, no WiFi, no browser, no IDE, mouse optional */

/* ============ System Configuration ============ */

typedef struct {
    sysprofile_t profile;
    uint32_t features;          /* FEATURE_* flags */
    
    /* Networking */
    int wifi_enabled;
    int lan_enabled;
    char hostname[64];
    
    /* Display */
    int gui_enabled;
    int gpu_enabled;            /* Always on for AI */
    uint32_t screen_width;
    uint32_t screen_height;
    
    /* Apps */
    int browser_installed;
    int reolab_installed;
    int filemanager_installed;
    
    /* System */
    int first_boot;
    char language[32];
    char timezone[64];
} sysconfig_t;

/* ============ Global Config ============ */

extern sysconfig_t system_config;

/* ============ Configuration API ============ */

/**
 * Initialize system configuration.
 */
void sysconfig_init(void);

/**
 * Load config from /etc/neolyx.conf
 */
int sysconfig_load(void);

/**
 * Save config to /etc/neolyx.conf
 */
int sysconfig_save(void);

/**
 * Apply profile settings.
 */
void sysconfig_apply_profile(sysprofile_t profile);

/**
 * Check if feature is enabled.
 */
int sysconfig_has_feature(uint32_t feature);

/**
 * Enable/disable a feature.
 */
void sysconfig_set_feature(uint32_t feature, int enabled);

/* ============ Profile Upgrade ============ */

/**
 * Upgrade from Server to Desktop mode.
 * In-place upgrade, all data preserved.
 * Called from recovery menu.
 */
int sysconfig_upgrade_to_desktop(void);

/**
 * Downgrade from Desktop to Server mode.
 * Removes GUI apps but keeps data.
 */
int sysconfig_downgrade_to_server(void);

/* ============ Apps per Profile ============ */

/* Apps installed in Desktop mode */
static const char *DESKTOP_APPS[] = {
    "/apps/terminal",
    "/apps/files",
    "/apps/settings",
    "/apps/browser",
    "/apps/reolab",
    "/apps/text-editor",
    "/apps/image-viewer",
    "/apps/system-monitor",
    NULL
};

/* Apps installed in Server mode (minimal) */
static const char *SERVER_APPS[] = {
    "/apps/terminal",
    "/apps/settings",       /* CLI version */
    "/apps/system-monitor", /* CLI version */
    NULL
};

/* ============ Services per Profile ============ */

/* Services that start in Desktop mode */
static const char *DESKTOP_SERVICES[] = {
    "display-manager",
    "audio-service",
    "network-manager",
    "bluetooth-service",
    NULL
};

/* Services that start in Server mode */
static const char *SERVER_SERVICES[] = {
    "network-manager",      /* LAN only */
    "ssh-server",
    NULL
};

#endif /* NEOLYX_SYSCONFIG_H */
