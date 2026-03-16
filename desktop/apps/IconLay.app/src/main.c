/*
 * IconLay.app - Main Entry Point
 * 
 * NeolyxOS Icon Editor with layer support and NXI export.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxi_format.h"
#include "layer.h"
#include "svg_import.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_WIDTH  1440
#define WINDOW_HEIGHT 900
#define CANVAS_SIZE   1024
#define PREVIEW_SIZE  400
#define LEFT_SIDEBAR_WIDTH 320
#define RIGHT_SIDEBAR_WIDTH 384
#define SIDEBAR_WIDTH LEFT_SIDEBAR_WIDTH
#define TOOLBAR_HEIGHT 56

/* Colors - Modern zinc dark theme */
#define COL_BG        0x09090BFF  /* zinc-950 */
#define COL_SIDEBAR   0x18181BFF  /* zinc-900 */
#define COL_PANEL     0x27272AFF  /* zinc-800 */
#define COL_BORDER    0x3F3F46FF  /* zinc-700 */
#define COL_ACCENT    0x3B82F6FF  /* blue-500 */
#define COL_ACCENT_HVR 0x2563EBFF /* blue-600 */
#define COL_TEXT      0xFAFAFAFF  /* zinc-50 */
#define COL_TEXT_DIM  0x71717AFF  /* zinc-500 */
#define COL_SUCCESS   0x22C55EFF  /* green-500 */
#define COL_CANVAS_BG 0x09090BFF  /* zinc-950 */
#define COL_GRID      0x27272A40  /* zinc-800 with alpha */
#define COL_PURPLE    0x8B5CF6FF  /* purple-500 */
#define COL_ORANGE    0xF97316FF  /* orange-500 */

/* Application state */
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas_texture;
    SDL_Texture* logo_texture;
    TTF_Font* font;
    TTF_Font* font_small;
    TTF_Font* font_bold;
    
    nxi_icon_t* icon;
    int selected_layer;
    
    uint8_t* preview_buffer;
    int running;
    
    int dragging_layer;
    float drag_offset_x, drag_offset_y;
    
    nxi_preset_t export_preset;
    const char* export_preset_name;
    
    int logo_w, logo_h;
    
    /* Color picker state */
    int color_picker_active;
    float picker_hue;        /* 0-360 */
    float picker_sat;        /* 0-1 */
    float picker_val;        /* 0-1 */
    int dragging_hue_bar;
    int dragging_sv_box;
    
    /* Hover state tracking */
    int mouse_x, mouse_y;
    int hovered_button;      /* ID of currently hovered button, -1 for none */
    SDL_Cursor* cursor_arrow;
    SDL_Cursor* cursor_hand;
    SDL_Cursor* cursor_move;
    SDL_Cursor* current_cursor;
    
    /* Base guide layer */
    int show_base_grid;      /* Show base template at 50% opacity */
    int ctrl_pressed;        /* For Ctrl+Scroll zoom */
    
    /* Material Effects state */
    int fx_glass;
    int fx_frost;
    int fx_metal;
    int fx_liquid;
    int fx_glow;
    
    /* Effect parameters (0-200 range) */
    int fx_blur;
    int fx_brightness;
    int fx_contrast;
    int fx_saturation;
    int fx_shadow;
    int fx_glow_amount;
    int fx_depth;
    
    /* Preview size selector */
    int preview_size;        /* Current preview size (16, 32, 64, 128, 256, 512) */
    
    /* Export sizes (bitmask) */
    int export_16, export_32, export_64, export_128, export_256, export_512, export_1024;
} app_state_t;

/* Render the preview canvas */
static void render_preview(app_state_t* app) {
    if (!app->icon || app->icon->layer_count == 0) {
        memset(app->preview_buffer, 0, CANVAS_SIZE * CANVAS_SIZE * 4);
        SDL_UpdateTexture(app->canvas_texture, NULL, 
                          app->preview_buffer, CANVAS_SIZE * 4);
        return;
    }
    
    layers_render_all(app->icon->layers, app->icon->layer_count,
                      app->preview_buffer, CANVAS_SIZE);
    
    SDL_UpdateTexture(app->canvas_texture, NULL, 
                      app->preview_buffer, CANVAS_SIZE * 4);
}

/* Draw filled rect */
static void draw_rect(SDL_Renderer* r, int x, int y, int w, int h, uint32_t c) {
    SDL_SetRenderDrawColor(r, (c >> 24) & 0xFF, (c >> 16) & 0xFF, 
                           (c >> 8) & 0xFF, c & 0xFF);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

/* Draw rect outline */
static void draw_rect_outline(SDL_Renderer* r, int x, int y, int w, int h, uint32_t c) {
    SDL_SetRenderDrawColor(r, (c >> 24) & 0xFF, (c >> 16) & 0xFF, 
                           (c >> 8) & 0xFF, c & 0xFF);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r, &rect);
}

/* Draw text with TTF */
static void draw_text(SDL_Renderer* r, TTF_Font* font, int x, int y, 
                      const char* text, uint32_t color) {
    if (!font || !text || !text[0]) return;
    
    SDL_Color col = {(color >> 24) & 0xFF, (color >> 16) & 0xFF, 
                     (color >> 8) & 0xFF, color & 0xFF};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text, col);
    if (!surf) return;
    
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

/* Draw rounded rect (approximation with rects) */
static void draw_rounded_rect(SDL_Renderer* r, int x, int y, int w, int h, 
                               int radius, uint32_t c) {
    SDL_SetRenderDrawColor(r, (c >> 24) & 0xFF, (c >> 16) & 0xFF, 
                           (c >> 8) & 0xFF, c & 0xFF);
    SDL_Rect rects[3] = {
        {x + radius, y, w - 2*radius, h},
        {x, y + radius, radius, h - 2*radius},
        {x + w - radius, y + radius, radius, h - 2*radius}
    };
    SDL_RenderFillRects(r, rects, 3);
    
    /* Fill corners (simplified) */
    SDL_Rect corners[4] = {
        {x, y, radius, radius},
        {x + w - radius, y, radius, radius},
        {x, y + h - radius, radius, radius},
        {x + w - radius, y + h - radius, radius, radius}
    };
    SDL_RenderFillRects(r, corners, 4);
}

/* Draw canvas grid */
static void draw_grid(SDL_Renderer* r, int x, int y, int size) {
    SDL_SetRenderDrawColor(r, 0x3A, 0x3A, 0x5A, 0x60);
    
    int step = size / 8;
    for (int i = 1; i < 8; i++) {
        SDL_RenderDrawLine(r, x + i * step, y, x + i * step, y + size);
        SDL_RenderDrawLine(r, x, y + i * step, x + size, y + i * step);
    }
    
    /* Center lines */
    SDL_SetRenderDrawColor(r, 0x5A, 0x5A, 0x8A, 0x80);
    SDL_RenderDrawLine(r, x + size/2, y, x + size/2, y + size);
    SDL_RenderDrawLine(r, x, y + size/2, x + size, y + size/2);
}

/* Check if point is inside rect */
static int point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

/* Draw button with hover detection */
static void draw_button_ex(app_state_t* app, int x, int y, int w, int h,
                           const char* label, int selected, int button_id) {
    SDL_Renderer* r = app->renderer;
    TTF_Font* font = app->font_small;
    int hovered = point_in_rect(app->mouse_x, app->mouse_y, x, y, w, h);
    
    /* Track hovered button for cursor change */
    if (hovered && app->hovered_button == -1) {
        app->hovered_button = button_id;
    }
    
    uint32_t bg = selected ? COL_ACCENT : (hovered ? 0x3D3D6CFF : COL_PANEL);
    draw_rounded_rect(r, x, y, w, h, 4, bg);
    
    /* Draw border on hover */
    if (hovered && !selected) {
        draw_rect_outline(r, x, y, w, h, COL_ACCENT);
    }
    
    int tw = 0, th = 0;
    if (font && label) {
        TTF_SizeUTF8(font, label, &tw, &th);
    }
    draw_text(r, font, x + (w - tw) / 2, y + (h - th) / 2, label, COL_TEXT);
}

/* Draw button (legacy wrapper) */
static void draw_button(SDL_Renderer* r, TTF_Font* font, int x, int y, int w, int h,
                        const char* label, int selected, int hovered) {
    uint32_t bg = selected ? COL_ACCENT : (hovered ? 0x3D3D6CFF : COL_PANEL);
    draw_rounded_rect(r, x, y, w, h, 4, bg);
    
    if (hovered && !selected) {
        draw_rect_outline(r, x, y, w, h, COL_ACCENT);
    }
    
    int tw = 0, th = 0;
    if (font && label) {
        TTF_SizeUTF8(font, label, &tw, &th);
    }
    draw_text(r, font, x + (w - tw) / 2, y + (h - th) / 2, label, COL_TEXT);
}

/* Convert HSV to RGB (h: 0-360, s,v: 0-1) */
static uint32_t hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float r, g, b;
    
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    
    uint8_t ri = (uint8_t)((r + m) * 255);
    uint8_t gi = (uint8_t)((g + m) * 255);
    uint8_t bi = (uint8_t)((b + m) * 255);
    return (ri << 24) | (gi << 16) | (bi << 8) | 0xFF;
}

/* Render color picker panel - optimized version */
static void render_color_picker(app_state_t* app, int x, int y, int w, int h) {
    SDL_Renderer* r = app->renderer;
    
    draw_rounded_rect(r, x, y, w, h, 8, COL_PANEL);
    draw_text(r, app->font_bold, x + 16, y + 12, "Color Picker", COL_TEXT);
    
    int sv_x = x + 16;
    int sv_y = y + 40;
    int sv_size = w - 52;
    int hue_x = x + w - 28;
    int hue_y = y + 40;
    int hue_w = 16;
    int hue_h = sv_size;
    
    /* Draw SV box: base color, then white-to-transparent gradient, then black-to-transparent */
    /* Base hue color */
    uint32_t base_col = hsv_to_rgb(app->picker_hue, 1.0f, 1.0f);
    draw_rect(r, sv_x, sv_y, sv_size, sv_size, base_col);
    
    /* White gradient (left to right) using horizontal lines with decreasing alpha */
    for (int px = 0; px < sv_size; px++) {
        int alpha = 255 - (px * 255 / sv_size);
        SDL_SetRenderDrawColor(r, 255, 255, 255, alpha);
        SDL_RenderDrawLine(r, sv_x + px, sv_y, sv_x + px, sv_y + sv_size);
    }
    
    /* Black gradient (top to bottom) using horizontal lines with increasing alpha */
    for (int py = 0; py < sv_size; py++) {
        int alpha = py * 255 / sv_size;
        SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
        SDL_RenderDrawLine(r, sv_x, sv_y + py, sv_x + sv_size, sv_y + py);
    }
    
    /* Draw hue bar with 12 color stops for smooth rainbow */
    for (int i = 0; i < 12; i++) {
        float hue = i * 30.0f;
        int seg_h = hue_h / 12;
        uint32_t col = hsv_to_rgb(hue, 1.0f, 1.0f);
        draw_rect(r, hue_x, hue_y + i * seg_h, hue_w, seg_h + 1, col);
    }
    
    /* Draw SV cursor */
    int sv_cx = sv_x + (int)(app->picker_sat * sv_size);
    int sv_cy = sv_y + (int)((1.0f - app->picker_val) * sv_size);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawLine(r, sv_cx - 5, sv_cy, sv_cx + 5, sv_cy);
    SDL_RenderDrawLine(r, sv_cx, sv_cy - 5, sv_cx, sv_cy + 5);
    
    /* Draw hue cursor */
    int hue_cy = hue_y + (int)(app->picker_hue / 360.0f * hue_h);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawLine(r, hue_x - 2, hue_cy, hue_x + hue_w + 2, hue_cy);
    
    /* Current color preview */
    uint32_t cur_col = hsv_to_rgb(app->picker_hue, app->picker_sat, app->picker_val);
    draw_rect(r, x + 16, y + sv_size + 50, 40, 24, cur_col);
    draw_rect_outline(r, x + 16, y + sv_size + 50, 40, 24, COL_BORDER);
    
    /* Hex value */
    char hex_buf[16];
    snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X", 
             (cur_col >> 24) & 0xFF, (cur_col >> 16) & 0xFF, (cur_col >> 8) & 0xFF);
    draw_text(r, app->font_small, x + 64, y + sv_size + 54, hex_buf, COL_TEXT);
    
    /* Apply button */
    draw_button(r, app->font_small, x + 16, y + sv_size + 84, w - 32, 28, "Apply Color", 0, 0);
}

/* Render header bar - modern design */
static void render_toolbar(app_state_t* app) {
    SDL_Renderer* r = app->renderer;
    
    draw_rect(r, 0, 0, WINDOW_WIDTH, TOOLBAR_HEIGHT, COL_SIDEBAR);
    draw_rect(r, 0, TOOLBAR_HEIGHT - 1, WINDOW_WIDTH, 1, COL_BORDER);
    
    /* File name and unsaved indicator */
    draw_text(r, app->font, LEFT_SIDEBAR_WIDTH + 20, 18, "untitled_icon.nxi", COL_TEXT_DIM);
    draw_rect(r, LEFT_SIDEBAR_WIDTH + 170, 24, 8, 8, COL_ORANGE); /* Unsaved dot */
    
    /* Right side buttons */
    int bx = WINDOW_WIDTH - RIGHT_SIDEBAR_WIDTH - 260;
    
    /* Zoom dropdown */
    draw_rounded_rect(r, bx, 12, 70, 32, 4, COL_PANEL);
    draw_rect_outline(r, bx, 12, 70, 32, COL_BORDER);
    draw_text(r, app->font_small, bx + 16, 18, "100%", COL_TEXT);
    
    /* Preview button */
    bx += 80;
    int preview_hovered = point_in_rect(app->mouse_x, app->mouse_y, bx, 12, 120, 32);
    draw_rounded_rect(r, bx, 12, 120, 32, 4, preview_hovered ? COL_PANEL : 0x3F3F46FF);
    draw_text(r, app->font_small, bx + 12, 18, "Preview", COL_TEXT);
    if (preview_hovered) app->hovered_button = 50;
    
    /* Export button */
    bx += 130;
    int export_hovered = point_in_rect(app->mouse_x, app->mouse_y, bx, 12, 100, 32);
    draw_rounded_rect(r, bx, 12, 100, 32, 4, export_hovered ? COL_ACCENT_HVR : COL_ACCENT);
    draw_text(r, app->font_small, bx + 14, 18, "Export .nxi", COL_TEXT);
    if (export_hovered) app->hovered_button = 51;
}

/* Render left sidebar - Layer Stack */
static void render_sidebar(app_state_t* app) {
    SDL_Renderer* r = app->renderer;
    int sy = TOOLBAR_HEIGHT;
    int sh = WINDOW_HEIGHT - TOOLBAR_HEIGHT;
    int sw = LEFT_SIDEBAR_WIDTH;
    
    /* Sidebar background */
    draw_rect(r, 0, sy, sw, sh, COL_SIDEBAR);
    draw_rect(r, sw - 1, sy, 1, sh, COL_BORDER);
    
    /* Header with branding */
    draw_rect(r, 0, sy, sw, 56, COL_SIDEBAR);
    draw_rect(r, 0, sy + 55, sw, 1, COL_BORDER);
    
    /* Logo icon */
    draw_rounded_rect(r, 16, sy + 12, 32, 32, 8, COL_ACCENT);
    draw_text(r, app->font_small, 24, sy + 20, "L", COL_TEXT);
    
    /* Title and subtitle */
    draw_text(r, app->font_bold, 56, sy + 12, "IconLay", COL_TEXT);
    draw_text(r, app->font_small, 56, sy + 30, "NeolyxOS Native", COL_TEXT_DIM);
    
    /* Import button */
    int by = sy + 72;
    int import_hovered = point_in_rect(app->mouse_x, app->mouse_y, 16, by, sw - 32, 40);
    draw_rounded_rect(r, 16, by, sw - 32, 40, 8, import_hovered ? COL_ACCENT_HVR : COL_ACCENT);
    draw_text(r, app->font, 70, by + 10, "Import Layer (SVG/PNG)", COL_TEXT);
    if (import_hovered) app->hovered_button = 1000;
    
    /* Layer Stack header */
    by += 56;
    draw_rect(r, 0, by, sw, 1, COL_BORDER);
    by += 16;
    
    char layer_count_buf[32];
    snprintf(layer_count_buf, sizeof(layer_count_buf), "LAYER STACK (%d)", 
             app->icon ? app->icon->layer_count : 0);
    draw_text(r, app->font_small, 16, by, layer_count_buf, COL_TEXT_DIM);
    by += 28;
    
    /* Layer list */
    if (app->icon) {
        for (uint32_t i = 0; i < app->icon->layer_count && i < 8; i++) {
            nxi_layer_t* layer = app->icon->layers[i];
            int selected = ((int)i == app->selected_layer);
            int hovered = point_in_rect(app->mouse_x, app->mouse_y, 12, by, sw - 24, 64);
            
            /* Layer card background */
            if (selected) {
                draw_rounded_rect(r, 12, by, sw - 24, 64, 8, 0x3B82F633);
                draw_rect_outline(r, 12, by, sw - 24, 64, 0x3B82F680);
            } else if (hovered) {
                draw_rounded_rect(r, 12, by, sw - 24, 64, 8, COL_PANEL);
            }
            if (hovered) app->hovered_button = 2000 + i;
            
            /* Type badge */
            const char* type_str = "PNG";
            if (strstr(layer->name, ".svg")) type_str = "SVG";
            draw_rounded_rect(r, 24, by + 12, 40, 40, 4, 0x09090BFF);
            draw_rect_outline(r, 24, by + 12, 40, 40, COL_BORDER);
            draw_text(r, app->font_small, 30, by + 26, type_str, COL_TEXT_DIM);
            
            /* Layer name */
            char name_buf[20];
            strncpy(name_buf, layer->name, 18);
            name_buf[18] = '\0';
            draw_text(r, app->font, 76, by + 14, name_buf, COL_TEXT);
            
            /* Blend mode and opacity */
            char info_buf[32];
            snprintf(info_buf, sizeof(info_buf), "Normal • %d%%", (layer->opacity * 100) / 255);
            draw_text(r, app->font_small, 76, by + 34, info_buf, COL_TEXT_DIM);
            
            /* Opacity slider (if selected) */
            if (selected) {
                by += 68;
                draw_text(r, app->font_small, 24, by + 4, "Opacity", COL_TEXT_DIM);
                int slider_w = sw - 120;
                draw_rect(r, 80, by + 8, slider_w, 4, COL_BORDER);
                int fill_w = (slider_w * layer->opacity) / 255;
                draw_rect(r, 80, by + 8, fill_w, 4, COL_ACCENT);
                char op_buf[8];
                snprintf(op_buf, sizeof(op_buf), "%d%%", (layer->opacity * 100) / 255);
                draw_text(r, app->font_small, sw - 50, by + 2, op_buf, COL_TEXT_DIM);
                by += 24;
            } else {
                by += 68;
            }
            
            by += 4;
        }
    }
    
    /* Export info footer */
    int footer_y = WINDOW_HEIGHT - 80;
    draw_rect(r, 0, footer_y, sw, 1, COL_BORDER);
    draw_rounded_rect(r, 0, footer_y + 1, sw, 80, 0, 0x18181BCC);
    draw_text(r, app->font_small, 16, footer_y + 16, "EXPORT FORMAT", COL_TEXT_DIM);
    draw_text(r, app->font, 16, footer_y + 38, ".nxi (NeolyxOS Icon)", COL_TEXT_DIM);
    draw_text(r, app->font_small, sw - 50, footer_y + 40, "v2.1", COL_ACCENT);
}

/* Render right sidebar - Effects Panel */
static void render_properties(app_state_t* app) {
    SDL_Renderer* r = app->renderer;
    int px = WINDOW_WIDTH - RIGHT_SIDEBAR_WIDTH;
    int py = TOOLBAR_HEIGHT;
    int pw = RIGHT_SIDEBAR_WIDTH;
    int ph = WINDOW_HEIGHT - TOOLBAR_HEIGHT;
    
    /* Sidebar background */
    draw_rect(r, px, py, pw, ph, COL_SIDEBAR);
    draw_rect(r, px, py, 1, ph, COL_BORDER);
    
    /* Header */
    draw_rect(r, px, py, pw, 56, COL_SIDEBAR);
    draw_rect(r, px, py + 55, pw, 1, COL_BORDER);
    draw_text(r, app->font_bold, px + 32, py + 18, "Effects & Compositing", COL_TEXT);
    
    int y = py + 72;
    
    /* Material FX Section */
    draw_text(r, app->font_small, px + 16, y, "MATERIAL FX", COL_TEXT_DIM);
    y += 28;
    
    /* Material effect toggle buttons (2x3 grid) */
    int bw = (pw - 48) / 2;
    int bh = 44;
    
    /* Row 1: Glass, Frost */
    int glass_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, bw, bh);
    draw_rounded_rect(r, px + 16, y, bw, bh, 8, 
                     app->fx_glass ? COL_ACCENT : (glass_hovered ? COL_PANEL : COL_PANEL));
    if (app->fx_glass) draw_rect_outline(r, px + 16, y, bw, bh, 0x3B82F6AA);
    draw_text(r, app->font, px + 50, y + 12, "Glass", COL_TEXT);
    if (glass_hovered) app->hovered_button = 300;
    
    int frost_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 24 + bw, y, bw, bh);
    draw_rounded_rect(r, px + 24 + bw, y, bw, bh, 8, 
                     app->fx_frost ? COL_ACCENT : (frost_hovered ? COL_PANEL : COL_PANEL));
    draw_text(r, app->font, px + 58 + bw, y + 12, "Frost", COL_TEXT);
    if (frost_hovered) app->hovered_button = 301;
    y += bh + 8;
    
    /* Row 2: Metal, Liquid */
    int metal_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, bw, bh);
    draw_rounded_rect(r, px + 16, y, bw, bh, 8, 
                     app->fx_metal ? COL_ACCENT : (metal_hovered ? COL_PANEL : COL_PANEL));
    draw_text(r, app->font, px + 50, y + 12, "Metal", COL_TEXT);
    if (metal_hovered) app->hovered_button = 302;
    
    int liquid_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 24 + bw, y, bw, bh);
    draw_rounded_rect(r, px + 24 + bw, y, bw, bh, 8, 
                     app->fx_liquid ? COL_ACCENT : (liquid_hovered ? COL_PANEL : COL_PANEL));
    draw_text(r, app->font, px + 58 + bw, y + 12, "Liquid", COL_TEXT);
    if (liquid_hovered) app->hovered_button = 303;
    y += bh + 8;
    
    /* Row 3: Glow */
    int glow_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, bw, bh);
    draw_rounded_rect(r, px + 16, y, bw, bh, 8, 
                     app->fx_glow ? COL_ACCENT : (glow_hovered ? COL_PANEL : COL_PANEL));
    draw_text(r, app->font, px + 50, y + 12, "Glow", COL_TEXT);
    if (glow_hovered) app->hovered_button = 304;
    y += bh + 24;
    
    /* Effect Parameters Section */
    draw_text(r, app->font_small, px + 16, y, "EFFECT PARAMETERS", COL_TEXT_DIM);
    y += 28;
    
    /* Effect sliders */
    const char* effect_names[] = {"Blur", "Brightness", "Contrast", "Saturation", "Shadow", "Glow", "Depth"};
    int* effect_values[] = {&app->fx_blur, &app->fx_brightness, &app->fx_contrast, 
                            &app->fx_saturation, &app->fx_shadow, &app->fx_glow_amount, &app->fx_depth};
    
    for (int i = 0; i < 7; i++) {
        draw_text(r, app->font, px + 16, y, effect_names[i], COL_TEXT);
        char val_buf[8];
        snprintf(val_buf, sizeof(val_buf), "%d", *effect_values[i]);
        draw_text(r, app->font_small, px + pw - 50, y, val_buf, COL_TEXT_DIM);
        y += 22;
        
        /* Slider track */
        int slider_w = pw - 32;
        draw_rect(r, px + 16, y, slider_w, 6, COL_PANEL);
        /* Slider fill */
        int fill_w = (slider_w * (*effect_values[i])) / 200;
        draw_rounded_rect(r, px + 16, y, fill_w, 6, 3, COL_ACCENT);
        y += 20;
    }
    
    y += 8;
    
    /* Export Sizes Section */
    draw_text(r, app->font_small, px + 16, y, "EXPORT SIZES (in .nxi)", COL_TEXT_DIM);
    y += 24;
    
    /* Export size checkboxes */
    typedef struct { const char* label; const char* size; int* checked; } SizeRow;
    SizeRow sizes[] = {
        {"Toolbar", "16×16", &app->export_16},
        {"List", "32×32", &app->export_32},
        {"Menu", "64×64", &app->export_64},
        {"Dock", "128×128", &app->export_128},
        {"AppIcon", "256×256", &app->export_256},
        {"Display", "512×512", &app->export_512},
        {"Retina", "1024×1024", &app->export_1024}
    };
    
    for (int i = 0; i < 7; i++) {
        int row_hovered = point_in_rect(app->mouse_x, app->mouse_y, px + 16, y, pw - 32, 28);
        if (row_hovered) {
            draw_rounded_rect(r, px + 16, y, pw - 32, 28, 4, COL_PANEL);
            app->hovered_button = 400 + i;
        }
        
        /* Checkbox */
        draw_rect_outline(r, px + 24, y + 6, 16, 16, COL_BORDER);
        if (*sizes[i].checked) {
            draw_rect(r, px + 27, y + 9, 10, 10, COL_ACCENT);
        }
        
        draw_text(r, app->font, px + 48, y + 6, sizes[i].label, COL_TEXT);
        draw_text(r, app->font_small, px + pw - 80, y + 8, sizes[i].size, COL_TEXT_DIM);
        y += 32;
    }
    
    y += 8;
    
    /* Universal Icon Format info box */
    draw_rounded_rect(r, px + 16, y, pw - 32, 80, 8, 0x3B82F620);
    draw_rect_outline(r, px + 16, y, pw - 32, 80, 0x3B82F640);
    
    /* Icon */
    draw_rounded_rect(r, px + 28, y + 16, 32, 32, 8, COL_ACCENT);
    draw_text(r, app->font_small, px + 38, y + 24, "*", COL_TEXT);
    
    /* Text */
    draw_text(r, app->font_bold, px + 72, y + 14, "Universal Icon Format", 0x93C5FDFF);
    draw_text(r, app->font_small, px + 72, y + 34, "Exports as .nxi with all", COL_TEXT_DIM);
    draw_text(r, app->font_small, px + 72, y + 50, "sizes for NeolyxOS.", COL_TEXT_DIM);
}

/* Render canvas area */
static void render_canvas(app_state_t* app) {
    SDL_Renderer* r = app->renderer;
    
    /* Center canvas between sidebars */
    int canvas_area_width = WINDOW_WIDTH - LEFT_SIDEBAR_WIDTH - RIGHT_SIDEBAR_WIDTH;
    int cx = LEFT_SIDEBAR_WIDTH + (canvas_area_width - PREVIEW_SIZE) / 2;
    int cy = TOOLBAR_HEIGHT + 80;
    
    /* Size indicator above preview */
    char size_buf[32];
    snprintf(size_buf, sizeof(size_buf), "%dx%dpx", app->preview_size, app->preview_size);
    int tw = 0, th = 0;
    if (app->font_small) TTF_SizeUTF8(app->font_small, size_buf, &tw, &th);
    draw_rounded_rect(r, cx + (PREVIEW_SIZE - tw - 24) / 2, cy - 40, tw + 24, 28, 8, COL_PANEL);
    draw_text(r, app->font_small, cx + (PREVIEW_SIZE - tw) / 2, cy - 34, size_buf, COL_TEXT_DIM);
    
    /* Canvas card with shadow effect */
    draw_rounded_rect(r, cx - 4, cy - 4, PREVIEW_SIZE + 8, PREVIEW_SIZE + 8, 12, COL_SIDEBAR);
    draw_rect_outline(r, cx - 4, cy - 4, PREVIEW_SIZE + 8, PREVIEW_SIZE + 8, COL_BORDER);
    
    /* Background grid pattern */
    int grid_step = 32;
    SDL_SetRenderDrawColor(r, 0x27, 0x27, 0x2A, 0x30);
    for (int gx = cx; gx < cx + PREVIEW_SIZE; gx += grid_step) {
        SDL_RenderDrawLine(r, gx, cy, gx, cy + PREVIEW_SIZE);
    }
    for (int gy = cy; gy < cy + PREVIEW_SIZE; gy += grid_step) {
        SDL_RenderDrawLine(r, cx, gy, cx + PREVIEW_SIZE, gy);
    }
    
    /* Render icon preview */
    render_preview(app);
    SDL_Rect canvas_rect = {cx, cy, PREVIEW_SIZE, PREVIEW_SIZE};
    SDL_RenderCopy(r, app->canvas_texture, NULL, &canvas_rect);
    
    /* Size variant buttons below canvas */
    int btn_sizes[] = {16, 32, 64, 128, 256, 512};
    int btn_count = 6;
    int btn_w = 48;
    int btn_gap = 8;
    int btns_total_w = btn_count * btn_w + (btn_count - 1) * btn_gap;
    int btn_x = cx + (PREVIEW_SIZE - btns_total_w) / 2;
    int btn_y = cy + PREVIEW_SIZE + 24;
    
    for (int i = 0; i < btn_count; i++) {
        int hovered = point_in_rect(app->mouse_x, app->mouse_y, btn_x, btn_y, btn_w, btn_w);
        int selected = (app->preview_size == btn_sizes[i]);
        
        draw_rounded_rect(r, btn_x, btn_y, btn_w, btn_w, 8, 
                         selected ? COL_ACCENT : (hovered ? COL_PANEL : COL_PANEL));
        if (hovered) app->hovered_button = 500 + i;
        
        char label[8];
        snprintf(label, sizeof(label), "%d", btn_sizes[i]);
        int lw = 0, lh = 0;
        if (app->font_small) TTF_SizeUTF8(app->font_small, label, &lw, &lh);
        draw_text(r, app->font_small, btn_x + (btn_w - lw) / 2, btn_y + (btn_w - lh) / 2, 
                 label, selected ? COL_TEXT : COL_TEXT_DIM);
        
        btn_x += btn_w + btn_gap;
    }
}

/* Render status bar */
static void render_statusbar(app_state_t* app) {
    SDL_Renderer* r = app->renderer;
    int sy = WINDOW_HEIGHT - 28;
    
    draw_rect(r, 0, sy, WINDOW_WIDTH, 28, COL_SIDEBAR);
    draw_rect(r, 0, sy, WINDOW_WIDTH, 1, COL_BORDER);
    
    draw_text(r, app->font_small, 16, sy + 6, 
              "Drag: move | Scroll: scale | S: save | E: export | I: import | 1-4: preset", 
              COL_TEXT_DIM);
    
    char size_buf[32];
    snprintf(size_buf, sizeof(size_buf), "1024x1024");
    draw_text(r, app->font_small, WINDOW_WIDTH - 100, sy + 6, size_buf, COL_TEXT_DIM);
}

/* Render the entire UI */
static void render_ui(app_state_t* app) {
    SDL_Renderer* r = app->renderer;
    
    /* Background: zinc-950 */
    SDL_SetRenderDrawColor(r, 0x09, 0x09, 0x0B, 0xFF);
    SDL_RenderClear(r);
    
    render_toolbar(app);
    render_sidebar(app);
    render_canvas(app);
    render_properties(app);
    
    /* Color picker overlay */
    if (app->color_picker_active) {
        int cp_x = LEFT_SIDEBAR_WIDTH + 150;
        int cp_y = TOOLBAR_HEIGHT + 80;
        render_color_picker(app, cp_x, cp_y, 280, 340);
    }
    
    SDL_RenderPresent(r);
}

/* Handle mouse click on canvas */
static void handle_canvas_click(app_state_t* app, int mx, int my) {
    if (!app->icon) return;
    
    int cx = SIDEBAR_WIDTH + 20;
    int cy = TOOLBAR_HEIGHT + 20;
    
    float nx = (float)(mx - cx) / PREVIEW_SIZE;
    float ny = (float)(my - cy) / PREVIEW_SIZE;
    
    if (nx < 0 || nx > 1 || ny < 0 || ny > 1) return;
    
    for (int i = app->icon->layer_count - 1; i >= 0; i--) {
        if (layer_hit_test(app->icon->layers[i], nx, ny)) {
            app->selected_layer = i;
            app->dragging_layer = i;
            app->drag_offset_x = nx - app->icon->layers[i]->pos_x;
            app->drag_offset_y = ny - app->icon->layers[i]->pos_y;
            return;
        }
    }
}

/* Handle mouse drag */
static void handle_drag(app_state_t* app, int mx, int my) {
    if (app->dragging_layer < 0 || !app->icon) return;
    
    int cx = SIDEBAR_WIDTH + 20;
    int cy = TOOLBAR_HEIGHT + 20;
    
    float nx = (float)(mx - cx) / PREVIEW_SIZE;
    float ny = (float)(my - cy) / PREVIEW_SIZE;
    
    nxi_layer_t* layer = app->icon->layers[app->dragging_layer];
    layer->pos_x = nx - app->drag_offset_x;
    layer->pos_y = ny - app->drag_offset_y;
    
    if (layer->pos_x < 0) layer->pos_x = 0;
    if (layer->pos_x > 1) layer->pos_x = 1;
    if (layer->pos_y < 0) layer->pos_y = 0;
    if (layer->pos_y > 1) layer->pos_y = 1;
}

/* Handle scroll wheel - only zoom with Ctrl held */
static void handle_scroll(app_state_t* app, int dy, int ctrl_held) {
    if (!app->icon || app->selected_layer < 0) return;
    if (!ctrl_held) return;  /* Only zoom with Ctrl+Scroll */
    
    nxi_layer_t* layer = app->icon->layers[app->selected_layer];
    layer->scale += dy * 0.05f;
    if (layer->scale < 0.1f) layer->scale = 0.1f;
    if (layer->scale > 3.0f) layer->scale = 3.0f;
}

/* Handle button clicks in UI */
static void handle_button_click(app_state_t* app, int mx, int my) {
    /* Color picker click handling */
    if (app->color_picker_active) {
        int cp_x = SIDEBAR_WIDTH + 150;
        int cp_y = TOOLBAR_HEIGHT + 80;
        int sv_x = cp_x + 16;
        int sv_y = cp_y + 40;
        int sv_size = 280 - 52;  /* w - 52 */
        int hue_x = cp_x + 280 - 28;
        int hue_y = cp_y + 40;
        int hue_w = 16;
        int hue_h = sv_size;
        
        /* SV box click */
        if (mx >= sv_x && mx <= sv_x + sv_size && my >= sv_y && my <= sv_y + sv_size) {
            app->picker_sat = (float)(mx - sv_x) / sv_size;
            app->picker_val = 1.0f - (float)(my - sv_y) / sv_size;
            if (app->picker_sat < 0) app->picker_sat = 0;
            if (app->picker_sat > 1) app->picker_sat = 1;
            if (app->picker_val < 0) app->picker_val = 0;
            if (app->picker_val > 1) app->picker_val = 1;
            return;
        }
        
        /* Hue bar click */
        if (mx >= hue_x && mx <= hue_x + hue_w && my >= hue_y && my <= hue_y + hue_h) {
            app->picker_hue = (float)(my - hue_y) / hue_h * 360.0f;
            if (app->picker_hue < 0) app->picker_hue = 0;
            if (app->picker_hue > 360) app->picker_hue = 360;
            return;
        }
        
        /* Apply button click */
        int apply_y = cp_y + sv_size + 84;
        if (mx >= cp_x + 16 && mx <= cp_x + 264 && my >= apply_y && my <= apply_y + 28) {
            if (app->icon && app->selected_layer >= 0 && 
                app->selected_layer < (int)app->icon->layer_count) {
                uint32_t new_color = hsv_to_rgb(app->picker_hue, app->picker_sat, app->picker_val);
                app->icon->layers[app->selected_layer]->fill_color = new_color;
                printf("Applied color #%06X to layer\n", new_color >> 8);
            }
            app->color_picker_active = 0;
            return;
        }
    }
    
    int px = SIDEBAR_WIDTH + PREVIEW_SIZE + 30;
    int pw = WINDOW_WIDTH - px - 20;
    int bw = (pw - 48) / 2;
    
    /* Export section starts at TOOLBAR_HEIGHT + 20 + 280 */
    int py = TOOLBAR_HEIGHT + 20 + 280;
    int by = py + 50;  /* First row of preset buttons */
    
    /* Check Desktop button (px + 16, by, bw, 28) */
    if (mx >= px + 16 && mx <= px + 16 + bw && my >= by && my <= by + 28) {
        app->export_preset = NXI_PRESET_DESKTOP;
        app->export_preset_name = "Desktop";
        printf("Selected preset: Desktop\n");
        return;
    }
    
    /* Check Mobile button (px + 24 + bw, by, bw, 28) */
    if (mx >= px + 24 + bw && mx <= px + 24 + bw + bw && my >= by && my <= by + 28) {
        app->export_preset = NXI_PRESET_MOBILE;
        app->export_preset_name = "Mobile";
        printf("Selected preset: Mobile\n");
        return;
    }
    
    by += 36;  /* Second row */
    
    /* Check Watch button */
    if (mx >= px + 16 && mx <= px + 16 + bw && my >= by && my <= by + 28) {
        app->export_preset = NXI_PRESET_WATCH;
        app->export_preset_name = "Watch";
        printf("Selected preset: Watch\n");
        return;
    }
    
    /* Check All button */
    if (mx >= px + 24 + bw && mx <= px + 24 + bw + bw && my >= by && my <= by + 28) {
        app->export_preset = NXI_PRESET_ALL;
        app->export_preset_name = "All";
        printf("Selected preset: All\n");
        return;
    }
    
    by += 44;  /* Export NXI button row */
    
    /* Check Export NXI button */
    if (mx >= px + 16 && mx <= px + pw - 16 && my >= by && my <= by + 36) {
        if (app->icon && app->icon->layer_count > 0) {
            char filename[256];
            snprintf(filename, sizeof(filename), "icon_%s.nxi", app->export_preset_name);
            nxi_save(app->icon, filename);
            nxi_export_preset(app->icon, app->export_preset, ".");
            printf("Exported: %s (%s sizes)\n", filename, app->export_preset_name);
        } else {
            printf("No layers to export\n");
        }
        return;
    }
    
    /* Check Add Layer button in sidebar */
    int add_y = TOOLBAR_HEIGHT + 50 + (app->icon ? app->icon->layer_count * 32 : 0) + 10;
    if (mx >= 16 && mx <= SIDEBAR_WIDTH - 16 && my >= add_y && my <= add_y + 28) {
        printf("Add Layer clicked (use I key to import SVG)\n");
        return;
    }
    
    /* Check layer selection in sidebar */
    if (app->icon && mx >= 8 && mx <= SIDEBAR_WIDTH - 8) {
        int ly = TOOLBAR_HEIGHT + 50;
        for (uint32_t i = 0; i < app->icon->layer_count && i < 10; i++) {
            if (my >= ly - 2 && my <= ly + 26) {
                app->selected_layer = i;
                printf("Selected layer: %s\n", app->icon->layers[i]->name);
                return;
            }
            ly += 32;
        }
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
    if (IMG_Init(IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }
    
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    
    app_state_t app = {0};
    app.selected_layer = -1;
    app.dragging_layer = -1;
    app.export_preset = NXI_PRESET_DESKTOP;
    app.export_preset_name = "Desktop";
    
    /* Initialize Material Effects state */
    app.fx_glass = 1;
    app.fx_frost = 0;
    app.fx_metal = 0;
    app.fx_liquid = 0;
    app.fx_glow = 1;
    
    /* Initialize effect parameters */
    app.fx_blur = 42;
    app.fx_brightness = 100;
    app.fx_contrast = 105;
    app.fx_saturation = 100;
    app.fx_shadow = 35;
    app.fx_glow_amount = 20;
    app.fx_depth = 15;
    
    /* Initialize preview size and export sizes */
    app.preview_size = 512;
    app.export_16 = 1;
    app.export_32 = 1;
    app.export_64 = 1;
    app.export_128 = 1;
    app.export_256 = 1;
    app.export_512 = 1;
    app.export_1024 = 1;
    
    app.window = SDL_CreateWindow("IconLay - NeolyxOS",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  WINDOW_WIDTH, WINDOW_HEIGHT,
                                  SDL_WINDOW_SHOWN);
    if (!app.window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    
    app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED);
    if (!app.renderer) {
        SDL_DestroyWindow(app.window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    
    /* Create cursors */
    app.cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    app.cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    app.cursor_move = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    app.current_cursor = app.cursor_arrow;
    app.hovered_button = -1;
    
    /* Load fonts - try multiple paths */
    const char* font_paths[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL
    };
    const char* font_bold_paths[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        NULL
    };
    
    for (int i = 0; font_paths[i] && !app.font; i++) {
        app.font = TTF_OpenFont(font_paths[i], 14);
        app.font_small = TTF_OpenFont(font_paths[i], 11);
    }
    for (int i = 0; font_bold_paths[i] && !app.font_bold; i++) {
        app.font_bold = TTF_OpenFont(font_bold_paths[i], 14);
    }
    if (!app.font_bold) app.font_bold = app.font;
    
    /* Load logo */
    SDL_Surface* logo_surf = IMG_Load("resource/iconlay.png");
    if (logo_surf) {
        app.logo_texture = SDL_CreateTextureFromSurface(app.renderer, logo_surf);
        app.logo_w = logo_surf->w;
        app.logo_h = logo_surf->h;
        SDL_FreeSurface(logo_surf);
    }
    
    /* Create canvas texture */
    app.canvas_texture = SDL_CreateTexture(app.renderer,
                                            SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            CANVAS_SIZE, CANVAS_SIZE);
    SDL_SetTextureBlendMode(app.canvas_texture, SDL_BLENDMODE_BLEND);
    
    app.preview_buffer = calloc(CANVAS_SIZE * CANVAS_SIZE * 4, 1);
    app.icon = nxi_create();
    
    /* Import SVG if provided */
    if (argc > 1) {
        svg_import_opts_t opts = svg_import_defaults();
        opts.default_color = 0xFFFFFFFF;
        
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
                        handle_button_click(&app, e.button.x, e.button.y);
                        handle_canvas_click(&app, e.button.x, e.button.y);
                    }
                    break;
                    
                case SDL_MOUSEBUTTONUP:
                    app.dragging_layer = -1;
                    break;
                    
                case SDL_MOUSEMOTION:
                    app.mouse_x = e.motion.x;
                    app.mouse_y = e.motion.y;
                    if (app.dragging_layer >= 0) {
                        handle_drag(&app, e.motion.x, e.motion.y);
                    }
                    break;
                    
                case SDL_MOUSEWHEEL: {
                    int ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
                    handle_scroll(&app, e.wheel.y, ctrl);
                    break;
                }
                    
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_s:
                            if (app.icon) {
                                nxi_save(app.icon, "output.nxi");
                                printf("Saved: output.nxi\n");
                            }
                            break;
                            
                        case SDLK_e:
                            if (app.icon) {
                                nxi_export_preset(app.icon, app.export_preset, ".");
                                printf("Exported %s preset\n", app.export_preset_name);
                            }
                            break;
                            
                        case SDLK_1:
                            app.export_preset = NXI_PRESET_DESKTOP;
                            app.export_preset_name = "Desktop";
                            break;
                            
                        case SDLK_2:
                            app.export_preset = NXI_PRESET_MOBILE;
                            app.export_preset_name = "Mobile";
                            break;
                            
                        case SDLK_3:
                            app.export_preset = NXI_PRESET_WATCH;
                            app.export_preset_name = "Watch";
                            break;
                            
                        case SDLK_4:
                            app.export_preset = NXI_PRESET_ALL;
                            app.export_preset_name = "All";
                            break;
                            
                        case SDLK_i:
                            if (app.icon) {
                                svg_import_opts_t opts = svg_import_defaults();
                                if (svg_import_multilayer(
                                    "/home/swana/Downloads/iconlib/neolyx-settings.svg",
                                    app.icon, &opts) == 0) {
                                    app.selected_layer = app.icon->layer_count - 1;
                                }
                            }
                            break;
                            
                        case SDLK_c:
                            app.color_picker_active = !app.color_picker_active;
                            if (app.color_picker_active) {
                                app.picker_hue = 0;
                                app.picker_sat = 1.0f;
                                app.picker_val = 1.0f;
                                printf("Color picker opened (use C to close)\n");
                            } else {
                                printf("Color picker closed\n");
                            }
                            break;
                            
                        case SDLK_DELETE:
                            if (app.icon && app.selected_layer >= 0) {
                                nxi_remove_layer(app.icon, app.selected_layer);
                                if (app.selected_layer >= (int)app.icon->layer_count) {
                                    app.selected_layer = app.icon->layer_count - 1;
                                }
                            }
                            break;
                            
                        case SDLK_ESCAPE:
                            app.running = 0;
                            break;
                    }
                    break;
            }
        }
        
        /* Reset hovered button before render (will be set during render) */
        app.hovered_button = -1;
        
        render_ui(&app);
        
        /* Update cursor based on hover state */
        SDL_Cursor* new_cursor = app.cursor_arrow;
        if (app.hovered_button >= 0) {
            new_cursor = app.cursor_hand;
        } else if (app.dragging_layer >= 0) {
            new_cursor = app.cursor_move;
        }
        if (new_cursor != app.current_cursor) {
            SDL_SetCursor(new_cursor);
            app.current_cursor = new_cursor;
        }
        
        SDL_Delay(16);
    }
    
    /* Cleanup */
    free(app.preview_buffer);
    if (app.canvas_texture) SDL_DestroyTexture(app.canvas_texture);
    if (app.logo_texture) SDL_DestroyTexture(app.logo_texture);
    if (app.font) TTF_CloseFont(app.font);
    if (app.font_small) TTF_CloseFont(app.font_small);
    if (app.font_bold) TTF_CloseFont(app.font_bold);
    if (app.cursor_arrow) SDL_FreeCursor(app.cursor_arrow);
    if (app.cursor_hand) SDL_FreeCursor(app.cursor_hand);
    if (app.cursor_move) SDL_FreeCursor(app.cursor_move);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    nxi_free(app.icon);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    
    return 0;
}
