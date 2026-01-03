/*
 * NeolyxOS Kernel App Launcher
 * 
 * Integration layer to launch REOX applications from kernel
 * Loads and runs NeolyxOS Disk Manager during installation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#include <stdint.h>

/* Application types */
#define APP_TYPE_REOX       1
#define APP_TYPE_NATIVE     2

/* Application state */
#define APP_STATE_STOPPED   0
#define APP_STATE_RUNNING   1
#define APP_STATE_PAUSED    2

/* Application info */
typedef struct {
    uint32_t app_id;
    uint8_t  type;
    uint8_t  state;
    char     name[64];
    char     path[256];
    uint64_t entry_point;
    void    *runtime_context;
} AppInfo;

/* ============ App Launcher API ============ */

/* Initialize app launcher */
int app_launcher_init(void);

/* Launch an application by path */
int app_launch(const char *path);

/* Launch built-in application by name */
int app_launch_builtin(const char *name);

/* Kill running application */
int app_kill(uint32_t app_id);

/* Get running app info */
int app_get_info(uint32_t app_id, AppInfo *info);

/* ============ Built-in Apps ============ */

/* Launch NeolyxOS Disk Manager */
int launch_disk_manager(void);

/* Launch System Settings */
int launch_settings(void);

/* Launch Desktop Shell */
int launch_desktop(void);

/* ============ REOX Runtime ============ */

/* Initialize REOX runtime */
int reox_runtime_init(void);

/* Execute REOX bytecode/binary */
int reox_execute(const char *path);

/* Get REOX runtime version */
const char *reox_version(void);

#endif /* APP_LAUNCHER_H */
