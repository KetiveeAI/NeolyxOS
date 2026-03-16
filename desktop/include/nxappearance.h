/*
 * NeolyxOS Appearance Preferences API
 * 
 * Shared between Settings.app and desktop_shell for theme configuration.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXAPPEARANCE_H
#define NXAPPEARANCE_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Theme Modes ============ */

typedef enum {
    THEME_LIGHT = 0,
    THEME_DARK = 1,
    THEME_AUTO = 2
} theme_mode_t;

typedef enum {
    GLASS_CLEAR = 0,
    GLASS_TINTED = 1,
    GLASS_FROSTED = 2
} glass_style_t;

/* ============ Accent Colors ============ */

#define ACCENT_MULTICOLOR   0
#define ACCENT_BLUE         1
#define ACCENT_PURPLE       2
#define ACCENT_PINK         3
#define ACCENT_RED          4
#define ACCENT_ORANGE       5
#define ACCENT_YELLOW       6
#define ACCENT_GREEN        7
#define ACCENT_GRAPHITE     8

/* Color values for each accent */
static const uint32_t accent_color_values[] = {
    0xFF007AFF,  /* Multicolor (uses blue) */
    0xFF007AFF,  /* Blue */
    0xFFAF52DE,  /* Purple */
    0xFFFF2D55,  /* Pink */
    0xFFFF3B30,  /* Red */
    0xFFFF9500,  /* Orange */
    0xFFFFCC00,  /* Yellow */
    0xFF34C759,  /* Green */
    0xFF8E8E93   /* Graphite */
};

/* Dock icon colors (vibrant, matching reference design) */
static const uint32_t dock_icon_colors[] = {
    0xFFF5C518,  /* Yellow - Files */
    0xFFAA42F5,  /* Purple/Magenta */
    0xFF42C5F5,  /* Cyan - Safari */
    0xFF42F566,  /* Green - Messages */
    0xFFFF9500,  /* Orange - Notes */
    0xFFFF4080,  /* Pink - Photos */
    0xFFFF3B30,  /* Red - Music */
    0xFF007AFF   /* Blue - Mail */
};

#define DOCK_ICON_COLOR_COUNT 8

/* ============ Cursor Styles ============ */

typedef enum {
    CURSOR_ARROW = 0,      /* Default arrow */
    CURSOR_POINTER = 1,    /* Hand pointer */
    CURSOR_CROSSHAIR = 2,  /* Crosshair */
    CURSOR_TEXT = 3        /* I-beam text */
} cursor_style_t;

/* ============ Appearance Settings Struct ============ */
/*
 * DESIGN PHILOSOPHY:
 * Everything rendered on the desktop is controlled by user preferences.
 * The system loads saved settings and renders accordingly.
 * Settings.app modifies these values, calls nxappearance_save(), and
 * the desktop re-reads them on next render or via nxappearance_apply().
 */

typedef struct {
    /* Theme */
    theme_mode_t theme;
    glass_style_t glass_style;
    
    /* Accent Color */
    int accent_color_index;
    
    /* Cursor - User configurable */
    int cursor_size;             /* 12-48, default 16 */
    uint32_t cursor_color;       /* Default white */
    uint32_t cursor_outline;     /* Default black for contrast */
    cursor_style_t cursor_style; /* Arrow, pointer, etc. */
    
    /* Dock */
    int dock_icon_size;          /* 32-80, default 48 */
    bool dock_magnification;
    float dock_magnification_amount;  /* 1.0-2.0 */
    bool dock_auto_hide;
    
    /* Glass opacity (0-255) */
    uint8_t glass_opacity;       /* Default 64 (25%) */
    
    /* Desktop */
    bool show_icons_on_desktop;
    int desktop_icon_size;
    
    /* Windows */
    float window_minimize_duration;  /* 0.1-1.0 seconds */
    float window_maximize_duration;
    bool window_animations_enabled;
    
    /* Animations */
    bool reduce_motion;
    float animation_speed;
    
    /* Transparency */
    bool reduce_transparency;
    
    /* Wallpaper */
    char wallpaper_path[256];    /* Path to wallpaper PNG */
    bool wallpaper_enabled;
    
} appearance_settings_t;

/* ============ Default Values ============ */

#define APPEARANCE_DEFAULTS { \
    .theme = THEME_DARK, \
    .glass_style = GLASS_CLEAR, \
    .accent_color_index = ACCENT_BLUE, \
    .cursor_size = 16, \
    .cursor_color = 0xFFFFFFFF, \
    .cursor_outline = 0xFF000000, \
    .cursor_style = CURSOR_ARROW, \
    .dock_icon_size = 48, \
    .dock_magnification = true, \
    .dock_magnification_amount = 1.5f, \
    .dock_auto_hide = false, \
    .glass_opacity = 64, \
    .show_icons_on_desktop = true, \
    .desktop_icon_size = 48, \
    .window_minimize_duration = 0.3f, \
    .window_maximize_duration = 0.2f, \
    .window_animations_enabled = true, \
    .reduce_motion = false, \
    .animation_speed = 1.0f, \
    .reduce_transparency = false, \
    .wallpaper_path = "/System/Wallpapers/default.png", \
    .wallpaper_enabled = true \
}

/* ============ API Functions ============ */

/* Load appearance settings (returns 0 on success, uses defaults on failure) */
int nxappearance_load(appearance_settings_t *out);

/* Save appearance settings (returns 0 on success) */
int nxappearance_save(const appearance_settings_t *settings);

/* Apply settings - notifies desktop to reload */
void nxappearance_apply(const appearance_settings_t *settings);

/* Get current global settings pointer */
appearance_settings_t* nxappearance_get(void);

/* ============ Helper Macros ============ */

/* Get glass color with current opacity */
#define GLASS_COLOR(base_rgb, settings) \
    (((uint32_t)(settings)->glass_opacity << 24) | ((base_rgb) & 0x00FFFFFF))

/* Get accent color value */
#define ACCENT_COLOR(settings) \
    accent_color_values[(settings)->accent_color_index]

#endif /* NXAPPEARANCE_H */
