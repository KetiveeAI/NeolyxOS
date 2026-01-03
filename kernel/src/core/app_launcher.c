/*
 * NeolyxOS App Launcher Implementation
 * 
 * Launches REOX applications from kernel
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../../include/core/app_launcher.h"
/* disk_ops.h removed - disk functions called via VFS */

/* External functions */
extern void serial_puts(const char *s);

/* Static memory functions */
static void launcher_memset(void *ptr, uint8_t val, uint64_t size) {
    uint8_t *p = (uint8_t *)ptr;
    while (size--) *p++ = val;
}

/* ============ App Launcher State ============ */

static AppInfo running_apps[16];
static uint32_t app_count = 0;
static uint8_t reox_initialized = 0;

/* ============ Initialization ============ */

int app_launcher_init(void) {
    serial_puts("[LAUNCHER] App launcher initialized\n");
    app_count = 0;
    launcher_memset(running_apps, 0, sizeof(running_apps));
    return 0;
}

/* ============ REOX Runtime ============ */

int reox_runtime_init(void) {
    if (reox_initialized) {
        return 0;
    }
    
    serial_puts("[REOX] Initializing REOX runtime...\n");
    
    /* TODO: 
     * 1. Initialize REOX memory allocator
     * 2. Set up UI bridge with NXRender
     * 3. Register system calls
     * 4. Load stdlib
     */
    
    reox_initialized = 1;
    serial_puts("[REOX] Runtime ready\n");
    return 0;
}

const char *reox_version(void) {
    return "REOX 1.0.0";
}

int reox_execute(const char *path) {
    serial_puts("[REOX] Executing: ");
    serial_puts(path);
    serial_puts("\n");
    
    if (!reox_initialized) {
        reox_runtime_init();
    }
    
    /* TODO:
     * 1. Load REOX binary/bytecode from path
     * 2. Parse and validate
     * 3. Execute main()
     */
    
    return 0;
}

/* ============ App Launch ============ */

int app_launch(const char *path) {
    serial_puts("[LAUNCHER] Launching app: ");
    serial_puts(path);
    serial_puts("\n");
    
    if (app_count >= 16) {
        serial_puts("[LAUNCHER] Error: Too many apps running\n");
        return -1;
    }
    
    AppInfo *app = &running_apps[app_count];
    launcher_memset(app, 0, sizeof(AppInfo));
    
    app->app_id = app_count;
    app->type = APP_TYPE_REOX;
    app->state = APP_STATE_RUNNING;
    
    /* Copy path */
    const char *p = path;
    int i = 0;
    while (*p && i < 255) {
        app->path[i++] = *p++;
    }
    
    app_count++;
    
    /* Execute REOX app */
    return reox_execute(path);
}

int app_launch_builtin(const char *name) {
    serial_puts("[LAUNCHER] Launching builtin: ");
    serial_puts(name);
    serial_puts("\n");
    
    /* Check name and launch appropriate app */
    if (name[0] == 'd' && name[1] == 'i' && name[2] == 's' && name[3] == 'k') {
        return launch_disk_manager();
    }
    if (name[0] == 's' && name[1] == 'e' && name[2] == 't') {
        return launch_settings();
    }
    if (name[0] == 'd' && name[1] == 'e' && name[2] == 's' && name[3] == 'k') {
        return launch_desktop();
    }
    
    serial_puts("[LAUNCHER] Unknown builtin app\n");
    return -1;
}

/* ============ Built-in Apps ============ */

int launch_disk_manager(void) {
    serial_puts("[LAUNCHER] === NeolyxOS Disk Manager ===\n");
    
    /* Disk operations use VFS layer */
    serial_puts("[LAUNCHER] Disk manager: use VFS for disk access\n");
    
    /* Launch REOX UI */
    return app_launch("/apps/disk_manager/disk_manager");
}

int launch_settings(void) {
    serial_puts("[LAUNCHER] Launching System Settings...\n");
    return app_launch("/apps/settings/settings");
}

int launch_desktop(void) {
    serial_puts("[LAUNCHER] Launching Desktop Shell...\n");
    return app_launch("/apps/desktop/desktop");
}

/* ============ App Control ============ */

int app_kill(uint32_t app_id) {
    if (app_id >= app_count) {
        return -1;
    }
    
    running_apps[app_id].state = APP_STATE_STOPPED;
    serial_puts("[LAUNCHER] App killed\n");
    return 0;
}

int app_get_info(uint32_t app_id, AppInfo *info) {
    if (app_id >= app_count || !info) {
        return -1;
    }
    
    *info = running_apps[app_id];
    return 0;
}
