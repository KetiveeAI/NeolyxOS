/*
 * NeolyxOS - NXRender Theme System Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "theme/theme.h"

static const nx_theme_t THEME_DARK = {
    .name = "Dark",
    .colors = {
        .primary       = { 100, 150, 255, 255 },
        .primary_hover = { 120, 170, 255, 255 },
        .primary_pressed = { 80, 130, 220, 255 },
        .secondary     = { 150, 100, 255, 255 },
        .background    = { 18, 18, 22, 255 },
        .surface       = { 30, 30, 35, 255 },
        .surface_variant = { 45, 45, 50, 255 },
        .text_primary  = { 255, 255, 255, 255 },
        .text_secondary = { 180, 180, 185, 255 },
        .text_disabled = { 100, 100, 105, 255 },
        .border        = { 60, 60, 65, 255 },
        .divider       = { 50, 50, 55, 255 },
        .error         = { 255, 90, 90, 255 },
        .success       = { 90, 220, 130, 255 },
        .warning       = { 255, 200, 80, 255 }
    },
    .typography = { .size_h1 = 32, .size_h2 = 24, .size_h3 = 20, .size_body = 14, .size_small = 12, .size_caption = 10 },
    .spacing = { .xs = 4, .sm = 8, .md = 16, .lg = 24, .xl = 32 },
    .corner_radius_sm = 4,
    .corner_radius_md = 8,
    .corner_radius_lg = 16,
    .border_width = 1
};

static const nx_theme_t THEME_LIGHT = {
    .name = "Light",
    .colors = {
        .primary       = { 50, 120, 220, 255 },
        .primary_hover = { 70, 140, 240, 255 },
        .primary_pressed = { 30, 100, 200, 255 },
        .secondary     = { 120, 80, 220, 255 },
        .background    = { 245, 245, 248, 255 },
        .surface       = { 255, 255, 255, 255 },
        .surface_variant = { 240, 240, 243, 255 },
        .text_primary  = { 20, 20, 25, 255 },
        .text_secondary = { 80, 80, 85, 255 },
        .text_disabled = { 160, 160, 165, 255 },
        .border        = { 200, 200, 205, 255 },
        .divider       = { 220, 220, 225, 255 },
        .error         = { 220, 60, 60, 255 },
        .success       = { 40, 180, 100, 255 },
        .warning       = { 230, 160, 40, 255 }
    },
    .typography = { .size_h1 = 32, .size_h2 = 24, .size_h3 = 20, .size_body = 14, .size_small = 12, .size_caption = 10 },
    .spacing = { .xs = 4, .sm = 8, .md = 16, .lg = 24, .xl = 32 },
    .corner_radius_sm = 4,
    .corner_radius_md = 8,
    .corner_radius_lg = 16,
    .border_width = 1
};

static const nx_theme_t* g_current_theme = &THEME_DARK;

const nx_theme_t* nx_theme_dark(void) { return &THEME_DARK; }
const nx_theme_t* nx_theme_light(void) { return &THEME_LIGHT; }

void nx_set_theme(const nx_theme_t* theme) {
    g_current_theme = theme ? theme : &THEME_DARK;
}

const nx_theme_t* nx_get_theme(void) {
    return g_current_theme;
}

nx_color_t nx_color_primary(void) { return g_current_theme->colors.primary; }
nx_color_t nx_color_background(void) { return g_current_theme->colors.background; }
nx_color_t nx_color_surface(void) { return g_current_theme->colors.surface; }
nx_color_t nx_color_text(void) { return g_current_theme->colors.text_primary; }
uint32_t nx_spacing_sm(void) { return g_current_theme->spacing.sm; }
uint32_t nx_spacing_md(void) { return g_current_theme->spacing.md; }
