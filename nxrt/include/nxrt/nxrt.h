/*
 * NXRT - NeolyxOS Application Runtime
 * 
 * Native runtime for building apps that:
 * - Use ZepraBrowser rendering components
 * - Run without server dependency
 * - Access NeolyxOS cloud services
 * - Deploy to NeolyxOS App Store
 * 
 * Features:
 * - Native UI rendering (WebView + Native widgets)
 * - JavaScript execution via ZebraScript
 * - IPC for app-to-system communication
 * - Secure storage and sandboxing
 * - Hardware access APIs
 * - Cloud service integration
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXRT_H
#define NXRT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ============ Version ============ */
#define NXRT_VERSION_MAJOR   1
#define NXRT_VERSION_MINOR   0
#define NXRT_VERSION_PATCH   0
#define NXRT_VERSION_STRING  "1.0.0"

/* ============ Handle Types ============ */
typedef int32_t nxrt_app_t;
typedef int32_t nxrt_window_t;
typedef int32_t nxrt_view_t;
typedef int32_t nxrt_service_t;
typedef int32_t nxrt_channel_t;

#define NXRT_INVALID_HANDLE (-1)

/* ============ Error Codes ============ */
typedef enum {
    NXRT_SUCCESS            = 0,
    NXRT_ERROR_INIT         = -1,
    NXRT_ERROR_INVALID      = -2,
    NXRT_ERROR_NO_MEMORY    = -3,
    NXRT_ERROR_NOT_FOUND    = -4,
    NXRT_ERROR_PERMISSION   = -5,
    NXRT_ERROR_IO           = -6,
    NXRT_ERROR_NETWORK      = -7,
    NXRT_ERROR_TIMEOUT      = -8,
    NXRT_ERROR_SANDBOX      = -9,
} nxrt_error_t;

/* ============ App State ============ */
typedef enum {
    NXRT_STATE_CREATED      = 0,
    NXRT_STATE_STARTING     = 1,
    NXRT_STATE_RUNNING      = 2,
    NXRT_STATE_PAUSED       = 3,
    NXRT_STATE_STOPPING     = 4,
    NXRT_STATE_STOPPED      = 5,
} nxrt_app_state_t;

/* ============ Window Types ============ */
typedef enum {
    NXRT_WINDOW_NORMAL      = 0,
    NXRT_WINDOW_FRAMELESS   = 1,
    NXRT_WINDOW_FULLSCREEN  = 2,
    NXRT_WINDOW_OVERLAY     = 3,
    NXRT_WINDOW_POPUP       = 4,
} nxrt_window_type_t;

/* ============ View Types ============ */
typedef enum {
    NXRT_VIEW_WEBVIEW       = 0,  /* ZepraBrowser WebView */
    NXRT_VIEW_NATIVE        = 1,  /* Native UI widgets */
    NXRT_VIEW_CANVAS        = 2,  /* OpenGL canvas */
    NXRT_VIEW_VIDEO         = 3,  /* Video player view */
} nxrt_view_type_t;

/* ============ Service Types ============ */
typedef enum {
    NXRT_SERVICE_STORAGE    = 0,  /* Secure storage */
    NXRT_SERVICE_NETWORK    = 1,  /* Network requests */
    NXRT_SERVICE_CLOUD      = 2,  /* NeolyxOS Cloud */
    NXRT_SERVICE_AUTH       = 3,  /* Authentication */
    NXRT_SERVICE_NOTIFY     = 4,  /* Notifications */
    NXRT_SERVICE_IPC        = 5,  /* Inter-process comm */
} nxrt_service_type_t;

/* ============ Permission Flags ============ */
typedef enum {
    NXRT_PERM_NONE          = 0,
    NXRT_PERM_NETWORK       = (1 << 0),
    NXRT_PERM_STORAGE       = (1 << 1),
    NXRT_PERM_CAMERA        = (1 << 2),
    NXRT_PERM_MICROPHONE    = (1 << 3),
    NXRT_PERM_LOCATION      = (1 << 4),
    NXRT_PERM_NOTIFICATIONS = (1 << 5),
    NXRT_PERM_BLUETOOTH     = (1 << 6),
    NXRT_PERM_USB           = (1 << 7),
    NXRT_PERM_FILESYSTEM    = (1 << 8),
    NXRT_PERM_ALL           = 0xFFFF,
} nxrt_permission_t;

/* ============ App Manifest ============ */
typedef struct {
    char            id[64];         /* Unique app ID (reverse domain) */
    char            name[128];      /* Display name */
    char            version[32];    /* Semantic version */
    char            author[128];    /* Author/company */
    char            description[512];
    char            icon[256];      /* Path to icon */
    char            entry[256];     /* Entry point (index.html or main.js) */
    uint32_t        permissions;    /* Permission flags */
    uint8_t         sandboxed;      /* Run in sandbox */
    uint8_t         single_instance;/* Only one instance allowed */
    uint8_t         auto_start;     /* Start on system boot */
    uint8_t         background;     /* Can run in background */
} nxrt_manifest_t;

/* ============ Window Config ============ */
typedef struct {
    char            title[256];
    int32_t         x, y;           /* Position (-1 = center) */
    int32_t         width, height;
    int32_t         min_width, min_height;
    int32_t         max_width, max_height;
    nxrt_window_type_t type;
    uint8_t         resizable;
    uint8_t         visible;
    uint8_t         always_on_top;
    uint8_t         transparent;    /* Transparent background */
} nxrt_window_config_t;

/* ============ View Config ============ */
typedef struct {
    nxrt_view_type_t type;
    char            url[1024];      /* For WebView: initial URL */
    uint8_t         dev_tools;      /* Enable dev tools */
    uint8_t         context_menu;   /* Enable right-click menu */
    uint8_t         allow_scripts;  /* Allow JavaScript */
} nxrt_view_config_t;

/* ============ Cloud Config ============ */
typedef struct {
    char            api_key[128];
    char            endpoint[256];
    char            region[32];
    uint8_t         use_tls;
    uint32_t        timeout_ms;
} nxrt_cloud_config_t;

/* ============ IPC Message ============ */
typedef struct {
    char            channel[64];
    char            event[64];
    void           *data;
    size_t          data_size;
    int32_t         reply_id;
} nxrt_message_t;

/* ============ Callbacks ============ */
typedef void (*nxrt_state_cb)(void *userdata, nxrt_app_state_t state);
typedef void (*nxrt_message_cb)(void *userdata, const nxrt_message_t *msg);
typedef void (*nxrt_error_cb)(void *userdata, nxrt_error_t error, const char *msg);
typedef void (*nxrt_ready_cb)(void *userdata);

/* ===========================================================================
 * Runtime API
 * =========================================================================*/

/**
 * Initialize NXRT runtime
 * Must be called once before using any other functions
 */
nxrt_error_t nxrt_init(void);

/**
 * Shutdown runtime
 * Stops all apps and releases resources
 */
void nxrt_shutdown(void);

/**
 * Get version string
 */
const char* nxrt_version(void);

/**
 * Get error description
 */
const char* nxrt_error_string(nxrt_error_t error);

/**
 * Run main event loop
 * Blocks until all apps exit
 */
nxrt_error_t nxrt_run(void);

/**
 * Exit event loop
 */
void nxrt_quit(void);

/* ===========================================================================
 * App API
 * =========================================================================*/

/**
 * Create app from manifest file
 */
nxrt_app_t nxrt_app_create(const char *manifest_path);

/**
 * Create app from manifest struct
 */
nxrt_app_t nxrt_app_create_ex(const nxrt_manifest_t *manifest);

/**
 * Destroy app
 */
void nxrt_app_destroy(nxrt_app_t app);

/**
 * Start app
 */
nxrt_error_t nxrt_app_start(nxrt_app_t app);

/**
 * Stop app
 */
nxrt_error_t nxrt_app_stop(nxrt_app_t app);

/**
 * Pause app (move to background)
 */
nxrt_error_t nxrt_app_pause(nxrt_app_t app);

/**
 * Resume app (bring to foreground)
 */
nxrt_error_t nxrt_app_resume(nxrt_app_t app);

/**
 * Get app state
 */
nxrt_app_state_t nxrt_app_state(nxrt_app_t app);

/**
 * Get app manifest
 */
nxrt_error_t nxrt_app_manifest(nxrt_app_t app, nxrt_manifest_t *manifest);

/**
 * Set app callbacks
 */
void nxrt_app_on_state(nxrt_app_t app, nxrt_state_cb callback, void *userdata);
void nxrt_app_on_error(nxrt_app_t app, nxrt_error_cb callback, void *userdata);
void nxrt_app_on_ready(nxrt_app_t app, nxrt_ready_cb callback, void *userdata);

/* ===========================================================================
 * Window API
 * =========================================================================*/

/**
 * Create window for app
 */
nxrt_window_t nxrt_window_create(nxrt_app_t app, const nxrt_window_config_t *config);

/**
 * Destroy window
 */
void nxrt_window_destroy(nxrt_window_t window);

/**
 * Show/hide window
 */
nxrt_error_t nxrt_window_show(nxrt_window_t window);
nxrt_error_t nxrt_window_hide(nxrt_window_t window);

/**
 * Window properties
 */
nxrt_error_t nxrt_window_set_title(nxrt_window_t window, const char *title);
nxrt_error_t nxrt_window_set_size(nxrt_window_t window, int32_t width, int32_t height);
nxrt_error_t nxrt_window_set_position(nxrt_window_t window, int32_t x, int32_t y);
nxrt_error_t nxrt_window_set_fullscreen(nxrt_window_t window, int fullscreen);

/**
 * Close window
 */
nxrt_error_t nxrt_window_close(nxrt_window_t window);

/* ===========================================================================
 * View API (WebView / Native UI)
 * =========================================================================*/

/**
 * Create view in window
 */
nxrt_view_t nxrt_view_create(nxrt_window_t window, const nxrt_view_config_t *config);

/**
 * Destroy view
 */
void nxrt_view_destroy(nxrt_view_t view);

/**
 * Navigate to URL (WebView)
 */
nxrt_error_t nxrt_view_navigate(nxrt_view_t view, const char *url);

/**
 * Load HTML content
 */
nxrt_error_t nxrt_view_load_html(nxrt_view_t view, const char *html, const char *base_url);

/**
 * Execute JavaScript
 */
nxrt_error_t nxrt_view_eval(nxrt_view_t view, const char *script);

/**
 * Inject native function into JS context
 */
nxrt_error_t nxrt_view_bind(nxrt_view_t view, const char *name,
                             void (*handler)(const char *args, char *result, size_t result_size));

/**
 * Reload view
 */
nxrt_error_t nxrt_view_reload(nxrt_view_t view);

/**
 * Go back/forward
 */
nxrt_error_t nxrt_view_back(nxrt_view_t view);
nxrt_error_t nxrt_view_forward(nxrt_view_t view);

/* ===========================================================================
 * IPC API (App-to-System communication)
 * =========================================================================*/

/**
 * Create IPC channel
 */
nxrt_channel_t nxrt_ipc_create(const char *name);

/**
 * Destroy IPC channel
 */
void nxrt_ipc_destroy(nxrt_channel_t channel);

/**
 * Send message
 */
nxrt_error_t nxrt_ipc_send(nxrt_channel_t channel, const nxrt_message_t *msg);

/**
 * Send message and wait for reply
 */
nxrt_error_t nxrt_ipc_invoke(nxrt_channel_t channel, const nxrt_message_t *msg,
                              nxrt_message_t *reply, uint32_t timeout_ms);

/**
 * Set message handler
 */
void nxrt_ipc_on_message(nxrt_channel_t channel, nxrt_message_cb callback, void *userdata);

/* ===========================================================================
 * Service API (Cloud, Storage, Auth)
 * =========================================================================*/

/**
 * Get service
 */
nxrt_service_t nxrt_service_get(nxrt_service_type_t type);

/**
 * Storage service
 */
nxrt_error_t nxrt_storage_set(nxrt_service_t svc, const char *key, const void *data, size_t size);
nxrt_error_t nxrt_storage_get(nxrt_service_t svc, const char *key, void *data, size_t *size);
nxrt_error_t nxrt_storage_delete(nxrt_service_t svc, const char *key);

/**
 * Cloud service
 */
nxrt_error_t nxrt_cloud_configure(nxrt_service_t svc, const nxrt_cloud_config_t *config);
nxrt_error_t nxrt_cloud_request(nxrt_service_t svc, const char *method, const char *path,
                                 const void *body, size_t body_size,
                                 void *response, size_t *response_size);

/**
 * Auth service
 */
nxrt_error_t nxrt_auth_login(nxrt_service_t svc, const char *provider);
nxrt_error_t nxrt_auth_logout(nxrt_service_t svc);
nxrt_error_t nxrt_auth_get_user(nxrt_service_t svc, char *user_json, size_t size);
int nxrt_auth_is_logged_in(nxrt_service_t svc);

/**
 * Notification service
 */
nxrt_error_t nxrt_notify_show(nxrt_service_t svc, const char *title, const char *body,
                               const char *icon);
nxrt_error_t nxrt_notify_request_permission(nxrt_service_t svc);

/* ===========================================================================
 * Permission API
 * =========================================================================*/

/**
 * Check if permission is granted
 */
int nxrt_permission_check(nxrt_app_t app, nxrt_permission_t perm);

/**
 * Request permission
 */
nxrt_error_t nxrt_permission_request(nxrt_app_t app, nxrt_permission_t perm);

/**
 * Revoke permission
 */
nxrt_error_t nxrt_permission_revoke(nxrt_app_t app, nxrt_permission_t perm);

/* ===========================================================================
 * Hardware API
 * =========================================================================*/

/**
 * Get system info
 */
typedef struct {
    char    os_name[64];
    char    os_version[32];
    char    device_name[128];
    char    cpu_model[128];
    uint32_t cpu_cores;
    uint64_t memory_total;
    uint64_t memory_available;
    char    gpu_name[128];
} nxrt_system_info_t;

nxrt_error_t nxrt_system_info(nxrt_system_info_t *info);

/**
 * Get display info
 */
typedef struct {
    int32_t width;
    int32_t height;
    float   scale;       /* DPI scale factor */
    float   refresh_rate;
} nxrt_display_info_t;

nxrt_error_t nxrt_display_info(int index, nxrt_display_info_t *info);
int nxrt_display_count(void);

#ifdef __cplusplus
}
#endif

#endif /* NXRT_H */
