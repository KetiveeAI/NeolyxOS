/*
 * NeolyxOS App Sandbox Header
 * 
 * Application installation security and sandboxing.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_APP_SANDBOX_H
#define NEOLYX_APP_SANDBOX_H

#include <stdint.h>

/* App capabilities */
#define APP_CAP_NETWORK     (1 << 0)   /* Network access */
#define APP_CAP_FILESYSTEM  (1 << 1)   /* Full filesystem access */
#define APP_CAP_CAMERA      (1 << 2)   /* Camera access */
#define APP_CAP_MICROPHONE  (1 << 3)   /* Microphone access */
#define APP_CAP_LOCATION    (1 << 4)   /* Location services */
#define APP_CAP_CONTACTS    (1 << 5)   /* Contacts access */
#define APP_CAP_SYSTEM      (1 << 6)   /* System settings */
#define APP_CAP_BACKGROUND  (1 << 7)   /* Background execution */
#define APP_CAP_USB         (1 << 8)   /* USB device access */
#define APP_CAP_BLUETOOTH   (1 << 9)   /* Bluetooth access */

/* App trust levels */
#define APP_TRUST_UNKNOWN   0
#define APP_TRUST_USER      1
#define APP_TRUST_VERIFIED  2
#define APP_TRUST_SYSTEM    3

/* Sandbox status */
#define SANDBOX_OK          0
#define SANDBOX_ERR_PERM    -1
#define SANDBOX_ERR_VERIFY  -2
#define SANDBOX_ERR_LIMIT   -3

/* App info structure */
typedef struct {
    uint32_t app_id;
    char     name[64];
    char     path[256];
    uint32_t capabilities;
    uint8_t  trust_level;
    uint8_t  sandboxed;
    uint8_t  installed;
    uint8_t  running;
    
    /* Signature info */
    uint8_t  signature[32];    /* SHA-256 */
    uint8_t  signed_by[64];
    
    /* Resource limits */
    uint64_t max_memory;
    uint32_t max_cpu_percent;
    uint32_t max_network_kbps;
    
    /* Statistics */
    uint64_t install_time;
    uint64_t last_run_time;
    uint32_t run_count;
} app_info_t;

/* Sandbox context */
typedef struct {
    uint32_t sandbox_id;
    uint32_t app_id;
    uint32_t pid;
    uint32_t capabilities;
    
    /* Resource tracking */
    uint64_t memory_used;
    uint32_t cpu_used;
    uint32_t network_used;
    
    /* Isolation */
    char     root_path[256];
    uint8_t  active;
} sandbox_t;

/* API Functions */
void app_sandbox_init(void);

/* App management */
int  app_register(app_info_t *app);
int  app_unregister(uint32_t app_id);
int  sandbox_app_get_info(uint32_t app_id, app_info_t *out);

/* Signature verification */
int  app_verify_signature(const char *path, const uint8_t *expected_sig);
int  app_compute_hash(const char *path, uint8_t *hash_out);

/* Installation */
int  app_install(const char *path, uint32_t trust_level);
int  app_uninstall(uint32_t app_id);
int  app_is_installed(uint32_t app_id);

/* Sandboxing */
int  sandbox_create(uint32_t app_id, sandbox_t *out);
int  sandbox_destroy(uint32_t sandbox_id);
int  sandbox_check_capability(uint32_t sandbox_id, uint32_t cap);
int  sandbox_grant_capability(uint32_t sandbox_id, uint32_t cap);
int  sandbox_revoke_capability(uint32_t sandbox_id, uint32_t cap);

/* Resource limits */
int  sandbox_set_memory_limit(uint32_t sandbox_id, uint64_t bytes);
int  sandbox_set_cpu_limit(uint32_t sandbox_id, uint32_t percent);
int  sandbox_set_network_limit(uint32_t sandbox_id, uint32_t kbps);

/* Trust management */
int  app_set_trust_level(uint32_t app_id, uint8_t level);
int  app_is_trusted(uint32_t app_id);

#endif /* NEOLYX_APP_SANDBOX_H */
