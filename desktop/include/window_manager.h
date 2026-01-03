/*
 * NeolyxOS Window Manager Header
 * 
 * Window animations, tiling, and minimized stack management.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <stdint.h>

/* ============ Animation Types ============ */

typedef enum {
    WINDOW_ANIM_NONE = 0,
    WINDOW_ANIM_MINIMIZE,
    WINDOW_ANIM_MAXIMIZE,
    WINDOW_ANIM_RESTORE,
    WINDOW_ANIM_CLOSE,
    WINDOW_ANIM_TILE
} wm_anim_type_t;

typedef enum {
    ANIM_STYLE_SCALE = 0,    /* Simple scale down/up */
    ANIM_STYLE_GENIE,        /* macOS genie effect */
    ANIM_STYLE_FADE          /* Fade in/out */
} wm_anim_style_t;

/* ============ Tile Positions ============ */

typedef enum {
    TILE_NONE = 0,
    TILE_LEFT_HALF,
    TILE_RIGHT_HALF,
    TILE_TOP_HALF,
    TILE_BOTTOM_HALF,
    TILE_QUADRANT_TL,
    TILE_QUADRANT_TR,
    TILE_QUADRANT_BL,
    TILE_QUADRANT_BR,
    TILE_CENTER,
    TILE_MAXIMIZE
} wm_tile_pos_t;

/* ============ Animation State ============ */

typedef struct {
    wm_anim_type_t type;
    wm_anim_style_t style;
    float progress;              /* 0.0 to 1.0 */
    float duration;              /* Seconds */
    
    /* Geometry interpolation */
    int32_t start_x, start_y;
    uint32_t start_w, start_h;
    int32_t end_x, end_y;
    uint32_t end_w, end_h;
    
    /* Genie effect: dock icon target */
    int32_t dock_x, dock_y;
    
    /* Opacity (for fade) */
    float start_opacity;
    float end_opacity;
} wm_animation_t;

/* ============ Minimized Window ============ */

#define WM_THUMB_W 64
#define WM_THUMB_H 48
#define WM_MAX_MINIMIZED 16

typedef struct {
    uint32_t window_id;
    uint32_t app_id;
    char title[64];
    int32_t dock_x, dock_y;       /* Icon position in dock */
    uint32_t thumbnail[WM_THUMB_W * WM_THUMB_H];
    int valid;
} wm_minimized_t;

/* ============ Window State Extension ============ */

/* Window display modes */
typedef enum {
    WIN_MODE_NORMAL = 0,
    WIN_MODE_PIP,           /* Floating always-on-top */
    WIN_MODE_SIDEBAR,       /* Slid to edge */
    WIN_MODE_MAXIMIZED
} wm_window_mode_t;

typedef struct {
    wm_animation_t animation;
    wm_tile_pos_t tile_pos;
    
    /* Pre-tile geometry (for restore) */
    int32_t pre_tile_x, pre_tile_y;
    uint32_t pre_tile_w, pre_tile_h;
    
    /* Pre-maximize geometry */
    int32_t pre_max_x, pre_max_y;
    uint32_t pre_max_w, pre_max_h;
    
    int is_tiled;
    int is_maximized;
    
    /* PiP and Sidebar mode */
    wm_window_mode_t mode;
    float opacity;              /* 0.0-1.0, for PiP transparency */
    
    /* Pre-mode geometry (for restore from PiP/sidebar) */
    int32_t pre_mode_x, pre_mode_y;
    uint32_t pre_mode_w, pre_mode_h;
    
    /* PiP specific */
    uint32_t pip_w, pip_h;      /* PiP size */
    
    /* Sidebar specific */
    int sidebar_expanded;       /* 1=center, 0=slid */
} wm_window_state_t;

/* Quick access menu (3 icons) */
typedef struct {
    int visible;
    uint32_t window_id;
    int32_t x, y;
    int hover_idx;              /* 0=pop, 1=max, 2=sidebar, -1=none */
} wm_quick_menu_t;

/* ============ Settings ============ */

typedef struct {
    wm_anim_style_t minimize_style;
    wm_anim_style_t maximize_style;
    float animation_speed;        /* 0.5 to 2.0 multiplier */
    
    int tiling_enabled;
    int snap_threshold;           /* Pixels from edge */
    int tile_gap;                 /* Gap between tiles */
    
    int auto_arrange_drawer;
    
    /* PiP settings */
    float pip_opacity;            /* Default PiP transparency */
    uint32_t pip_default_w;       /* Default PiP width */
    uint32_t pip_default_h;       /* Default PiP height */
} wm_settings_t;

/* ============ API Functions ============ */

/* Initialization */
void wm_init(uint32_t screen_w, uint32_t screen_h);
void wm_set_settings(const wm_settings_t *settings);
wm_settings_t* wm_get_settings(void);

/* Window state */
wm_window_state_t* wm_get_window_state(uint32_t window_id);
void wm_register_window(uint32_t window_id);
void wm_unregister_window(uint32_t window_id);

/* Animations */
void wm_minimize_window(uint32_t window_id, int32_t dock_x, int32_t dock_y);
void wm_maximize_window(uint32_t window_id);
void wm_restore_window(uint32_t window_id);
void wm_close_window_animated(uint32_t window_id);

/* Animation tick - call each frame */
void wm_tick(float dt);
int wm_is_animating(uint32_t window_id);

/* Get interpolated geometry during animation */
int wm_get_animated_geometry(uint32_t window_id,
                             int32_t *x, int32_t *y,
                             uint32_t *w, uint32_t *h,
                             float *opacity);

/* Minimized stack */
int wm_add_minimized(uint32_t window_id, uint32_t app_id, 
                     const char *title, int32_t dock_x, int32_t dock_y);
void wm_remove_minimized(uint32_t window_id);
wm_minimized_t* wm_get_minimized_stack(int *count);
int wm_is_minimized(uint32_t window_id);
void wm_generate_thumbnail(uint32_t window_id, const uint32_t *content,
                           uint32_t src_w, uint32_t src_h);

/* Tiling */
wm_tile_pos_t wm_suggest_tile(int32_t mouse_x, int32_t mouse_y,
                               int32_t win_x, int32_t win_y);
void wm_apply_tile(uint32_t window_id, wm_tile_pos_t tile);
void wm_get_tile_geometry(wm_tile_pos_t tile,
                          int32_t *x, int32_t *y,
                          uint32_t *w, uint32_t *h);
void wm_untile_window(uint32_t window_id);

/* Tile preview - returns 1 if preview should be shown */
int wm_get_tile_preview(int32_t *x, int32_t *y, uint32_t *w, uint32_t *h);
void wm_set_tile_preview(wm_tile_pos_t tile);
void wm_clear_tile_preview(void);

/* ============ PiP and Sidebar Mode ============ */

/* Enter PiP mode (floating always-on-top) */
void wm_enter_pip_mode(uint32_t window_id);
void wm_exit_pip_mode(uint32_t window_id);
int wm_is_pip(uint32_t window_id);

/* Enter sidebar mode (slid to edge) */
void wm_enter_sidebar_mode(uint32_t window_id);
void wm_exit_sidebar_mode(uint32_t window_id);
void wm_toggle_sidebar_expand(uint32_t window_id);
int wm_is_sidebar(uint32_t window_id);

/* Set window opacity (for PiP) */
void wm_set_window_opacity(uint32_t window_id, float opacity);
void wm_get_window_opacity(uint32_t window_id, float *out_opacity);

/* Get PiP windows (for rendering last) */
int wm_get_pip_windows(uint32_t *out_ids, int max_count);

/* Get sidebar windows */
int wm_get_sidebar_windows(uint32_t *out_ids, int max_count);

/* ============ Quick Access Menu ============ */

void wm_show_quick_menu(uint32_t window_id, int32_t x, int32_t y);
void wm_hide_quick_menu(void);
wm_quick_menu_t* wm_get_quick_menu(void);
int wm_quick_menu_hit_test(int32_t mouse_x, int32_t mouse_y);
void wm_quick_menu_click(int32_t mouse_x, int32_t mouse_y);

#endif /* WINDOW_MANAGER_H */

