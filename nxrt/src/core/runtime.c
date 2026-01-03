/*
 * NXRT Core Implementation
 * 
 * Runtime initialization, event loop, and system management.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nxrt/nxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

/* ============ Internal State ============ */
static int g_initialized = 0;
static int g_running = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============ Error Strings ============ */
static const char* g_error_strings[] = {
    "Success",
    "Initialization error",
    "Invalid parameter",
    "Out of memory",
    "Not found",
    "Permission denied",
    "I/O error",
    "Network error",
    "Timeout",
    "Sandbox violation",
};

/* ============ App State ============ */
#define MAX_APPS 32

typedef struct {
    int             id;
    nxrt_manifest_t manifest;
    nxrt_app_state_t state;
    
    /* Callbacks */
    nxrt_state_cb   on_state;
    void           *on_state_data;
    nxrt_error_cb   on_error;
    void           *on_error_data;
    nxrt_ready_cb   on_ready;
    void           *on_ready_data;
    
    /* Windows */
    int             window_ids[16];
    int             window_count;
    
} app_state_t;

static app_state_t g_apps[MAX_APPS];
static int g_next_app_id = 1;

/* ============ Window State ============ */
#define MAX_WINDOWS 64

typedef struct {
    int                  id;
    int                  app_id;
    nxrt_window_config_t config;
    int                  visible;
    
    /* Platform handle */
    void                *native_handle;
    
    /* Views */
    int                  view_ids[8];
    int                  view_count;
    
} window_state_t;

static window_state_t g_windows[MAX_WINDOWS];
static int g_next_window_id = 1;

/* ============ View State ============ */
#define MAX_VIEWS 128

typedef struct {
    int               id;
    int               window_id;
    nxrt_view_config_t config;
    
    /* WebView state */
    char              current_url[1024];
    int               can_go_back;
    int               can_go_forward;
    
    /* Native bindings */
    struct {
        char          name[64];
        void         (*handler)(const char*, char*, size_t);
    } bindings[32];
    int               binding_count;
    
} view_state_t;

static view_state_t g_views[MAX_VIEWS];
static int g_next_view_id = 1;

/* ============ Service State ============ */
typedef struct {
    nxrt_service_type_t type;
    int                 initialized;
    
    /* Storage */
    char                storage_path[256];
    
    /* Cloud */
    nxrt_cloud_config_t cloud_config;
    
    /* Auth */
    int                 logged_in;
    char                user_json[1024];
    
} service_state_t;

static service_state_t g_services[8];

/* ============ Internal Helpers ============ */

static app_state_t* get_app(nxrt_app_t id) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (g_apps[i].id == id) {
            return &g_apps[i];
        }
    }
    return NULL;
}

static window_state_t* get_window(nxrt_window_t id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == id) {
            return &g_windows[i];
        }
    }
    return NULL;
}

static view_state_t* get_view(nxrt_view_t id) {
    for (int i = 0; i < MAX_VIEWS; i++) {
        if (g_views[i].id == id) {
            return &g_views[i];
        }
    }
    return NULL;
}

static void set_app_state(app_state_t *app, nxrt_app_state_t state) {
    if (app->state != state) {
        app->state = state;
        if (app->on_state) {
            app->on_state(app->on_state_data, state);
        }
    }
}

/* ===========================================================================
 * Runtime API
 * =========================================================================*/

nxrt_error_t nxrt_init(void) {
    if (g_initialized) {
        return NXRT_SUCCESS;
    }
    
    printf("[NXRT] Initializing NeolyxOS Runtime v%s\n", NXRT_VERSION_STRING);
    
    /* Initialize state */
    memset(g_apps, 0, sizeof(g_apps));
    memset(g_windows, 0, sizeof(g_windows));
    memset(g_views, 0, sizeof(g_views));
    memset(g_services, 0, sizeof(g_services));
    
    /* Initialize services */
    g_services[NXRT_SERVICE_STORAGE].type = NXRT_SERVICE_STORAGE;
    g_services[NXRT_SERVICE_STORAGE].initialized = 1;
    snprintf(g_services[NXRT_SERVICE_STORAGE].storage_path, 255, 
             "%s/.nxrt/storage", getenv("HOME") ?: "/tmp");
    
    g_services[NXRT_SERVICE_NETWORK].type = NXRT_SERVICE_NETWORK;
    g_services[NXRT_SERVICE_NETWORK].initialized = 1;
    
    g_services[NXRT_SERVICE_CLOUD].type = NXRT_SERVICE_CLOUD;
    g_services[NXRT_SERVICE_AUTH].type = NXRT_SERVICE_AUTH;
    g_services[NXRT_SERVICE_NOTIFY].type = NXRT_SERVICE_NOTIFY;
    g_services[NXRT_SERVICE_IPC].type = NXRT_SERVICE_IPC;
    
    g_initialized = 1;
    printf("[NXRT] Runtime initialized\n");
    
    return NXRT_SUCCESS;
}

void nxrt_shutdown(void) {
    if (!g_initialized) return;
    
    printf("[NXRT] Shutting down\n");
    
    /* Stop all apps */
    for (int i = 0; i < MAX_APPS; i++) {
        if (g_apps[i].id != 0) {
            nxrt_app_stop(g_apps[i].id);
            nxrt_app_destroy(g_apps[i].id);
        }
    }
    
    g_initialized = 0;
    g_running = 0;
}

const char* nxrt_version(void) {
    return NXRT_VERSION_STRING;
}

const char* nxrt_error_string(nxrt_error_t error) {
    int idx = -error;
    if (idx < 0 || idx >= (int)(sizeof(g_error_strings) / sizeof(g_error_strings[0]))) {
        return "Unknown error";
    }
    return g_error_strings[idx];
}

nxrt_error_t nxrt_run(void) {
    if (!g_initialized) {
        return NXRT_ERROR_INIT;
    }
    
    printf("[NXRT] Starting event loop\n");
    g_running = 1;
    
    while (g_running) {
        /* Process events */
        /* In real implementation:
         * - Poll platform events (X11, Wayland, etc.)
         * - Process IPC messages
         * - Update views
         */
        
        /* Check if any apps still running */
        int apps_running = 0;
        for (int i = 0; i < MAX_APPS; i++) {
            if (g_apps[i].id != 0 && 
                g_apps[i].state == NXRT_STATE_RUNNING) {
                apps_running++;
            }
        }
        
        if (apps_running == 0) {
            printf("[NXRT] No apps running, exiting\n");
            break;
        }
        
        usleep(16666); /* ~60 FPS */
    }
    
    printf("[NXRT] Event loop ended\n");
    return NXRT_SUCCESS;
}

void nxrt_quit(void) {
    g_running = 0;
}

/* ===========================================================================
 * App API
 * =========================================================================*/

nxrt_app_t nxrt_app_create(const char *manifest_path) {
    printf("[NXRT] Loading app from: %s\n", manifest_path);
    
    /* In real implementation, parse JSON manifest */
    nxrt_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    
    /* Default manifest */
    strncpy(manifest.id, "com.example.app", 63);
    strncpy(manifest.name, "Example App", 127);
    strncpy(manifest.version, "1.0.0", 31);
    strncpy(manifest.entry, "index.html", 255);
    manifest.permissions = NXRT_PERM_NETWORK | NXRT_PERM_STORAGE;
    manifest.sandboxed = 1;
    
    return nxrt_app_create_ex(&manifest);
}

nxrt_app_t nxrt_app_create_ex(const nxrt_manifest_t *manifest) {
    if (!manifest) return NXRT_INVALID_HANDLE;
    
    /* Find free slot */
    app_state_t *app = NULL;
    for (int i = 0; i < MAX_APPS; i++) {
        if (g_apps[i].id == 0) {
            app = &g_apps[i];
            break;
        }
    }
    
    if (!app) {
        fprintf(stderr, "[NXRT] Max apps reached\n");
        return NXRT_INVALID_HANDLE;
    }
    
    memset(app, 0, sizeof(app_state_t));
    app->id = g_next_app_id++;
    app->manifest = *manifest;
    app->state = NXRT_STATE_CREATED;
    
    printf("[NXRT] Created app %d: %s (%s)\n", 
           app->id, manifest->name, manifest->id);
    
    return app->id;
}

void nxrt_app_destroy(nxrt_app_t app_id) {
    app_state_t *app = get_app(app_id);
    if (!app) return;
    
    /* Destroy all windows */
    for (int i = 0; i < app->window_count; i++) {
        nxrt_window_destroy(app->window_ids[i]);
    }
    
    printf("[NXRT] Destroyed app %d\n", app->id);
    app->id = 0;
}

nxrt_error_t nxrt_app_start(nxrt_app_t app_id) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] Starting app: %s\n", app->manifest.name);
    
    set_app_state(app, NXRT_STATE_STARTING);
    
    /* Create main window if needed */
    if (app->window_count == 0) {
        nxrt_window_config_t wc = {0};
        strncpy(wc.title, app->manifest.name, 255);
        wc.x = -1;  /* Center */
        wc.y = -1;
        wc.width = 1280;
        wc.height = 720;
        wc.type = NXRT_WINDOW_NORMAL;
        wc.resizable = 1;
        wc.visible = 1;
        
        nxrt_window_t win = nxrt_window_create(app_id, &wc);
        if (win == NXRT_INVALID_HANDLE) {
            return NXRT_ERROR_INIT;
        }
        
        /* Create WebView */
        nxrt_view_config_t vc = {0};
        vc.type = NXRT_VIEW_WEBVIEW;
        snprintf(vc.url, 1023, "file://%s", app->manifest.entry);
        vc.allow_scripts = 1;
        vc.context_menu = 1;
        
        nxrt_view_create(win, &vc);
    }
    
    set_app_state(app, NXRT_STATE_RUNNING);
    
    if (app->on_ready) {
        app->on_ready(app->on_ready_data);
    }
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_app_stop(nxrt_app_t app_id) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_ERROR_INVALID;
    
    set_app_state(app, NXRT_STATE_STOPPING);
    
    /* Hide all windows */
    for (int i = 0; i < app->window_count; i++) {
        nxrt_window_hide(app->window_ids[i]);
    }
    
    set_app_state(app, NXRT_STATE_STOPPED);
    
    printf("[NXRT] Stopped app: %s\n", app->manifest.name);
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_app_pause(nxrt_app_t app_id) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_ERROR_INVALID;
    
    set_app_state(app, NXRT_STATE_PAUSED);
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_app_resume(nxrt_app_t app_id) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_ERROR_INVALID;
    
    set_app_state(app, NXRT_STATE_RUNNING);
    return NXRT_SUCCESS;
}

nxrt_app_state_t nxrt_app_state(nxrt_app_t app_id) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_STATE_STOPPED;
    return app->state;
}

nxrt_error_t nxrt_app_manifest(nxrt_app_t app_id, nxrt_manifest_t *manifest) {
    app_state_t *app = get_app(app_id);
    if (!app || !manifest) return NXRT_ERROR_INVALID;
    
    *manifest = app->manifest;
    return NXRT_SUCCESS;
}

void nxrt_app_on_state(nxrt_app_t app_id, nxrt_state_cb callback, void *userdata) {
    app_state_t *app = get_app(app_id);
    if (!app) return;
    app->on_state = callback;
    app->on_state_data = userdata;
}

void nxrt_app_on_error(nxrt_app_t app_id, nxrt_error_cb callback, void *userdata) {
    app_state_t *app = get_app(app_id);
    if (!app) return;
    app->on_error = callback;
    app->on_error_data = userdata;
}

void nxrt_app_on_ready(nxrt_app_t app_id, nxrt_ready_cb callback, void *userdata) {
    app_state_t *app = get_app(app_id);
    if (!app) return;
    app->on_ready = callback;
    app->on_ready_data = userdata;
}

/* ===========================================================================
 * Window API
 * =========================================================================*/

nxrt_window_t nxrt_window_create(nxrt_app_t app_id, const nxrt_window_config_t *config) {
    app_state_t *app = get_app(app_id);
    if (!app || !config) return NXRT_INVALID_HANDLE;
    
    /* Find free slot */
    window_state_t *win = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == 0) {
            win = &g_windows[i];
            break;
        }
    }
    
    if (!win) return NXRT_INVALID_HANDLE;
    
    memset(win, 0, sizeof(window_state_t));
    win->id = g_next_window_id++;
    win->app_id = app_id;
    win->config = *config;
    
    /* Add to app's window list */
    if (app->window_count < 16) {
        app->window_ids[app->window_count++] = win->id;
    }
    
    printf("[NXRT] Created window %d: %s (%dx%d)\n",
           win->id, config->title, config->width, config->height);
    
    /* In real implementation:
     * - Create platform window (X11, Wayland, etc.)
     * - Set up OpenGL context
     */
    
    return win->id;
}

void nxrt_window_destroy(nxrt_window_t window_id) {
    window_state_t *win = get_window(window_id);
    if (!win) return;
    
    /* Destroy all views */
    for (int i = 0; i < win->view_count; i++) {
        nxrt_view_destroy(win->view_ids[i]);
    }
    
    printf("[NXRT] Destroyed window %d\n", win->id);
    win->id = 0;
}

nxrt_error_t nxrt_window_show(nxrt_window_t window_id) {
    window_state_t *win = get_window(window_id);
    if (!win) return NXRT_ERROR_INVALID;
    
    win->visible = 1;
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_window_hide(nxrt_window_t window_id) {
    window_state_t *win = get_window(window_id);
    if (!win) return NXRT_ERROR_INVALID;
    
    win->visible = 0;
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_window_set_title(nxrt_window_t window_id, const char *title) {
    window_state_t *win = get_window(window_id);
    if (!win || !title) return NXRT_ERROR_INVALID;
    
    strncpy(win->config.title, title, 255);
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_window_set_size(nxrt_window_t window_id, int32_t width, int32_t height) {
    window_state_t *win = get_window(window_id);
    if (!win) return NXRT_ERROR_INVALID;
    
    win->config.width = width;
    win->config.height = height;
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_window_set_position(nxrt_window_t window_id, int32_t x, int32_t y) {
    window_state_t *win = get_window(window_id);
    if (!win) return NXRT_ERROR_INVALID;
    
    win->config.x = x;
    win->config.y = y;
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_window_set_fullscreen(nxrt_window_t window_id, int fullscreen) {
    window_state_t *win = get_window(window_id);
    if (!win) return NXRT_ERROR_INVALID;
    
    win->config.type = fullscreen ? NXRT_WINDOW_FULLSCREEN : NXRT_WINDOW_NORMAL;
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_window_close(nxrt_window_t window_id) {
    nxrt_window_destroy(window_id);
    return NXRT_SUCCESS;
}

/* ===========================================================================
 * View API
 * =========================================================================*/

nxrt_view_t nxrt_view_create(nxrt_window_t window_id, const nxrt_view_config_t *config) {
    window_state_t *win = get_window(window_id);
    if (!win || !config) return NXRT_INVALID_HANDLE;
    
    /* Find free slot */
    view_state_t *view = NULL;
    for (int i = 0; i < MAX_VIEWS; i++) {
        if (g_views[i].id == 0) {
            view = &g_views[i];
            break;
        }
    }
    
    if (!view) return NXRT_INVALID_HANDLE;
    
    memset(view, 0, sizeof(view_state_t));
    view->id = g_next_view_id++;
    view->window_id = window_id;
    view->config = *config;
    
    if (config->url[0]) {
        strncpy(view->current_url, config->url, 1023);
    }
    
    /* Add to window's view list */
    if (win->view_count < 8) {
        win->view_ids[win->view_count++] = view->id;
    }
    
    printf("[NXRT] Created view %d in window %d (type=%d)\n",
           view->id, window_id, config->type);
    
    /* In real implementation:
     * - Create ZepraBrowser WebView
     * - Set up JS context
     * - Navigate to URL
     */
    
    return view->id;
}

void nxrt_view_destroy(nxrt_view_t view_id) {
    view_state_t *view = get_view(view_id);
    if (!view) return;
    
    printf("[NXRT] Destroyed view %d\n", view->id);
    view->id = 0;
}

nxrt_error_t nxrt_view_navigate(nxrt_view_t view_id, const char *url) {
    view_state_t *view = get_view(view_id);
    if (!view || !url) return NXRT_ERROR_INVALID;
    
    strncpy(view->current_url, url, 1023);
    printf("[NXRT] View %d navigating to: %s\n", view->id, url);
    
    /* In real implementation:
     * - Load URL in ZepraBrowser
     */
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_view_load_html(nxrt_view_t view_id, const char *html, const char *base_url) {
    view_state_t *view = get_view(view_id);
    if (!view || !html) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] View %d loading HTML (%zu bytes)\n", view->id, strlen(html));
    
    /* In real implementation:
     * - Load HTML in ZepraBrowser
     */
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_view_eval(nxrt_view_t view_id, const char *script) {
    view_state_t *view = get_view(view_id);
    if (!view || !script) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] View %d eval: %.50s...\n", view->id, script);
    
    /* In real implementation:
     * - Execute JS via ZebraScript
     */
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_view_bind(nxrt_view_t view_id, const char *name,
                             void (*handler)(const char*, char*, size_t)) {
    view_state_t *view = get_view(view_id);
    if (!view || !name || !handler) return NXRT_ERROR_INVALID;
    
    if (view->binding_count >= 32) return NXRT_ERROR_NO_MEMORY;
    
    int idx = view->binding_count++;
    strncpy(view->bindings[idx].name, name, 63);
    view->bindings[idx].handler = handler;
    
    printf("[NXRT] View %d bound function: %s\n", view->id, name);
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_view_reload(nxrt_view_t view_id) {
    view_state_t *view = get_view(view_id);
    if (!view) return NXRT_ERROR_INVALID;
    
    return nxrt_view_navigate(view_id, view->current_url);
}

nxrt_error_t nxrt_view_back(nxrt_view_t view_id) {
    view_state_t *view = get_view(view_id);
    if (!view || !view->can_go_back) return NXRT_ERROR_INVALID;
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_view_forward(nxrt_view_t view_id) {
    view_state_t *view = get_view(view_id);
    if (!view || !view->can_go_forward) return NXRT_ERROR_INVALID;
    return NXRT_SUCCESS;
}

/* ===========================================================================
 * Service API
 * =========================================================================*/

nxrt_service_t nxrt_service_get(nxrt_service_type_t type) {
    if (type < 0 || type >= 8) return NXRT_INVALID_HANDLE;
    return (nxrt_service_t)type;
}

nxrt_error_t nxrt_storage_set(nxrt_service_t svc, const char *key, 
                               const void *data, size_t size) {
    if (svc != NXRT_SERVICE_STORAGE) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] Storage set: %s (%zu bytes)\n", key, size);
    /* In real implementation: save to secure storage */
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_storage_get(nxrt_service_t svc, const char *key,
                               void *data, size_t *size) {
    if (svc != NXRT_SERVICE_STORAGE) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] Storage get: %s\n", key);
    /* In real implementation: load from secure storage */
    
    return NXRT_ERROR_NOT_FOUND;
}

nxrt_error_t nxrt_storage_delete(nxrt_service_t svc, const char *key) {
    if (svc != NXRT_SERVICE_STORAGE) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] Storage delete: %s\n", key);
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_cloud_configure(nxrt_service_t svc, const nxrt_cloud_config_t *config) {
    if (svc != NXRT_SERVICE_CLOUD || !config) return NXRT_ERROR_INVALID;
    
    g_services[NXRT_SERVICE_CLOUD].cloud_config = *config;
    g_services[NXRT_SERVICE_CLOUD].initialized = 1;
    
    printf("[NXRT] Cloud configured: %s\n", config->endpoint);
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_auth_login(nxrt_service_t svc, const char *provider) {
    if (svc != NXRT_SERVICE_AUTH) return NXRT_ERROR_INVALID;
    
    printf("[NXRT] Auth login via: %s\n", provider);
    g_services[NXRT_SERVICE_AUTH].logged_in = 1;
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_auth_logout(nxrt_service_t svc) {
    if (svc != NXRT_SERVICE_AUTH) return NXRT_ERROR_INVALID;
    
    g_services[NXRT_SERVICE_AUTH].logged_in = 0;
    return NXRT_SUCCESS;
}

int nxrt_auth_is_logged_in(nxrt_service_t svc) {
    if (svc != NXRT_SERVICE_AUTH) return 0;
    return g_services[NXRT_SERVICE_AUTH].logged_in;
}

/* ===========================================================================
 * Hardware API
 * =========================================================================*/

nxrt_error_t nxrt_system_info(nxrt_system_info_t *info) {
    if (!info) return NXRT_ERROR_INVALID;
    
    memset(info, 0, sizeof(nxrt_system_info_t));
    
#ifdef __linux__
    struct utsname un;
    if (uname(&un) == 0) {
        strncpy(info->os_name, un.sysname, 63);
        strncpy(info->os_version, un.release, 31);
        strncpy(info->device_name, un.nodename, 127);
    }
    
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info->memory_total = si.totalram * si.mem_unit;
        info->memory_available = si.freeram * si.mem_unit;
    }
    
    info->cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
#else
    strncpy(info->os_name, "NeolyxOS", 63);
    strncpy(info->os_version, "1.0.0", 31);
#endif
    
    return NXRT_SUCCESS;
}

int nxrt_display_count(void) {
    return 1;  /* In real implementation: query platform */
}

nxrt_error_t nxrt_display_info(int index, nxrt_display_info_t *info) {
    if (index < 0 || index >= nxrt_display_count() || !info) {
        return NXRT_ERROR_INVALID;
    }
    
    /* Default values */
    info->width = 1920;
    info->height = 1080;
    info->scale = 1.0f;
    info->refresh_rate = 60.0f;
    
    return NXRT_SUCCESS;
}

/* ===========================================================================
 * Permission API
 * =========================================================================*/

int nxrt_permission_check(nxrt_app_t app_id, nxrt_permission_t perm) {
    app_state_t *app = get_app(app_id);
    if (!app) return 0;
    
    return (app->manifest.permissions & perm) != 0;
}

nxrt_error_t nxrt_permission_request(nxrt_app_t app_id, nxrt_permission_t perm) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_ERROR_INVALID;
    
    /* In real implementation: show permission dialog */
    app->manifest.permissions |= perm;
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_permission_revoke(nxrt_app_t app_id, nxrt_permission_t perm) {
    app_state_t *app = get_app(app_id);
    if (!app) return NXRT_ERROR_INVALID;
    
    app->manifest.permissions &= ~perm;
    return NXRT_SUCCESS;
}
