/*
 * Settings - Display Manager Panel
 * Resolution, refresh rate, HDR, zoom, color profiles
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include <nxcolor.h>
#include <string.h>
#include <stdio.h>

/* ============ Display Info ============ */

typedef struct {
    int width;
    int height;
    int refresh_rate;
} resolution_t;

typedef struct {
    char name[64];
    int id;
    int current_width;
    int current_height;
    int current_refresh;
    bool is_primary;
    bool is_builtin;
    
    /* Available resolutions */
    resolution_t resolutions[32];
    int resolution_count;
    
    /* HDR support */
    bool hdr_supported;
    bool hdr_enabled;
    
    /* Brightness */
    float brightness;
    bool auto_brightness;
    
    /* Scaling */
    float scale;  /* 1.0, 1.25, 1.5, 2.0 */
    
    /* Rotation */
    int rotation;  /* 0, 90, 180, 270 */
    
    /* Color profile */
    char color_profile[64];
} display_info_t;

#define MAX_DISPLAYS 8
static display_info_t displays[MAX_DISPLAYS];
static int display_count = 0;
static int selected_display = 0;

/* ============ Forward Declarations ============ */

extern int nxdisplay_enumerate(display_info_t *out, int max);
extern int nxdisplay_set_resolution(int id, int width, int height, int refresh);
extern int nxdisplay_set_hdr(int id, bool enable);
extern int nxdisplay_set_brightness(int id, float brightness);
extern int nxdisplay_set_scale(int id, float scale);
extern int nxdisplay_set_rotation(int id, int degrees);
extern int nxdisplay_set_primary(int id);

/* ============ Panel Callbacks ============ */

void display_manager_panel_init(settings_panel_t *panel) {
    panel->title = "Displays";
    panel->icon = "display";
    
    display_count = nxdisplay_enumerate(displays, MAX_DISPLAYS);
    selected_display = 0;
}

void display_manager_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20;
    int x = panel->x + 20;
    int w = panel->width - 40;
    
    /* Header */
    panel_draw_header(x, y, "Displays");
    y += 45;
    
    /* Display arrangement preview */
    panel_draw_subheader(x, y, "Arrangement");
    y += 30;
    
    /* Draw display rectangles */
    int preview_x = x + 50;
    for (int i = 0; i < display_count; i++) {
        display_info_t *disp = &displays[i];
        
        int pw = disp->current_width / 20;
        int ph = disp->current_height / 20;
        if (pw > 150) { pw = 150; ph = pw * disp->current_height / disp->current_width; }
        
        uint32_t bg = (i == selected_display) ? 0x007AFF : 0x48484A;
        panel_fill_rect(preview_x, y, pw, ph, bg);
        panel_draw_rect(preview_x, y, pw, ph, 0x888888);
        
        /* Display number */
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        panel_draw_text(preview_x + pw/2 - 5, y + ph/2 - 8, num, 0xFFFFFF);
        
        preview_x += pw + 20;
    }
    y += 100;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    if (display_count == 0) {
        panel_draw_text(x, y, "No displays detected", 0xFF3B30);
        return;
    }
    
    display_info_t *disp = &displays[selected_display];
    
    /* Display name */
    panel_draw_subheader(x, y, disp->name);
    if (disp->is_builtin) {
        panel_draw_badge(x + 200, y, "Built-in", 0x007AFF);
    }
    if (disp->is_primary) {
        panel_draw_badge(x + 280, y, "Primary", 0x34C759);
    }
    y += 40;
    
    /* Resolution */
    panel_draw_label(x, y, "Resolution:");
    char res_str[32];
    snprintf(res_str, sizeof(res_str), "%d x %d", disp->current_width, disp->current_height);
    panel_draw_dropdown(x + 120, y, 180, res_str, NULL, disp->resolution_count);
    y += 40;
    
    /* Refresh Rate */
    panel_draw_label(x, y, "Refresh Rate:");
    char rate_str[16];
    snprintf(rate_str, sizeof(rate_str), "%d Hz", disp->current_refresh);
    panel_draw_dropdown(x + 120, y, 100, rate_str, NULL, 0);
    
    /* ProMotion indicator */
    if (disp->current_refresh >= 120) {
        panel_draw_badge(x + 240, y, "ProMotion", 0x007AFF);
    }
    y += 40;
    
    /* Scaling */
    panel_draw_label(x, y, "Scale:");
    const char *scale_options[] = {"100%", "125%", "150%", "200%"};
    int scale_idx = (int)((disp->scale - 1.0f) / 0.25f);
    if (scale_idx < 0) scale_idx = 0;
    if (scale_idx > 3) scale_idx = 3;
    panel_draw_dropdown(x + 120, y, 100, scale_options[scale_idx], scale_options, 4);
    y += 40;
    
    /* Rotation */
    panel_draw_label(x, y, "Rotation:");
    const char *rotation_options[] = {"Standard", "90°", "180°", "270°"};
    int rot_idx = disp->rotation / 90;
    panel_draw_dropdown(x + 120, y, 120, rotation_options[rot_idx], rotation_options, 4);
    y += 40;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Brightness */
    panel_draw_label(x, y, "Brightness:");
    panel_draw_slider(x + 120, y, 200, disp->brightness, 0, 1);
    char bright_str[8];
    snprintf(bright_str, sizeof(bright_str), "%.0f%%", disp->brightness * 100);
    panel_draw_text(x + 340, y, bright_str, 0x888888);
    y += 35;
    
    /* Auto-brightness */
    panel_draw_label(x, y, "Auto-brightness:");
    panel_draw_toggle(x + 150, y, disp->auto_brightness);
    y += 40;
    
    /* HDR */
    if (disp->hdr_supported) {
        panel_draw_label(x, y, "HDR:");
        panel_draw_toggle(x + 120, y, disp->hdr_enabled);
        panel_draw_text(x + 180, y, "(High Dynamic Range)", 0x666666);
        y += 40;
    }
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Color Profile */
    panel_draw_label(x, y, "Color Profile:");
    panel_draw_dropdown(x + 120, y, 200, disp->color_profile, NULL, 0);
    panel_draw_button(x + 340, y - 5, "Calibrate...", 0x007AFF);
    y += 45;
    
    /* Buttons */
    if (!disp->is_primary) {
        panel_draw_button(x, y, "Set as Primary Display", 0x007AFF);
    }
    panel_draw_button(x + 200, y, "Detect Displays", 0x48484A);
}

void display_manager_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (event->type == EVT_CLICK) {
        /* Handle display preview clicks */
        int preview_y = panel->y + 95;
        int preview_x = panel->x + 70;
        
        for (int i = 0; i < display_count; i++) {
            display_info_t *disp = &displays[i];
            int pw = disp->current_width / 20;
            if (pw > 150) pw = 150;
            
            if (event->y >= preview_y && event->y <= preview_y + 80 &&
                event->x >= preview_x && event->x <= preview_x + pw) {
                selected_display = i;
                panel->needs_redraw = true;
                break;
            }
            preview_x += pw + 20;
        }
    }
}

void display_manager_panel_cleanup(settings_panel_t *panel) {
    (void)panel;
}
