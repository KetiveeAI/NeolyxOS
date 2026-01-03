/*
 * NeolyxOS User Authentication System Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/user_auth.h"
#include <stddef.h>

/* External dependencies */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/* ============ Static State ============ */

static user_account_t g_users[MAX_USERS];
static user_session_t g_current_session;
static int g_user_count = 0;
static int g_initialized = 0;

/* Simple SHA-256 placeholder - in production use real crypto */
static void hash_password(const char *password, uint8_t *hash_out) {
    /* Simple hash for now - replace with real SHA-256 */
    for (int i = 0; i < PASSWORD_HASH_LEN; i++) {
        hash_out[i] = 0;
    }
    const char *p = password;
    int i = 0;
    while (*p && i < PASSWORD_HASH_LEN) {
        hash_out[i] ^= *p;
        hash_out[(i + 7) % PASSWORD_HASH_LEN] += *p;
        p++;
        i++;
    }
    /* Add some mixing */
    for (int j = 0; j < PASSWORD_HASH_LEN; j++) {
        hash_out[j] = (hash_out[j] * 31 + j) & 0xFF;
    }
}

static int compare_hash(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < PASSWORD_HASH_LEN; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ============ Public API ============ */

int user_auth_init(void) {
    if (g_initialized) return 0;
    
    serial_puts("[AUTH] Initializing user authentication subsystem...\n");
    
    /* Clear user database */
    for (int i = 0; i < MAX_USERS; i++) {
        g_users[i].uid = 0;
        g_users[i].status = USER_STATUS_INACTIVE;
    }
    
    /* Clear current session */
    g_current_session.session_id = 0;
    g_current_session.uid = 0;
    g_current_session.locked = 0;
    
    /* Create default root user (uid 0) */
    user_account_t *root = &g_users[0];
    root->uid = 0;
    root->gid = 0;
    str_copy(root->username, "root", MAX_USERNAME_LEN);
    str_copy(root->fullname, "System Administrator", MAX_FULLNAME_LEN);
    str_copy(root->home_dir, "/System", MAX_HOME_PATH_LEN);
    str_copy(root->shell, "/bin/nxsh", 64);
    root->privilege = USER_PRIV_ROOT;
    root->status = USER_STATUS_ACTIVE;
    root->created_time = pit_get_ticks();
    root->last_login = 0;
    root->login_count = 0;
    root->failed_attempts = 0;
    /* Root has no password by default */
    for (int i = 0; i < PASSWORD_HASH_LEN; i++) {
        root->password_hash[i] = 0;
    }
    g_user_count = 1;
    
    g_initialized = 1;
    serial_puts("[AUTH] Ready\n");
    return 0;
}

int user_create(const char *username, const char *password,
                const char *fullname, user_privilege_t privilege) {
    if (!g_initialized) return -1;
    if (!username || !password) return -2;
    
    /* Check if username already exists */
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].status != USER_STATUS_INACTIVE) {
            if (str_equal(g_users[i].username, username)) {
                serial_puts("[AUTH] User already exists: ");
                serial_puts(username);
                serial_puts("\n");
                return -3;
            }
        }
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].status == USER_STATUS_INACTIVE) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        serial_puts("[AUTH] Max users reached\n");
        return -4;
    }
    
    user_account_t *user = &g_users[slot];
    user->uid = slot + 1000;  /* User IDs start at 1000 */
    user->gid = slot + 1000;
    str_copy(user->username, username, MAX_USERNAME_LEN);
    str_copy(user->fullname, fullname ? fullname : username, MAX_FULLNAME_LEN);
    
    /* Build home directory path */
    char home[MAX_HOME_PATH_LEN];
    str_copy(home, "/Users/", MAX_HOME_PATH_LEN);
    int len = 7;
    const char *u = username;
    while (*u && len < MAX_HOME_PATH_LEN - 1) {
        home[len++] = *u++;
    }
    home[len] = '\0';
    str_copy(user->home_dir, home, MAX_HOME_PATH_LEN);
    
    str_copy(user->shell, "/bin/nxsh", 64);
    user->privilege = privilege;
    user->status = USER_STATUS_ACTIVE;
    user->created_time = pit_get_ticks();
    user->last_login = 0;
    user->login_count = 0;
    user->failed_attempts = 0;
    
    hash_password(password, user->password_hash);
    
    g_user_count++;
    
    serial_puts("[AUTH] Created user: ");
    serial_puts(username);
    serial_puts("\n");
    
    return user->uid;
}

int user_delete(uint32_t uid) {
    if (!g_initialized) return -1;
    if (uid == 0) {
        serial_puts("[AUTH] Cannot delete root user\n");
        return -2;
    }
    
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].uid == uid && g_users[i].status != USER_STATUS_INACTIVE) {
            serial_puts("[AUTH] Deleted user: ");
            serial_puts(g_users[i].username);
            serial_puts("\n");
            g_users[i].status = USER_STATUS_INACTIVE;
            g_user_count--;
            return 0;
        }
    }
    return -3;  /* User not found */
}

int user_login(const char *username, const char *password) {
    if (!g_initialized) return -1;
    if (!username) return -2;
    
    /* Find user */
    user_account_t *user = NULL;
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].status == USER_STATUS_ACTIVE) {
            if (str_equal(g_users[i].username, username)) {
                user = &g_users[i];
                break;
            }
        }
    }
    
    if (!user) {
        serial_puts("[AUTH] User not found: ");
        serial_puts(username);
        serial_puts("\n");
        return -3;
    }
    
    if (user->status == USER_STATUS_LOCKED) {
        serial_puts("[AUTH] Account locked: ");
        serial_puts(username);
        serial_puts("\n");
        return -4;
    }
    
    /* Verify password */
    uint8_t hash[PASSWORD_HASH_LEN];
    hash_password(password ? password : "", hash);
    
    /* Check if no password set (first login) */
    int empty_hash = 1;
    for (int i = 0; i < PASSWORD_HASH_LEN; i++) {
        if (user->password_hash[i] != 0) {
            empty_hash = 0;
            break;
        }
    }
    
    if (!empty_hash && !compare_hash(hash, user->password_hash)) {
        user->failed_attempts++;
        if (user->failed_attempts >= 5) {
            user->status = USER_STATUS_LOCKED;
            serial_puts("[AUTH] Account locked due to failed attempts\n");
        }
        serial_puts("[AUTH] Invalid password for: ");
        serial_puts(username);
        serial_puts("\n");
        return -5;
    }
    
    /* Login successful */
    user->failed_attempts = 0;
    user->login_count++;
    user->last_login = pit_get_ticks();
    
    /* Create session */
    g_current_session.session_id = pit_get_ticks() & 0xFFFFFFFF;
    g_current_session.uid = user->uid;
    g_current_session.login_time = pit_get_ticks();
    g_current_session.last_activity = g_current_session.login_time;
    g_current_session.locked = 0;
    
    serial_puts("[AUTH] Login successful: ");
    serial_puts(username);
    serial_puts("\n");
    
    return 0;
}

int user_logout(uint32_t session_id) {
    if (g_current_session.session_id == session_id || session_id == 0) {
        serial_puts("[AUTH] Session ended\n");
        g_current_session.session_id = 0;
        g_current_session.uid = 0;
        g_current_session.locked = 0;
        return 0;
    }
    return -1;
}

user_account_t *user_get_current(void) {
    if (g_current_session.session_id == 0) return NULL;
    return user_get_by_uid(g_current_session.uid);
}

user_account_t *user_get_by_uid(uint32_t uid) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].uid == uid && g_users[i].status != USER_STATUS_INACTIVE) {
            return &g_users[i];
        }
    }
    return NULL;
}

user_account_t *user_get_by_name(const char *username) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].status != USER_STATUS_INACTIVE) {
            if (str_equal(g_users[i].username, username)) {
                return &g_users[i];
            }
        }
    }
    return NULL;
}

int user_change_password(uint32_t uid, const char *old_pass, const char *new_pass) {
    user_account_t *user = user_get_by_uid(uid);
    if (!user) return -1;
    
    /* Verify old password */
    uint8_t hash[PASSWORD_HASH_LEN];
    hash_password(old_pass ? old_pass : "", hash);
    
    /* Check if password was set */
    int empty_hash = 1;
    for (int i = 0; i < PASSWORD_HASH_LEN; i++) {
        if (user->password_hash[i] != 0) {
            empty_hash = 0;
            break;
        }
    }
    
    if (!empty_hash && !compare_hash(hash, user->password_hash)) {
        return -2;  /* Old password incorrect */
    }
    
    /* Set new password */
    hash_password(new_pass, user->password_hash);
    
    serial_puts("[AUTH] Password changed for: ");
    serial_puts(user->username);
    serial_puts("\n");
    
    return 0;
}

user_session_t *session_get_current(void) {
    if (g_current_session.session_id == 0) return NULL;
    return &g_current_session;
}

int session_lock(void) {
    if (g_current_session.session_id == 0) return -1;
    g_current_session.locked = 1;
    serial_puts("[AUTH] Session locked\n");
    return 0;
}

int session_unlock(const char *password) {
    if (g_current_session.session_id == 0) return -1;
    if (!g_current_session.locked) return 0;  /* Already unlocked */
    
    user_account_t *user = user_get_by_uid(g_current_session.uid);
    if (!user) return -2;
    
    /* Verify password */
    uint8_t hash[PASSWORD_HASH_LEN];
    hash_password(password ? password : "", hash);
    
    if (!compare_hash(hash, user->password_hash)) {
        serial_puts("[AUTH] Unlock failed - wrong password\n");
        return -3;
    }
    
    g_current_session.locked = 0;
    g_current_session.last_activity = pit_get_ticks();
    serial_puts("[AUTH] Session unlocked\n");
    return 0;
}

int session_is_locked(void) {
    return g_current_session.locked;
}

int user_get_count(void) {
    return g_user_count;
}

int user_enumerate(user_account_t *buffer, int max_count) {
    int count = 0;
    for (int i = 0; i < MAX_USERS && count < max_count; i++) {
        if (g_users[i].status != USER_STATUS_INACTIVE) {
            if (buffer) {
                buffer[count] = g_users[i];
            }
            count++;
        }
    }
    return count;
}
