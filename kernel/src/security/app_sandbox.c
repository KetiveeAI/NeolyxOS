/*
 * NeolyxOS App Sandbox Implementation
 * 
 * Application installation security and execution sandboxing.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "security/app_sandbox.h"

/* External dependencies */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/* App registry */
#define MAX_APPS 64
static app_info_t apps[MAX_APPS];
static int app_count = 0;
static uint32_t next_app_id = 1;

/* Sandbox pool */
#define MAX_SANDBOXES 32
static sandbox_t sandboxes[MAX_SANDBOXES];
static int sandbox_count = 0;
static uint32_t next_sandbox_id = 1;

/* ============ String Helpers ============ */

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ============ SHA-256 Stub ============ */

static void sha256(const uint8_t *data, uint64_t len, uint8_t *hash) {
    /* Simplified hash for now - real implementation needed */
    uint64_t h = 0x5BE0CD19137E2179ULL;
    for (uint64_t i = 0; i < len; i++) {
        h = h * 31 + data[i];
    }
    for (int i = 0; i < 32; i++) {
        hash[i] = (h >> (i * 2)) & 0xFF;
    }
}

/* ============ Initialization ============ */

void app_sandbox_init(void) {
    serial_puts("[SANDBOX] Initializing app sandbox system...\n");
    
    app_count = 0;
    next_app_id = 1;
    sandbox_count = 0;
    next_sandbox_id = 1;
    
    /* Clear registries */
    for (int i = 0; i < MAX_APPS; i++) {
        apps[i].app_id = 0;
        apps[i].installed = 0;
    }
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        sandboxes[i].sandbox_id = 0;
        sandboxes[i].active = 0;
    }
    
    serial_puts("[SANDBOX] Initialized\n");
}

/* ============ App Management ============ */

int app_register(app_info_t *app) {
    if (!app) return -1;
    if (app_count >= MAX_APPS) return SANDBOX_ERR_LIMIT;
    
    /* Find slot */
    int slot = -1;
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return SANDBOX_ERR_LIMIT;
    
    /* Copy app info */
    apps[slot] = *app;
    apps[slot].app_id = next_app_id++;
    apps[slot].installed = 1;
    apps[slot].install_time = pit_get_ticks();
    apps[slot].run_count = 0;
    
    app_count++;
    
    serial_puts("[SANDBOX] App registered: ");
    serial_puts(app->name);
    serial_puts("\n");
    
    return apps[slot].app_id;
}

int app_unregister(uint32_t app_id) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == app_id) {
            apps[i].app_id = 0;
            apps[i].installed = 0;
            app_count--;
            serial_puts("[SANDBOX] App unregistered\n");
            return 0;
        }
    }
    return -1;
}

int sandbox_app_get_info(uint32_t app_id, app_info_t *out) {
    if (!out) return -1;
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == app_id) {
            *out = apps[i];
            return 0;
        }
    }
    return -1;
}

/* ============ Signature Verification ============ */

int app_compute_hash(const char *path, uint8_t *hash_out) {
    if (!path || !hash_out) return -1;
    
    /* TODO: Read file and compute real hash */
    /* For now, hash the path itself */
    sha256((const uint8_t *)path, 256, hash_out);
    
    return 0;
}

int app_verify_signature(const char *path, const uint8_t *expected_sig) {
    if (!path || !expected_sig) return SANDBOX_ERR_VERIFY;
    
    uint8_t computed_hash[32];
    app_compute_hash(path, computed_hash);
    
    /* Compare hashes */
    for (int i = 0; i < 32; i++) {
        if (computed_hash[i] != expected_sig[i]) {
            serial_puts("[SANDBOX] Signature verification FAILED\n");
            return SANDBOX_ERR_VERIFY;
        }
    }
    
    serial_puts("[SANDBOX] Signature verified\n");
    return SANDBOX_OK;
}

/* ============ Installation ============ */

int app_install(const char *path, uint32_t trust_level) {
    if (!path) return -1;
    
    app_info_t app;
    for (int i = 0; i < 256; i++) app.path[i] = 0;
    str_copy(app.path, path, 256);
    
    /* Extract name from path */
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') name = p + 1;
    }
    str_copy(app.name, name, 64);
    
    app.trust_level = trust_level;
    app.sandboxed = (trust_level < APP_TRUST_SYSTEM);
    
    /* Default capabilities based on trust */
    if (trust_level >= APP_TRUST_SYSTEM) {
        app.capabilities = 0xFFFFFFFF;  /* All caps */
    } else if (trust_level >= APP_TRUST_VERIFIED) {
        app.capabilities = APP_CAP_NETWORK | APP_CAP_FILESYSTEM;
    } else {
        app.capabilities = 0;  /* No caps by default */
    }
    
    /* Resource limits */
    app.max_memory = 256 * 1024 * 1024;  /* 256MB */
    app.max_cpu_percent = 50;
    app.max_network_kbps = 1024;  /* 1MB/s */
    
    return app_register(&app);
}

int app_uninstall(uint32_t app_id) {
    return app_unregister(app_id);
}

int app_is_installed(uint32_t app_id) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == app_id && apps[i].installed) {
            return 1;
        }
    }
    return 0;
}

/* ============ Sandboxing ============ */

int sandbox_create(uint32_t app_id, sandbox_t *out) {
    if (!out) return -1;
    if (sandbox_count >= MAX_SANDBOXES) return SANDBOX_ERR_LIMIT;
    
    /* Find app */
    app_info_t *app = NULL;
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == app_id) {
            app = &apps[i];
            break;
        }
    }
    if (!app) return -1;
    
    /* Find sandbox slot */
    int slot = -1;
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (sandboxes[i].sandbox_id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return SANDBOX_ERR_LIMIT;
    
    /* Create sandbox */
    sandboxes[slot].sandbox_id = next_sandbox_id++;
    sandboxes[slot].app_id = app_id;
    sandboxes[slot].pid = 0;  /* Set when process starts */
    sandboxes[slot].capabilities = app->capabilities;
    sandboxes[slot].memory_used = 0;
    sandboxes[slot].cpu_used = 0;
    sandboxes[slot].network_used = 0;
    sandboxes[slot].active = 1;
    
    /* Set up isolated root path */
    str_copy(sandboxes[slot].root_path, "/sandbox/", 256);
    
    sandbox_count++;
    *out = sandboxes[slot];
    
    serial_puts("[SANDBOX] Created sandbox for: ");
    serial_puts(app->name);
    serial_puts("\n");
    
    return sandboxes[slot].sandbox_id;
}

int sandbox_destroy(uint32_t sandbox_id) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (sandboxes[i].sandbox_id == sandbox_id) {
            sandboxes[i].sandbox_id = 0;
            sandboxes[i].active = 0;
            sandbox_count--;
            serial_puts("[SANDBOX] Sandbox destroyed\n");
            return 0;
        }
    }
    return -1;
}

int sandbox_check_capability(uint32_t sandbox_id, uint32_t cap) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (sandboxes[i].sandbox_id == sandbox_id) {
            return (sandboxes[i].capabilities & cap) ? 1 : 0;
        }
    }
    return 0;
}

int sandbox_grant_capability(uint32_t sandbox_id, uint32_t cap) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (sandboxes[i].sandbox_id == sandbox_id) {
            sandboxes[i].capabilities |= cap;
            return 0;
        }
    }
    return -1;
}

int sandbox_revoke_capability(uint32_t sandbox_id, uint32_t cap) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (sandboxes[i].sandbox_id == sandbox_id) {
            sandboxes[i].capabilities &= ~cap;
            return 0;
        }
    }
    return -1;
}

/* ============ Resource Limits ============ */

int sandbox_set_memory_limit(uint32_t sandbox_id, uint64_t bytes) {
    (void)sandbox_id; (void)bytes;
    return 0;  /* TODO: Implement */
}

int sandbox_set_cpu_limit(uint32_t sandbox_id, uint32_t percent) {
    (void)sandbox_id; (void)percent;
    return 0;
}

int sandbox_set_network_limit(uint32_t sandbox_id, uint32_t kbps) {
    (void)sandbox_id; (void)kbps;
    return 0;
}

/* ============ Trust Management ============ */

int app_set_trust_level(uint32_t app_id, uint8_t level) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == app_id) {
            apps[i].trust_level = level;
            return 0;
        }
    }
    return -1;
}

int app_is_trusted(uint32_t app_id) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].app_id == app_id) {
            return apps[i].trust_level >= APP_TRUST_VERIFIED;
        }
    }
    return 0;
}
