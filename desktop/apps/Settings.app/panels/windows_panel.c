/*
 * Settings - Windows Panel
 * Window animations, tiling, and arrangement settings
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include "../../../include/window_manager.h"

/* ============ Colors ============ */

#define COLOR_BG            0x1E1E2E
#define COLOR_CARD          0x313244
#define COLOR_TEXT          0xCDD6F4
#define COLOR_TEXT_DIM      0x6C7086
#define COLOR_PRIMARY       0x89B4FA
#define COLOR_BORDER        0x45475A

/* ============ Animation Style Labels ============ */

static const char *anim_styles[] = {
    "Scale",
    "Genie",
    "Fade"
};
#define ANIM_STYLE_COUNT 3

/* ============ Panel State ============ */

static struct {
    int scroll_y;
    int hover_item;
} panel_state = {0};

/* ============ Drawing Helpers ============ */

static void draw_section_header(void *ctx, int x, int y, const char *title) {
    nx_draw_text(ctx, title, x, y, COLOR_TEXT);
}

static void draw_dropdown(void *ctx, int x, int y, int w, const char *value) {
    nx_fill_rect(ctx, x, y, w, 28, COLOR_CARD);
    nx_draw_rect(ctx, x, y, w, 28, COLOR_BORDER);
    nx_draw_text(ctx, value, x + 8, y + 6, COLOR_TEXT);
    nx_draw_text(ctx, "v", x + w - 20, y + 6, COLOR_TEXT_DIM);
}

static void draw_slider(void *ctx, int x, int y, int w, float value) {
    nx_fill_rect(ctx, x, y + 4, w, 4, COLOR_CARD);
    int fill_w = (int)(value * w);
    nx_fill_rect(ctx, x, y + 4, fill_w, 4, COLOR_PRIMARY);
    nx_fill_rect(ctx, x + fill_w - 6, y, 12, 12, COLOR_PRIMARY);
}

static void draw_toggle(void *ctx, int x, int y, int enabled) {
    uint32_t bg = enabled ? COLOR_PRIMARY : COLOR_CARD;
    nx_fill_rect(ctx, x, y, 44, 24, bg);
    int knob_x = enabled ? x + 22 : x + 2;
    nx_fill_rect(ctx, knob_x, y + 2, 20, 20, COLOR_TEXT);
}

/* ============ Panel Callbacks ============ */

void windows_panel_init(settings_panel_t *panel) {
    panel_state.scroll_y = 0;
    panel_state.hover_item = -1;
}

void windows_panel_render(settings_panel_t *panel) {
    wm_settings_t *settings = wm_get_settings();
    
    int y = panel->y + 20 - panel_state.scroll_y;
    int x = panel->x + 24;
    int w = panel->width - 48;
    
    /* Header */
    nx_draw_text(ctx, "Windows", x, y, COLOR_TEXT);
    y += 45;
    
    /* ============ Animations ============ */
    draw_section_header(ctx, x, y, "Animations");
    y += 30;
    
    /* Minimize animation style */
    nx_draw_text(ctx, "Minimize Effect", x, y + 6, COLOR_TEXT);
    draw_dropdown(ctx, x + 180, y, 140, anim_styles[settings->minimize_style]);
    y += 40;
    
    /* Maximize animation style */
    nx_draw_text(ctx, "Maximize Effect", x, y + 6, COLOR_TEXT);
    draw_dropdown(ctx, x + 180, y, 140, anim_styles[settings->maximize_style]);
    y += 40;
    
    /* Animation speed */
    nx_draw_text(ctx, "Animation Speed", x, y + 6, COLOR_TEXT);
    float speed_val = (settings->animation_speed - 0.5f) / 1.5f;
    draw_slider(ctx, x + 180, y, 140, speed_val);
    char speed_str[16];
    snprintf(speed_str, sizeof(speed_str), "%.1fx", settings->animation_speed);
    nx_draw_text(ctx, speed_str, x + 340, y + 6, COLOR_TEXT_DIM);
    y += 50;
    
    /* ============ Tiling ============ */
    draw_section_header(ctx, x, y, "Window Tiling");
    y += 30;
    
    /* Enable tiling */
    nx_draw_text(ctx, "Enable Tiling", x, y + 6, COLOR_TEXT);
    draw_toggle(ctx, x + 250, y, settings->tiling_enabled);
    y += 40;
    
    /* Snap threshold */
    nx_draw_text(ctx, "Snap Distance", x, y + 6, COLOR_TEXT);
    float snap_val = settings->snap_threshold / 60.0f;
    draw_slider(ctx, x + 180, y, 140, snap_val);
    char snap_str[16];
    snprintf(snap_str, sizeof(snap_str), "%dpx", settings->snap_threshold);
    nx_draw_text(ctx, snap_str, x + 340, y + 6, COLOR_TEXT_DIM);
    y += 40;
    
    /* Tile gap */
    nx_draw_text(ctx, "Window Gap", x, y + 6, COLOR_TEXT);
    float gap_val = settings->tile_gap / 20.0f;
    draw_slider(ctx, x + 180, y, 140, gap_val);
    char gap_str[16];
    snprintf(gap_str, sizeof(gap_str), "%dpx", settings->tile_gap);
    nx_draw_text(ctx, gap_str, x + 340, y + 6, COLOR_TEXT_DIM);
    y += 50;
    
    /* ============ App Drawer ============ */
    draw_section_header(ctx, x, y, "App Drawer");
    y += 30;
    
    /* Auto-arrange */
    nx_draw_text(ctx, "Auto-Arrange Apps", x, y + 6, COLOR_TEXT);
    draw_toggle(ctx, x + 250, y, settings->auto_arrange_drawer);
    y += 40;
    
    nx_draw_text(ctx, "When disabled, manually drag apps to arrange.", 
                 x, y, COLOR_TEXT_DIM);
    y += 40;
    
    /* ============ Picture-in-Picture ============ */
    draw_section_header(ctx, x, y, "Picture-in-Picture");
    y += 30;
    
    /* PiP Opacity */
    nx_draw_text(ctx, "PiP Opacity", x, y + 6, COLOR_TEXT);
    draw_slider(ctx, x + 180, y, 140, settings->pip_opacity);
    char opacity_str[16];
    snprintf(opacity_str, sizeof(opacity_str), "%d%%", (int)(settings->pip_opacity * 100));
    nx_draw_text(ctx, opacity_str, x + 340, y + 6, COLOR_TEXT_DIM);
    y += 40;
    
    /* PiP Default Size */
    nx_draw_text(ctx, "Default Width", x, y + 6, COLOR_TEXT);
    float size_val = (settings->pip_default_w - 200) / 200.0f;
    if (size_val < 0) size_val = 0; if (size_val > 1) size_val = 1;
    draw_slider(ctx, x + 180, y, 140, size_val);
    char size_str[16];
    snprintf(size_str, sizeof(size_str), "%dpx", settings->pip_default_w);
    nx_draw_text(ctx, size_str, x + 340, y + 6, COLOR_TEXT_DIM);
}

void windows_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (!event) return;
    
    wm_settings_t *settings = wm_get_settings();
    int x = panel->x + 24;
    int y = panel->y + 20 - panel_state.scroll_y;
    
    switch (event->type) {
        case EVENT_SCROLL:
            panel_state.scroll_y -= event->scroll_delta * 20;
            if (panel_state.scroll_y < 0) panel_state.scroll_y = 0;
            break;
            
        case EVENT_CLICK: {
            int mx = event->x;
            int my = event->y;
            
            /* Minimize style dropdown (y=95) */
            if (my >= y + 75 && my < y + 103 && mx >= x + 180 && mx < x + 320) {
                settings->minimize_style = (settings->minimize_style + 1) % ANIM_STYLE_COUNT;
            }
            
            /* Maximize style dropdown (y=135) */
            if (my >= y + 115 && my < y + 143 && mx >= x + 180 && mx < x + 320) {
                settings->maximize_style = (settings->maximize_style + 1) % ANIM_STYLE_COUNT;
            }
            
            /* Animation speed slider (y=175) */
            if (my >= y + 155 && my < y + 187 && mx >= x + 180 && mx < x + 320) {
                float val = (float)(mx - x - 180) / 140.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings->animation_speed = 0.5f + val * 1.5f;
            }
            
            /* Tiling toggle (y=255) */
            if (my >= y + 235 && my < y + 265 && mx >= x + 250 && mx < x + 294) {
                settings->tiling_enabled = !settings->tiling_enabled;
            }
            
            /* Snap threshold slider (y=295) */
            if (my >= y + 275 && my < y + 307 && mx >= x + 180 && mx < x + 320) {
                float val = (float)(mx - x - 180) / 140.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings->snap_threshold = (int)(val * 60);
            }
            
            /* Tile gap slider (y=335) */
            if (my >= y + 315 && my < y + 347 && mx >= x + 180 && mx < x + 320) {
                float val = (float)(mx - x - 180) / 140.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings->tile_gap = (int)(val * 20);
            }
            
            /* Auto-arrange toggle (y=415) */
            if (my >= y + 395 && my < y + 425 && mx >= x + 250 && mx < x + 294) {
                settings->auto_arrange_drawer = !settings->auto_arrange_drawer;
            }
            
            /* PiP Opacity slider (y=485) */
            if (my >= y + 465 && my < y + 497 && mx >= x + 180 && mx < x + 320) {
                float val = (float)(mx - x - 180) / 140.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings->pip_opacity = 0.3f + val * 0.7f;  /* Range: 30% to 100% */
            }
            
            /* PiP Default Width slider (y=525) */
            if (my >= y + 505 && my < y + 537 && mx >= x + 180 && mx < x + 320) {
                float val = (float)(mx - x - 180) / 140.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings->pip_default_w = 200 + (uint32_t)(val * 200);  /* Range: 200 to 400 */
                settings->pip_default_h = settings->pip_default_w * 2 / 3;  /* Keep 3:2 ratio */
            }
            break;
        }
        
        default:
            break;
    }
}

void windows_panel_cleanup(settings_panel_t *panel) {
    /* Nothing to clean up */
}

/* ============ Panel Creation ============ */

rx_view* windows_panel_create(settings_ctx_t *ctx) {
    /* Create a simple panel container */
    settings_panel_t *panel = settings_panel_new();
    if (!panel) return NULL;
    
    panel->init = windows_panel_init;
    panel->render = windows_panel_render;
    panel->handle_event = windows_panel_handle_event;
    panel->cleanup = windows_panel_cleanup;
    
    windows_panel_init(panel);
    
    return (rx_view*)panel;
}
