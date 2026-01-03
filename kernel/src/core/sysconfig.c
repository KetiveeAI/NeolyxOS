/*
 * NeolyxOS System Configuration Implementation
 * 
 * Profile management and feature control.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/sysconfig.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Global Config ============ */

sysconfig_t system_config;

/* ============ Helpers ============ */

static void cfg_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ Configuration API ============ */

void sysconfig_init(void) {
    serial_puts("[CONFIG] Initializing system configuration...\n");
    
    /* Default to Desktop profile */
    system_config.profile = SYSPROFILE_DESKTOP;
    system_config.features = PROFILE_DESKTOP_FEATURES;
    
    system_config.wifi_enabled = 1;
    system_config.lan_enabled = 1;
    cfg_strcpy(system_config.hostname, "neolyx", 64);
    
    system_config.gui_enabled = 1;
    system_config.gpu_enabled = 1;  /* Always on for AI */
    system_config.screen_width = 1920;
    system_config.screen_height = 1080;
    
    system_config.browser_installed = 1;
    system_config.reolab_installed = 1;
    system_config.filemanager_installed = 1;
    
    system_config.first_boot = 1;
    cfg_strcpy(system_config.language, "en_US", 32);
    cfg_strcpy(system_config.timezone, "UTC", 64);
    
    serial_puts("[CONFIG] Initialized with Desktop profile\n");
}

void sysconfig_apply_profile(sysprofile_t profile) {
    serial_puts("[CONFIG] Applying profile: ");
    
    system_config.profile = profile;
    
    if (profile == SYSPROFILE_DESKTOP) {
        serial_puts("Desktop\n");
        system_config.features = PROFILE_DESKTOP_FEATURES;
        system_config.gui_enabled = 1;
        system_config.wifi_enabled = 1;
        system_config.browser_installed = 1;
        system_config.reolab_installed = 1;
        system_config.filemanager_installed = 1;
    } else {
        serial_puts("Server\n");
        system_config.features = PROFILE_SERVER_FEATURES;
        system_config.gui_enabled = 0;
        system_config.wifi_enabled = 0;  /* Disabled by default in Server */
        system_config.browser_installed = 0;
        system_config.reolab_installed = 0;
        system_config.filemanager_installed = 0;
    }
    
    /* GPU always enabled (for AI) */
    system_config.gpu_enabled = 1;
}

int sysconfig_has_feature(uint32_t feature) {
    return (system_config.features & feature) != 0;
}

void sysconfig_set_feature(uint32_t feature, int enabled) {
    if (enabled) {
        system_config.features |= feature;
    } else {
        system_config.features &= ~feature;
    }
}

int sysconfig_load(void) {
    serial_puts("[CONFIG] Loading /etc/neolyx.conf...\n");
    /* TODO: Read from NXFS */
    return 0;
}

int sysconfig_save(void) {
    serial_puts("[CONFIG] Saving /etc/neolyx.conf...\n");
    /* TODO: Write to NXFS */
    return 0;
}

/* ============ Profile Upgrade/Downgrade ============ */

int sysconfig_upgrade_to_desktop(void) {
    serial_puts("[CONFIG] Upgrading to Desktop mode...\n");
    serial_puts("[CONFIG] This is an in-place upgrade.\n");
    serial_puts("[CONFIG] All data will be preserved.\n");
    
    if (system_config.profile == SYSPROFILE_DESKTOP) {
        serial_puts("[CONFIG] Already in Desktop mode.\n");
        return 0;
    }
    
    /* Apply desktop profile */
    sysconfig_apply_profile(SYSPROFILE_DESKTOP);
    
    /* Install desktop apps */
    serial_puts("[CONFIG] Installing desktop applications...\n");
    for (int i = 0; DESKTOP_APPS[i] != NULL; i++) {
        serial_puts("  - ");
        serial_puts(DESKTOP_APPS[i]);
        serial_puts("\n");
        /* TODO: Copy app files */
    }
    
    /* Start desktop services */
    serial_puts("[CONFIG] Enabling desktop services...\n");
    for (int i = 0; DESKTOP_SERVICES[i] != NULL; i++) {
        serial_puts("  - ");
        serial_puts(DESKTOP_SERVICES[i]);
        serial_puts("\n");
        /* TODO: Enable service */
    }
    
    /* Save config */
    sysconfig_save();
    
    serial_puts("[CONFIG] Upgrade complete. Reboot to enter Desktop mode.\n");
    return 0;
}

int sysconfig_downgrade_to_server(void) {
    serial_puts("[CONFIG] Downgrading to Server mode...\n");
    serial_puts("[CONFIG] GUI apps will be removed. Data preserved.\n");
    
    if (system_config.profile == SYSPROFILE_SERVER) {
        serial_puts("[CONFIG] Already in Server mode.\n");
        return 0;
    }
    
    /* Apply server profile */
    sysconfig_apply_profile(SYSPROFILE_SERVER);
    
    /* Disable desktop services */
    serial_puts("[CONFIG] Disabling desktop services...\n");
    for (int i = 0; DESKTOP_SERVICES[i] != NULL; i++) {
        /* TODO: Disable service */
    }
    
    /* Save config */
    sysconfig_save();
    
    serial_puts("[CONFIG] Downgrade complete. Reboot to enter Server mode.\n");
    return 0;
}

/* ============ Feature Status ============ */

void sysconfig_print_status(void) {
    serial_puts("\n=== NeolyxOS System Configuration ===\n");
    
    serial_puts("Profile: ");
    serial_puts((system_config.profile == SYSPROFILE_DESKTOP) ? "Desktop" : "Server");
    serial_puts("\n");
    
    serial_puts("Hostname: ");
    serial_puts(system_config.hostname);
    serial_puts("\n");
    
    serial_puts("\nFeatures:\n");
    serial_puts("  GUI:     ");
    serial_puts(sysconfig_has_feature(FEATURE_GUI) ? "Yes" : "No");
    serial_puts("\n");
    
    serial_puts("  WiFi:    ");
    serial_puts(sysconfig_has_feature(FEATURE_WIFI) ? "Yes" : "No");
    serial_puts("\n");
    
    serial_puts("  LAN:     ");
    serial_puts(sysconfig_has_feature(FEATURE_LAN) ? "Yes" : "No");
    serial_puts("\n");
    
    serial_puts("  GPU:     ");
    serial_puts(sysconfig_has_feature(FEATURE_GPU) ? "Yes (AI Ready)" : "No");
    serial_puts("\n");
    
    serial_puts("  Browser: ");
    serial_puts(sysconfig_has_feature(FEATURE_BROWSER) ? "Installed" : "Not installed");
    serial_puts("\n");
    
    serial_puts("  ReoLab:  ");
    serial_puts(sysconfig_has_feature(FEATURE_REOLAB) ? "Installed" : "Not installed");
    serial_puts("\n");
    
    serial_puts("=========================================\n\n");
}
