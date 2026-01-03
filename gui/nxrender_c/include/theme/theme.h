/*
 * NeolyxOS - NXRender Theme System
 * Light and dark theme support
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_THEME_H
#define NXRENDER_THEME_H

#include "nxgfx/nxgfx.h"

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
} nx_theme_t;

/* Built-in themes */
const nx_theme_t* nx_theme_dark(void);
const nx_theme_t* nx_theme_light(void);

/* Global theme */
void nx_set_theme(const nx_theme_t* theme);
const nx_theme_t* nx_get_theme(void);

/* Theme accessors (uses global theme) */
nx_color_t nx_color_primary(void);
nx_color_t nx_color_background(void);
nx_color_t nx_color_surface(void);
nx_color_t nx_color_text(void);
uint32_t nx_spacing_sm(void);
uint32_t nx_spacing_md(void);

#ifdef __cplusplus
}
#endif
#endif
