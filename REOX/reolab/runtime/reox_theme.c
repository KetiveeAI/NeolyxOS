/*
 * REOX Theme System - Implementation
 * Self-contained theming for REOX UI
 * 
 * REOX is an independent language compiled by reolab compiler.
 * This is the runtime library - NXRender is a separate renderer.
 */

#include "reox_theme.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Global Theme State
 * ============================================================================ */

rx_theme* rx_current_theme = NULL;

/* ============================================================================
 * Default Values
 * ============================================================================ */

static rx_typography default_typography(void) {
    return (rx_typography){
        .display_large = { "System", 57, 400, 64, -0.25 },
        .display_medium = { "System", 45, 400, 52, 0 },
        .display_small = { "System", 36, 400, 44, 0 },
        
        .headline_large = { "System", 32, 400, 40, 0 },
        .headline_medium = { "System", 28, 400, 36, 0 },
        .headline_small = { "System", 24, 400, 32, 0 },
        
        .title_large = { "System", 22, 500, 28, 0 },
        .title_medium = { "System", 16, 500, 24, 0.15 },
        .title_small = { "System", 14, 500, 20, 0.1 },
        
        .body_large = { "System", 16, 400, 24, 0.5 },
        .body_medium = { "System", 14, 400, 20, 0.25 },
        .body_small = { "System", 12, 400, 16, 0.4 },
        
        .label_large = { "System", 14, 500, 20, 0.1 },
        .label_medium = { "System", 12, 500, 16, 0.5 },
        .label_small = { "System", 11, 500, 16, 0.5 },
        
        .code = { "Monospace", 14, 400, 20, 0 },
    };
}

static rx_spacing default_spacing(void) {
    return (rx_spacing){
        .xs = 4.0f,
        .sm = 8.0f,
        .md = 16.0f,
        .lg = 24.0f,
        .xl = 32.0f,
        .xxl = 48.0f,
    };
}

static rx_radii default_radii(void) {
    return (rx_radii){
        .none = 0.0f,
        .sm = 4.0f,
        .md = 6.0f,
        .lg = 8.0f,
        .xl = 12.0f,
        .full = 9999.0f,
    };
}

static rx_shadows default_shadows(void) {
    return (rx_shadows){
        .none = { 0, 0, 0, 0, { 0, 0, 0, 0 } },
        .sm = { 0, 1, 2, 0, { 0, 0, 0, 20 } },
        .md = { 0, 4, 6, -1, { 0, 0, 0, 25 } },
        .lg = { 0, 10, 15, -3, { 0, 0, 0, 30 } },
        .xl = { 0, 20, 25, -5, { 0, 0, 0, 35 } },
    };
}

/* ============================================================================
 * Light Theme
 * ============================================================================ */

rx_theme* rx_theme_light(void) {
    rx_theme* theme = (rx_theme*)calloc(1, sizeof(rx_theme));
    if (!theme) return NULL;
    
    theme->name = "Light";
    theme->mode = RX_THEME_LIGHT;
    
    /* Semantic Colors */
    theme->semantic.primary = color_rgb(59, 130, 246);
    theme->semantic.primary_hover = color_rgb(37, 99, 235);
    theme->semantic.primary_pressed = color_rgb(29, 78, 216);
    
    theme->semantic.secondary = color_rgb(107, 114, 128);
    theme->semantic.secondary_hover = color_rgb(75, 85, 99);
    
    theme->semantic.success = color_rgb(34, 197, 94);
    theme->semantic.success_hover = color_rgb(22, 163, 74);
    
    theme->semantic.warning = color_rgb(245, 158, 11);
    theme->semantic.warning_hover = color_rgb(217, 119, 6);
    
    theme->semantic.error = color_rgb(239, 68, 68);
    theme->semantic.error_hover = color_rgb(220, 38, 38);
    
    theme->semantic.info = color_rgb(96, 165, 250);
    theme->semantic.info_hover = color_rgb(59, 130, 246);
    
    /* Surface Colors */
    theme->surface.background = color_rgb(255, 255, 255);
    theme->surface.surface = color_rgb(249, 250, 251);
    theme->surface.surface_hover = color_rgb(243, 244, 246);
    theme->surface.surface_selected = color_rgb(239, 246, 255);
    theme->surface.overlay = color_rgba(0, 0, 0, 128);
    theme->surface.card = color_rgb(255, 255, 255);
    theme->surface.modal = color_rgb(255, 255, 255);
    
    /* Text Colors */
    theme->text.primary = color_rgb(17, 24, 39);
    theme->text.secondary = color_rgb(75, 85, 99);
    theme->text.disabled = color_rgb(156, 163, 175);
    theme->text.on_primary = color_rgb(255, 255, 255);
    theme->text.placeholder = color_rgb(156, 163, 175);
    theme->text.link = color_rgb(37, 99, 235);
    theme->text.link_hover = color_rgb(29, 78, 216);
    
    /* Border Colors */
    theme->border.default_border = color_rgb(209, 213, 219);
    theme->border.focus = theme->semantic.primary;
    theme->border.error = theme->semantic.error;
    theme->border.divider = color_rgb(229, 231, 235);
    
    /* Typography, Spacing, etc. */
    theme->typography = default_typography();
    theme->spacing = default_spacing();
    theme->radius = default_radii();
    theme->shadows = default_shadows();
    
    return theme;
}

/* ============================================================================
 * Dark Theme
 * ============================================================================ */

rx_theme* rx_theme_dark(void) {
    rx_theme* theme = (rx_theme*)calloc(1, sizeof(rx_theme));
    if (!theme) return NULL;
    
    theme->name = "Dark";
    theme->mode = RX_THEME_DARK;
    
    /* Semantic Colors */
    theme->semantic.primary = color_rgb(96, 165, 250);
    theme->semantic.primary_hover = color_rgb(147, 197, 253);
    theme->semantic.primary_pressed = color_rgb(191, 219, 254);
    
    theme->semantic.secondary = color_rgb(156, 163, 175);
    theme->semantic.secondary_hover = color_rgb(209, 213, 219);
    
    theme->semantic.success = color_rgb(74, 222, 128);
    theme->semantic.success_hover = color_rgb(134, 239, 172);
    
    theme->semantic.warning = color_rgb(251, 191, 36);
    theme->semantic.warning_hover = color_rgb(252, 211, 77);
    
    theme->semantic.error = color_rgb(248, 113, 113);
    theme->semantic.error_hover = color_rgb(252, 165, 165);
    
    theme->semantic.info = color_rgb(147, 197, 253);
    theme->semantic.info_hover = color_rgb(191, 219, 254);
    
    /* Surface Colors */
    theme->surface.background = color_rgb(17, 24, 39);
    theme->surface.surface = color_rgb(31, 41, 55);
    theme->surface.surface_hover = color_rgb(55, 65, 81);
    theme->surface.surface_selected = color_rgb(30, 58, 138);
    theme->surface.overlay = color_rgba(0, 0, 0, 180);
    theme->surface.card = color_rgb(31, 41, 55);
    theme->surface.modal = color_rgb(31, 41, 55);
    
    /* Text Colors */
    theme->text.primary = color_rgb(243, 244, 246);
    theme->text.secondary = color_rgb(156, 163, 175);
    theme->text.disabled = color_rgb(75, 85, 99);
    theme->text.on_primary = color_rgb(255, 255, 255);
    theme->text.placeholder = color_rgb(107, 114, 128);
    theme->text.link = color_rgb(96, 165, 250);
    theme->text.link_hover = color_rgb(147, 197, 253);
    
    /* Border Colors */
    theme->border.default_border = color_rgb(75, 85, 99);
    theme->border.focus = theme->semantic.primary;
    theme->border.error = theme->semantic.error;
    theme->border.divider = color_rgb(55, 65, 81);
    
    /* Typography, Spacing, etc. */
    theme->typography = default_typography();
    theme->spacing = default_spacing();
    theme->radius = default_radii();
    theme->shadows = default_shadows();
    
    return theme;
}

/* ============================================================================
 * NeolyxOS Themes
 * ============================================================================ */

rx_theme* rx_theme_neolyx_dark(void) {
    rx_theme* theme = rx_theme_dark();
    if (!theme) return NULL;
    
    theme->name = "NeolyxOS Dark";
    
    /* NeolyxOS brand colors */
    theme->semantic.primary = color_rgb(0, 122, 255);
    theme->semantic.primary_hover = color_rgb(10, 132, 255);
    theme->semantic.primary_pressed = color_rgb(0, 112, 245);
    
    /* Deeper dark background */
    theme->surface.background = color_rgb(28, 28, 30);
    theme->surface.surface = color_rgb(44, 44, 46);
    theme->surface.card = color_rgb(44, 44, 46);
    
    /* Accent */
    theme->semantic.secondary = color_rgb(88, 86, 214);
    
    return theme;
}

rx_theme* rx_theme_neolyx_light(void) {
    rx_theme* theme = rx_theme_light();
    if (!theme) return NULL;
    
    theme->name = "NeolyxOS Light";
    
    /* NeolyxOS brand colors */
    theme->semantic.primary = color_rgb(0, 122, 255);
    theme->semantic.primary_hover = color_rgb(0, 112, 245);
    theme->semantic.primary_pressed = color_rgb(0, 102, 235);
    
    /* Light background */
    theme->surface.background = color_rgb(242, 242, 247);
    theme->surface.surface = color_rgb(255, 255, 255);
    
    return theme;
}

rx_theme* rx_theme_high_contrast(void) {
    rx_theme* theme = rx_theme_dark();
    if (!theme) return NULL;
    
    theme->name = "High Contrast";
    
    /* Pure black/white */
    theme->surface.background = color_rgb(0, 0, 0);
    theme->surface.surface = color_rgb(0, 0, 0);
    theme->surface.card = color_rgb(0, 0, 0);
    
    theme->text.primary = color_rgb(255, 255, 255);
    theme->text.secondary = color_rgb(255, 255, 255);
    
    theme->semantic.primary = color_rgb(0, 255, 255);
    theme->border.default_border = color_rgb(255, 255, 255);
    theme->border.focus = color_rgb(255, 255, 0);
    
    return theme;
}

rx_theme* rx_theme_system(void) {
    return rx_theme_neolyx_dark();
}

/* ============================================================================
 * Theme Lifecycle
 * ============================================================================ */

void rx_theme_destroy(rx_theme* theme) {
    free(theme);
}

void rx_theme_set_current(rx_theme* theme) {
    rx_current_theme = theme;
}

rx_theme* rx_theme_get_current(void) {
    if (!rx_current_theme) {
        rx_current_theme = rx_theme_neolyx_dark();
    }
    return rx_current_theme;
}

void rx_theme_toggle(void) {
    if (!rx_current_theme) {
        rx_current_theme = rx_theme_neolyx_dark();
        return;
    }
    
    rx_theme* old = rx_current_theme;
    rx_current_theme = (old->mode == RX_THEME_DARK) ? 
        rx_theme_neolyx_light() : rx_theme_neolyx_dark();
    rx_theme_destroy(old);
}

/* ============================================================================
 * Theme Builder
 * ============================================================================ */

rx_theme_builder* rx_theme_builder_new(rx_theme_mode mode) {
    rx_theme_builder* b = (rx_theme_builder*)calloc(1, sizeof(rx_theme_builder));
    if (!b) return NULL;
    
    rx_theme* base = (mode == RX_THEME_DARK) ? rx_theme_dark() : rx_theme_light();
    if (base) {
        b->theme = *base;
        free(base);
    }
    
    return b;
}

rx_theme_builder* rx_theme_builder_set_primary(rx_theme_builder* b, rx_color color) {
    if (!b) return NULL;
    b->theme.semantic.primary = color;
    b->theme.semantic.primary_hover = color_lighten(color, 0.1f);
    b->theme.semantic.primary_pressed = color_darken(color, 0.1f);
    return b;
}

rx_theme_builder* rx_theme_builder_set_secondary(rx_theme_builder* b, rx_color color) {
    if (!b) return NULL;
    b->theme.semantic.secondary = color;
    return b;
}

rx_theme_builder* rx_theme_builder_set_background(rx_theme_builder* b, rx_color color) {
    if (!b) return NULL;
    b->theme.surface.background = color;
    return b;
}

rx_theme_builder* rx_theme_builder_set_surface(rx_theme_builder* b, rx_color color) {
    if (!b) return NULL;
    b->theme.surface.surface = color;
    b->theme.surface.card = color;
    return b;
}

rx_theme_builder* rx_theme_builder_set_text(rx_theme_builder* b, rx_color color) {
    if (!b) return NULL;
    b->theme.text.primary = color;
    return b;
}

rx_theme_builder* rx_theme_builder_set_font_family(rx_theme_builder* b, const char* family) {
    if (!b) return NULL;
    b->theme.typography.body_medium.family = family;
    return b;
}

rx_theme* rx_theme_builder_build(rx_theme_builder* b) {
    if (!b) return NULL;
    
    rx_theme* theme = (rx_theme*)malloc(sizeof(rx_theme));
    if (theme) {
        *theme = b->theme;
    }
    return theme;
}

void rx_theme_builder_destroy(rx_theme_builder* b) {
    free(b);
}
