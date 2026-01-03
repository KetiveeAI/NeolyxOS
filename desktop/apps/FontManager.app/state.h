/*
 * NeolyxOS Font Manager - Redux-Style State Management
 * 
 * Centralized state with actions and reducers for predictable UI updates.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef FONTMANAGER_STATE_H
#define FONTMANAGER_STATE_H

#include <stdint.h>

/* ============ Action Types ============ */

typedef enum {
    FM_ACTION_INIT,
    FM_ACTION_LOAD_FONTS,
    FM_ACTION_SELECT_FONT,
    FM_ACTION_PREVIEW_SIZE,
    FM_ACTION_PREVIEW_TEXT,
    FM_ACTION_INSTALL_FONT,
    FM_ACTION_UNINSTALL_FONT,
    FM_ACTION_FILTER,
    FM_ACTION_SORT,
    FM_ACTION_ERROR,
} fm_action_type_t;

/* ============ Font Data ============ */

#define FM_MAX_FONTS 256
#define FM_MAX_NAME_LEN 64
#define FM_MAX_PREVIEW_LEN 256

typedef enum {
    FM_FONT_TTF,
    FM_FONT_OTF,
    FM_FONT_WOFF,
    FM_FONT_NXFONT,  /* NeolyxOS native vector font */
} fm_font_type_t;

typedef enum {
    FM_WEIGHT_THIN = 100,
    FM_WEIGHT_EXTRALIGHT = 200,
    FM_WEIGHT_LIGHT = 300,
    FM_WEIGHT_REGULAR = 400,
    FM_WEIGHT_MEDIUM = 500,
    FM_WEIGHT_SEMIBOLD = 600,
    FM_WEIGHT_BOLD = 700,
    FM_WEIGHT_EXTRABOLD = 800,
    FM_WEIGHT_BLACK = 900,
} fm_font_weight_t;

typedef enum {
    FM_STYLE_NORMAL,
    FM_STYLE_ITALIC,
    FM_STYLE_OBLIQUE,
} fm_font_style_t;

typedef struct {
    uint32_t id;
    char name[FM_MAX_NAME_LEN];
    char family[FM_MAX_NAME_LEN];
    char path[128];
    fm_font_type_t type;
    fm_font_weight_t weight;
    fm_font_style_t style;
    uint8_t installed;
    uint8_t system_font;
    uint32_t glyph_count;
    /* TTF data (if loaded) */
    const uint8_t *data;
    uint32_t data_size;
} fm_font_entry_t;

/* ============ Filter & Sort ============ */

typedef enum {
    FM_FILTER_ALL,
    FM_FILTER_INSTALLED,
    FM_FILTER_SYSTEM,
    FM_FILTER_USER,
    FM_FILTER_SERIF,
    FM_FILTER_SANS_SERIF,
    FM_FILTER_MONO,
} fm_filter_t;

typedef enum {
    FM_SORT_NAME_ASC,
    FM_SORT_NAME_DESC,
    FM_SORT_FAMILY,
    FM_SORT_WEIGHT,
    FM_SORT_RECENTLY_USED,
} fm_sort_t;

/* ============ Application State ============ */

typedef struct {
    /* Font library */
    fm_font_entry_t fonts[FM_MAX_FONTS];
    uint32_t font_count;
    
    /* Selection */
    int32_t selected_font_idx;
    
    /* Preview settings */
    uint32_t preview_size;
    char preview_text[FM_MAX_PREVIEW_LEN];
    
    /* Filtering */
    fm_filter_t filter;
    fm_sort_t sort;
    char search_query[64];
    
    /* UI State */
    uint8_t loading;
    uint8_t sidebar_visible;
    uint8_t preview_panel_visible;
    uint8_t error_visible;
    char error_message[128];
    
    /* Window dimensions (for rendering) */
    int32_t window_x, window_y;
    uint32_t window_w, window_h;
    
    /* Scroll state */
    int32_t scroll_offset;
    int32_t max_scroll;
} fm_state_t;

/* ============ Actions ============ */

typedef struct {
    fm_action_type_t type;
    union {
        /* FM_ACTION_SELECT_FONT */
        struct { int32_t font_idx; } select;
        
        /* FM_ACTION_PREVIEW_SIZE */
        struct { uint32_t size; } preview_size;
        
        /* FM_ACTION_PREVIEW_TEXT */
        struct { const char *text; } preview_text;
        
        /* FM_ACTION_INSTALL_FONT / UNINSTALL */
        struct { uint32_t font_id; } install;
        
        /* FM_ACTION_FILTER */
        struct { fm_filter_t filter; } filter;
        
        /* FM_ACTION_SORT */
        struct { fm_sort_t sort; } sort;
        
        /* FM_ACTION_ERROR */
        struct { const char *message; } error;
    } payload;
} fm_action_t;

/* ============ Redux-like Functions ============ */

/* Initialize state with defaults */
void fm_state_init(fm_state_t *state);

/* Dispatch an action to update state */
void fm_dispatch(fm_state_t *state, const fm_action_t *action);

/* Get current state (read-only) */
const fm_state_t* fm_get_state(void);

/* ============ Selectors (Computed Values) ============ */

/* Get filtered font list */
uint32_t fm_select_filtered_fonts(const fm_state_t *state, 
                                   fm_font_entry_t **out_fonts,
                                   uint32_t max_count);

/* Get currently selected font */
const fm_font_entry_t* fm_select_current_font(const fm_state_t *state);

/* Count fonts by type */
uint32_t fm_select_count_installed(const fm_state_t *state);
uint32_t fm_select_count_system(const fm_state_t *state);

#endif /* FONTMANAGER_STATE_H */
