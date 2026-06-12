/*
 * NeolyxOS - NXRender Theme System Implementation
 * Vajra Design Language
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "theme/theme.h"

/* ============ Vajra Dark Theme (default) ============ */

static const nx_theme_t THEME_VAJRA_DARK = {
    .name = "Vajra Dark",
    .colors = {
        .primary         = { 232, 102, 10, 255 },   /* Saffron orange #E8660A */
        .primary_hover   = { 245, 120, 30, 255 },
        .primary_pressed = { 200, 85,  8, 255 },
        .secondary       = { 109, 40, 217, 255 },   /* Deep violet #6D28D9 */
        .background      = {   7, 11,  20, 255 },   /* Near-black navy #070B14 */
        .surface         = {  13, 20,  36, 255 },   /* Dark surface #0D1424 */
        .surface_variant = {  20, 32,  56, 255 },   /* Elevated surface #142038 */
        .text_primary    = { 240, 244, 255, 255 },   /* Off-white */
        .text_secondary  = { 140, 160, 192, 255 },   /* Muted blue-gray */
        .text_disabled   = {  80,  90, 110, 255 },
        .border          = {  31,  48,  80, 255 },   /* Dark border #1F3050 */
        .divider         = {  25,  38,  64, 255 },
        .error           = { 239,  68,  68, 255 },
        .success         = {  16, 185, 129, 255 },   /* Green #10B981 */
        .warning         = { 217, 119,   6, 255 },   /* Amber #D97706 */
        .accent          = {  14, 165, 233, 255 },   /* Sky blue #0EA5E9 */
        .on_surface      = { 240, 244, 255, 255 },   /* Off-white (on dark bg) */
        .on_surface_muted= { 140, 160, 192, 255 },   /* Muted (on dark bg) */
    },
    .typography = {
        .size_h1 = 32, .size_h2 = 24, .size_h3 = 20,
        .size_body = 14, .size_small = 12, .size_caption = 10
    },
    .spacing = { .xs = 4, .sm = 8, .md = 16, .lg = 24, .xl = 32 },
    .corner_radius_sm = 4,
    .corner_radius_md = 8,
    .corner_radius_lg = 16,
    .border_width = 1,

    .glass = {
        .enabled = true,
        .blur_radius = 12,
        .surface_alpha = 165,     /* ~65% opacity */
    },
    .shadow = {
        .enabled = true,
        .spread = 8,
        .opacity = 0.55f,
        .y_offset = 4,
    },
    .gradient = {
        .start = { 232, 102, 10, 255 },   /* Saffron orange */
        .end   = { 217, 119,  6, 255 },   /* Warm amber */
        .angle = 135,
    },
    .yantra = {
        .enabled = true,
        .opacity = 13,            /* ~5% visibility */
    },
    .motion = {
        .anim_speed_mult = 1.0f,
        .reduce_motion = false,
    },
    .night_mode_strength = 0.0f,
};

/* ============ Legacy Dark Theme (backward compat) ============ */

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
        .warning       = { 255, 200, 80, 255 },
        .accent        = { 100, 150, 255, 255 },
        .on_surface    = { 255, 255, 255, 255 },
        .on_surface_muted = { 180, 180, 185, 255 },
    },
    .typography = { .size_h1 = 32, .size_h2 = 24, .size_h3 = 20, .size_body = 14, .size_small = 12, .size_caption = 10 },
    .spacing = { .xs = 4, .sm = 8, .md = 16, .lg = 24, .xl = 32 },
    .corner_radius_sm = 4,
    .corner_radius_md = 8,
    .corner_radius_lg = 16,
    .border_width = 1,
    .glass    = { .enabled = false, .blur_radius = 0, .surface_alpha = 255 },
    .shadow   = { .enabled = true, .spread = 6, .opacity = 0.40f, .y_offset = 3 },
    .gradient = { .start = {100,150,255,255}, .end = {150,100,255,255}, .angle = 135 },
    .yantra   = { .enabled = false, .opacity = 0 },
    .motion   = { .anim_speed_mult = 1.0f, .reduce_motion = false },
    .night_mode_strength = 0.0f,
};

/* ============ Light Theme ============ */

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
        .warning       = { 230, 160, 40, 255 },
        .accent        = { 50, 120, 220, 255 },
        .on_surface    = { 20, 20, 25, 255 },
        .on_surface_muted = { 80, 80, 85, 255 },
    },
    .typography = { .size_h1 = 32, .size_h2 = 24, .size_h3 = 20, .size_body = 14, .size_small = 12, .size_caption = 10 },
    .spacing = { .xs = 4, .sm = 8, .md = 16, .lg = 24, .xl = 32 },
    .corner_radius_sm = 4,
    .corner_radius_md = 8,
    .corner_radius_lg = 16,
    .border_width = 1,
    .glass    = { .enabled = false, .blur_radius = 0, .surface_alpha = 255 },
    .shadow   = { .enabled = true, .spread = 4, .opacity = 0.20f, .y_offset = 2 },
    .gradient = { .start = {50,120,220,255}, .end = {120,80,220,255}, .angle = 135 },
    .yantra   = { .enabled = false, .opacity = 0 },
    .motion   = { .anim_speed_mult = 1.0f, .reduce_motion = false },
    .night_mode_strength = 0.0f,
};

/* ============ Global State ============ */

static const nx_theme_t* g_current_theme = &THEME_VAJRA_DARK;

/* ============ Theme Accessors ============ */

const nx_theme_t* nx_theme_dark(void) { return &THEME_DARK; }
const nx_theme_t* nx_theme_light(void) { return &THEME_LIGHT; }
const nx_theme_t* nx_theme_vajra_dark(void) { return &THEME_VAJRA_DARK; }

void nx_set_theme(const nx_theme_t* theme) {
    g_current_theme = theme ? theme : &THEME_VAJRA_DARK;
}

const nx_theme_t* nx_get_theme(void) {
    return g_current_theme;
}

nx_color_t nx_color_primary(void) { return g_current_theme->colors.primary; }
nx_color_t nx_color_background(void) { return g_current_theme->colors.background; }
nx_color_t nx_color_surface(void) { return g_current_theme->colors.surface; }
nx_color_t nx_color_text(void) { return g_current_theme->colors.text_primary; }
nx_color_t nx_color_accent(void) { return g_current_theme->colors.accent; }
nx_color_t nx_color_on_surface(void) { return g_current_theme->colors.on_surface; }
uint32_t nx_spacing_sm(void) { return g_current_theme->spacing.sm; }
uint32_t nx_spacing_md(void) { return g_current_theme->spacing.md; }
