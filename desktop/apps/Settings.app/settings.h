/*
 * NeolyxOS Settings Application - Header
 * 
 * Common definitions and includes for the Settings app.
 * Uses REOX UI library for native GUI.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "reox_runtime.h"
#include "reox_ui.h"
#include "reox_theme.h"

/* ============================================================================
 * Panel Identifiers
 * ============================================================================ */

typedef enum {
    PANEL_SYSTEM = 0,
    PANEL_NETWORK,
    PANEL_BLUETOOTH,
    PANEL_DISPLAY,
    PANEL_SOUND,
    PANEL_STORAGE,
    PANEL_ACCOUNTS,
    PANEL_SECURITY,
    PANEL_PRIVACY,
    PANEL_APPEARANCE,
    PANEL_POWER,
    PANEL_PROCESSES,
    PANEL_STARTUP,
    PANEL_SCHEDULER,
    PANEL_EXTENSIONS,
    PANEL_UPDATES,
    PANEL_BOOTLOADER,
    PANEL_DEVELOPER,
    PANEL_ABOUT,
    PANEL_KEYBOARD,
    PANEL_TRACKPAD,
    PANEL_COLOR,
    PANEL_APPS,
    PANEL_DISPLAY_MANAGER,
    PANEL_DEVICE,
    PANEL_ICONS,
    PANEL_WINDOWS,
    PANEL_MOUSE,
    PANEL_COUNT
} panel_id_t;

/* ============================================================================
 * Page Navigation System
 * ============================================================================ */

#define MAX_PAGE_STACK 16

typedef struct page_entry {
    panel_id_t panel;
    const char* sub_page;    /* NULL for main panel, or sub-page name */
    void* data;              /* Custom data for the page */
} page_entry_t;

typedef struct page_stack {
    page_entry_t entries[MAX_PAGE_STACK];
    int depth;
} page_stack_t;

/* ============================================================================
 * Search System
 * ============================================================================ */

typedef struct search_result {
    const char* title;
    const char* description;
    panel_id_t panel;
    const char* sub_page;
    int score;               /* Search relevance score */
} search_result_t;

#define MAX_SEARCH_RESULTS 50

typedef struct search_ctx {
    char query[256];
    search_result_t results[MAX_SEARCH_RESULTS];
    int result_count;
    bool active;
} search_ctx_t;

/* ============================================================================
 * Update System
 * ============================================================================ */

typedef enum {
    UPDATE_STATE_IDLE,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_AVAILABLE,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_READY,
    UPDATE_STATE_INSTALLING,
    UPDATE_STATE_ERROR
} update_state_t;

typedef struct update_info {
    char version[32];
    char current_version[32];
    char release_notes[1024];
    uint64_t download_size;
    uint64_t downloaded;
    update_state_t state;
    int progress;            /* 0-100 */
    char error[256];
} update_info_t;

typedef struct app_update {
    char name[64];
    char app_id[128];
    char current[32];
    char available[32];
    uint64_t size;
    bool selected;
    struct app_update* next;
} app_update_t;

/* ============================================================================
 * Settings Context
 * ============================================================================ */

typedef struct settings_ctx {
    rx_app* app;
    rx_window* window;
    rx_view* content_area;
    rx_view* sidebar;
    panel_id_t active_panel;
    
    /* Page navigation */
    page_stack_t page_stack;
    
    /* Search */
    search_ctx_t search;
    
    /* Updates */
    update_info_t system_update;
    app_update_t* app_updates;
    int app_update_count;
} settings_ctx_t;

/* ============================================================================
 * Panel Function Signatures
 * ============================================================================ */

typedef rx_view* (*panel_create_fn)(settings_ctx_t* ctx);
typedef void (*panel_update_fn)(settings_ctx_t* ctx, rx_view* panel);
typedef void (*panel_destroy_fn)(rx_view* panel);

typedef struct panel_desc {
    const char* name;
    const char* icon;
    const char* description;
    panel_create_fn create;
    panel_update_fn update;
    panel_destroy_fn destroy;
    const char** search_keywords;
    int keyword_count;
} panel_desc_t;

/* ============================================================================
 * Panel Creation Functions
 * ============================================================================ */

extern rx_view* system_panel_create(settings_ctx_t* ctx);
extern rx_view* network_panel_create(settings_ctx_t* ctx);
extern rx_view* display_panel_create(settings_ctx_t* ctx);
extern rx_view* sound_panel_create(settings_ctx_t* ctx);
extern rx_view* storage_panel_create(settings_ctx_t* ctx);
extern rx_view* accounts_panel_create(settings_ctx_t* ctx);
extern rx_view* security_panel_create(settings_ctx_t* ctx);
extern rx_view* privacy_panel_create(settings_ctx_t* ctx);
extern rx_view* appearance_panel_create(settings_ctx_t* ctx);
extern rx_view* power_panel_create(settings_ctx_t* ctx);
extern rx_view* processes_panel_create(settings_ctx_t* ctx);
extern rx_view* startup_panel_create(settings_ctx_t* ctx);
extern rx_view* scheduler_panel_create(settings_ctx_t* ctx);
extern rx_view* extensions_panel_create(settings_ctx_t* ctx);
extern rx_view* updates_panel_create(settings_ctx_t* ctx);
extern rx_view* bootloader_panel_create(settings_ctx_t* ctx);
extern rx_view* developer_panel_create(settings_ctx_t* ctx);
extern rx_view* about_panel_create(settings_ctx_t* ctx);
extern rx_view* bluetooth_panel_create(settings_ctx_t* ctx);
extern rx_view* keyboard_panel_create(settings_ctx_t* ctx);
extern rx_view* trackpad_panel_create(settings_ctx_t* ctx);
extern rx_view* color_panel_create(settings_ctx_t* ctx);
extern rx_view* apps_panel_create(settings_ctx_t* ctx);
extern rx_view* display_manager_panel_create(settings_ctx_t* ctx);
extern rx_view* device_panel_create(settings_ctx_t* ctx);
extern rx_view* icons_panel_create(settings_ctx_t* ctx);
extern rx_view* windows_panel_create(settings_ctx_t* ctx);
extern rx_view* mouse_panel_create(settings_ctx_t* ctx);

/* ============================================================================
 * Page Navigation
 * ============================================================================ */

static inline void page_push(settings_ctx_t* ctx, panel_id_t panel, const char* sub_page, void* data) {
    if (ctx->page_stack.depth < MAX_PAGE_STACK - 1) {
        page_entry_t* entry = &ctx->page_stack.entries[ctx->page_stack.depth++];
        entry->panel = panel;
        entry->sub_page = sub_page;
        entry->data = data;
    }
}

static inline page_entry_t* page_pop(settings_ctx_t* ctx) {
    if (ctx->page_stack.depth > 0) {
        return &ctx->page_stack.entries[--ctx->page_stack.depth];
    }
    return NULL;
}

static inline page_entry_t* page_current(settings_ctx_t* ctx) {
    if (ctx->page_stack.depth > 0) {
        return &ctx->page_stack.entries[ctx->page_stack.depth - 1];
    }
    return NULL;
}

static inline bool page_can_go_back(settings_ctx_t* ctx) {
    return ctx->page_stack.depth > 1;
}

/* ============================================================================
 * Search Functions
 * ============================================================================ */

extern void settings_search_init(settings_ctx_t* ctx);
extern void settings_search_query(settings_ctx_t* ctx, const char* query);
extern void settings_search_clear(settings_ctx_t* ctx);
extern rx_view* search_results_view(settings_ctx_t* ctx);

/* ============================================================================
 * UI Utilities
 * ============================================================================ */

/* Create a card with title */
static inline rx_view* settings_card(const char* title) {
    rx_view* card = vstack_new(12.0f);
    if (!card) return NULL;
    
    rx_color surface = { 44, 44, 46, 255 };
    rx_color shadow_col = { 0, 0, 0, 80 };
    
    card->box.background = surface;
    card->box.padding = insets_all(16.0f);
    card->box.corner_radius = corners_all(12.0f);
    card->box.shadow = shadow(0.0f, 4.0f, 16.0f, shadow_col);
    
    if (title) {
        rx_text_view* header = text_view_new(title);
        if (header) {
            text_view_set_font_size(header, 18.0f);
            header->font_weight = 600;
            header->color = (rx_color){ 255, 255, 255, 255 };
            view_add_child(card, (rx_view*)header);
        }
    }
    
    return card;
}

/* Create a settings row (label + value) */
static inline rx_view* settings_row(const char* label, const char* value) {
    rx_view* row = hstack_new(8.0f);
    if (!row) return NULL;
    
    rx_text_view* lbl = text_view_new(label);
    if (lbl) {
        lbl->color = (rx_color){ 255, 255, 255, 255 };
        view_add_child(row, (rx_view*)lbl);
    }
    
    /* Spacer */
    rx_view* spacer = vstack_new(0);
    if (spacer) {
        spacer->box.width = -1;  /* Flex */
        view_add_child(row, spacer);
    }
    
    if (value) {
        rx_text_view* val = text_view_new(value);
        if (val) {
            val->color = (rx_color){ 174, 174, 178, 255 };
            view_add_child(row, (rx_view*)val);
        }
    }
    
    return row;
}

/* Create a dim or normal label */
static inline rx_text_view* settings_label(const char* text, bool dim) {
    rx_text_view* label = text_view_new(text);
    if (label) {
        if (dim) {
            label->color = (rx_color){ 174, 174, 178, 255 };
        } else {
            label->color = (rx_color){ 255, 255, 255, 255 };
        }
    }
    return label;
}

/* Create a section header */
static inline rx_view* settings_section(const char* title) {
    rx_text_view* header = text_view_new(title);
    if (header) {
        text_view_set_font_size(header, 13.0f);
        header->font_weight = 600;
        header->color = (rx_color){ 140, 140, 145, 255 };
    }
    return (rx_view*)header;
}

/* ============================================================================
 * Color Constants for NeolyxOS Theme
 * ============================================================================ */

#define NX_COLOR_PRIMARY     ((rx_color){ 0, 122, 255, 255 })
#define NX_COLOR_ACCENT      ((rx_color){ 255, 69, 58, 255 })
#define NX_COLOR_BACKGROUND  ((rx_color){ 28, 28, 30, 255 })
#define NX_COLOR_SURFACE     ((rx_color){ 44, 44, 46, 255 })
#define NX_COLOR_TEXT        ((rx_color){ 255, 255, 255, 255 })
#define NX_COLOR_TEXT_DIM    ((rx_color){ 174, 174, 178, 255 })
#define NX_COLOR_SUCCESS     ((rx_color){ 34, 197, 94, 255 })
#define NX_COLOR_WARNING     ((rx_color){ 251, 191, 36, 255 })
#define NX_COLOR_ERROR       ((rx_color){ 239, 68, 68, 255 })
#define NX_COLOR_INFO        ((rx_color){ 59, 130, 246, 255 })

#endif /* SETTINGS_H */

