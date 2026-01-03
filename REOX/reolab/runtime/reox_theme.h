/*
 * REOX Theme System
 * Self-contained theming for REOX UI
 * 
 * Note: REOX UI defines themes, NXRender decodes and renders them.
 * This header has no NXRender dependencies.
 */

#ifndef REOX_THEME_H
#define REOX_THEME_H

#include "reox_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * REOX Theme Mode
 * ============================================================================ */

typedef enum rx_theme_mode {
    RX_THEME_LIGHT,
    RX_THEME_DARK,
    RX_THEME_SYSTEM,    /* Follow system preference */
} rx_theme_mode;

/* ============================================================================
 * Semantic Colors
 * ============================================================================ */

typedef struct rx_semantic_colors {
    rx_color primary;
    rx_color primary_hover;
    rx_color primary_pressed;
    
    rx_color secondary;
    rx_color secondary_hover;
    
    rx_color success;
    rx_color success_hover;
    
    rx_color warning;
    rx_color warning_hover;
    
    rx_color error;
    rx_color error_hover;
    
    rx_color info;
    rx_color info_hover;
} rx_semantic_colors;

/* ============================================================================
 * Surface Colors
 * ============================================================================ */

typedef struct rx_surface_colors {
    rx_color background;
    rx_color surface;
    rx_color surface_hover;
    rx_color surface_selected;
    rx_color overlay;
    rx_color card;
    rx_color modal;
} rx_surface_colors;

/* ============================================================================
 * Text Colors
 * ============================================================================ */

typedef struct rx_text_colors {
    rx_color primary;
    rx_color secondary;
    rx_color disabled;
    rx_color on_primary;
    rx_color placeholder;
    rx_color link;
    rx_color link_hover;
} rx_text_colors;

/* ============================================================================
 * Border Colors
 * ============================================================================ */

typedef struct rx_border_colors {
    rx_color default_border;
    rx_color focus;
    rx_color error;
    rx_color divider;
} rx_border_colors;

/* ============================================================================
 * Typography
 * ============================================================================ */

typedef struct rx_font_style {
    const char* family;
    float size;
    int weight;     /* 100-900 */
    float line_height;
    float letter_spacing;
} rx_font_style;

typedef struct rx_typography {
    rx_font_style display_large;    /* Hero headings */
    rx_font_style display_medium;
    rx_font_style display_small;
    
    rx_font_style headline_large;   /* Page headings */
    rx_font_style headline_medium;
    rx_font_style headline_small;
    
    rx_font_style title_large;      /* Section titles */
    rx_font_style title_medium;
    rx_font_style title_small;
    
    rx_font_style body_large;       /* Body text */
    rx_font_style body_medium;
    rx_font_style body_small;
    
    rx_font_style label_large;      /* Labels, buttons */
    rx_font_style label_medium;
    rx_font_style label_small;
    
    rx_font_style code;             /* Monospace */
} rx_typography;

/* ============================================================================
 * Spacing
 * ============================================================================ */

typedef struct rx_spacing {
    float xs;       /* 4px */
    float sm;       /* 8px */
    float md;       /* 16px */
    float lg;       /* 24px */
    float xl;       /* 32px */
    float xxl;      /* 48px */
} rx_spacing;

/* ============================================================================
 * Border Radius
 * ============================================================================ */

typedef struct rx_radii {
    float none;     /* 0 */
    float sm;       /* 4px */
    float md;       /* 6px */
    float lg;       /* 8px */
    float xl;       /* 12px */
    float full;     /* 9999px (pill shape) */
} rx_radii;

/* ============================================================================
 * Shadows
 * ============================================================================ */

typedef struct rx_shadow_def {
    float offset_x;
    float offset_y;
    float blur;
    float spread;
    rx_color color;
} rx_shadow_def;

typedef struct rx_shadows {
    rx_shadow_def none;
    rx_shadow_def sm;
    rx_shadow_def md;
    rx_shadow_def lg;
    rx_shadow_def xl;
} rx_shadows;

/* ============================================================================
 * Complete Theme
 * ============================================================================ */

typedef struct rx_theme {
    const char* name;
    rx_theme_mode mode;
    
    /* Colors */
    rx_semantic_colors semantic;
    rx_surface_colors surface;
    rx_text_colors text;
    rx_border_colors border;
    
    /* Typography */
    rx_typography typography;
    
    /* Spacing & Layout */
    rx_spacing spacing;
    rx_radii radius;
    rx_shadows shadows;
    
} rx_theme;

/* ============================================================================
 * Theme Creation
 * ============================================================================ */

/* Create light theme */
extern rx_theme* rx_theme_light(void);

/* Create dark theme */
extern rx_theme* rx_theme_dark(void);

/* Create theme following system preference */
extern rx_theme* rx_theme_system(void);

/* Destroy theme */
extern void rx_theme_destroy(rx_theme* theme);

/* ============================================================================
 * Theme Switching
 * ============================================================================ */

/* Global current theme */
extern rx_theme* rx_current_theme;

/* Set current theme */
extern void rx_theme_set_current(rx_theme* theme);

/* Get current theme */
extern rx_theme* rx_theme_get_current(void);

/* Toggle between light/dark */
extern void rx_theme_toggle(void);

/* ============================================================================
 * NeolyxOS Preset Themes
 * ============================================================================ */

/* NeolyxOS default dark theme (primary) */
extern rx_theme* rx_theme_neolyx_dark(void);

/* NeolyxOS light theme */
extern rx_theme* rx_theme_neolyx_light(void);

/* High contrast theme (accessibility) */
extern rx_theme* rx_theme_high_contrast(void);

/* ============================================================================
 * Color Utilities
 * ============================================================================ */

/* Get color by name from theme */
static inline rx_color rx_theme_color(rx_theme* theme, const char* name) {
    if (!theme) return RX_COLOR_PRIMARY;
    return theme->semantic.primary;
}

/* ============================================================================
 * Theme Builder (for custom themes)
 * ============================================================================ */

typedef struct rx_theme_builder {
    rx_theme theme;
} rx_theme_builder;

extern rx_theme_builder* rx_theme_builder_new(rx_theme_mode mode);
extern rx_theme_builder* rx_theme_builder_set_primary(rx_theme_builder* b, rx_color color);
extern rx_theme_builder* rx_theme_builder_set_secondary(rx_theme_builder* b, rx_color color);
extern rx_theme_builder* rx_theme_builder_set_background(rx_theme_builder* b, rx_color color);
extern rx_theme_builder* rx_theme_builder_set_surface(rx_theme_builder* b, rx_color color);
extern rx_theme_builder* rx_theme_builder_set_text(rx_theme_builder* b, rx_color color);
extern rx_theme_builder* rx_theme_builder_set_font_family(rx_theme_builder* b, const char* family);
extern rx_theme* rx_theme_builder_build(rx_theme_builder* b);
extern void rx_theme_builder_destroy(rx_theme_builder* b);

#ifdef __cplusplus
}
#endif

#endif /* REOX_THEME_H */
