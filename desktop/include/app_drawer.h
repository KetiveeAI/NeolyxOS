/*
 * NeolyxOS App Drawer
 * 
 * Android-style app drawer/launcher showing all installed applications
 * in a grid view. Opens with NX key press.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_APP_DRAWER_H
#define NEOLYX_APP_DRAWER_H

#include <stdint.h>

/* ============ App Drawer Constants ============ */

#define APP_DRAWER_COLS         4       /* Apps per row */
#define APP_DRAWER_ROWS         5       /* Visible rows */
#define APP_DRAWER_ICON_SIZE    64      /* Icon size in pixels */
#define APP_DRAWER_ICON_GAP     20      /* Gap between icons */
#define APP_DRAWER_LABEL_HEIGHT 24      /* Height for app name */
#define APP_DRAWER_BG_COLOR     0xE0101018  /* Semi-transparent dark */
#define APP_DRAWER_SEARCH_HEIGHT 48     /* Search bar height */
#define APP_DRAWER_PAGE_DOTS    8       /* Max page indicators */

#define MAX_APPS                64      /* Maximum apps in drawer */
#define APP_NAME_MAX            32      /* Max app name length */
#define APP_PATH_MAX            128     /* Max app path length */

/* ============ App Entry Structure ============ */

typedef struct {
    char name[APP_NAME_MAX];        /* Display name */
    char path[APP_PATH_MAX];        /* Path to .app bundle */
    char icon_path[APP_PATH_MAX];   /* Path to icon (NXI format) */
    uint32_t category;              /* App category */
    uint8_t pinned;                 /* Pinned to dock */
    uint8_t recent;                 /* Recently used */
    uint8_t visible;                /* Visible in drawer */
} app_entry_t;

/* App categories */
typedef enum {
    APP_CAT_ALL = 0,
    APP_CAT_SYSTEM,         /* Settings, Path, Terminal */
    APP_CAT_PRODUCTIVITY,   /* Office, Notes */
    APP_CAT_MEDIA,          /* RoleCut, Rivee, Music */
    APP_CAT_UTILITIES,      /* Calculator, Clock, Calendar */
    APP_CAT_DEVELOPMENT,    /* IDE, Debugger */
    APP_CAT_GAMES,          /* Games */
    APP_CAT_COUNT
} app_category_t;

/* Category names */
static const char* APP_CAT_NAMES[] = {
    "All Apps",
    "System",
    "Productivity",
    "Media",
    "Utilities",
    "Development",
    "Games"
};

/* ============ App Drawer State ============ */

typedef struct {
    /* Apps list */
    app_entry_t apps[MAX_APPS];
    uint32_t app_count;
    
    /* Filtered list (for search/category) */
    app_entry_t *filtered[MAX_APPS];
    uint32_t filtered_count;
    
    /* UI State */
    uint8_t visible;                /* Drawer is open */
    uint8_t animating;              /* Slide animation active */
    float animation_progress;       /* 0.0 = closed, 1.0 = open */
    
    /* Current view */
    uint32_t current_page;          /* Current page in grid */
    uint32_t total_pages;           /* Total pages */
    app_category_t current_category; /* Active category filter */
    
    /* Search */
    char search_query[64];          /* Current search text */
    uint8_t search_active;          /* Search field focused */
    
    /* Selection */
    int32_t selected_index;         /* -1 = none, else index in filtered */
    int32_t hover_index;            /* Mouse hover */
    
    /* Dimensions (calculated on init) */
    int32_t drawer_x, drawer_y;
    uint32_t drawer_width, drawer_height;
    int32_t grid_x, grid_y;
    
} app_drawer_state_t;

/* ============ App Drawer API ============ */

/**
 * app_drawer_init - Initialize the app drawer
 * 
 * @screen_width: Screen width for centering
 * @screen_height: Screen height for sizing
 * 
 * Returns: 0 on success, -1 on error
 */
int app_drawer_init(uint32_t screen_width, uint32_t screen_height);

/**
 * app_drawer_scan_apps - Scan for installed applications
 * 
 * Searches /Applications and /System/Applications for .app bundles
 * 
 * Returns: Number of apps found
 */
int app_drawer_scan_apps(void);

/**
 * app_drawer_register_app - Manually register an application
 * 
 * @name: Display name
 * @path: Path to .app bundle
 * @icon_path: Path to icon file
 * @category: App category
 * 
 * Returns: 0 on success, -1 if full
 */
int app_drawer_register_app(const char *name, const char *path, 
                            const char *icon_path, app_category_t category);

/**
 * app_drawer_open - Open the app drawer with animation
 */
void app_drawer_open(void);

/**
 * app_drawer_close - Close the app drawer with animation
 */
void app_drawer_close(void);

/**
 * app_drawer_toggle - Toggle drawer open/closed (for NX key)
 */
void app_drawer_toggle(void);

/**
 * app_drawer_is_open - Check if drawer is visible
 */
int app_drawer_is_open(void);

/**
 * app_drawer_render - Render the app drawer
 * 
 * Call each frame when drawer is visible or animating.
 */
void app_drawer_render(void);

/**
 * app_drawer_update - Update drawer animations
 * 
 * @delta_ms: Time since last update in milliseconds
 */
void app_drawer_update(uint32_t delta_ms);

/**
 * app_drawer_handle_mouse - Handle mouse input
 * 
 * @x, y: Mouse position
 * @buttons: Button state
 * 
 * Returns: 1 if event consumed, 0 if should pass through
 */
int app_drawer_handle_mouse(int32_t x, int32_t y, uint8_t buttons);

/**
 * app_drawer_handle_key - Handle keyboard input
 * 
 * @scancode: Key scancode
 * @pressed: 1 if pressed, 0 if released
 * 
 * Returns: 1 if event consumed, 0 if should pass through
 */
int app_drawer_handle_key(uint8_t scancode, uint8_t pressed);

/**
 * app_drawer_set_category - Filter by category
 * 
 * @category: Category to filter (APP_CAT_ALL for all)
 */
void app_drawer_set_category(app_category_t category);

/**
 * app_drawer_search - Filter by search query
 * 
 * @query: Search text (empty string to clear)
 */
void app_drawer_search(const char *query);

/**
 * app_drawer_launch_app - Launch the selected app
 * 
 * @index: Index in filtered list
 * 
 * Returns: 0 on success, -1 on error
 */
int app_drawer_launch_app(uint32_t index);

/**
 * app_drawer_get_state - Get drawer state for external use
 */
const app_drawer_state_t* app_drawer_get_state(void);

#endif /* NEOLYX_APP_DRAWER_H */
