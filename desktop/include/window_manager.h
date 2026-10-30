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
#include "nxcanvas.h"

/* Title bar height of every shell window (matches APP_VIEW_TITLE_H). */
#define WM_TITLEBAR_H   24
/* Minimum content size a user resize can reach. */
#define WM_MIN_W        320
#define WM_MIN_H        180
/* Work area reserved for the menu bar (top) and the dock (bottom). */
#define WM_MENUBAR_H    28
#define WM_DOCK_RESERVE 80

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
    TILE_MAXIMIZE,
    /* Third-split positions */
    TILE_LEFT_THIRD,
    TILE_CENTER_THIRD,
    TILE_RIGHT_THIRD,
    TILE_LEFT_TWO_THIRDS,
    TILE_RIGHT_TWO_THIRDS,
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

    /* Spaces and visibility (see "Spaces" below) */
    int space;                  /* index of the space the window lives on */
    uint8_t hidden;             /* WM_HIDE_* reasons; 0 = may be shown */
} wm_window_state_t;

/* Reasons a window is hidden from the desktop (bit mask). */
#define WM_HIDE_SPACE    0x01u  /* lives on another space */
#define WM_HIDE_STAGE    0x02u  /* parked in the Stage strip */
#define WM_HIDE_CLOSING  0x04u  /* close animation running */

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

/* ============ Spaces (virtual desktops) ============ */

#define WM_MAX_SPACES 6

int  wm_space_count(void);
int  wm_space_current(void);
/* Append a space. Returns its index, or -1 when WM_MAX_SPACES exist. */
int  wm_space_add(void);
/* Remove space idx (never the last one); its windows move to the space
 * before it. Returns 0, or -1 on a bad index. */
int  wm_space_remove(int idx);
/* Make idx current; updates WM_HIDE_SPACE on every window. Returns 0/-1. */
int  wm_space_switch(int idx);
/* Space of a window, or -1 if unknown. */
int  wm_window_space(uint32_t window_id);
/* Move a window to a space. Returns 0, or -1 on bad id/index. */
int  wm_window_set_space(uint32_t window_id, int space);
/* Hidden mask of a window (WM_HIDE_*), 0 if unknown. */
uint8_t wm_window_hidden(uint32_t window_id);
void wm_window_set_hidden(uint32_t window_id, uint8_t reason, int on);

/* ============ Window snapshots ============ */
/*
 * Downscaled copies of whole windows (chrome + content) for Overview,
 * Stage and the minimize animation. Captures run only on request, in
 * wm_snapshot_service() at the start of a frame, by drawing the window
 * into the (about to be overwritten) frame backbuffer at (0, 0) and
 * area-averaging it into a slot surface of at most WM_SNAP_MAX_W x
 * WM_SNAP_MAX_H. Slot surfaces are allocated on first use and reused
 * for good (the shell heap is a bump allocator). With no consumer
 * asking, the service does nothing.
 */

#define WM_SNAP_MAX_W  480
#define WM_SNAP_MAX_H  340
#define WM_SNAP_SLOTS  16

/* Draws window `window_id` with its top-left corner at (0, 0) into c, at
 * full size and without shadow. Writes the drawn size (title bar
 * included) and returns 0, or -1 if the window cannot be drawn. */
typedef int (*wm_snapshot_renderer_t)(uint32_t window_id, nxc_canvas_t *c,
                                      int32_t *out_w, int32_t *out_h);
void wm_snapshot_set_renderer(wm_snapshot_renderer_t renderer);

/* Ask for a fresh capture on the next service pass. */
void wm_snapshot_request(uint32_t window_id);
/* Keep a window's snapshot no older than max_age_ms (call while the
 * consumer is visible; captures are spread one per frame). */
void wm_snapshot_keep_fresh(uint32_t window_id, uint32_t max_age_ms);
/* The window's content changed: the next keep_fresh/request recaptures. */
void wm_window_content_changed(uint32_t window_id);
/* Run pending captures (urgent requests all, periodic ones at most one).
 * Must be called before the frame's wallpaper is drawn. */
void wm_snapshot_service(nxc_canvas_t *frame);
/* Snapshot of a window. Returns 1 and sets *surf and *src (the used
 * region) when one is available, else 0. */
int  wm_snapshot_get(uint32_t window_id, const nxc_surface_t **surf, nxc_irect_t *src);
/* Generation counter, bumped on every capture of this window (0 = none). */
uint32_t wm_snapshot_generation(uint32_t window_id);

/* ============ Shell hooks (implemented in desktop_shell.c) ============ */
/*
 * The window list lives in the desktop shell. Overview, Stage and the
 * switcher use these to read and move windows. Frames are the window
 * top-left (title bar included) and the CONTENT size (title excluded).
 */

/* Window ids in z-order, topmost first. Returns the count. */
int  wm_shell_window_order(uint32_t *ids, int max);
int  wm_shell_get_frame(uint32_t window_id, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h);
/* Move/resize a window; animate=1 springs there. Returns 0/-1. */
int  wm_shell_set_frame(uint32_t window_id, int32_t x, int32_t y, uint32_t w, uint32_t h,
                        int animate);
/* Raise without changing focus rules elsewhere (focus + top of z-order). */
void wm_shell_raise(uint32_t window_id);
/* Animated minimize to the dock. Returns 0/-1. */
int  wm_shell_minimize(uint32_t window_id);
/* Draw a window snapshot (or a placeholder card when none exists) into
 * rect r of c with alpha. Used by Overview, Stage and the switcher. */
void wm_shell_draw_window_image(nxc_canvas_t *c, uint32_t window_id, nxc_rect_t r, uint8_t alpha);

#endif /* WINDOW_MANAGER_H */

