/*
 * NeolyxOS - NXRender Theme System
 * Vajra Design Language + Neo-Aura Rendering
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef NXRENDER_THEME_H
#define NXRENDER_THEME_H

#include "nxgfx/nxgfx.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Color palette */
typedef struct {
    nx_color_t primary;
    nx_color_t primary_hover;
    nx_color_t primary_pressed;
    nx_color_t secondary;
    nx_color_t background;
    nx_color_t surface;
    nx_color_t surface_variant;
    nx_color_t text_primary;
    nx_color_t text_secondary;
    nx_color_t text_disabled;
    nx_color_t border;
    nx_color_t divider;
    nx_color_t error;
    nx_color_t success;
    nx_color_t warning;

    /* Extended palette */
    nx_color_t accent;           /* Sky blue — highlights, info badges */
    nx_color_t on_surface;       /* Off-white — primary text on dark surfaces */
    nx_color_t on_surface_muted; /* Muted blue-gray — secondary/caption text */
} nx_color_palette_t;

/* Typography */
typedef struct {
    uint32_t size_h1;
    uint32_t size_h2;
    uint32_t size_h3;
    uint32_t size_body;
    uint32_t size_small;
    uint32_t size_caption;
} nx_typography_t;

/* Spacing scale */
typedef struct {
    uint32_t xs;   /* 4 */
    uint32_t sm;   /* 8 */
    uint32_t md;   /* 16 */
    uint32_t lg;   /* 24 */
    uint32_t xl;   /* 32 */
} nx_spacing_t;

/* Glass / translucency configuration */
typedef struct {
    bool     enabled;        /* Master toggle */
    uint8_t  blur_radius;    /* Kawase blur radius, default 12 */
    uint8_t  surface_alpha;  /* Surface opacity 0-255, default 165 (~65%) */
} nx_glass_config_t;

/* Per-window shadow configuration */
typedef struct {
    bool     enabled;        /* Master toggle */
    uint8_t  spread;         /* Shadow spread in px, default 8 */
    float    opacity;        /* Shadow opacity 0-1, default 0.55 */
    int8_t   y_offset;       /* Downward bias in px, default 4 */
} nx_shadow_config_t;

/* Gradient accent (for buttons, progress bars, focus rings) */
typedef struct {
    nx_color_t start;        /* e.g. Saffron orange #E8660A */
    nx_color_t end;          /* e.g. Warm amber #D97706 */
    uint8_t    angle;        /* Degrees, default 135 */
} nx_gradient_accent_t;

/* Yantra Grid overlay configuration */
typedef struct {
    bool    enabled;         /* Master toggle */
    uint8_t opacity;         /* 0-255, default 13 (~5%) */
} nx_yantra_config_t;

/* Motion / accessibility */
typedef struct {
    float anim_speed_mult;   /* 0=off, 1=normal, 2=slow-motion, default 1.0 */
    bool  reduce_motion;     /* Accessibility: skip all animations */
} nx_motion_config_t;

/* Complete theme */
typedef struct {
    const char* name;
    nx_color_palette_t colors;
    nx_typography_t typography;
    nx_spacing_t spacing;
    uint32_t corner_radius_sm;
    uint32_t corner_radius_md;
    uint32_t corner_radius_lg;
    uint32_t border_width;

    /* Neo-Aura rendering extensions */
    nx_glass_config_t    glass;
    nx_shadow_config_t   shadow;
    nx_gradient_accent_t gradient;
    nx_yantra_config_t   yantra;
    nx_motion_config_t   motion;
    float                night_mode_strength; /* 0.0=off, 1.0=full warmth */
} nx_theme_t;

/* Built-in themes */
const nx_theme_t* nx_theme_dark(void);
const nx_theme_t* nx_theme_light(void);
const nx_theme_t* nx_theme_vajra_dark(void);

/* Global theme */
void nx_set_theme(const nx_theme_t* theme);
const nx_theme_t* nx_get_theme(void);

/* Theme accessors (uses global theme) */
nx_color_t nx_color_primary(void);
nx_color_t nx_color_background(void);
nx_color_t nx_color_surface(void);
nx_color_t nx_color_text(void);
nx_color_t nx_color_accent(void);
nx_color_t nx_color_on_surface(void);
uint32_t nx_spacing_sm(void);
uint32_t nx_spacing_md(void);

#ifdef __cplusplus
}
#endif
#endif

