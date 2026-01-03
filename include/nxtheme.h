/*
 * NeolyxOS Desktop Theme API
 * Window controls, hover effects, dock, app grid
 * 
 * IMPORTANT: Window close button is on RIGHT side
 * (NeolyxOS design, different from macOS)
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef NXTHEME_H
#define NXTHEME_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Theme Colors ============ */

typedef struct {
    /* Window chrome */
    uint32_t window_bg;
    uint32_t window_titlebar;
    uint32_t window_titlebar_text;
    uint32_t window_border;
    
    /* Window buttons (on RIGHT side) */
    uint32_t button_close;       /* Red - rightmost */
    uint32_t button_maximize;    /* Green - middle */
    uint32_t button_minimize;    /* Yellow - left */
    
    /* Accent colors */
    uint32_t accent_primary;
    uint32_t accent_secondary;
    uint32_t accent_hover;
    uint32_t accent_active;
    
    /* Text */
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_disabled;
    uint32_t text_highlight;
    
    /* Backgrounds */
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_tertiary;
    uint32_t bg_hover;
    uint32_t bg_selected;
    
    /* Borders */
    uint32_t border_primary;
    uint32_t border_secondary;
    uint32_t border_focus;
    
    /* Controls */
    uint32_t control_bg;
    uint32_t control_border;
    uint32_t control_active;
    
    /* Status */
    uint32_t success;
    uint32_t warning;
    uint32_t error;
    uint32_t info;
    
} nxtheme_colors_t;

/* ============ Window Button Layout ============ */

/*
 * NeolyxOS window button order (LEFT to RIGHT):
 * [Title] .................. [─] [□] [×]
 *                            min max close
 *
 * This is the OPPOSITE of macOS!
 */

typedef enum {
    WINDOW_BUTTON_MINIMIZE,   /* Position 0 (left) */
    WINDOW_BUTTON_MAXIMIZE,   /* Position 1 (middle) */
    WINDOW_BUTTON_CLOSE       /* Position 2 (right, always last) */
} window_button_t;

typedef struct {
    int x, y;
    int size;
    uint32_t color;
    uint32_t hover_color;
    bool visible;
} window_button_style_t;

/* ============ Hover Effects ============ */

typedef struct {
    bool enabled;
    
    /* Background highlight */
    bool bg_highlight;
    uint32_t bg_color;
    float bg_opacity;
    
    /* Scale effect */
    bool scale;
    float scale_amount;     /* 1.0 - 1.2 */
    
    /* Glow effect */
    bool glow;
    uint32_t glow_color;
    float glow_radius;
    
    /* Shadow */
    bool shadow;
    float shadow_offset;
    float shadow_blur;
    uint32_t shadow_color;
    
    /* Animation */
    float transition_duration;  /* milliseconds */
    
} nxtheme_hover_t;

/* ============ Dock Settings ============ */

typedef enum {
    DOCK_POSITION_BOTTOM,
    DOCK_POSITION_LEFT,
    DOCK_POSITION_RIGHT
} dock_position_t;

typedef struct {
    dock_position_t position;
    int icon_size;
    int spacing;
    
    /* Magnification (like macOS) */
    bool magnification;
    float magnify_scale;    /* 1.0 - 2.0 */
    int magnify_range;      /* Pixels from cursor */
    
    /* Auto-hide */
    bool auto_hide;
    int show_delay_ms;
    int hide_delay_ms;
    
    /* Appearance */
    uint32_t background;
    float opacity;
    float corner_radius;
    bool separator_visible;
    
    /* Effects */
    bool bounce_on_launch;
    bool glow_on_running;
    
} nxtheme_dock_t;

/* ============ App Grid / Launchpad ============ */

typedef struct {
    int columns;
    int rows;
    int icon_size;
    int spacing;
    
    /* Labels */
    bool show_labels;
    int label_max_chars;
    
    /* Background */
    uint32_t background;
    float blur_amount;
    
    /* Folders */
    int folder_columns;
    int folder_rows;
    
    /* Search */
    bool search_bar_visible;
    
} nxtheme_app_grid_t;

/* ============ Theme Definition ============ */

typedef struct {
    char name[64];
    char author[64];
    char version[16];
    
    bool is_dark;
    nxtheme_colors_t colors;
    nxtheme_hover_t hover;
    nxtheme_dock_t dock;
    nxtheme_app_grid_t app_grid;
    
    /* Typography */
    char font_ui[64];
    char font_mono[64];
    int font_size_small;
    int font_size_normal;
    int font_size_large;
    int font_size_title;
    
    /* Spacing */
    int spacing_xs;
    int spacing_sm;
    int spacing_md;
    int spacing_lg;
    int spacing_xl;
    
    /* Corners */
    int corner_radius_sm;
    int corner_radius_md;
    int corner_radius_lg;
    
    /* Animations */
    float animation_speed;
    bool reduce_motion;
    
} nxtheme_t;

/* ============ Theme API ============ */

/* System themes */
nxtheme_t *nxtheme_get_light(void);
nxtheme_t *nxtheme_get_dark(void);
nxtheme_t *nxtheme_get_current(void);

/* Load/save custom themes */
nxtheme_t *nxtheme_load(const char *path);
int nxtheme_save(const nxtheme_t *theme, const char *path);

/* Apply theme */
void nxtheme_apply(const nxtheme_t *theme);

/* Get individual settings */
nxtheme_colors_t *nxtheme_colors(void);
nxtheme_hover_t *nxtheme_hover(void);
nxtheme_dock_t *nxtheme_dock(void);
nxtheme_app_grid_t *nxtheme_app_grid(void);

/* Window button helpers */
void nxtheme_get_window_buttons(window_button_style_t *buttons);
int nxtheme_window_button_at(int x, int y, int title_bar_width);

/* Convenience functions */
uint32_t nxtheme_accent_color(void);
uint32_t nxtheme_bg_color(void);
uint32_t nxtheme_text_color(void);
bool nxtheme_is_dark(void);

/* ============ Zoom / Accessibility ============ */

typedef struct {
    float zoom_level;       /* 1.0 - 4.0 */
    bool zoom_follows_cursor;
    bool zoom_keyboard_enabled;
    
    /* Screen magnification */
    bool magnifier_enabled;
    float magnifier_zoom;
    int magnifier_size;
    
} nxtheme_zoom_t;

void nxtheme_zoom_set(float level);
float nxtheme_zoom_get(void);
void nxtheme_zoom_in(void);
void nxtheme_zoom_out(void);
void nxtheme_zoom_reset(void);

#endif /* NXTHEME_H */
