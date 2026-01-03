/*
 * NeolyxOS Boot Mode Manager
 * 
 * Handles system boot mode selection:
 *   - Desktop Mode (default): Full GUI desktop environment
 *   - Server Mode: Shell-only for servers
 * 
 * After installation, mode is saved in /etc/neolyx.conf
 * At boot, reads mode and starts appropriate environment.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_BOOT_MODE_H
#define NEOLYX_BOOT_MODE_H

#include <stdint.h>

/* ============ Boot Modes ============ */

typedef enum {
    BOOT_MODE_DESKTOP = 0,      /* Full desktop environment (default) */
    BOOT_MODE_SERVER = 1,       /* Shell only, no GUI */
    BOOT_MODE_RECOVERY = 2,     /* Recovery mode */
    BOOT_MODE_INSTALLER = 3,    /* First-time installation */
} boot_mode_t;

/* ============ Boot Mode API ============ */

/**
 * Get current boot mode.
 */
boot_mode_t boot_get_mode(void);

/**
 * Set boot mode for next reboot.
 */
int boot_set_mode(boot_mode_t mode);

/**
 * Show boot mode selection screen.
 * Returns selected mode.
 */
boot_mode_t boot_mode_selector(void);

/**
 * Initialize boot mode manager.
 * Reads configuration or shows installer.
 */
void boot_mode_init(void);

/**
 * Start the appropriate environment based on mode.
 */
void boot_start_environment(void);

/* ============ Installer ============ */

/**
 * Run first-time installation wizard.
 * Shows mode selection and configures system.
 */
void installer_run(void);

/**
 * Check if installation is needed.
 */
int installer_needed(void);

#endif /* NEOLYX_BOOT_MODE_H */
