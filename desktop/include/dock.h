/*
 * NeolyxOS Dock Module Header
 * 
 * Modular dock component with configurable icons, magnification,
 * and autohide. Uses nxappearance for settings.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef DOCK_H
#define DOCK_H

#include <stdint.h>
#include "nxappearance.h"
#include "nxevent.h"

/* ==============================================================================
 * ⚠️  ICON RENDERING POLICY - READ BEFORE EDITING  ⚠️
 * ==============================================================================
 *
 * ALL ICONS MUST BE LOADED FROM .NXI FILES
 * 
 * ❌ DO NOT:
 *   - Draw hardcoded shapes (circles, rectangles) as icons
 *   - Add inline icon drawing functions (dock_draw_folder_icon, etc.)
 *   - Use placeholder colors as icon representation
 *
 * ✅ DO:
 *   - Use nxi_render.h functions to render icons
 *   - Load icons from /System/Icons/ or app bundle resources
 *   - Create missing icons using IconLay.app, then reference by ID
 *
 * Icon files: /System/Icons/*.nxi
 * API: nxi_draw_icon_fallback(), nxi_render_icon_by_name()
 * See: desktop/include/nxi_render.h
 *
 * This policy ensures:
 *   - Consistent icon quality across the OS
 *   - Themeable/swappable icons
 *   - No hardcoded visual elements in code
 * ============================================================================== */

/* ============ Constants ============ */

#define DOCK_MAX_ICONS      16
#define DOCK_ICON_GAP       8
#define DOCK_PADDING        20
#define DOCK_MARGIN_BOTTOM  15

/* Dock position (matches config string values) */
typedef enum {
    DOCK_POS_BOTTOM = 0,
    DOCK_POS_LEFT   = 1,
    DOCK_POS_RIGHT  = 2
} dock_position_t;

/* ============ Dock Icon ============ */

typedef struct {
    uint32_t app_id;            /* App bundle ID hash */
    uint32_t icon_id;           /* NXI icon ID (resolved at add-time, NOT per-frame) */
    uint32_t color;             /* Icon background color (fallback) */
    char label[32];             /* App name */
    int active;                 /* Has focused windows */
    int running;                /* App is running (background) */
    int minimized;              /* Has minimized windows */
    int bouncing;               /* Launch animation */
    int bounce_frame;           /* Animation frame counter */
} dock_icon_t;

/* ============ Dock State ============ */

typedef struct {
    /* Screen info */
    uint32_t screen_width;
    uint32_t screen_height;
    
    /* Dock geometry (computed) */
    int x, y;
    int width, height;
    
    /* Icons */
    dock_icon_t icons[DOCK_MAX_ICONS];
    int icon_count;
    
    /* Interaction state */
    int hover_idx;              /* Currently hovered icon (-1 if none) */
    int pressed_idx;            /* Currently pressed icon (-1 if none) */
    
    /* Animation state */
    int minimize_anim_idx;      /* Icon receiving minimized window */
    int minimize_progress;      /* 0-100 animation progress */
    
    /* Autohide state */
    int visible;                /* Dock currently visible */
    int autohide_timer;         /* Ticks until hide */
    
    /* Position (from settings) */
    dock_position_t position;
    
    /* Settings reference */
    appearance_settings_t *settings;
} dock_state_t;

/* ============ API Functions ============ */

/**
 * Initialize dock module
 * @param screen_w Screen width
 * @param screen_h Screen height
 */
void dock_init(uint32_t screen_w, uint32_t screen_h);

/**
 * Add icon to dock
 * @param app_id App bundle ID hash
 * @param label App display name
 * @param color Fallback background color
 * @return Index in dock, or -1 on error
 */
int dock_add_icon(uint32_t app_id, const char *label, uint32_t color);

/**
 * Remove icon from dock
 * @param app_id App to remove
 */
void dock_remove_icon(uint32_t app_id);

/**
 * Set app running state (shows indicator dot)
 * @param app_id App bundle ID
 * @param running 1 if running, 0 if not
 */
void dock_set_running(uint32_t app_id, int running);

/**
 * Start bounce animation for app launch
 * @param app_id App being launched
 */
void dock_start_bounce(uint32_t app_id);

/**
 * Render dock to framebuffer
 * Note: Uses nxi_render.h for icon drawing - NO hardcoded icons
 */
void dock_render(uint32_t *fb, uint32_t pitch, int mouse_x, int mouse_y);

/**
 * Handle mouse input
 * @return 1 if event was consumed, 0 otherwise
 */
int dock_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons);

/**
 * Get dock state (for external queries)
 */
dock_state_t* dock_get_state(void);

/**
 * Update dock from settings
 */
void dock_update_settings(void);

/**
 * Tick animation (call in main loop)
 */
void dock_tick(void);

/**
 * Event bus handler for dock-related events
 */
void dock_event_handler(const nx_event_t *event, void *userdata);

#endif /* DOCK_H */