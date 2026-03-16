/*
 * IconLay.app - NeolyxOS Native Icon FX Compositor
 * 
 * Native NeolyxOS app - NOT third-party.
 * Imports flat geometry (SVG/PNG), applies FX, exports .nxi
 * 
 * Copyright (c) 2025 KetiveeAI / NeolyxOS
 */

#include "nxi_format.h"
#include "layer.h"
#include "svg_import.h"
#include "nxrender_backend.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Layout */
#define WINDOW_WIDTH   1280
#define WINDOW_HEIGHT  860
#define TOOLBAR_HEIGHT 56
#define SIDEBAR_WIDTH  260
#define PREVIEW_BAR_HEIGHT 110

#define CANVAS_SIZE    480

/* Colors (NeolyxOS native dark) */
#define COL_BG          0x18181BFF
#define COL_SIDEBAR     0x27272AFF
#define COL_TOOLBAR     0x1F1F23FF
#define COL_PANEL       0x3F3F46FF
#define COL_BORDER      0x52525BFF
#define COL_ACCENT      0x6366F1FF  /* Indigo */
#define COL_ACCENT_H    0x818CF8FF
#define COL_TEXT        0xFAFAFAFF
#define COL_TEXT_DIM    0xA1A1AAFF
#define COL_SUCCESS     0x22C55EFF
#define COL_WARNING     0xF59E0BFF
#define COL_CANVAS_BG   0x0F0F12FF

/* Effect types */
typedef enum {
    FX_NONE = 0,
    FX_GLASS,
    FX_FROST,
    FX_LIQUID,
    FX_EDGE_COLOR,
    FX_OPACITY_LIGHT
} fx_type_t;

/* Background modes */
typedef enum {
    BG_DARK,
    BG_LIGHT,
    BG_COLOR,
    BG_IMAGE
} bg_mode_t;

/* NeolyxOS Platform Targets */
typedef enum {
    PLATFORM_DESKTOP,   /* NeolyxOS Desktop */
    PLATFORM_MOBILE,    /* NeolyxOS Mobile */
    PLATFORM_WATCH,     /* NeolyxOS Watch */
    PLATFORM_TABLET     /* NeolyxOS Tablet */
} platform_t;

typedef struct {
    SDL_Window* window;
    SDL_Surface* surface;
    
    nxi_icon_t* icon;
    nxsvg_image_t** layer_svgs;
    int selected_layer;
    
    int running;
    int dragging_layer;
    float drag_offset_x, drag_offset_y;
    
    /* View */
    int show_grid;
    int show_base;
    bg_mode_t bg_mode;
    uint32_t bg_color;
    platform_t preview_platform;
    float canvas_zoom;  /* 1.0 = 100%, max 256.0 = 25600% */
    float canvas_pan_x, canvas_pan_y;  /* Pan offset */
    
    /* Right-click context menu */
    int context_menu_open;
    int context_menu_x, context_menu_y;
    
    /* FX */
    fx_type_t current_fx;
    float fx_intensity;
    float fx_blur;
    float fx_edge;
    
    /* Color picker */
    int picker_open;
    float picker_h, picker_s, picker_v;
    
    /* Dragging sliders */
    int drag_slider;  /* 0=none, 1=intensity, 2=blur, 3=opacity, 4=picker_sv, 5=picker_h, 6=scale, 7=gradient_angle */
    
    /* Export modal */
    int export_modal_open;
    int export_format;   /* 0 = NXI, 1 = PNG, 2 = Both */
    int export_preset;   /* 0 = Desktop, 1 = Mobile, 2 = Watch, 3 = All */
} app_state_t;

/* Open file dialog using zenity (Linux) */
static int open_file_dialog(char* filepath, size_t filepath_size, const char* filter_name, const char* filter_pattern) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), 
        "zenity --file-selection --title='Import SVG' --file-filter='%s | %s' 2>/dev/null",
        filter_name, filter_pattern);
    
    FILE* p = popen(cmd, "r");
    if (!p) return -1;
    
    if (fgets(filepath, filepath_size, p) == NULL) {
        pclose(p);
        return -1;
    }
    
    pclose(p);
    
    /* Remove trailing newline */
    size_t len = strlen(filepath);
    if (len > 0 && filepath[len - 1] == '\n') {
        filepath[len - 1] = '\0';
    }
    
    return strlen(filepath) > 0 ? 0 : -1;
}

/* Save file dialog using zenity (Linux) */
static int save_file_dialog(char* filepath, size_t filepath_size, const char* default_name) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), 
        "zenity --file-selection --save --confirm-overwrite --title='Save As' --filename='%s' 2>/dev/null",
        default_name);
    
    FILE* p = popen(cmd, "r");
    if (!p) return -1;
    
    if (fgets(filepath, filepath_size, p) == NULL) {
        pclose(p);
        return -1;
    }
    
    pclose(p);
    
    size_t len = strlen(filepath);
    if (len > 0 && filepath[len - 1] == '\n') {
        filepath[len - 1] = '\0';
    }
    
    return strlen(filepath) > 0 ? 0 : -1;
}

/* HSV to RGBA */
static uint32_t hsv_to_rgba(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    
    uint8_t ir = (uint8_t)((r + m) * 255);
    uint8_t ig = (uint8_t)((g + m) * 255);
    uint8_t ib = (uint8_t)((b + m) * 255);
    return (ir << 24) | (ig << 16) | (ib << 8) | 0xFF;
}

/* RGBA to HSV - extracts Hue, Saturation, Value from RGBA color */
static void rgba_to_hsv(uint32_t rgba, float* h, float* s, float* v) {
    float r = ((rgba >> 24) & 0xFF) / 255.0f;
    float g = ((rgba >> 16) & 0xFF) / 255.0f;
    float b = ((rgba >> 8) & 0xFF) / 255.0f;
    
    float cmax = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float cmin = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = cmax - cmin;
    
    /* Value */
    *v = cmax;
    
    /* Saturation */
    if (cmax > 0.0001f) {
        *s = delta / cmax;
    } else {
        *s = 0.0f;
    }
    
    /* Hue */
    if (delta < 0.0001f) {
        *h = 0.0f;
    } else if (cmax == r) {
        *h = 60.0f * fmodf((g - b) / delta + 6.0f, 6.0f);
    } else if (cmax == g) {
        *h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        *h = 60.0f * ((r - g) / delta + 4.0f);
    }
}

/* Draw button with proper styling */
static void draw_button(int x, int y, int w, int h, const char* label, int selected, int hovered) {
    uint32_t bg = selected ? COL_ACCENT : (hovered ? COL_PANEL : 0x3F3F4680);
    uint32_t fg = selected ? 0xFFFFFFFF : COL_TEXT;
    
    nxrender_backend_fill_rounded_rect(x, y, w, h, bg, 8);
    if (!selected) {
        /* Border */
        nxrender_backend_fill_rounded_rect(x, y, w, 1, COL_BORDER, 0);
        nxrender_backend_fill_rounded_rect(x, y + h - 1, w, 1, COL_BORDER, 0);
    }
    
    int tw = (int)strlen(label) * 7;
    nxrender_backend_draw_text(label, x + (w - tw) / 2, y + (h - 12) / 2, fg, 12);
}

/* Draw slider */
static void draw_slider(int x, int y, int w, float value, const char* label, uint32_t color) {
    /* Label */
    nxrender_backend_draw_text(label, x, y, COL_TEXT_DIM, 11);
    
    /* Track */
    int ty = y + 18;
    nxrender_backend_fill_rounded_rect(x, ty, w, 8, 0x3F3F46FF, 4);
    
    /* Fill */
    int fw = (int)(w * value);
    if (fw > 0) {
        nxrender_backend_fill_rounded_rect(x, ty, fw, 8, color, 4);
    }
    
    /* Thumb */
    int tx = x + fw - 6;
    if (tx < x) tx = x;
    nxrender_backend_fill_circle(tx + 6, ty + 4, 8, 0xFFFFFFFF);
    nxrender_backend_fill_circle(tx + 6, ty + 4, 6, color);
}

/* Draw platform base shape */
static void draw_base(platform_t platform, int cx, int cy, int size, uint32_t color) {
    int x = cx - size / 2;
    int y = cy - size / 2;
    
    switch (platform) {
        case PLATFORM_WATCH:
            nxrender_backend_fill_circle(cx, cy, size / 2 - 4, color);
            break;
        case PLATFORM_MOBILE:
            nxrender_backend_fill_rounded_rect(x + 4, y + 4, size - 8, size - 8, color, size / 4);
            break;
        case PLATFORM_TABLET:
            nxrender_backend_fill_rounded_rect(x + 2, y + 2, size - 4, size - 4, color, size / 8);
            break;
        case PLATFORM_DESKTOP:
        default:
            nxrender_backend_fill_rounded_rect(x + 4, y + 4, size - 8, size - 8, color, size / 10);
            break;
    }
}

/* Draw icon grid overlay */
static void draw_grid(int cx, int cy, int size) {
    int x = cx - size / 2;
    int y = cy - size / 2;
    uint32_t col = 0x6366F140;
    
    /* Border */
    nxrender_backend_fill_rect(x, y, size, 1, col);
    nxrender_backend_fill_rect(x, y + size - 1, size, 1, col);
    nxrender_backend_fill_rect(x, y, 1, size, col);
    nxrender_backend_fill_rect(x + size - 1, y, 1, size, col);
    
    /* Center cross */
    nxrender_backend_draw_line(cx, y + 20, cx, y + size - 20, col, 1);
    nxrender_backend_draw_line(x + 20, cy, x + size - 20, cy, col, 1);
    
    /* Circle guide */
    int r = size / 2 - 16;
    for (int i = 0; i < 72; i++) {
        float rad = i * 5.0f * 3.14159f / 180.0f;
        int px = cx + (int)(r * cosf(rad));
        int py = cy + (int)(r * sinf(rad));
        nxrender_backend_fill_rect(px, py, 2, 2, col);
    }
}

/* Render toolbar */
static void render_toolbar(app_state_t* app) {
    nxrender_backend_fill_rect(0, 0, WINDOW_WIDTH, TOOLBAR_HEIGHT, COL_TOOLBAR);
    nxrender_backend_fill_rect(0, TOOLBAR_HEIGHT - 1, WINDOW_WIDTH, 1, COL_BORDER);
    
    /* App icon */
    nxrender_backend_fill_rounded_rect(16, 12, 32, 32, COL_ACCENT, 8);
    nxrender_backend_draw_text("IL", 20, 18, 0xFFFFFFFF, 14);
    
    /* Tool buttons */
    int bx = 64;
    draw_button(bx, 10, 80, 36, "Import", 0, 0); bx += 88;
    draw_button(bx, 10, 80, 36, "Export", 0, 0); bx += 88;
    draw_button(bx, 10, 60, 36, "Save", 0, 0); bx += 68;
    
    /* Separator */
    nxrender_backend_fill_rect(bx + 8, 12, 1, 32, COL_BORDER);
    bx += 24;
    
    /* View toggles */
    draw_button(bx, 10, 60, 36, "Grid", app->show_grid, 0); bx += 68;
    draw_button(bx, 10, 60, 36, "Base", app->show_base, 0); bx += 68;
    
    /* Separator */
    nxrender_backend_fill_rect(bx + 8, 12, 1, 32, COL_BORDER);
    bx += 24;
    
    /* Color picker button */
    uint32_t cur_color = 0xFFFFFFFF;
    if (app->icon && app->selected_layer >= 0 && app->selected_layer < (int)app->icon->layer_count) {
        cur_color = app->icon->layers[app->selected_layer]->fill_color;
    }
    nxrender_backend_fill_rounded_rect(bx, 10, 36, 36, cur_color, 8);
    nxrender_backend_fill_rounded_rect(bx, 10, 36, 2, COL_BORDER, 0);
    nxrender_backend_fill_rounded_rect(bx, 44, 36, 2, COL_BORDER, 0);
    nxrender_backend_draw_text("Color", bx + 42, 18, COL_TEXT_DIM, 11);
    
    /* App name */
    nxrender_backend_draw_text("IconLay", WINDOW_WIDTH - 90, 20, COL_TEXT_DIM, 14);
}

/* Render layers panel */
static void render_layers(app_state_t* app) {
    int x = 0;
    int y = TOOLBAR_HEIGHT;
    int w = SIDEBAR_WIDTH;
    int h = WINDOW_HEIGHT - TOOLBAR_HEIGHT - PREVIEW_BAR_HEIGHT;
    
    nxrender_backend_fill_rect(x, y, w, h, COL_SIDEBAR);
    nxrender_backend_fill_rect(w - 1, y, 1, h, COL_BORDER);
    
    /* Header */
    nxrender_backend_draw_text("Layers", 20, y + 18, COL_TEXT, 14);
    
    int ly = y + 52;
    
    /* Base layer (locked) */
    int base_sel = (app->selected_layer == -1);
    uint32_t base_bg = base_sel ? COL_ACCENT : COL_PANEL;
    nxrender_backend_fill_rounded_rect(12, ly, w - 24, 44, base_bg, 10);
    nxrender_backend_draw_text("[LOCK]", 24, ly + 6, 0xFFD60AFF, 10);
    nxrender_backend_draw_text("Base Shape", 24, ly + 22, COL_TEXT, 12);
    ly += 52;
    
    /* Separator */
    nxrender_backend_fill_rect(20, ly, w - 40, 1, COL_BORDER);
    ly += 16;
    
    /* Vector layers */
    if (app->icon) {
        for (uint32_t i = 0; i < app->icon->layer_count && i < 10; i++) {
            nxi_layer_t* layer = app->icon->layers[i];
            uint32_t bg = ((int)i == app->selected_layer) ? COL_ACCENT : COL_PANEL;
            
            nxrender_backend_fill_rounded_rect(12, ly, w - 24, 48, bg, 10);
            
            /* Visibility dot */
            uint32_t vis = layer->visible ? COL_SUCCESS : COL_TEXT_DIM;
            nxrender_backend_fill_circle(28, ly + 24, 6, vis);
            
            /* Name */
            char name[18];
            strncpy(name, layer->name, 16);
            name[16] = '\0';
            nxrender_backend_draw_text(name, 44, ly + 10, COL_TEXT, 12);
            
            /* FX badge */
            if (layer->effect_flags) {
                nxrender_backend_fill_rounded_rect(44, ly + 28, 24, 14, COL_WARNING, 4);
                nxrender_backend_draw_text("FX", 48, ly + 30, 0x000000FF, 9);
            }
            
            /* Color swatch */
            nxrender_backend_fill_rounded_rect(w - 52, ly + 12, 28, 24, layer->fill_color, 6);
            
            ly += 56;
        }
    }
    
    /* Add button */
    ly += 12;
    nxrender_backend_fill_rounded_rect(12, ly, w - 24, 44, COL_PANEL, 10);
    nxrender_backend_draw_text("+ Import SVG", 70, ly + 14, COL_ACCENT, 13);
}

/* Render FX panel */
static void render_fx_panel(app_state_t* app) {
    int x = WINDOW_WIDTH - SIDEBAR_WIDTH;
    int y = TOOLBAR_HEIGHT;
    int w = SIDEBAR_WIDTH;
    int h = WINDOW_HEIGHT - TOOLBAR_HEIGHT - PREVIEW_BAR_HEIGHT;
    
    nxrender_backend_fill_rect(x, y, w, h, COL_SIDEBAR);
    nxrender_backend_fill_rect(x, y, 1, h, COL_BORDER);
    
    /* Header */
    nxrender_backend_draw_text("Effects", x + 20, y + 18, COL_TEXT, 14);
    
    if (app->selected_layer >= 0 && app->icon && app->selected_layer < (int)app->icon->layer_count) {
        int row = y + 52;
        nxi_layer_t* sel_layer = app->icon->layers[app->selected_layer];
        
        /* FX buttons grid - check against layer's effect_flags */
        const char* fx_labels[] = {"None", "Glass", "Frost", "Blur", "Edge", "Glow"};
        uint8_t fx_flags[] = {NXI_EFFECT_NONE, NXI_EFFECT_GLASS, NXI_EFFECT_FROST, 
                              NXI_EFFECT_BLUR, NXI_EFFECT_EDGE, NXI_EFFECT_GLOW};
        
        for (int i = 0; i < 6; i++) {
            int bx = x + 16 + (i % 3) * 74;
            int by = row + (i / 3) * 40;
            int sel = (i == 0 && sel_layer->effect_flags == 0) || 
                      (sel_layer->effect_flags & fx_flags[i]);
            draw_button(bx, by, 68, 34, fx_labels[i], sel, 0);
        }
        row += 96;
        
        /* Sliders */
        draw_slider(x + 16, row, w - 32, app->fx_intensity, "Intensity", COL_ACCENT);
        row += 50;
        
        draw_slider(x + 16, row, w - 32, app->fx_blur, "Blur", 0x8B5CF6FF);
        row += 50;
        
        /* Layer opacity */
        nxi_layer_t* layer = app->icon->layers[app->selected_layer];
        draw_slider(x + 16, row, w - 32, layer->opacity / 255.0f, "Opacity", COL_SUCCESS);
        row += 50;
        
        /* Layer scale */
        draw_slider(x + 16, row, w - 32, layer->scale / 2.0f, "Scale", 0xEC4899FF);
        row += 55;
        
        /* Gradient Section */
        nxrender_backend_fill_rect(x + 16, row, w - 32, 1, COL_BORDER);
        row += 12;
        
        nxrender_backend_draw_text("Gradient Fill", x + 16, row, COL_TEXT, 12);
        row += 24;
        
        /* Gradient toggle button */
        int grad_enabled = (sel_layer->effect_flags & NXI_EFFECT_GRADIENT) != 0;
        draw_button(x + 16, row, 100, 30, grad_enabled ? "Enabled" : "Disabled", grad_enabled, 0);
        row += 38;
        
        /* Gradient type buttons (only show if enabled) */
        if (grad_enabled) {
            int is_linear = (sel_layer->effects.gradient.type == NXI_GRADIENT_LINEAR);
            int is_radial = (sel_layer->effects.gradient.type == NXI_GRADIENT_RADIAL);
            
            draw_button(x + 16, row, 80, 28, "Linear", is_linear, 0);
            draw_button(x + 104, row, 80, 28, "Radial", is_radial, 0);
            row += 36;
            
            /* Angle slider for linear */
            if (is_linear) {
                draw_slider(x + 16, row, w - 32, sel_layer->effects.gradient.angle / 360.0f, 
                           "Angle", 0xF97316FF);
                row += 45;
            }
            
            /* Gradient color preview */
            nxrender_backend_draw_text("Colors", x + 16, row, COL_TEXT_DIM, 10);
            row += 18;
            
            /* Show two color stops */
            if (sel_layer->effects.gradient.stop_count >= 2) {
                uint32_t c1 = sel_layer->effects.gradient.stops[0].color;
                uint32_t c2 = sel_layer->effects.gradient.stops[1].color;
                nxrender_backend_fill_rounded_rect(x + 16, row, 50, 24, c1, 6);
                nxrender_backend_fill_rounded_rect(x + 74, row, 50, 24, c2, 6);
                nxrender_backend_draw_text("→", x + 130, row + 4, COL_TEXT_DIM, 12);
            }
        }
        
    } else if (app->selected_layer == -1) {
        nxrender_backend_draw_text("Base Layer", x + 20, y + 60, COL_TEXT, 13);
        nxrender_backend_draw_text("No effects on base", x + 20, y + 84, COL_TEXT_DIM, 11);
        nxrender_backend_draw_text("Auto-generated per", x + 20, y + 104, COL_TEXT_DIM, 11);
        nxrender_backend_draw_text("platform on export", x + 20, y + 120, COL_TEXT_DIM, 11);
    }
    
    /* Background section */
    int by = y + h - 100;
    nxrender_backend_fill_rect(x + 20, by, w - 40, 1, COL_BORDER);
    by += 20;
    
    nxrender_backend_draw_text("Background Preview", x + 20, by, COL_TEXT, 12);
    by += 28;
    
    const char* bg_labels[] = {"Dark", "Light", "Color"};
    for (int i = 0; i < 3; i++) {
        int sel = (app->bg_mode == i);
        draw_button(x + 16 + i * 74, by, 68, 32, bg_labels[i], sel, 0);
    }
}

/* Render canvas - Figma-style zoom: viewport is fixed, content zooms inside */
static void render_canvas(app_state_t* app) {
    /* Fixed viewport position and size */
    int vp_x = SIDEBAR_WIDTH + 20;
    int vp_y = TOOLBAR_HEIGHT + 20;
    int vp_w = WINDOW_WIDTH - SIDEBAR_WIDTH * 2 - 40;
    int vp_h = WINDOW_HEIGHT - TOOLBAR_HEIGHT - PREVIEW_BAR_HEIGHT - 60;
    int vp_cx = vp_x + vp_w / 2;
    int vp_cy = vp_y + vp_h / 2;
    
    float zoom = app->canvas_zoom;
    
    /* Zoom percentage display - top-left corner */
    char zoom_str[32];
    snprintf(zoom_str, sizeof(zoom_str), "%.0f%%", zoom * 100);
    nxrender_backend_fill_rounded_rect(vp_x + 8, vp_y + 8, 50, 22, 0x27272ACC, 6);
    nxrender_backend_draw_text(zoom_str, vp_x + 12, vp_y + 12, COL_TEXT, 12);
    
    /* Canvas card shadow */
    nxrender_backend_fill_rounded_rect(vp_x - 4, vp_y + 4, vp_w + 8, vp_h + 8, 0x00000040, 16);
    
    /* Canvas background (viewport) */
    uint32_t bg = 0x1E1E22FF;
    if (app->bg_mode == BG_LIGHT) bg = 0xF4F4F5FF;
    else if (app->bg_mode == BG_COLOR) bg = app->bg_color;
    nxrender_backend_fill_rounded_rect(vp_x, vp_y, vp_w, vp_h, bg, 12);
    
    /* Set clipping to viewport */
    nxrender_backend_set_clip(vp_x, vp_y, vp_w, vp_h);
    
    /* Content center with pan offset */
    float content_cx = vp_cx + app->canvas_pan_x;
    float content_cy = vp_cy + app->canvas_pan_y;
    
    /* Content size at current zoom - fit icon inside canvas with margin */
    int min_dim = vp_w < vp_h ? vp_w : vp_h;
    float content_size = min_dim * 0.60f * zoom;  /* 60% of smaller dimension for margin */
    
    /* Base shape (icon mask shape) */
    if (app->show_base) {
        draw_base(app->preview_platform, (int)content_cx, (int)content_cy, (int)(content_size - 20 * zoom), 0x52525BFF);
    }
    
    /* Vector layers with FX effects */
    if (app->icon) {
        for (uint32_t i = 0; i < app->icon->layer_count; i++) {
            nxi_layer_t* layer = app->icon->layers[i];
            if (!layer->visible || layer->opacity == 0) continue;
            
            if (!app->layer_svgs) app->layer_svgs = calloc(32, sizeof(nxsvg_image_t*));
            if (!app->layer_svgs[i]) {
                /* Paths are normalized to 0-1 range, so SVG is 1x1 */
                app->layer_svgs[i] = nxrender_backend_paths_to_svg(
                    layer->paths, layer->path_count, 1.0f, 1.0f, layer->fill_color);
            }
            
            if (app->layer_svgs[i]) {
                /* Scale from 0-1 normalized coords to content_size */
                float scale_factor = content_size * layer->scale;
                float px = content_cx;
                float py = content_cy;
                
                /* Apply visual FX effects */
                if (layer->effect_flags & NXI_EFFECT_GLOW) {
                    /* Glow: render larger semi-transparent version behind */
                    uint32_t glow_col = (layer->fill_color & 0xFFFFFF00) | 0x60;
                    float glow_scale = scale_factor * (1.0f + app->fx_intensity * 0.3f);
                    nxrender_backend_render_svg(app->layer_svgs[i], px, py, glow_scale, glow_col);
                    nxrender_backend_render_svg(app->layer_svgs[i], px, py, glow_scale * 1.1f, 
                                                (glow_col & 0xFFFFFF00) | 0x30);
                }
                
                if (layer->effect_flags & NXI_EFFECT_BLUR) {
                    /* Blur: render multiple offset versions for blur effect */
                    int blur_r = (int)(app->fx_blur * 6 + 1);
                    uint32_t blur_col = (layer->fill_color & 0xFFFFFF00) | 
                                        (uint8_t)(layer->opacity / blur_r);
                    for (int bx = -blur_r; bx <= blur_r; bx += 2) {
                        for (int by = -blur_r; by <= blur_r; by += 2) {
                            nxrender_backend_render_svg(app->layer_svgs[i], 
                                                        px + bx, py + by, scale_factor, blur_col);
                        }
                    }
                }
                
                if (layer->effect_flags & NXI_EFFECT_GLASS) {
                    /* Glass: render with highlight on top */
                    nxrender_backend_render_svg(app->layer_svgs[i], px, py, scale_factor, layer->fill_color);
                    /* Add white highlight gradient at top */
                    uint32_t highlight = 0xFFFFFF40;
                    float hs = content_size * 0.4f * scale_factor;
                    nxrender_backend_fill_rounded_rect((int)(px - hs), 
                                                        (int)(py - hs), 
                                                        (int)(hs * 2), 
                                                        (int)(hs * 0.75f), highlight, 12);
                }
                
                if (layer->effect_flags & NXI_EFFECT_FROST) {
                    /* Frost: similar to blur but with noise pattern */
                    int frost_r = (int)(app->fx_blur * 8 + 2);
                    uint32_t frost_col = (layer->fill_color & 0xFFFFFF00) | 
                                         (uint8_t)(layer->opacity * 0.7f);
                    for (int j = 0; j < 8; j++) {
                        float ox = ((j * 17 + 5) % 13) - 6;
                        float oy = ((j * 23 + 7) % 11) - 5;
                        nxrender_backend_render_svg(app->layer_svgs[i], 
                                                    px + ox * (frost_r / 6.0f), 
                                                    py + oy * (frost_r / 6.0f), scale_factor, frost_col);
                    }
                }
                
                if (layer->effect_flags & NXI_EFFECT_EDGE) {
                    /* Edge: render white outline first */
                    uint32_t edge_col = 0xFFFFFFFF;
                    float edge_off = 1.0f + app->fx_intensity;
                    nxrender_backend_render_svg(app->layer_svgs[i], px - edge_off, py, scale_factor, edge_col);
                    nxrender_backend_render_svg(app->layer_svgs[i], px + edge_off, py, scale_factor, edge_col);
                    nxrender_backend_render_svg(app->layer_svgs[i], px, py - edge_off, scale_factor, edge_col);
                    nxrender_backend_render_svg(app->layer_svgs[i], px, py + edge_off, scale_factor, edge_col);
                }
                
                /* Main layer render (unless glass which already rendered) */
                if (!(layer->effect_flags & NXI_EFFECT_GLASS)) {
                    uint32_t col = layer->fill_color;
                    if (layer->opacity < 255) {
                        col = (col & 0xFFFFFF00) | layer->opacity;
                    }
                    nxrender_backend_render_svg(app->layer_svgs[i], px, py, scale_factor, col);
                }
            }
        }
    }
    
    /* Selection indicator */
    if (app->icon && app->selected_layer >= 0 && 
        app->selected_layer < (int)app->icon->layer_count) {
        nxi_layer_t* sel = app->icon->layers[app->selected_layer];
        if (sel->visible) {
            float sx = content_cx + (sel->pos_x - 0.5f) * content_size;
            float sy = content_cy + (sel->pos_y - 0.5f) * content_size;
            float half = sel->scale * content_size * 0.35f;
            int bx = (int)(sx - half);
            int by = (int)(sy - half);
            int bw = (int)(half * 2);
            int bh = (int)(half * 2);
            /* Draw selection box with cyan color */
            uint32_t sel_col = 0x22D3EEFF;
            nxrender_backend_fill_rect(bx - 1, by - 1, bw + 2, 2, sel_col);
            nxrender_backend_fill_rect(bx - 1, by + bh - 1, bw + 2, 2, sel_col);
            nxrender_backend_fill_rect(bx - 1, by - 1, 2, bh + 2, sel_col);
            nxrender_backend_fill_rect(bx + bw - 1, by - 1, 2, bh + 2, sel_col);
            /* Corner handles */
            nxrender_backend_fill_rect(bx - 4, by - 4, 8, 8, sel_col);
            nxrender_backend_fill_rect(bx + bw - 4, by - 4, 8, 8, sel_col);
            nxrender_backend_fill_rect(bx - 4, by + bh - 4, 8, 8, sel_col);
            nxrender_backend_fill_rect(bx + bw - 4, by + bh - 4, 8, 8, sel_col);
        }
    }
    
    /* Grid */
    if (app->show_grid) {
        draw_grid((int)content_cx, (int)content_cy, (int)(content_size - 20 * zoom));
    }
    
    /* Clear clipping */
    nxrender_backend_clear_clip();
}

/* Render platform preview bar */
static void render_previews(app_state_t* app) {
    int y = WINDOW_HEIGHT - PREVIEW_BAR_HEIGHT;
    
    nxrender_backend_fill_rect(0, y, WINDOW_WIDTH, PREVIEW_BAR_HEIGHT, COL_TOOLBAR);
    nxrender_backend_fill_rect(0, y, WINDOW_WIDTH, 1, COL_BORDER);
    
    nxrender_backend_draw_text("Platform Export Preview", 20, y + 12, COL_TEXT_DIM, 11);
    
    const char* labels[] = {"Desktop", "Mobile", "Watch", "Tablet"};
    platform_t plats[] = {PLATFORM_DESKTOP, PLATFORM_MOBILE, PLATFORM_WATCH, PLATFORM_TABLET};
    int size = 60;
    int gap = 40;
    int sx = (WINDOW_WIDTH - (4 * size + 3 * gap)) / 2;
    
    for (int i = 0; i < 4; i++) {
        int px = sx + i * (size + gap);
        int py = y + 32;
        int pcx = px + size / 2;
        int pcy = py + size / 2;
        
        /* Selection ring */
        if (app->preview_platform == plats[i]) {
            nxrender_backend_fill_rounded_rect(px - 4, py - 4, size + 8, size + 8, COL_ACCENT, 14);
        }
        
        /* BG */
        nxrender_backend_fill_rounded_rect(px, py, size, size, COL_PANEL, 10);
        
        /* Base shape */
        draw_base(plats[i], pcx, pcy, size - 8, 0x71717AFF);
        
        /* Icon placeholder */
        nxrender_backend_fill_circle(pcx, pcy, 10, COL_ACCENT);
        
        /* Label */
        nxrender_backend_draw_text(labels[i], px - 8, py + size + 8, COL_TEXT_DIM, 10);
    }
}

/* Render color picker modal */
static void render_color_picker(app_state_t* app) {
    if (!app->picker_open) return;
    
    /* Backdrop */
    nxrender_backend_fill_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0x00000099);
    
    int px = (WINDOW_WIDTH - 320) / 2;
    int py = (WINDOW_HEIGHT - 400) / 2;
    int pw = 320;
    int ph = 400;
    
    /* Modal */
    nxrender_backend_fill_rounded_rect(px, py, pw, ph, COL_SIDEBAR, 20);
    nxrender_backend_draw_text("Color Picker", px + 20, py + 20, COL_TEXT, 16);
    
    /* SV box */
    int sv_x = px + 20;
    int sv_y = py + 56;
    int sv_size = pw - 64;
    
    /* Base hue color */
    uint32_t hue_col = hsv_to_rgba(app->picker_h, 1.0f, 1.0f);
    nxrender_backend_fill_rounded_rect(sv_x, sv_y, sv_size, sv_size, hue_col, 8);
    
    /* White gradient left to right */
    for (int gx = 0; gx < sv_size; gx++) {
        float s = (float)gx / sv_size;
        uint32_t col = hsv_to_rgba(app->picker_h, s, 1.0f);
        col = (col & 0xFFFFFF00) | (uint8_t)(255 * (1.0f - s));
        nxrender_backend_fill_rect(sv_x + gx, sv_y, 1, sv_size, 0xFFFFFF00 | (uint8_t)(255 * (1.0f - s)));
    }
    
    /* Black gradient top to bottom */
    for (int gy = 0; gy < sv_size; gy++) {
        float v = 1.0f - (float)gy / sv_size;
        nxrender_backend_fill_rect(sv_x, sv_y + gy, sv_size, 1, 0x000000FF & ~((uint8_t)(255 * v)));
    }
    
    /* SV cursor */
    int sv_cx = sv_x + (int)(app->picker_s * sv_size);
    int sv_cy = sv_y + (int)((1.0f - app->picker_v) * sv_size);
    nxrender_backend_fill_circle(sv_cx, sv_cy, 10, 0xFFFFFFFF);
    nxrender_backend_fill_circle(sv_cx, sv_cy, 7, hsv_to_rgba(app->picker_h, app->picker_s, app->picker_v));
    
    /* Hue bar */
    int hue_x = px + pw - 32;
    int hue_y = sv_y;
    int hue_h = sv_size;
    
    for (int hi = 0; hi < hue_h; hi++) {
        float h = (float)hi / hue_h * 360.0f;
        nxrender_backend_fill_rect(hue_x, hue_y + hi, 20, 1, hsv_to_rgba(h, 1.0f, 1.0f));
    }
    
    /* Hue cursor */
    int hue_cy = hue_y + (int)(app->picker_h / 360.0f * hue_h);
    nxrender_backend_fill_rect(hue_x - 4, hue_cy - 2, 28, 4, 0xFFFFFFFF);
    
    /* Preview */
    uint32_t preview = hsv_to_rgba(app->picker_h, app->picker_s, app->picker_v);
    int prev_y = sv_y + sv_size + 20;
    nxrender_backend_fill_rounded_rect(px + 20, prev_y, 60, 40, preview, 8);
    
    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", (preview >> 24) & 0xFF, (preview >> 16) & 0xFF, (preview >> 8) & 0xFF);
    nxrender_backend_draw_text(hex, px + 92, prev_y + 12, COL_TEXT, 14);
    
    /* Apply button */
    draw_button(px + 20, py + ph - 60, pw - 40, 44, "Apply Color", 1, 0);
}

/* Render export modal */
static void render_export_modal(app_state_t* app) {
    if (!app->export_modal_open) return;
    
    /* Backdrop */
    nxrender_backend_fill_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0x00000099);
    
    int mx = (WINDOW_WIDTH - 360) / 2;
    int my = (WINDOW_HEIGHT - 320) / 2;
    int mw = 360;
    int mh = 320;
    
    /* Modal background */
    nxrender_backend_fill_rounded_rect(mx, my, mw, mh, COL_SIDEBAR, 16);
    nxrender_backend_draw_text("Export Icon", mx + 20, my + 20, COL_TEXT, 16);
    
    /* Format section */
    nxrender_backend_draw_text("Format:", mx + 20, my + 60, COL_TEXT, 12);
    
    const char* fmt_labels[] = {"NXI", "PNG", "Both"};
    for (int i = 0; i < 3; i++) {
        int bx = mx + 20 + i * 105;
        draw_button(bx, my + 80, 95, 32, fmt_labels[i], app->export_format == i, 0);
    }
    
    /* Preset section */
    nxrender_backend_draw_text("Size Preset:", mx + 20, my + 130, COL_TEXT, 12);
    
    const char* preset_labels[] = {"Desktop", "Mobile", "Watch", "All"};
    for (int i = 0; i < 4; i++) {
        int bx = mx + 20 + (i % 2) * 160;
        int by = my + 150 + (i / 2) * 40;
        draw_button(bx, by, 145, 32, preset_labels[i], app->export_preset == i, 0);
    }
    
    /* Action buttons */
    draw_button(mx + 20, my + mh - 60, 150, 44, "Export", 1, 0);
    draw_button(mx + 190, my + mh - 60, 150, 44, "Cancel", 0, 0);
}

/* Full render */
static void render_ui(app_state_t* app) {
    nxrender_backend_clear(COL_BG);
    
    render_toolbar(app);
    render_layers(app);
    render_canvas(app);
    render_fx_panel(app);
    render_previews(app);
    render_color_picker(app);
    render_export_modal(app);
    
    nxrender_backend_present();
}

/* Handle drag */
static void handle_drag(app_state_t* app, int mx, int my) {
    if (app->dragging_layer < 0 || !app->icon) return;
    
    int cx = WINDOW_WIDTH / 2;
    int cy = TOOLBAR_HEIGHT + 30 + CANVAS_SIZE / 2;
    
    float nx = (float)(mx - cx) / CANVAS_SIZE + 0.5f;
    float ny = (float)(my - cy) / CANVAS_SIZE + 0.5f;
    
    nxi_layer_t* layer = app->icon->layers[app->dragging_layer];
    layer->pos_x = nx - app->drag_offset_x + 0.5f;
    layer->pos_y = ny - app->drag_offset_y + 0.5f;
    
    if (layer->pos_x < 0) layer->pos_x = 0;
    if (layer->pos_x > 1) layer->pos_x = 1;
    if (layer->pos_y < 0) layer->pos_y = 0;
    if (layer->pos_y > 1) layer->pos_y = 1;
    
    if (app->layer_svgs && app->layer_svgs[app->dragging_layer]) {
        nxsvg_free(app->layer_svgs[app->dragging_layer]);
        app->layer_svgs[app->dragging_layer] = NULL;
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG);
    
    app_state_t app = {0};
    app.selected_layer = -1;
    app.dragging_layer = -1;
    app.show_grid = 1;
    app.show_base = 1;
    app.bg_mode = BG_DARK;
    app.bg_color = 0x6366F1FF;
    app.preview_platform = PLATFORM_DESKTOP;
    app.fx_intensity = 0.5f;
    app.fx_blur = 0.3f;
    app.picker_h = 0;
    app.picker_s = 1;
    app.picker_v = 1;
    app.canvas_zoom = 1.0f;  /* 100% */
    app.canvas_pan_x = 0;
    app.canvas_pan_y = 0;
    
    app.window = SDL_CreateWindow("IconLay - NeolyxOS",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!app.window) return 1;
    
    app.surface = SDL_GetWindowSurface(app.window);
    if (nxrender_backend_init(app.surface->pixels, WINDOW_WIDTH, WINDOW_HEIGHT, app.surface->pitch) != 0) {
        SDL_DestroyWindow(app.window);
        return 1;
    }
    
    printf("IconLay - NeolyxOS Native Icon FX Compositor\n");
    
    app.icon = nxi_create();
    
    if (argc > 1) {
        svg_import_opts_t opts = svg_import_defaults();
        if (svg_import_multilayer(argv[1], app.icon, &opts) == 0) {
            app.selected_layer = 0;
            printf("Imported: %s\n", argv[1]);
        }
    }
    
    app.running = 1;
    
    while (app.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    app.running = 0;
                    break;
                    
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        int mx = e.button.x, my = e.button.y;
                        
                        /* Color picker modal */
                        if (app.picker_open) {
                            int px = (WINDOW_WIDTH - 320) / 2;
                            int py = (WINDOW_HEIGHT - 400) / 2;
                            
                            /* Apply button */
                            if (mx >= px + 20 && mx < px + 300 && my >= py + 340 && my < py + 384) {
                                if (app.icon && app.selected_layer >= 0) {
                                    app.icon->layers[app.selected_layer]->fill_color = 
                                        hsv_to_rgba(app.picker_h, app.picker_s, app.picker_v);
                                    if (app.layer_svgs && app.layer_svgs[app.selected_layer]) {
                                        nxsvg_free(app.layer_svgs[app.selected_layer]);
                                        app.layer_svgs[app.selected_layer] = NULL;
                                    }
                                }
                                app.picker_open = 0;
                            }
                            /* SV box */
                            else if (mx >= px + 20 && mx < px + 256 && my >= py + 56 && my < py + 292) {
                                app.drag_slider = 4;
                                app.picker_s = (float)(mx - px - 20) / 236;
                                app.picker_v = 1.0f - (float)(my - py - 56) / 236;
                            }
                            /* Hue bar */
                            else if (mx >= px + 288 && mx < px + 308 && my >= py + 56 && my < py + 292) {
                                app.drag_slider = 5;
                                app.picker_h = (float)(my - py - 56) / 236 * 360.0f;
                            }
                            break;
                        }
                        
                        /* Toolbar buttons */
                        if (my < TOOLBAR_HEIGHT) {
                            /* Color swatch */
                            if (mx >= 468 && mx < 504) {
                                app.picker_open = 1;
                                if (app.icon && app.selected_layer >= 0) {
                                    /* Initialize picker with current layer color */
                                    uint32_t col = app.icon->layers[app.selected_layer]->fill_color;
                                    rgba_to_hsv(col, &app.picker_h, &app.picker_s, &app.picker_v);
                                }
                            }
                            /* Grid toggle */
                            else if (mx >= 316 && mx < 376) {
                                app.show_grid = !app.show_grid;
                            }
                            /* Base toggle */
                            else if (mx >= 384 && mx < 444) {
                                app.show_base = !app.show_base;
                            }
                        }
                        
                        /* Layers panel */
                        if (mx < SIDEBAR_WIDTH && my > TOOLBAR_HEIGHT + 52) {
                            int ly = TOOLBAR_HEIGHT + 52;
                            if (my >= ly && my < ly + 44) {
                                app.selected_layer = -1;
                            }
                            ly += 68;
                            if (app.icon) {
                                for (uint32_t i = 0; i < app.icon->layer_count; i++) {
                                    if (my >= ly && my < ly + 48) {
                                        app.selected_layer = i;
                                        break;
                                    }
                                    ly += 56;
                                }
                            }
                        }
                        
                        /* Platform previews */
                        int prev_y = WINDOW_HEIGHT - PREVIEW_BAR_HEIGHT + 32;
                        if (my >= prev_y && my < prev_y + 60) {
                            platform_t pls[] = {PLATFORM_DESKTOP, PLATFORM_MOBILE, PLATFORM_WATCH, PLATFORM_TABLET};
                            int sx = (WINDOW_WIDTH - (4 * 60 + 3 * 40)) / 2;
                            for (int i = 0; i < 4; i++) {
                                int ppx = sx + i * 100;
                                if (mx >= ppx && mx < ppx + 60) {
                                    app.preview_platform = pls[i];
                                    break;
                                }
                            }
                        }
                        
                        /* FX panel button clicks */
                        int fx_x = WINDOW_WIDTH - SIDEBAR_WIDTH;
                        if (mx >= fx_x && app.selected_layer >= 0 && app.icon) {
                            int fx_row = TOOLBAR_HEIGHT + 52;
                            
                            /* FX type buttons (2 rows of 3) */
                            for (int i = 0; i < 6; i++) {
                                int bx = fx_x + 16 + (i % 3) * 74;
                                int by = fx_row + (i / 3) * 40;
                                if (mx >= bx && mx < bx + 68 && my >= by && my < by + 34) {
                                    /* Map button index to effect flags */
                                    nxi_layer_t* layer = app.icon->layers[app.selected_layer];
                                    switch (i) {
                                        case 0: layer->effect_flags = NXI_EFFECT_NONE; app.current_fx = FX_NONE; break;
                                        case 1: layer->effect_flags = NXI_EFFECT_GLASS; app.current_fx = FX_GLASS; break;
                                        case 2: layer->effect_flags = NXI_EFFECT_FROST; app.current_fx = FX_FROST; break;
                                        case 3: layer->effect_flags = NXI_EFFECT_BLUR; app.current_fx = FX_LIQUID; break;
                                        case 4: layer->effect_flags = NXI_EFFECT_EDGE; app.current_fx = FX_EDGE_COLOR; break;
                                        case 5: layer->effect_flags = NXI_EFFECT_GLOW; app.current_fx = FX_OPACITY_LIGHT; break;
                                    }
                                    /* Invalidate cached SVG */
                                    if (app.layer_svgs && app.layer_svgs[app.selected_layer]) {
                                        nxsvg_free(app.layer_svgs[app.selected_layer]);
                                        app.layer_svgs[app.selected_layer] = NULL;
                                    }
                                    break;
                                }
                            }
                            
                            /* Intensity slider */
                            int slider_y = fx_row + 96;
                            if (my >= slider_y + 18 && my < slider_y + 30) {
                                app.drag_slider = 1;
                                app.fx_intensity = fmaxf(0, fminf(1, (float)(mx - fx_x - 16) / (SIDEBAR_WIDTH - 32)));
                            }
                            /* Blur slider */
                            slider_y += 50;
                            if (my >= slider_y + 18 && my < slider_y + 30) {
                                app.drag_slider = 2;
                                app.fx_blur = fmaxf(0, fminf(1, (float)(mx - fx_x - 16) / (SIDEBAR_WIDTH - 32)));
                            }
                            /* Opacity slider */
                            slider_y += 50;
                            if (my >= slider_y + 18 && my < slider_y + 30) {
                                app.drag_slider = 3;
                                nxi_layer_t* layer = app.icon->layers[app.selected_layer];
                                layer->opacity = (uint8_t)(fmaxf(0, fminf(1, (float)(mx - fx_x - 16) / (SIDEBAR_WIDTH - 32))) * 255);
                            }
                            
                            /* Scale slider */
                            slider_y += 50;
                            if (my >= slider_y + 18 && my < slider_y + 30) {
                                app.drag_slider = 6;  /* 6 = scale slider */
                                nxi_layer_t* layer = app.icon->layers[app.selected_layer];
                                layer->scale = fmaxf(0, fminf(1, (float)(mx - fx_x - 16) / (SIDEBAR_WIDTH - 32))) * 2.0f;
                                /* Invalidate cached SVG */
                                if (app.layer_svgs && app.layer_svgs[app.selected_layer]) {
                                    nxsvg_free(app.layer_svgs[app.selected_layer]);
                                    app.layer_svgs[app.selected_layer] = NULL;
                                }
                            }
                            
                            /* Gradient section (after scale) */
                            int grad_section_y = slider_y + 55 + 12 + 24;  /* separator + label */
                            
                            /* Gradient toggle button */
                            if (my >= grad_section_y && my < grad_section_y + 30 &&
                                mx >= fx_x + 16 && mx < fx_x + 116) {
                                nxi_layer_t* layer = app.icon->layers[app.selected_layer];
                                
                                if (layer->effect_flags & NXI_EFFECT_GRADIENT) {
                                    /* Disable gradient */
                                    layer->effect_flags &= ~NXI_EFFECT_GRADIENT;
                                } else {
                                    /* Enable gradient with defaults */
                                    layer->effect_flags |= NXI_EFFECT_GRADIENT;
                                    layer->effects.gradient.type = NXI_GRADIENT_LINEAR;
                                    layer->effects.gradient.angle = 90.0f;
                                    layer->effects.gradient.cx = 0.5f;
                                    layer->effects.gradient.cy = 0.5f;
                                    layer->effects.gradient.radius = 0.5f;
                                    layer->effects.gradient.stop_count = 2;
                                    layer->effects.gradient.stops[0].position = 0.0f;
                                    layer->effects.gradient.stops[0].color = layer->fill_color;
                                    layer->effects.gradient.stops[1].position = 1.0f;
                                    layer->effects.gradient.stops[1].color = 0x000000FF;
                                }
                            }
                            grad_section_y += 38;
                            
                            /* Linear/Radial type buttons (if gradient enabled) */
                            nxi_layer_t* layer = app.icon->layers[app.selected_layer];
                            if (layer->effect_flags & NXI_EFFECT_GRADIENT) {
                                if (my >= grad_section_y && my < grad_section_y + 28) {
                                    if (mx >= fx_x + 16 && mx < fx_x + 96) {
                                        layer->effects.gradient.type = NXI_GRADIENT_LINEAR;
                                    } else if (mx >= fx_x + 104 && mx < fx_x + 184) {
                                        layer->effects.gradient.type = NXI_GRADIENT_RADIAL;
                                    }
                                }
                                grad_section_y += 36;
                                
                                /* Angle slider for linear */
                                if (layer->effects.gradient.type == NXI_GRADIENT_LINEAR) {
                                    if (my >= grad_section_y + 18 && my < grad_section_y + 30) {
                                        app.drag_slider = 7;  /* 7 = gradient angle */
                                        layer->effects.gradient.angle = fmaxf(0, fminf(1, (float)(mx - fx_x - 16) / (SIDEBAR_WIDTH - 32))) * 360.0f;
                                    }
                                }
                            }
                            
                            /* Background mode buttons */
                            int bg_y = TOOLBAR_HEIGHT + WINDOW_HEIGHT - TOOLBAR_HEIGHT - PREVIEW_BAR_HEIGHT - 72;
                            if (my >= bg_y && my < bg_y + 32) {
                                for (int i = 0; i < 3; i++) {
                                    int bbx = fx_x + 16 + i * 74;
                                    if (mx >= bbx && mx < bbx + 68) {
                                        app.bg_mode = i;
                                        break;
                                    }
                                }
                            }
                        }
                        
                        /* Canvas click - select layer by hit test */
                        int cx = WINDOW_WIDTH / 2;
                        int cy = TOOLBAR_HEIGHT + 30 + CANVAS_SIZE / 2;
                        if (app.icon && 
                            mx >= cx - CANVAS_SIZE/2 && mx < cx + CANVAS_SIZE/2 &&
                            my >= cy - CANVAS_SIZE/2 && my < cy + CANVAS_SIZE/2) {
                            
                            /* Convert to normalized coords */
                            float click_nx = (float)(mx - cx) / CANVAS_SIZE + 0.5f;
                            float click_ny = (float)(my - cy) / CANVAS_SIZE + 0.5f;
                            
                            /* Hit test layers from top to bottom */
                            int hit_layer = -1;
                            for (int li = (int)app.icon->layer_count - 1; li >= 0; li--) {
                                nxi_layer_t* layer = app.icon->layers[li];
                                if (!layer->visible) continue;
                                
                                /* Calculate layer bounds */
                                float half_size = layer->scale * 0.35f;
                                float lx1 = layer->pos_x - half_size;
                                float lx2 = layer->pos_x + half_size;
                                float ly1 = layer->pos_y - half_size;
                                float ly2 = layer->pos_y + half_size;
                                
                                /* Check if click is inside layer bounds */
                                if (click_nx >= lx1 && click_nx <= lx2 &&
                                    click_ny >= ly1 && click_ny <= ly2) {
                                    hit_layer = li;
                                    break;  /* Found topmost layer */
                                }
                            }
                            
                            if (hit_layer >= 0) {
                                /* Select the clicked layer */
                                app.selected_layer = hit_layer;
                                
                                /* Start dragging */
                                app.dragging_layer = hit_layer;
                                nxi_layer_t* layer = app.icon->layers[hit_layer];
                                app.drag_offset_x = click_nx - layer->pos_x + 0.5f;
                                app.drag_offset_y = click_ny - layer->pos_y + 0.5f;
                            } else {
                                /* Clicked empty space - deselect */
                                app.selected_layer = -1;
                            }
                        }
                    }
                    break;
                    
                case SDL_MOUSEBUTTONUP:
                    app.dragging_layer = -1;
                    app.drag_slider = 0;
                    break;
                    
                case SDL_MOUSEMOTION:
                    if (app.dragging_layer >= 0) {
                        handle_drag(&app, e.motion.x, e.motion.y);
                    }
                    /* FX sliders drag */
                    if (app.drag_slider >= 1 && app.drag_slider <= 7 && app.icon && app.selected_layer >= 0) {
                        int fx_x = WINDOW_WIDTH - SIDEBAR_WIDTH;
                        float val = fmaxf(0, fminf(1, (float)(e.motion.x - fx_x - 16) / (SIDEBAR_WIDTH - 32)));
                        if (app.drag_slider == 1) app.fx_intensity = val;
                        else if (app.drag_slider == 2) app.fx_blur = val;
                        else if (app.drag_slider == 3) {
                            app.icon->layers[app.selected_layer]->opacity = (uint8_t)(val * 255);
                        }
                        else if (app.drag_slider == 6) {
                            /* Scale slider */
                            app.icon->layers[app.selected_layer]->scale = val * 2.0f;
                            /* Invalidate cached SVG */
                            if (app.layer_svgs && app.layer_svgs[app.selected_layer]) {
                                nxsvg_free(app.layer_svgs[app.selected_layer]);
                                app.layer_svgs[app.selected_layer] = NULL;
                            }
                        }
                        else if (app.drag_slider == 7) {
                            /* Gradient angle slider */
                            app.icon->layers[app.selected_layer]->effects.gradient.angle = val * 360.0f;
                        }
                    }
                    /* Color picker SV */
                    if (app.drag_slider == 4) {
                        int px = (WINDOW_WIDTH - 320) / 2;
                        int py = (WINDOW_HEIGHT - 400) / 2;
                        app.picker_s = fmaxf(0, fminf(1, (float)(e.motion.x - px - 20) / 236));
                        app.picker_v = fmaxf(0, fminf(1, 1.0f - (float)(e.motion.y - py - 56) / 236));
                    }
                    /* Color picker Hue */
                    if (app.drag_slider == 5) {
                        int py = (WINDOW_HEIGHT - 400) / 2;
                        app.picker_h = fmaxf(0, fminf(360, (float)(e.motion.y - py - 56) / 236 * 360.0f));
                    }
                    break;
                    
                case SDL_MOUSEWHEEL: {
                    int ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
                    int shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    
                    /* Get mouse position */
                    int mx, my;
                    SDL_GetMouseState(&mx, &my);
                    
                    /* Viewport center for reference */
                    int vp_cx = SIDEBAR_WIDTH + 20 + (WINDOW_WIDTH - SIDEBAR_WIDTH * 2 - 40) / 2;
                    int vp_cy = TOOLBAR_HEIGHT + 20 + (WINDOW_HEIGHT - TOOLBAR_HEIGHT - PREVIEW_BAR_HEIGHT - 60) / 2;
                    
                    if (ctrl) {
                        /* Cursor-focused zoom (Figma-style) */
                        float old_zoom = app.canvas_zoom;
                        float zoom_factor = 1.0f + e.wheel.y * 0.1f;
                        app.canvas_zoom *= zoom_factor;
                        if (app.canvas_zoom < 0.1f) app.canvas_zoom = 0.1f;
                        if (app.canvas_zoom > 256.0f) app.canvas_zoom = 256.0f;
                        
                        /* Calculate mouse position relative to content center */
                        float mouse_rel_x = mx - (vp_cx + app.canvas_pan_x);
                        float mouse_rel_y = my - (vp_cy + app.canvas_pan_y);
                        
                        /* Adjust pan so content under cursor stays in place */
                        /* After zoom, the same content point should be at the same screen pos */
                        float scale_change = app.canvas_zoom / old_zoom;
                        app.canvas_pan_x -= mouse_rel_x * (scale_change - 1.0f);
                        app.canvas_pan_y -= mouse_rel_y * (scale_change - 1.0f);
                        
                    } else if (shift) {
                        /* Shift+Scroll = Horizontal pan */
                        app.canvas_pan_x += e.wheel.y * 20.0f;
                    } else {
                        /* Regular scroll = Vertical pan */
                        app.canvas_pan_y += e.wheel.y * 20.0f;
                    }
                    break;
                }
                    
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_s: if (app.icon) { nxi_save(app.icon, "output.nxi"); printf("Saved NXI\n"); } break;
                        case SDLK_e: 
                            if (app.icon) { 
                                nxi_export_preset(app.icon, NXI_PRESET_ALL, "."); 
                                nxi_export_png_preset(app.icon, NXI_PRESET_ALL, ".", NULL);
                                printf("Exported NXI + PNG presets\n"); 
                            } 
                            break;
                        case SDLK_p:
                            if (app.icon) {
                                nxi_export_png(app.icon, 512, "icon_512.png", NULL);
                                printf("Exported icon_512.png\n");
                            }
                            break;
                        case SDLK_g: app.show_grid = !app.show_grid; break;
                        case SDLK_b: app.show_base = !app.show_base; break;
                        case SDLK_c: app.picker_open = !app.picker_open; break;
                        case SDLK_1: app.bg_mode = BG_DARK; break;
                        case SDLK_2: app.bg_mode = BG_LIGHT; break;
                        case SDLK_DELETE: 
                            if (app.icon && app.selected_layer >= 0) {
                                nxi_remove_layer(app.icon, app.selected_layer);
                                app.selected_layer = app.icon->layer_count > 0 ? 0 : -1;
                            }
                            break;
                        case SDLK_ESCAPE:
                            if (app.picker_open) app.picker_open = 0;
                            else app.running = 0;
                            break;
                    }
                    break;
            }
        }
        
        render_ui(&app);
        SDL_UpdateWindowSurface(app.window);
        SDL_Delay(16);
    }
    
    if (app.layer_svgs) {
        for (uint32_t i = 0; app.icon && i < app.icon->layer_count; i++) {
            if (app.layer_svgs[i]) nxsvg_free(app.layer_svgs[i]);
        }
        free(app.layer_svgs);
    }
    
    nxrender_backend_shutdown();
    nxi_free(app.icon);
    SDL_DestroyWindow(app.window);
    IMG_Quit();
    SDL_Quit();
    
    return 0;
}
