/*
 * IconLay - Reox UI Implementation
 * Modern UI using Reox framework widgets
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "iconlay.h"
#include "iconlay_widgets.h"
#include <nxgfx/nxgfx.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Color Palette (Zinc-based dark theme)
 * ============================================================================ */

#define COL_BG          ((nx_color_t){0x09, 0x09, 0x0B, 0xFF})  /* zinc-950 */
#define COL_SIDEBAR     ((nx_color_t){0x18, 0x18, 0x1B, 0xFF})  /* zinc-900 */
#define COL_PANEL       ((nx_color_t){0x27, 0x27, 0x2A, 0xFF})  /* zinc-800 */
#define COL_BORDER      ((nx_color_t){0x3F, 0x3F, 0x46, 0xFF})  /* zinc-700 */
#define COL_TEXT        ((nx_color_t){0xFA, 0xFA, 0xFA, 0xFF})  /* zinc-50 */
#define COL_TEXT_DIM    ((nx_color_t){0xA1, 0xA1, 0xAA, 0xFF})  /* zinc-400 */
#define COL_ACCENT      ((nx_color_t){0x3B, 0x82, 0xF6, 0xFF})  /* blue-500 */
#define COL_ACCENT_HVR  ((nx_color_t){0x60, 0xA5, 0xFA, 0xFF})  /* blue-400 */
#define COL_ORANGE      ((nx_color_t){0xF9, 0x73, 0x16, 0xFF})  /* orange-500 */
#define COL_PURPLE      ((nx_color_t){0xA8, 0x55, 0xF7, 0xFF})  /* purple-500 */

/* ============================================================================
 * UI Helpers
 * ============================================================================ */

static bool point_in_rect(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* ============================================================================
 * Reox Widget Initialization
 * ============================================================================ */

static void slider_callback(float value, void* user_data);
static void toggle_callback(bool value, void* user_data);

void iconlay_ui_init_widgets(iconlay_app_t* app) {
    if (!app) return;
    
    int px = app->fb_width - ICONLAY_RIGHT_PANEL_WIDTH;
    int pw = ICONLAY_RIGHT_PANEL_WIDTH;
    int y = ICONLAY_TOOLBAR_HEIGHT + 72 + 28 + (44 + 8) * 3 + 24 + 28;
    
    /* Create effect sliders */
    const char* slider_names[] = {"Blur", "Brightness", "Contrast", "Saturation", "Shadow", "Glow", "Depth"};
    float* slider_values[] = {&app->fx_blur, &app->fx_brightness, &app->fx_contrast,
                              &app->fx_saturation, &app->fx_shadow, &app->fx_glow_amount, &app->fx_depth};
    
    for (int i = 0; i < 7; i++) {
        iconlay_slider_t* s = iconlay_slider_create(slider_names[i], 0, 200, *slider_values[i]);
        if (s) {
            iconlay_slider_set_bounds(s, px + 16, y + i * 38, pw - 32, 32);
            iconlay_slider_set_callback(s, slider_callback, app);
            app->effect_sliders[i] = s;
        }
    }
    
    /* Create effect toggles */
    const char* toggle_names[] = {"Glass", "Frost", "Metal", "Liquid", "Glow"};
    bool* toggle_values[] = {&app->fx_glass_enabled, &app->fx_frost_enabled, 
                             &app->fx_metal_enabled, &app->fx_liquid_enabled, &app->fx_glow_enabled};
    
    int ty = ICONLAY_TOOLBAR_HEIGHT + 72 + 28;
    int bw = (pw - 48) / 2;
    
    for (int i = 0; i < 5; i++) {
        iconlay_toggle_t* t = iconlay_toggle_create(toggle_names[i], *toggle_values[i]);
        if (t) {
            int tx = (i % 2 == 0) ? px + 16 : px + 24 + bw;
            int row = i / 2;
            iconlay_toggle_set_bounds(t, tx, ty + row * 52, bw, 44);
            iconlay_toggle_set_callback(t, toggle_callback, app);
            app->fx_toggles[i] = t;
        }
    }
    
    /* Create color picker */
    iconlay_color_t initial = {59, 130, 246, 255};
    app->color_picker = iconlay_color_picker_create(initial);
}

void iconlay_ui_cleanup_widgets(iconlay_app_t* app) {
    if (!app) return;
    
    for (int i = 0; i < 7; i++) {
        if (app->effect_sliders[i]) {
            iconlay_slider_destroy((iconlay_slider_t*)app->effect_sliders[i]);
            app->effect_sliders[i] = NULL;
        }
    }
    
    for (int i = 0; i < 5; i++) {
        if (app->fx_toggles[i]) {
            iconlay_toggle_destroy((iconlay_toggle_t*)app->fx_toggles[i]);
            app->fx_toggles[i] = NULL;
        }
    }
    
    if (app->color_picker) {
        iconlay_color_picker_destroy((iconlay_color_picker_t*)app->color_picker);
        app->color_picker = NULL;
    }
}

static void slider_callback(float value, void* user_data) {
    iconlay_app_t* app = (iconlay_app_t*)user_data;
    if (!app) return;
    
    /* Update app effect values based on slider */
    for (int i = 0; i < 7; i++) {
        iconlay_slider_t* s = (iconlay_slider_t*)app->effect_sliders[i];
        if (s) {
            float* values[] = {&app->fx_blur, &app->fx_brightness, &app->fx_contrast,
                               &app->fx_saturation, &app->fx_shadow, &app->fx_glow_amount, &app->fx_depth};
            *values[i] = iconlay_slider_get_value(s);
        }
    }
    (void)value;
    app->unsaved_changes = true;
    iconlay_invalidate_all(app);
}

static void toggle_callback(bool value, void* user_data) {
    iconlay_app_t* app = (iconlay_app_t*)user_data;
    if (!app) return;
    
    /* Update app effect toggles */
    bool* toggles[] = {&app->fx_glass_enabled, &app->fx_frost_enabled,
                       &app->fx_metal_enabled, &app->fx_liquid_enabled, &app->fx_glow_enabled};
    
    for (int i = 0; i < 5; i++) {
        iconlay_toggle_t* t = (iconlay_toggle_t*)app->fx_toggles[i];
        if (t) {
            *toggles[i] = iconlay_toggle_get_value(t);
        }
    }
    (void)value;
    app->unsaved_changes = true;
    iconlay_invalidate_all(app);
}

/* ============================================================================
 * Toolbar Rendering
 * ============================================================================ */

static void render_toolbar(iconlay_app_t* app) {
    nx_context_t* ctx = app->gfx;
    int h = ICONLAY_TOOLBAR_HEIGHT;
    
    /* Background */
    nxgfx_fill_rect(ctx, nx_rect(0, 0, app->fb_width, h), COL_SIDEBAR);
    nxgfx_fill_rect(ctx, nx_rect(0, h - 1, app->fb_width, 1), COL_BORDER);
    
    /* File name with unsaved indicator */
    nx_font_t* font = nxgfx_font_default(ctx, 14);
    nxgfx_draw_text(ctx, app->filename, (nx_point_t){ICONLAY_LEFT_PANEL_WIDTH + 20, 18}, font, COL_TEXT_DIM);
    
    if (app->unsaved_changes) {
        nxgfx_fill_circle(ctx, (nx_point_t){ICONLAY_LEFT_PANEL_WIDTH + 180, 26}, 4, COL_ORANGE);
    }
    
    /* Zoom dropdown */
    int bx = app->fb_width - ICONLAY_RIGHT_PANEL_WIDTH - 260;
    nxgfx_fill_rounded_rect(ctx, nx_rect(bx, 12, 70, 32), COL_PANEL, 4);
    nxgfx_draw_rect(ctx, nx_rect(bx, 12, 70, 32), COL_BORDER, 1);
    nxgfx_draw_text(ctx, "100%", (nx_point_t){bx + 18, 18}, font, COL_TEXT);
    
    /* Preview button */
    bx += 80;
    bool preview_hovered = point_in_rect(app->mouse_x, app->mouse_y, bx, 12, 100, 32);
    nxgfx_fill_rounded_rect(ctx, nx_rect(bx, 12, 100, 32), preview_hovered ? COL_PANEL : COL_PANEL, 4);
    nxgfx_draw_text(ctx, "Preview", (nx_point_t){bx + 24, 18}, font, COL_TEXT);
    if (preview_hovered) app->hovered_button = 50;
    
    /* Export button */
    bx += 110;
    bool export_hovered = point_in_rect(app->mouse_x, app->mouse_y, bx, 12, 120, 32);
    nxgfx_fill_rounded_rect(ctx, nx_rect(bx, 12, 120, 32), export_hovered ? COL_ACCENT_HVR : COL_ACCENT, 4);
    nxgfx_draw_text(ctx, "Export .nxi", (nx_point_t){bx + 20, 18}, font, COL_TEXT);
    if (export_hovered) app->hovered_button = 51;
}

/* ============================================================================
 * Left Sidebar - Layer Stack
 * ============================================================================ */

static void render_layer_panel(iconlay_app_t* app) {
    nx_context_t* ctx = app->gfx;
    int sx = 0;
    int sy = ICONLAY_TOOLBAR_HEIGHT;
    int sw = ICONLAY_LEFT_PANEL_WIDTH;
    int sh = app->fb_height - ICONLAY_TOOLBAR_HEIGHT;
    
    /* Background */
    nxgfx_fill_rect(ctx, nx_rect(sx, sy, sw, sh), COL_SIDEBAR);
    nxgfx_fill_rect(ctx, nx_rect(sw - 1, sy, 1, sh), COL_BORDER);
    
    nx_font_t* font = nxgfx_font_default(ctx, 14);
    nx_font_t* font_small = nxgfx_font_default(ctx, 11);
    nx_font_t* font_bold = nxgfx_font_default(ctx, 14);
    
    /* Header with branding */
    nxgfx_fill_rect(ctx, nx_rect(sx, sy, sw, 56), COL_SIDEBAR);
    nxgfx_fill_rect(ctx, nx_rect(sx, sy + 55, sw, 1), COL_BORDER);
    
    /* Logo */
    nxgfx_fill_rounded_rect(ctx, nx_rect(16, sy + 12, 32, 32), COL_ACCENT, 8);
    nxgfx_draw_text(ctx, "I", (nx_point_t){28, sy + 18}, font_bold, COL_TEXT);
    
    nxgfx_draw_text(ctx, "IconLay", (nx_point_t){56, sy + 12}, font_bold, COL_TEXT);
    nxgfx_draw_text(ctx, "NeolyxOS Native", (nx_point_t){56, sy + 30}, font_small, COL_TEXT_DIM);
    
    /* Import button */
    int by = sy + 72;
    bool import_hovered = point_in_rect(app->mouse_x, app->mouse_y, 16, by, sw - 32, 40);
    nxgfx_fill_rounded_rect(ctx, nx_rect(16, by, sw - 32, 40), import_hovered ? COL_ACCENT_HVR : COL_ACCENT, 8);
    nxgfx_draw_text(ctx, "+ Import Layer (SVG/PNG)", (nx_point_t){40, by + 12}, font, COL_TEXT);
    if (import_hovered) app->hovered_button = 1000;
    
    /* Layer Stack header */
    by += 56;
    nxgfx_fill_rect(ctx, nx_rect(sx, by, sw, 1), COL_BORDER);
    by += 16;
    
    char layer_count_buf[32];
    snprintf(layer_count_buf, sizeof(layer_count_buf), "LAYER STACK (%d)", app->layer_count);
    nxgfx_draw_text(ctx, layer_count_buf, (nx_point_t){16, by}, font_small, COL_TEXT_DIM);
    by += 28;
    
    /* Layer list */
    for (uint32_t i = 0; i < app->layer_count && i < 8; i++) {
        iconlay_layer_t* layer = &app->layers[i];
        bool selected = ((int32_t)i == app->selected_layer);
        bool hovered = point_in_rect(app->mouse_x, app->mouse_y, 12, by, sw - 24, 72);
        
        /* Layer card background */
        if (selected) {
            nxgfx_fill_rounded_rect(ctx, nx_rect(12, by, sw - 24, 72), 
                                    (nx_color_t){0x3B, 0x82, 0xF6, 0x33}, 8);
            nxgfx_draw_rect(ctx, nx_rect(12, by, sw - 24, 72), 
                           (nx_color_t){0x3B, 0x82, 0xF6, 0x80}, 1);
        } else if (hovered) {
            nxgfx_fill_rounded_rect(ctx, nx_rect(12, by, sw - 24, 72), COL_PANEL, 8);
        }
        if (hovered) app->hovered_button = 2000 + i;
        
        /* Type badge */
        const char* type_str = layer->svg ? "SVG" : "PNG";
        nxgfx_fill_rounded_rect(ctx, nx_rect(24, by + 16, 40, 40), COL_BG, 4);
        nxgfx_draw_rect(ctx, nx_rect(24, by + 16, 40, 40), COL_BORDER, 1);
        nxgfx_draw_text(ctx, type_str, (nx_point_t){30, by + 30}, font_small, COL_TEXT_DIM);
        
        /* Layer name */
        char name_buf[20];
        strncpy(name_buf, layer->name, 18);
        name_buf[18] = '\0';
        nxgfx_draw_text(ctx, name_buf, (nx_point_t){76, by + 18}, font, COL_TEXT);
        
        /* Blend mode and opacity */
        char info_buf[32];
        snprintf(info_buf, sizeof(info_buf), "Normal • %d%%", (layer->opacity * 100) / 255);
        nxgfx_draw_text(ctx, info_buf, (nx_point_t){76, by + 38}, font_small, COL_TEXT_DIM);
        
        /* Visibility toggle */
        nxgfx_fill_rounded_rect(ctx, nx_rect(sw - 50, by + 24, 24, 24), 
                                layer->visible ? COL_ACCENT : COL_PANEL, 4);
        
        by += 80;
    }
    
    /* Export format footer */
    int footer_y = app->fb_height - 80;
    nxgfx_fill_rect(ctx, nx_rect(sx, footer_y, sw, 1), COL_BORDER);
    nxgfx_draw_text(ctx, "EXPORT FORMAT", (nx_point_t){16, footer_y + 16}, font_small, COL_TEXT_DIM);
    nxgfx_draw_text(ctx, ".nxi (NeolyxOS Icon)", (nx_point_t){16, footer_y + 36}, font, COL_TEXT);
    nxgfx_draw_text(ctx, "v2.1", (nx_point_t){sw - 50, footer_y + 36}, font_small, COL_ACCENT);
}

/* ============================================================================
 * Right Sidebar - Effects Panel
 * ============================================================================ */

static void render_effects_panel(iconlay_app_t* app) {
    nx_context_t* ctx = app->gfx;
    int px = app->fb_width - ICONLAY_RIGHT_PANEL_WIDTH;
    int py = ICONLAY_TOOLBAR_HEIGHT;
    int pw = ICONLAY_RIGHT_PANEL_WIDTH;
    int ph = app->fb_height - ICONLAY_TOOLBAR_HEIGHT;
    
    /* Background */
    nxgfx_fill_rect(ctx, nx_rect(px, py, pw, ph), COL_SIDEBAR);
    nxgfx_fill_rect(ctx, nx_rect(px, py, 1, ph), COL_BORDER);
    
    nx_font_t* font = nxgfx_font_default(ctx, 14);
    nx_font_t* font_small = nxgfx_font_default(ctx, 11);
    nx_font_t* font_bold = nxgfx_font_default(ctx, 14);
    
    /* Header */
    nxgfx_fill_rect(ctx, nx_rect(px, py, pw, 56), COL_SIDEBAR);
    nxgfx_fill_rect(ctx, nx_rect(px, py + 55, pw, 1), COL_BORDER);
    nxgfx_draw_text(ctx, "Effects & Compositing", (nx_point_t){px + 32, py + 18}, font_bold, COL_TEXT);
    
    int y = py + 72;
    
    /* Material FX Section */
    nxgfx_draw_text(ctx, "MATERIAL FX", (nx_point_t){px + 16, y}, font_small, COL_TEXT_DIM);
    y += 28;
    
    int bw = (pw - 48) / 2;
    int bh = 44;
    
    /* Row 1: Glass, Frost */
    bool glass_active = app->fx_glass_enabled;
    bool glass_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, bw, bh);
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, bw, bh), 
                            glass_active ? COL_ACCENT : COL_PANEL, 8);
    if (glass_active) nxgfx_draw_rect(ctx, nx_rect(px + 16, y, bw, bh), COL_ACCENT_HVR, 1);
    nxgfx_draw_text(ctx, "Glass", (nx_point_t){px + 50, y + 14}, font, COL_TEXT);
    if (glass_hovered) app->hovered_button = 300;
    
    bool frost_active = app->fx_frost_enabled;
    bool frost_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 24 + bw, y, bw, bh);
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 24 + bw, y, bw, bh), 
                            frost_active ? COL_ACCENT : COL_PANEL, 8);
    nxgfx_draw_text(ctx, "Frost", (nx_point_t){px + 58 + bw, y + 14}, font, COL_TEXT);
    if (frost_hovered) app->hovered_button = 301;
    y += bh + 8;
    
    /* Row 2: Metal, Liquid */
    bool metal_active = app->fx_metal_enabled;
    bool metal_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, bw, bh);
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, bw, bh), 
                            metal_active ? COL_ACCENT : COL_PANEL, 8);
    nxgfx_draw_text(ctx, "Metal", (nx_point_t){px + 50, y + 14}, font, COL_TEXT);
    if (metal_hovered) app->hovered_button = 302;
    
    bool liquid_active = app->fx_liquid_enabled;
    bool liquid_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 24 + bw, y, bw, bh);
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 24 + bw, y, bw, bh), 
                            liquid_active ? COL_ACCENT : COL_PANEL, 8);
    nxgfx_draw_text(ctx, "Liquid", (nx_point_t){px + 58 + bw, y + 14}, font, COL_TEXT);
    if (liquid_hovered) app->hovered_button = 303;
    y += bh + 8;
    
    /* Row 3: Glow */
    bool glow_active = app->fx_glow_enabled;
    bool glow_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, bw, bh);
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, bw, bh), 
                            glow_active ? COL_ACCENT : COL_PANEL, 8);
    nxgfx_draw_text(ctx, "Glow", (nx_point_t){px + 50, y + 14}, font, COL_TEXT);
    if (glow_hovered) app->hovered_button = 304;
    y += bh + 24;
    
    /* Effect Parameters Section */
    nxgfx_draw_text(ctx, "EFFECT PARAMETERS", (nx_point_t){px + 16, y}, font_small, COL_TEXT_DIM);
    y += 28;
    
    /* Sliders */
    const char* slider_names[] = {"Blur", "Brightness", "Contrast", "Saturation", "Shadow", "Glow", "Depth"};
    float* slider_values[] = {&app->fx_blur, &app->fx_brightness, &app->fx_contrast,
                              &app->fx_saturation, &app->fx_shadow, &app->fx_glow_amount, &app->fx_depth};
    
    for (int i = 0; i < 7; i++) {
        nxgfx_draw_text(ctx, slider_names[i], (nx_point_t){px + 16, y}, font, COL_TEXT);
        
        char val_buf[8];
        snprintf(val_buf, sizeof(val_buf), "%d", (int)*slider_values[i]);
        nxgfx_draw_text(ctx, val_buf, (nx_point_t){px + pw - 50, y}, font_small, COL_TEXT_DIM);
        y += 20;
        
        /* Slider track */
        int slider_w = pw - 32;
        nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, slider_w, 6), COL_PANEL, 3);
        
        /* Slider fill */
        int fill_w = (int)(slider_w * (*slider_values[i]) / 200.0f);
        if (fill_w > 0) {
            nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, fill_w, 6), COL_ACCENT, 3);
        }
        y += 18;
    }
    
    y += 8;
    
    /* Export Sizes Section */
    nxgfx_draw_text(ctx, "EXPORT SIZES (in .nxi)", (nx_point_t){px + 16, y}, font_small, COL_TEXT_DIM);
    y += 24;
    
    const char* size_labels[] = {"Toolbar", "List", "Menu", "Dock", "AppIcon", "Display", "Retina"};
    const char* size_dims[] = {"16×16", "32×32", "64×64", "128×128", "256×256", "512×512", "1024×1024"};
    bool* size_enabled[] = {&app->export_16, &app->export_32, &app->export_64, 
                            &app->export_128, &app->export_256, &app->export_512, &app->export_1024};
    
    for (int i = 0; i < 7; i++) {
        bool row_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, pw - 32, 28);
        if (row_hovered) {
            nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, pw - 32, 28), COL_PANEL, 4);
            app->hovered_button = 400 + i;
        }
        
        /* Checkbox */
        nxgfx_draw_rect(ctx, nx_rect(px + 24, y + 6, 16, 16), COL_BORDER, 1);
        if (*size_enabled[i]) {
            nxgfx_fill_rect(ctx, nx_rect(px + 27, y + 9, 10, 10), COL_ACCENT);
        }
        
        nxgfx_draw_text(ctx, size_labels[i], (nx_point_t){px + 48, y + 6}, font, COL_TEXT);
        nxgfx_draw_text(ctx, size_dims[i], (nx_point_t){px + pw - 80, y + 8}, font_small, COL_TEXT_DIM);
        y += 32;
    }
    
    y += 8;
    
    /* Universal Icon Format info box */
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 16, y, pw - 32, 80), 
                            (nx_color_t){0x3B, 0x82, 0xF6, 0x20}, 8);
    nxgfx_draw_rect(ctx, nx_rect(px + 16, y, pw - 32, 80), 
                   (nx_color_t){0x3B, 0x82, 0xF6, 0x40}, 1);
    
    nxgfx_fill_rounded_rect(ctx, nx_rect(px + 28, y + 16, 32, 32), COL_ACCENT, 8);
    nxgfx_draw_text(ctx, "*", (nx_point_t){px + 40, y + 24}, font_bold, COL_TEXT);
    
    nxgfx_draw_text(ctx, "Universal Icon Format", (nx_point_t){px + 72, y + 14}, font_bold, COL_ACCENT_HVR);
    nxgfx_draw_text(ctx, "Exports as .nxi with all", (nx_point_t){px + 72, y + 34}, font_small, COL_TEXT_DIM);
    nxgfx_draw_text(ctx, "sizes for NeolyxOS.", (nx_point_t){px + 72, y + 50}, font_small, COL_TEXT_DIM);
}

/* ============================================================================
 * Canvas Area
 * ============================================================================ */

static void render_canvas(iconlay_app_t* app) {
    nx_context_t* ctx = app->gfx;
    
    /* Calculate canvas center */
    int canvas_area_w = app->fb_width - ICONLAY_LEFT_PANEL_WIDTH - ICONLAY_RIGHT_PANEL_WIDTH;
    int cx = ICONLAY_LEFT_PANEL_WIDTH + (canvas_area_w - ICONLAY_CANVAS_SIZE) / 2;
    int cy = ICONLAY_TOOLBAR_HEIGHT + 80;
    
    nx_font_t* font_small = nxgfx_font_default(ctx, 11);
    
    /* Size indicator */
    char size_buf[32];
    snprintf(size_buf, sizeof(size_buf), "%dx%dpx", app->preview_size, app->preview_size);
    nx_size_t text_size = nxgfx_measure_text(ctx, size_buf, font_small);
    int badge_w = text_size.width + 24;
    nxgfx_fill_rounded_rect(ctx, nx_rect(cx + (ICONLAY_CANVAS_SIZE - badge_w) / 2, cy - 40, badge_w, 28), COL_PANEL, 8);
    nxgfx_draw_text(ctx, size_buf, (nx_point_t){cx + (ICONLAY_CANVAS_SIZE - text_size.width) / 2, cy - 34}, font_small, COL_TEXT_DIM);
    
    /* Canvas card */
    nxgfx_fill_rounded_rect(ctx, nx_rect(cx - 4, cy - 4, ICONLAY_CANVAS_SIZE + 8, ICONLAY_CANVAS_SIZE + 8), COL_SIDEBAR, 12);
    nxgfx_draw_rect(ctx, nx_rect(cx - 4, cy - 4, ICONLAY_CANVAS_SIZE + 8, ICONLAY_CANVAS_SIZE + 8), COL_BORDER, 1);
    
    /* Grid pattern */
    for (int gx = cx; gx < cx + ICONLAY_CANVAS_SIZE; gx += 32) {
        nxgfx_draw_line(ctx, (nx_point_t){gx, cy}, (nx_point_t){gx, cy + ICONLAY_CANVAS_SIZE}, 
                        (nx_color_t){0x27, 0x27, 0x2A, 0x30}, 1);
    }
    for (int gy = cy; gy < cy + ICONLAY_CANVAS_SIZE; gy += 32) {
        nxgfx_draw_line(ctx, (nx_point_t){cx, gy}, (nx_point_t){cx + ICONLAY_CANVAS_SIZE, gy}, 
                        (nx_color_t){0x27, 0x27, 0x2A, 0x30}, 1);
    }
    
    /* Render preview */
    if (app->preview_buffer && app->preview_buffer->pixels) {
        /* Scale preview to canvas size */
        nx_image_t* preview_img = nxgfx_image_create(ctx, app->preview_buffer->pixels, 
                                                     app->preview_buffer->width, 
                                                     app->preview_buffer->height);
        if (preview_img) {
            nxgfx_draw_image_scaled(ctx, preview_img, nx_rect(cx, cy, ICONLAY_CANVAS_SIZE, ICONLAY_CANVAS_SIZE));
            nxgfx_image_destroy(preview_img);
        }
    }
    
    /* Size variant buttons */
    int btn_sizes[] = {16, 32, 64, 128, 256, 512};
    int btn_count = 6;
    int btn_w = 48;
    int btn_gap = 8;
    int btns_total_w = btn_count * btn_w + (btn_count - 1) * btn_gap;
    int btn_x = cx + (ICONLAY_CANVAS_SIZE - btns_total_w) / 2;
    int btn_y = cy + ICONLAY_CANVAS_SIZE + 24;
    
    for (int i = 0; i < btn_count; i++) {
        bool hovered = point_in_rect(app->mouse_x, app->mouse_y, btn_x, btn_y, btn_w, btn_w);
        bool selected = (app->preview_size == (uint32_t)btn_sizes[i]);
        
        nxgfx_fill_rounded_rect(ctx, nx_rect(btn_x, btn_y, btn_w, btn_w), 
                                selected ? COL_ACCENT : COL_PANEL, 8);
        if (hovered) app->hovered_button = 500 + i;
        
        char label[8];
        snprintf(label, sizeof(label), "%d", btn_sizes[i]);
        nx_size_t lsize = nxgfx_measure_text(ctx, label, font_small);
        nxgfx_draw_text(ctx, label, 
                        (nx_point_t){btn_x + (btn_w - lsize.width) / 2, btn_y + (btn_w - lsize.height) / 2},
                        font_small, selected ? COL_TEXT : COL_TEXT_DIM);
        
        btn_x += btn_w + btn_gap;
    }
}

/* ============================================================================
 * Main Render Function
 * ============================================================================ */

void iconlay_render(iconlay_app_t* app) {
    if (!app || !app->gfx) return;
    
    /* Clear background */
    nxgfx_clear(app->gfx, COL_BG);
    
    /* Reset hover state */
    app->hovered_button = -1;
    
    /* Render UI components */
    render_toolbar(app);
    render_layer_panel(app);
    render_canvas(app);
    render_effects_panel(app);
    
    /* Present frame */
    nxgfx_present(app->gfx);
}

/* ============================================================================
 * UI Event Handling Helpers
 * ============================================================================ */

static void handle_material_fx_click(iconlay_app_t* app, int button_id) {
    switch (button_id) {
        case 300: app->fx_glass_enabled = !app->fx_glass_enabled; break;
        case 301: app->fx_frost_enabled = !app->fx_frost_enabled; break;
        case 302: app->fx_metal_enabled = !app->fx_metal_enabled; break;
        case 303: app->fx_liquid_enabled = !app->fx_liquid_enabled; break;
        case 304: app->fx_glow_enabled = !app->fx_glow_enabled; break;
    }
    iconlay_invalidate_all(app);
}

static void handle_export_size_click(iconlay_app_t* app, int button_id) {
    int idx = button_id - 400;
    bool* sizes[] = {&app->export_16, &app->export_32, &app->export_64,
                     &app->export_128, &app->export_256, &app->export_512, &app->export_1024};
    if (idx >= 0 && idx < 7) {
        *sizes[idx] = !*sizes[idx];
    }
}

static void handle_preview_size_click(iconlay_app_t* app, int button_id) {
    int idx = button_id - 500;
    int sizes[] = {16, 32, 64, 128, 256, 512};
    if (idx >= 0 && idx < 6) {
        iconlay_update_preview(app, sizes[idx]);
    }
}

void iconlay_handle_ui_click(iconlay_app_t* app, int32_t x, int32_t y) {
    if (!app) return;
    
    int button = app->hovered_button;
    if (button < 0) return;
    
    /* Toolbar buttons */
    if (button == 50) {
        /* Preview button - cycle preview sizes */
        uint32_t next_sizes[] = {64, 128, 256, 512, 64};
        for (int i = 0; i < 4; i++) {
            if (app->preview_size == next_sizes[i]) {
                iconlay_update_preview(app, next_sizes[i + 1]);
                break;
            }
        }
    } else if (button == 51) {
        /* Export button */
        iconlay_export_nxi(app, app->filename);
        app->unsaved_changes = false;
    } else if (button == 1000) {
        /* Import button - would trigger file dialog */
        printf("[IconLay] Import clicked\n");
    } else if (button >= 2000 && button < 3000) {
        /* Layer selection */
        iconlay_select_layer(app, button - 2000);
    } else if (button >= 300 && button < 400) {
        /* Material FX toggles */
        handle_material_fx_click(app, button);
    } else if (button >= 400 && button < 500) {
        /* Export size checkboxes */
        handle_export_size_click(app, button);
    } else if (button >= 500 && button < 600) {
        /* Preview size buttons */
        handle_preview_size_click(app, button);
    }
}
