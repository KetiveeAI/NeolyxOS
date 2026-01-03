/*
 * NeolyxOS User Authentication System
 * 
 * Handles user accounts, login, sessions, and authentication.
 * Based on XNU security concepts.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef _NEOLYX_USER_AUTH_H
#define _NEOLYX_USER_AUTH_H

#include <stdint.h>

/* ============ User Account Types ============ */

#define MAX_USERS           64
#define MAX_USERNAME_LEN    32
#define MAX_PASSWORD_LEN    64
#define MAX_FULLNAME_LEN    64
#define MAX_HOME_PATH_LEN   128
#define PASSWORD_HASH_LEN   32

/* User privilege levels */
typedef enum {
    USER_PRIV_GUEST      = 0,   /* Guest account (limited) */
    USER_PRIV_STANDARD   = 1,   /* Standard user */
    USER_PRIV_ADMIN      = 2,   /* Administrator */
    USER_PRIV_ROOT       = 3    /* Root/System */
} user_privilege_t;

/* User account status */
typedef enum {
    USER_STATUS_INACTIVE = 0,
    USER_STATUS_ACTIVE   = 1,
    USER_STATUS_LOCKED   = 2,
    USER_STATUS_DISABLED = 3
} user_status_t;

/* User account structure */
typedef struct {
    uint32_t uid;                           /* User ID */
    uint32_t gid;                           /* Primary group ID */
    char username[MAX_USERNAME_LEN];        /* Login name */
    char fullname[MAX_FULLNAME_LEN];        /* Display name */
    char home_dir[MAX_HOME_PATH_LEN];       /* Home directory */
    uint8_t password_hash[PASSWORD_HASH_LEN]; /* SHA-256 hash of password */
    user_privilege_t privilege;             /* Privilege level */
    user_status_t status;                   /* Account status */
    uint64_t created_time;                  /* Account creation time */
    uint64_t last_login;                    /* Last login time */
    uint32_t login_count;                   /* Number of logins */
    uint32_t failed_attempts;               /* Failed login attempts */
    char shell[64];                         /* Default shell */
} user_account_t;

/* Session structure */
typedef struct {
    uint32_t session_id;                    /* Session ID */
    uint32_t uid;                           /* User ID */
    uint64_t login_time;                    /* Session start time */
    uint64_t last_activity;                 /* Last activity time */
    int locked;                             /* Screen lock status */
    char token[64];                         /* Session token */
} user_session_t;

/* ============ Authentication Functions ============ */

/* Initialize user authentication subsystem */
int user_auth_init(void);

/* Create a new user account */
int user_create(const char *username, const char *password, 
                const char *fullname, user_privilege_t privilege);

/* Delete a user account */
int user_delete(uint32_t uid);

/* Authenticate user with password */
int user_login(const char *username, const char *password);

/* Logout current user session */
int user_logout(uint32_t session_id);

/* Get current user */
user_account_t *user_get_current(void);

/* Get user by UID */
user_account_t *user_get_by_uid(uint32_t uid);

/* Get user by username */
user_account_t *user_get_by_name(const char *username);

/* Change user password */
int user_change_password(uint32_t uid, const char *old_pass, const char *new_pass);

/* Get current session */
user_session_t *session_get_current(void);

/* Lock screen */
int session_lock(void);

/* Unlock screen with password */
int session_unlock(const char *password);

/* Check if screen is locked */
int session_is_locked(void);

/* Get number of registered users */
int user_get_count(void);

/* Enumerate users */
int user_enumerate(user_account_t *buffer, int max_count);

#endif /* _NEOLYX_USER_AUTH_H */
