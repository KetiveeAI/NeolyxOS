/*
 * NeolyxOS First Boot Wizard
 * 
 * Runs once after installation to configure user account, language, and settings.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_FIRST_BOOT_H
#define NEOLYX_FIRST_BOOT_H

#include <stdint.h>

/* ============ First Boot States ============ */

typedef enum {
    FIRST_BOOT_LANGUAGE,        /* Language/Region selection */
    FIRST_BOOT_HOSTNAME,        /* Computer name configuration */
    FIRST_BOOT_CREATE_USER,     /* Create admin user account */
    FIRST_BOOT_PASSWORD,        /* Set user password */
    FIRST_BOOT_APPEARANCE,      /* Light/Dark theme selection */
    FIRST_BOOT_COMPLETE,        /* Setup complete */
} first_boot_state_t;

/* ============ User Account Info ============ */

typedef struct {
    char username[64];
    char display_name[128];
    char password_hash[128];    /* SHA-256 hash */
    int is_admin;               /* 1 = administrator */
    int auto_login;             /* 1 = skip login screen */
} user_account_t;

/* ============ System Configuration ============ */

typedef struct {
    char hostname[64];
    char language[8];           /* e.g., "en_US" */
    char timezone[64];          /* e.g., "America/New_York" */
    int dark_mode;              /* 0 = light, 1 = dark */
    int reduce_motion;          /* 0 = normal, 1 = reduced */
} system_prefs_t;

/* ============ First Boot API ============ */

/* Check if first boot setup is needed */
int first_boot_needed(void);

/* Run the first boot wizard */
int first_boot_run_wizard(void);

/* Save user account to /Users/{username}/ */
int first_boot_create_user(user_account_t *user);

/* Save system preferences to /etc/neolyx.conf */
int first_boot_save_prefs(system_prefs_t *prefs);

/* Mark first boot as complete */
int first_boot_mark_complete(void);

#endif /* NEOLYX_FIRST_BOOT_H */
