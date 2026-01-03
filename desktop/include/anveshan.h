/*
 * NeolyxOS Anveshan Global Search System
 * 
 * Unified search across apps, settings, and files.
 * "Anveshan" means "search" in Sanskrit.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ANVESHAN_H
#define ANVESHAN_H

#include <stdint.h>
#include "nxappearance.h"
#include "nxevent.h"

/* ============ Constants ============ */

#define ANVESHAN_MAX_QUERY      256
#define ANVESHAN_MAX_RESULTS    16
#define ANVESHAN_MAX_SUGGESTIONS 10

/* Result types */
typedef enum {
    ANVESHAN_TYPE_APP      = 0,
    ANVESHAN_TYPE_SETTING  = 1,
    ANVESHAN_TYPE_FILE     = 2,
    ANVESHAN_TYPE_ACTION   = 3,
    ANVESHAN_TYPE_WEB      = 4
} anveshan_result_type_t;

/* ============ Search Settings ============ */

typedef struct {
    /* Dimensions (configurable in Settings.app) */
    int width;                  /* 280-600px, default 320 */
    int height;                 /* 28-48px, default 36 */
    int y_position;             /* Top offset, default 85 */
    
    /* Appearance */
    uint8_t opacity;            /* 0-255, default 180 */
    uint8_t blur_radius;        /* 0-20, default 10 */
    uint32_t bg_color;          /* Glass background color */
    uint32_t text_color;        /* Placeholder/input text */
    uint32_t icon_color;        /* Search/mic icon tint */
    
    /* Behavior */
    int max_suggestions;        /* 3-10, default 5 */
    int search_apps;            /* bool */
    int search_settings;        /* bool */
    int search_files;           /* bool */
    int show_icons;             /* bool: icons in results */
    int voice_enabled;          /* bool: microphone button */
    int auto_suggest;           /* bool: live suggestions */
    
    /* Text */
    char placeholder[64];       /* "Anveshan | search your wish" */
} anveshan_settings_t;

/* Default settings */
#define ANVESHAN_DEFAULTS { \
    .width = 320, \
    .height = 36, \
    .y_position = 85, \
    .opacity = 180, \
    .blur_radius = 10, \
    .bg_color = 0x303050, \
    .text_color = 0x888888, \
    .icon_color = 0x888888, \
    .max_suggestions = 5, \
    .search_apps = 1, \
    .search_settings = 1, \
    .search_files = 0, \
    .show_icons = 1, \
    .voice_enabled = 1, \
    .auto_suggest = 1, \
    .placeholder = "Anveshan | search your wish" \
}

/* ============ Search Result ============ */

typedef struct {
    anveshan_result_type_t type;
    char title[64];
    char subtitle[128];
    char icon_path[256];        /* Path to .nxi or .svg */
    uint32_t icon_color;        /* Fallback color for icon */
    int score;                  /* Relevance score (higher = better) */
    
    /* Action data */
    union {
        struct {
            char bundle_id[64];
            char binary_path[256];
        } app;
        struct {
            int panel_id;
            char sub_page[32];
        } setting;
        struct {
            char file_path[256];
            int is_dir;
        } file;
    } action;
} anveshan_result_t;

/* ============ Search State ============ */

typedef struct {
    /* Input */
    char query[ANVESHAN_MAX_QUERY];
    int cursor_pos;
    int active;                 /* Search bar focused */
    int open;                   /* Dropdown visible */
    
    /* Results */
    anveshan_result_t results[ANVESHAN_MAX_RESULTS];
    int result_count;
    int selected_idx;           /* Highlighted result (-1 = none) */
    
    /* Settings */
    anveshan_settings_t settings;
    
    /* Screen dimensions */
    uint32_t screen_width;
    uint32_t screen_height;
    
    /* Computed geometry */
    int bar_x, bar_y;
    int dropdown_height;
} anveshan_state_t;

/* ============ API Functions ============ */

/**
 * Initialize Anveshan search system
 * @param screen_w Screen width
 * @param screen_h Screen height
 */
void anveshan_init(uint32_t screen_w, uint32_t screen_h);

/**
 * Load settings from config
 */
void anveshan_load_settings(void);

/**
 * Save settings to config
 */
void anveshan_save_settings(void);

/**
 * Execute search query
 * @param query Search string
 */
void anveshan_query(const char *query);

/**
 * Clear search results and close dropdown
 */
void anveshan_clear(void);

/**
 * Render search bar and dropdown
 * @param fb Framebuffer pointer
 * @param pitch Framebuffer pitch in bytes
 */
void anveshan_render(uint32_t *fb, uint32_t pitch);

/**
 * Handle mouse input
 * @param mouse_x Mouse X position
 * @param mouse_y Mouse Y position
 * @param buttons Button state
 * @param prev_buttons Previous button state
 * @return 1 if event consumed, 0 otherwise
 */
int anveshan_handle_mouse(int mouse_x, int mouse_y, 
                           uint8_t buttons, uint8_t prev_buttons);

/**
 * Handle keyboard input
 * @param scancode Key scancode
 * @param pressed 1 if pressed, 0 if released
 * @param ascii ASCII character (if printable)
 * @return 1 if event consumed, 0 otherwise
 */
int anveshan_handle_key(uint8_t scancode, int pressed, char ascii);

/**
 * Get search state
 */
anveshan_state_t* anveshan_get_state(void);

/**
 * Update settings from appearance preferences
 */
void anveshan_update_settings(const anveshan_settings_t *settings);

/**
 * Check if search bar is focused/active
 */
int anveshan_is_active(void);

/* ============ App Index ============ */

typedef struct {
    char name[64];
    char bundle_id[64];
    char binary[256];
    char icon[256];
    uint32_t icon_color;
    char keywords[256];         /* Space-separated search keywords */
} anveshan_app_t;

/**
 * Scan apps directory and build search index
 */
void anveshan_index_apps(void);

/**
 * Get app count in index
 */
int anveshan_get_app_count(void);

/* ============ Icons ============ */

/* Icon paths (from Path.app/resources/icons) */
#define ANVESHAN_ICON_SEARCH    "/System/Library/Icons/search.svg"
#define ANVESHAN_ICON_MIC       "/System/Library/Icons/mic.svg"
#define ANVESHAN_ICON_APP       "/System/Library/Icons/app.svg"
#define ANVESHAN_ICON_SETTING   "/System/Library/Icons/setting.svg"
#define ANVESHAN_ICON_FILE      "/System/Library/Icons/file.nxi"
#define ANVESHAN_ICON_FOLDER    "/System/Library/Icons/folder.nxi"

/* Event types for search */
#define NX_EVENT_SEARCH_QUERY   0x0800
#define NX_EVENT_SEARCH_RESULT  0x0801
#define NX_EVENT_SEARCH_CLEAR   0x0802

#endif /* ANVESHAN_H */
