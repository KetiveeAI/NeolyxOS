/*
 * NeolyxOS Window Manager Implementation
 * 
 * Window animations, tiling, and minimized stack management.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/window_manager.h"
#include "../include/nxsyscall.h"
#include <stddef.h>

/* ============ Static State ============ */

static int g_wm_initialized = 0;
static uint32_t g_screen_w = 0;
static uint32_t g_screen_h = 0;

static wm_settings_t g_settings = {
    .minimize_style = ANIM_STYLE_SCALE,
    .maximize_style = ANIM_STYLE_SCALE,
    .animation_speed = 1.0f,
    .tiling_enabled = 1,
    .snap_threshold = 30,
    .tile_gap = 8,
    .auto_arrange_drawer = 1,
    .pip_opacity = 0.9f,
    .pip_default_w = 300,
    .pip_default_h = 200
};

/* Window states (indexed by window_id % MAX) */
#define WM_MAX_WINDOWS 32
static wm_window_state_t g_window_states[WM_MAX_WINDOWS];
static uint32_t g_window_ids[WM_MAX_WINDOWS];

/* Minimized stack */
static wm_minimized_t g_minimized[WM_MAX_MINIMIZED];
static int g_minimized_count = 0;

/* Tile preview */
static int g_preview_active = 0;
static wm_tile_pos_t g_preview_tile = TILE_NONE;

/* Quick access menu */
static wm_quick_menu_t g_quick_menu = {0};

/* Spaces */
static int g_space_count = 1;
static int g_space_cur = 0;

/* ============ Easing Functions ============ */

/* Note: These use output pointers instead of return values to avoid
 * "SSE register return with SSE disabled" error from -mno-sse flags */

static void ease_out_cubic(float t, float *out) {
    float x = 1.0f - t;
    *out = 1.0f - x * x * x;
}

static void ease_in_out_cubic(float t, float *out) {
    if (t < 0.5f) {
        *out = 4.0f * t * t * t;
    } else {
        float x = -2.0f * t + 2.0f;
        *out = 1.0f - x * x * x / 2.0f;
    }
}

static void ease_out_back(float t, float *out) {
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    float x = t - 1.0f;
    *out = 1.0f + c3 * x * x * x + c1 * x * x;
}

/* ============ Helpers ============ */

static int find_window_slot(uint32_t window_id) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_window_ids[i] == window_id) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_window_ids[i] == 0) {
            return i;
        }
    }
    return -1;
}

static void strcpy_safe(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Initialization ============ */

void wm_init(uint32_t screen_w, uint32_t screen_h) {
    nx_debug_print("[WM] wm_init() entered\n");
    
    if (g_wm_initialized) return;
    
    g_screen_w = screen_w;
    g_screen_h = screen_h;
    
    nx_debug_print("[WM] Initializing window states...\n");
    
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        g_window_ids[i] = 0;
        g_window_states[i].animation.type = WINDOW_ANIM_NONE;
        g_window_states[i].tile_pos = TILE_NONE;
        g_window_states[i].is_tiled = 0;
        g_window_states[i].is_maximized = 0;
        g_window_states[i].mode = WIN_MODE_NORMAL;
        g_window_states[i].opacity = 1.0f;
        g_window_states[i].sidebar_expanded = 0;
        g_window_states[i].space = 0;
        g_window_states[i].hidden = 0;
    }

    nx_debug_print("[WM] Window states done, minimized stack...\n");
    
    for (int i = 0; i < WM_MAX_MINIMIZED; i++) {
        g_minimized[i].valid = 0;
    }
    g_minimized_count = 0;
    
    g_quick_menu.visible = 0;
    
    g_wm_initialized = 1;
    nx_debug_print("[WM] wm_init() complete\n");
}

void wm_set_settings(const wm_settings_t *settings) {
    if (settings) {
        g_settings = *settings;
    }
}

wm_settings_t* wm_get_settings(void) {
    return &g_settings;
}

/* ============ Window State ============ */

wm_window_state_t* wm_get_window_state(uint32_t window_id) {
    int slot = find_window_slot(window_id);
    if (slot >= 0) {
        return &g_window_states[slot];
    }
    return NULL;
}

void wm_register_window(uint32_t window_id) {
    if (find_window_slot(window_id) >= 0) return;
    
    int slot = find_free_slot();
    if (slot >= 0) {
        g_window_ids[slot] = window_id;
        wm_window_state_t *state = &g_window_states[slot];
        state->animation.type = WINDOW_ANIM_NONE;
        state->animation.progress = 0.0f;
        state->tile_pos = TILE_NONE;
        state->is_tiled = 0;
        state->is_maximized = 0;
        state->mode = WIN_MODE_NORMAL;
        state->opacity = 1.0f;
        state->sidebar_expanded = 0;
        state->space = g_space_cur;
        state->hidden = 0;
        state->pre_max_w = 0;
        state->pre_tile_w = 0;
    } else {
        nx_debug_print("[WM] warning: window state table full\n");
    }
}

static void snap_release(uint32_t window_id);

void wm_unregister_window(uint32_t window_id) {
    int slot = find_window_slot(window_id);
    if (slot >= 0) {
        g_window_ids[slot] = 0;
    }
    wm_remove_minimized(window_id);
    snap_release(window_id);
}

/* ============ Animations ============ */

void wm_minimize_window(uint32_t window_id, int32_t dock_x, int32_t dock_y) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    
    state->animation.type = WINDOW_ANIM_MINIMIZE;
    state->animation.style = g_settings.minimize_style;
    state->animation.progress = 0.0f;
    state->animation.duration = 0.3f / g_settings.animation_speed;
    state->animation.dock_x = dock_x;
    state->animation.dock_y = dock_y;
    state->animation.start_opacity = 1.0f;
    state->animation.end_opacity = 0.0f;
}

void wm_maximize_window(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    
    state->animation.type = WINDOW_ANIM_MAXIMIZE;
    state->animation.style = g_settings.maximize_style;
    state->animation.progress = 0.0f;
    state->animation.duration = 0.25f / g_settings.animation_speed;
    
    state->animation.end_x = 0;
    state->animation.end_y = 28;
    state->animation.end_w = g_screen_w;
    state->animation.end_h = g_screen_h - 28 - 80;
    
    state->is_maximized = 1;
    state->mode = WIN_MODE_MAXIMIZED;
}

void wm_restore_window(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    
    state->animation.type = WINDOW_ANIM_RESTORE;
    state->animation.style = g_settings.minimize_style;
    state->animation.progress = 0.0f;
    state->animation.duration = 0.3f / g_settings.animation_speed;
    state->animation.start_opacity = 0.0f;
    state->animation.end_opacity = 1.0f;
    
    wm_remove_minimized(window_id);
}

void wm_close_window_animated(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    
    state->animation.type = WINDOW_ANIM_CLOSE;
    state->animation.style = ANIM_STYLE_FADE;
    state->animation.progress = 0.0f;
    state->animation.duration = 0.15f / g_settings.animation_speed;
    state->animation.start_opacity = 1.0f;
    state->animation.end_opacity = 0.0f;
}

void wm_tick(float dt) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_window_ids[i] == 0) continue;
        
        wm_window_state_t *state = &g_window_states[i];
        if (state->animation.type == WINDOW_ANIM_NONE) continue;
        
        state->animation.progress += dt / state->animation.duration;
        
        if (state->animation.progress >= 1.0f) {
            state->animation.progress = 1.0f;
            state->animation.type = WINDOW_ANIM_NONE;
        }
    }
}

int wm_is_animating(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    return state && state->animation.type != WINDOW_ANIM_NONE;
}

int wm_get_animated_geometry(uint32_t window_id,
                             int32_t *x, int32_t *y,
                             uint32_t *w, uint32_t *h,
                             float *opacity) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state || state->animation.type == WINDOW_ANIM_NONE) {
        return 0;
    }
    
    wm_animation_t *anim = &state->animation;
    float t = anim->progress;
    float eased;
    
    switch (anim->style) {
        case ANIM_STYLE_GENIE:
            ease_out_back(t, &eased);
            break;
        case ANIM_STYLE_FADE:
            ease_in_out_cubic(t, &eased);
            break;
        default:
            ease_out_cubic(t, &eased);
            break;
    }
    
    if (x) *x = anim->start_x + (int32_t)((anim->end_x - anim->start_x) * eased);
    if (y) *y = anim->start_y + (int32_t)((anim->end_y - anim->start_y) * eased);
    if (w) *w = anim->start_w + (uint32_t)((anim->end_w - anim->start_w) * eased);
    if (h) *h = anim->start_h + (uint32_t)((anim->end_h - anim->start_h) * eased);
    
    if (opacity) {
        *opacity = anim->start_opacity + (anim->end_opacity - anim->start_opacity) * eased;
    }
    
    return 1;
}

/* ============ Minimized Stack ============ */

int wm_add_minimized(uint32_t window_id, uint32_t app_id,
                     const char *title, int32_t dock_x, int32_t dock_y) {
    if (g_minimized_count >= WM_MAX_MINIMIZED) return -1;
    
    for (int i = 0; i < WM_MAX_MINIMIZED; i++) {
        if (!g_minimized[i].valid) {
            g_minimized[i].window_id = window_id;
            g_minimized[i].app_id = app_id;
            strcpy_safe(g_minimized[i].title, title, 64);
            g_minimized[i].dock_x = dock_x;
            g_minimized[i].dock_y = dock_y;
            g_minimized[i].valid = 1;
            g_minimized_count++;
            return i;
        }
    }
    return -1;
}

void wm_remove_minimized(uint32_t window_id) {
    for (int i = 0; i < WM_MAX_MINIMIZED; i++) {
        if (g_minimized[i].valid && g_minimized[i].window_id == window_id) {
            g_minimized[i].valid = 0;
            g_minimized_count--;
            return;
        }
    }
}

wm_minimized_t* wm_get_minimized_stack(int *count) {
    if (count) *count = g_minimized_count;
    return g_minimized;
}

int wm_is_minimized(uint32_t window_id) {
    for (int i = 0; i < WM_MAX_MINIMIZED; i++) {
        if (g_minimized[i].valid && g_minimized[i].window_id == window_id) {
            return 1;
        }
    }
    return 0;
}

void wm_generate_thumbnail(uint32_t window_id, const uint32_t *content,
                           uint32_t src_w, uint32_t src_h) {
    for (int i = 0; i < WM_MAX_MINIMIZED; i++) {
        if (g_minimized[i].valid && g_minimized[i].window_id == window_id) {
            for (uint32_t ty = 0; ty < WM_THUMB_H; ty++) {
                for (uint32_t tx = 0; tx < WM_THUMB_W; tx++) {
                    uint32_t sx = (tx * src_w) / WM_THUMB_W;
                    uint32_t sy = (ty * src_h) / WM_THUMB_H;
                    g_minimized[i].thumbnail[ty * WM_THUMB_W + tx] = 
                        content[sy * src_w + sx];
                }
            }
            return;
        }
    }
}

/* ============ Tiling ============ */

wm_tile_pos_t wm_suggest_tile(int32_t mouse_x, int32_t mouse_y,
                               int32_t win_x, int32_t win_y) {
    if (!g_settings.tiling_enabled) return TILE_NONE;
    
    (void)win_x; (void)win_y;
    /* The pointer must touch a screen edge (like pushing against it);
     * near the top/bottom of a side edge it picks a quarter. */
    int edge = g_settings.snap_threshold / 4;
    if (edge < 4) edge = 4;
    int corner = g_settings.snap_threshold * 3;

    int at_left = (mouse_x < edge);
    int at_right = (mouse_x >= (int32_t)g_screen_w - edge);
    int at_top = (mouse_y < WM_MENUBAR_H + edge);
    int near_top = (mouse_y < WM_MENUBAR_H + corner);
    int near_bottom = (mouse_y > (int32_t)g_screen_h - WM_DOCK_RESERVE - corner);

    if (at_left && near_top) return TILE_QUADRANT_TL;
    if (at_right && near_top) return TILE_QUADRANT_TR;
    if (at_left && near_bottom) return TILE_QUADRANT_BL;
    if (at_right && near_bottom) return TILE_QUADRANT_BR;
    
    if (at_left) return TILE_LEFT_HALF;
    if (at_right) return TILE_RIGHT_HALF;
    if (at_top) return TILE_MAXIMIZE;
    
    return TILE_NONE;
}

void wm_get_tile_geometry(wm_tile_pos_t tile,
                          int32_t *x, int32_t *y,
                          uint32_t *w, uint32_t *h) {
    int gap = g_settings.tile_gap;
    int menu_h = 28;
    int dock_h = 80;
    int usable_h = g_screen_h - menu_h - dock_h;
    
    switch (tile) {
        case TILE_LEFT_HALF:
            if (x) *x = gap;
            if (y) *y = menu_h + gap;
            if (w) *w = g_screen_w / 2 - gap * 2;
            if (h) *h = usable_h - gap * 2;
            break;
        case TILE_RIGHT_HALF:
            if (x) *x = g_screen_w / 2 + gap;
            if (y) *y = menu_h + gap;
            if (w) *w = g_screen_w / 2 - gap * 2;
            if (h) *h = usable_h - gap * 2;
            break;
        case TILE_QUADRANT_TL:
            if (x) *x = gap;
            if (y) *y = menu_h + gap;
            if (w) *w = g_screen_w / 2 - gap * 2;
            if (h) *h = usable_h / 2 - gap * 2;
            break;
        case TILE_QUADRANT_TR:
            if (x) *x = g_screen_w / 2 + gap;
            if (y) *y = menu_h + gap;
            if (w) *w = g_screen_w / 2 - gap * 2;
            if (h) *h = usable_h / 2 - gap * 2;
            break;
        case TILE_QUADRANT_BL:
            if (x) *x = gap;
            if (y) *y = menu_h + usable_h / 2 + gap;
            if (w) *w = g_screen_w / 2 - gap * 2;
            if (h) *h = usable_h / 2 - gap * 2;
            break;
        case TILE_QUADRANT_BR:
            if (x) *x = g_screen_w / 2 + gap;
            if (y) *y = menu_h + usable_h / 2 + gap;
            if (w) *w = g_screen_w / 2 - gap * 2;
            if (h) *h = usable_h / 2 - gap * 2;
            break;
        case TILE_MAXIMIZE:
        case TILE_CENTER:
            if (x) *x = gap;
            if (y) *y = menu_h + gap;
            if (w) *w = g_screen_w - gap * 2;
            if (h) *h = usable_h - gap * 2;
            break;
        default:
            break;
    }
}

void wm_apply_tile(uint32_t window_id, wm_tile_pos_t tile) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    
    state->tile_pos = tile;
    state->is_tiled = (tile != TILE_NONE);
    
    state->animation.type = WINDOW_ANIM_TILE;
    state->animation.style = g_settings.maximize_style;
    state->animation.progress = 0.0f;
    state->animation.duration = 0.2f / g_settings.animation_speed;
    
    wm_get_tile_geometry(tile, 
                         &state->animation.end_x, &state->animation.end_y,
                         &state->animation.end_w, &state->animation.end_h);
}

void wm_untile_window(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    
    if (state->is_tiled) {
        state->tile_pos = TILE_NONE;
        state->is_tiled = 0;
    }
}

/* ============ Tile Preview ============ */

int wm_get_tile_preview(int32_t *x, int32_t *y, uint32_t *w, uint32_t *h) {
    if (!g_preview_active || g_preview_tile == TILE_NONE) {
        return 0;
    }
    wm_get_tile_geometry(g_preview_tile, x, y, w, h);
    return 1;
}

void wm_set_tile_preview(wm_tile_pos_t tile) {
    g_preview_active = (tile != TILE_NONE);
    g_preview_tile = tile;
}

void wm_clear_tile_preview(void) {
    g_preview_active = 0;
    g_preview_tile = TILE_NONE;
}

/* ============ PiP Mode ============ */

void wm_enter_pip_mode(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state || state->mode == WIN_MODE_PIP) return;
    
    state->mode = WIN_MODE_PIP;
    state->opacity = g_settings.pip_opacity;
    state->pip_w = g_settings.pip_default_w;
    state->pip_h = g_settings.pip_default_h;
}

void wm_exit_pip_mode(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state || state->mode != WIN_MODE_PIP) return;
    
    state->mode = WIN_MODE_NORMAL;
    state->opacity = 1.0f;
}

int wm_is_pip(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    return state && state->mode == WIN_MODE_PIP;
}

/* ============ Sidebar Mode ============ */

void wm_enter_sidebar_mode(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state || state->mode == WIN_MODE_SIDEBAR) return;
    
    state->mode = WIN_MODE_SIDEBAR;
    state->sidebar_expanded = 0;
}

void wm_exit_sidebar_mode(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state || state->mode != WIN_MODE_SIDEBAR) return;
    
    state->mode = WIN_MODE_NORMAL;
    state->sidebar_expanded = 0;
}

void wm_toggle_sidebar_expand(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state || state->mode != WIN_MODE_SIDEBAR) return;
    
    state->sidebar_expanded = !state->sidebar_expanded;
}

int wm_is_sidebar(uint32_t window_id) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    return state && state->mode == WIN_MODE_SIDEBAR;
}

/* ============ Opacity ============ */

void wm_set_window_opacity(uint32_t window_id, float opacity) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (!state) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    state->opacity = opacity;
}

void wm_get_window_opacity(uint32_t window_id, float *out_opacity) {
    wm_window_state_t *state = wm_get_window_state(window_id);
    if (out_opacity) {
        *out_opacity = state ? state->opacity : 1.0f;
    }
}

/* ============ Get PiP/Sidebar Windows ============ */

int wm_get_pip_windows(uint32_t *out_ids, int max_count) {
    int count = 0;
    for (int i = 0; i < WM_MAX_WINDOWS && count < max_count; i++) {
        if (g_window_ids[i] && g_window_states[i].mode == WIN_MODE_PIP) {
            out_ids[count++] = g_window_ids[i];
        }
    }
    return count;
}

int wm_get_sidebar_windows(uint32_t *out_ids, int max_count) {
    int count = 0;
    for (int i = 0; i < WM_MAX_WINDOWS && count < max_count; i++) {
        if (g_window_ids[i] && g_window_states[i].mode == WIN_MODE_SIDEBAR) {
            out_ids[count++] = g_window_ids[i];
        }
    }
    return count;
}

/* ============ Quick Access Menu ============ */

#define QUICK_MENU_ICON_SIZE 32
#define QUICK_MENU_ICON_GAP 8
#define QUICK_MENU_WIDTH (3 * QUICK_MENU_ICON_SIZE + 2 * QUICK_MENU_ICON_GAP + 16)
#define QUICK_MENU_HEIGHT (QUICK_MENU_ICON_SIZE + 16)

void wm_show_quick_menu(uint32_t window_id, int32_t x, int32_t y) {
    g_quick_menu.visible = 1;
    g_quick_menu.window_id = window_id;
    g_quick_menu.x = x;
    g_quick_menu.y = y;
    g_quick_menu.hover_idx = -1;
}

void wm_hide_quick_menu(void) {
    g_quick_menu.visible = 0;
    g_quick_menu.window_id = 0;
}

wm_quick_menu_t* wm_get_quick_menu(void) {
    return &g_quick_menu;
}

int wm_quick_menu_hit_test(int32_t mouse_x, int32_t mouse_y) {
    if (!g_quick_menu.visible) return -1;
    
    int32_t mx = g_quick_menu.x;
    int32_t my = g_quick_menu.y;
    
    if (mouse_x < mx || mouse_x >= mx + QUICK_MENU_WIDTH ||
        mouse_y < my || mouse_y >= my + QUICK_MENU_HEIGHT) {
        return -1;
    }
    
    int icon_x = mx + 8;
    for (int i = 0; i < 3; i++) {
        if (mouse_x >= icon_x && mouse_x < icon_x + QUICK_MENU_ICON_SIZE) {
            g_quick_menu.hover_idx = i;
            return i;
        }
        icon_x += QUICK_MENU_ICON_SIZE + QUICK_MENU_ICON_GAP;
    }
    
    g_quick_menu.hover_idx = -1;
    return -1;
}

void wm_quick_menu_click(int32_t mouse_x, int32_t mouse_y) {
    int idx = wm_quick_menu_hit_test(mouse_x, mouse_y);
    if (idx < 0) {
        wm_hide_quick_menu();
        return;
    }
    
    uint32_t wid = g_quick_menu.window_id;
    
    switch (idx) {
        case 0: /* PiP */
            if (wm_is_pip(wid)) {
                wm_exit_pip_mode(wid);
            } else {
                wm_enter_pip_mode(wid);
            }
            break;
        case 1: /* Maximize */
            wm_maximize_window(wid);
            break;
        case 2: /* Sidebar */
            if (wm_is_sidebar(wid)) {
                wm_exit_sidebar_mode(wid);
            } else {
                wm_enter_sidebar_mode(wid);
            }
            break;
    }

    wm_hide_quick_menu();
}

/* ============ Spaces ============ */

int wm_space_count(void) { return g_space_count; }
int wm_space_current(void) { return g_space_cur; }

/* Recompute WM_HIDE_SPACE for every registered window. */
static void space_sync_hidden(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_window_ids[i] == 0) continue;
        wm_window_state_t *st = &g_window_states[i];
        if (st->space == g_space_cur) st->hidden &= (uint8_t)~WM_HIDE_SPACE;
        else st->hidden |= WM_HIDE_SPACE;
    }
}

int wm_space_add(void) {
    if (g_space_count >= WM_MAX_SPACES) {
        nx_debug_print("[WM] space add refused: limit reached\n");
        return -1;
    }
    g_space_count++;
    nx_debug_print("[WM] space added\n");
    return g_space_count - 1;
}

int wm_space_remove(int idx) {
    if (idx < 0 || idx >= g_space_count || g_space_count <= 1) return -1;
    int dest = idx > 0 ? idx - 1 : 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_window_ids[i] == 0) continue;
        wm_window_state_t *st = &g_window_states[i];
        if (st->space == idx) st->space = dest;
        else if (st->space > idx) st->space--;
    }
    g_space_count--;
    if (g_space_cur == idx) g_space_cur = dest;
    else if (g_space_cur > idx) g_space_cur--;
    space_sync_hidden();
    nx_debug_print("[WM] space removed\n");
    return 0;
}

int wm_space_switch(int idx) {
    if (idx < 0 || idx >= g_space_count) return -1;
    g_space_cur = idx;
    space_sync_hidden();
    nx_debug_print("[WM] switched space\n");
    return 0;
}

int wm_window_space(uint32_t window_id) {
    wm_window_state_t *st = wm_get_window_state(window_id);
    return st ? st->space : -1;
}

int wm_window_set_space(uint32_t window_id, int space) {
    wm_window_state_t *st = wm_get_window_state(window_id);
    if (!st || space < 0 || space >= g_space_count) return -1;
    st->space = space;
    space_sync_hidden();
    return 0;
}

uint8_t wm_window_hidden(uint32_t window_id) {
    wm_window_state_t *st = wm_get_window_state(window_id);
    return st ? st->hidden : 0;
}

void wm_window_set_hidden(uint32_t window_id, uint8_t reason, int on) {
    wm_window_state_t *st = wm_get_window_state(window_id);
    if (!st) return;
    if (on) st->hidden |= reason;
    else st->hidden &= (uint8_t)~reason;
}

/* ============ Window snapshots ============ */

typedef struct {
    nxc_surface_t surf;         /* WM_SNAP_MAX_W x WM_SNAP_MAX_H once allocated */
    uint32_t window_id;         /* 0 = free */
    int32_t  w, h;              /* used region */
    int      valid;             /* holds a capture */
    int      urgent;            /* capture on the next service pass */
    int      stale;             /* content changed since the capture */
    uint32_t max_age_ms;        /* periodic refresh lease for this frame (0 = none) */
    uint64_t captured_ms;
    uint32_t generation;
} wm_snap_slot_t;

static wm_snap_slot_t g_snaps[WM_SNAP_SLOTS];
static wm_snapshot_renderer_t g_snap_renderer = NULL;
static int g_snap_rr = 0;                   /* round-robin cursor for periodic refresh */
static int g_snap_alloc_failed = 0;

void wm_snapshot_set_renderer(wm_snapshot_renderer_t renderer) {
    g_snap_renderer = renderer;
}

static wm_snap_slot_t *snap_find(uint32_t window_id) {
    if (window_id == 0) return NULL;
    for (int i = 0; i < WM_SNAP_SLOTS; i++) {
        if (g_snaps[i].window_id == window_id) return &g_snaps[i];
    }
    return NULL;
}

/* Find or claim a slot. Prefers a free slot whose surface already exists. */
static wm_snap_slot_t *snap_claim(uint32_t window_id) {
    wm_snap_slot_t *sl = snap_find(window_id);
    if (sl) return sl;
    if (window_id == 0 || !wm_get_window_state(window_id)) return NULL;
    wm_snap_slot_t *fresh = NULL;
    for (int i = 0; i < WM_SNAP_SLOTS; i++) {
        if (g_snaps[i].window_id != 0) continue;
        if (g_snaps[i].surf.pixels) { fresh = &g_snaps[i]; break; }
        if (!fresh) fresh = &g_snaps[i];
    }
    if (!fresh) return NULL;
    fresh->window_id = window_id;
    fresh->valid = 0;
    fresh->urgent = 0;
    fresh->stale = 1;
    fresh->max_age_ms = 0;
    fresh->captured_ms = 0;
    return fresh;
}

static void snap_release(uint32_t window_id) {
    wm_snap_slot_t *sl = snap_find(window_id);
    if (!sl) return;
    /* Keep the surface: the next window reuses the memory. */
    sl->window_id = 0;
    sl->valid = 0;
    sl->urgent = 0;
    sl->max_age_ms = 0;
}

void wm_snapshot_request(uint32_t window_id) {
    wm_snap_slot_t *sl = snap_claim(window_id);
    if (sl) sl->urgent = 1;
}

void wm_snapshot_keep_fresh(uint32_t window_id, uint32_t max_age_ms) {
    wm_snap_slot_t *sl = snap_claim(window_id);
    if (!sl) return;
    if (max_age_ms < 100) max_age_ms = 100;
    if (sl->max_age_ms == 0 || max_age_ms < sl->max_age_ms) sl->max_age_ms = max_age_ms;
    if (!sl->valid && sl->captured_ms == 0) sl->urgent = 1;
}

void wm_window_content_changed(uint32_t window_id) {
    wm_snap_slot_t *sl = snap_find(window_id);
    if (sl) sl->stale = 1;
}

/* Capture one slot. Returns 1 on success. */
static int snap_capture(wm_snap_slot_t *sl, nxc_canvas_t *frame) {
    if (!g_snap_renderer || !frame || !frame->target) return 0;
    sl->captured_ms = nx_gettime();   /* also rate-limits retries after a failure */
    if (!sl->surf.pixels) {
        if (g_snap_alloc_failed) return 0;
        nxc_status_t st = nxc_surface_create(&sl->surf, WM_SNAP_MAX_W, WM_SNAP_MAX_H, 0);
        if (st != NXC_OK) {
            /* Consumers fall back to placeholder cards; never retry. */
            g_snap_alloc_failed = 1;
            nx_debug_print("[WM] snapshot surface allocation failed; using placeholders\n");
            return 0;
        }
    }
    int32_t ww = 0, wh = 0;
    nxc_save(frame);
    nxc_clip_rect(frame, nxc_irect(0, 0, frame->target->width, frame->target->height));
    int rc = g_snap_renderer(sl->window_id, frame, &ww, &wh);
    nxc_restore(frame);
    if (rc != 0 || ww <= 0 || wh <= 0) return 0;
    if (ww > frame->target->width) ww = frame->target->width;
    if (wh > frame->target->height) wh = frame->target->height;

    /* Fit into the slot keeping the aspect ratio (never upscale). */
    float sx = (float)WM_SNAP_MAX_W / (float)ww, sy = (float)WM_SNAP_MAX_H / (float)wh;
    float sc = sx < sy ? sx : sy;
    if (sc > 1.0f) sc = 1.0f;
    int32_t dw = (int32_t)((float)ww * sc + 0.5f), dh = (int32_t)((float)wh * sc + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    nxc_canvas_t snap_canvas;
    if (nxc_canvas_init(&snap_canvas, &sl->surf) != NXC_OK) return 0;
    nxc_surface_clear(&sl->surf, 0);
    /* Bake the rounded window corners so stale backbuffer pixels never show. */
    float radius = 12.0f * sc;
    if (radius < 2.0f) radius = 2.0f;
    nxc_draw_surface_rrect(&snap_canvas, frame->target, nxc_irect(0, 0, ww, wh),
                           nxc_rect(0.0f, 0.0f, (float)dw, (float)dh), radius, 255, NXC_FILTER_SMOOTH);
    sl->w = dw;
    sl->h = dh;
    sl->valid = 1;
    sl->urgent = 0;
    sl->stale = 0;
    sl->generation++;
    if (sl->generation == 0) sl->generation = 1;
    return 1;
}

int wm_snapshot_service(nxc_canvas_t *frame) {
    if (!frame || !g_snap_renderer) return 0;
    int drawn = 0;   /* captures attempted: each one may have drawn into frame */
    /* Urgent captures (a consumer view just opened): all of them now. */
    for (int i = 0; i < WM_SNAP_SLOTS; i++) {
        wm_snap_slot_t *sl = &g_snaps[i];
        if (sl->window_id == 0 || !sl->urgent) continue;
        drawn++;
        if (!snap_capture(sl, frame)) sl->urgent = 0;   /* failure: placeholder */
    }
    /* Periodic refresh: at most one window per frame, round robin. A
     * window marked changed refreshes four times sooner. */
    uint64_t now = nx_gettime();
    for (int n = 0; n < WM_SNAP_SLOTS; n++) {
        int i = (g_snap_rr + n) % WM_SNAP_SLOTS;
        wm_snap_slot_t *sl = &g_snaps[i];
        if (sl->window_id == 0 || sl->max_age_ms == 0) continue;
        uint64_t age = now - sl->captured_ms;
        uint32_t limit = sl->stale ? sl->max_age_ms / 4u : sl->max_age_ms;
        if (age >= limit) {
            drawn++;
            snap_capture(sl, frame);
            g_snap_rr = (i + 1) % WM_SNAP_SLOTS;
            break;
        }
    }
    /* keep_fresh is a per-frame lease: consumers renew it while visible,
     * so nothing is captured once Overview/Stage are off. */
    for (int i = 0; i < WM_SNAP_SLOTS; i++) g_snaps[i].max_age_ms = 0;
    return drawn;
}

int wm_snapshot_get(uint32_t window_id, const nxc_surface_t **surf, nxc_irect_t *src) {
    wm_snap_slot_t *sl = snap_find(window_id);
    if (!sl || !sl->valid || !sl->surf.pixels) return 0;
    if (surf) *surf = &sl->surf;
    if (src) *src = nxc_irect(0, 0, sl->w, sl->h);
    return 1;
}

uint32_t wm_snapshot_generation(uint32_t window_id) {
    wm_snap_slot_t *sl = snap_find(window_id);
    return (sl && sl->valid) ? sl->generation : 0;
}
