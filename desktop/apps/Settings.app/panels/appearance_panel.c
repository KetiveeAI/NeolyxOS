/*
 * Settings - Appearance Panel
 * Theme, accent colors, hover effects, dock, app drawer
 * 
 * NOTE: Window close button is on RIGHT side (not left like macOS)
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include <string.h>
#include <stdio.h>

/* ============ Appearance Types ============ */

typedef enum {
    THEME_LIGHT,
    THEME_DARK,
    THEME_AUTO    /* Match system time */
} theme_mode_t;

typedef enum {
    GLASS_CLEAR,
    GLASS_TINTED,
    GLASS_FROSTED
} liquid_glass_t;

typedef enum {
    ICON_STYLE_DEFAULT,
    ICON_STYLE_DARK,
    ICON_STYLE_CLEAR,
    ICON_STYLE_TINTED
} icon_style_t;

/* Accent colors */
typedef struct {
    const char *name;
    uint32_t color;
} accent_color_t;

static const accent_color_t accent_colors[] = {
    {"Multicolor", 0x007AFF},
    {"Blue", 0x007AFF},
    {"Purple", 0xAF52DE},
    {"Pink", 0xFF2D55},
    {"Red", 0xFF3B30},
    {"Orange", 0xFF9500},
    {"Yellow", 0xFFCC00},
    {"Green", 0x34C759},
    {"Graphite", 0x8E8E93}
};
#define ACCENT_COLOR_COUNT 9

/* ============ Appearance Settings ============ */

typedef struct {
    /* Theme */
    theme_mode_t theme;
    liquid_glass_t glass_style;
    
    /* Colors */
    int accent_color_index;
    int highlight_color_index;
    int folder_color_index;
    
    /* Icons & Widgets */
    icon_style_t icon_style;
    
    /* Window controls */
    bool close_button_right;  /* TRUE - NeolyxOS uses RIGHT side */
    bool show_window_buttons;
    
    /* Hover effects */
    bool hover_highlight;
    bool hover_scale;
    float hover_scale_amount;
    
    /* App Grid */
    int app_grid_columns;
    int app_grid_icon_size;
    bool app_grid_show_labels;
    
    /* Dock */
    int dock_icon_size;
    bool dock_magnification;
    float dock_magnification_amount;
    bool dock_auto_hide;
    
    /* Desktop */
    bool show_icons_on_desktop;
    bool snap_icons_to_grid;
    int desktop_icon_size;
    
    /* Search Bar (Anveshan) */
    int search_width;           /* 280-600px, default 320 */
    int search_transparency;    /* 0-100%, default 70 */
    int search_max_suggestions; /* 3-10, default 5 */
    bool search_apps;           /* Search applications */
    bool search_settings;       /* Search settings panels */
    bool search_files;          /* Search files */
    bool search_voice;          /* Voice search enabled */
    
    /* Animations */
    bool reduce_motion;
    float animation_speed;  /* 0.5 - 2.0 */
    
    /* Transparency */
    bool reduce_transparency;
    float window_opacity;
    
} appearance_settings_t;

static appearance_settings_t settings = {
    .theme = THEME_DARK,
    .glass_style = GLASS_CLEAR,
    .accent_color_index = 1,  /* Blue */
    .highlight_color_index = 0,  /* Auto */
    .folder_color_index = 0,  /* Auto */
    .icon_style = ICON_STYLE_DEFAULT,
    .close_button_right = true,  /* NeolyxOS standard */
    .show_window_buttons = true,
    .hover_highlight = true,
    .hover_scale = true,
    .hover_scale_amount = 1.05f,
    .app_grid_columns = 5,
    .app_grid_icon_size = 64,
    .app_grid_show_labels = true,
    .dock_icon_size = 48,
    .dock_magnification = true,
    .dock_magnification_amount = 1.5f,
    .dock_auto_hide = false,
    .show_icons_on_desktop = true,
    .snap_icons_to_grid = true,
    .desktop_icon_size = 48,
    .search_width = 320,
    .search_transparency = 70,
    .search_max_suggestions = 5,
    .search_apps = true,
    .search_settings = true,
    .search_files = false,
    .search_voice = true,
    .reduce_motion = false,
    .animation_speed = 1.0f,
    .reduce_transparency = false,
    .window_opacity = 1.0f
};

/* ============ Forward Declarations ============ */

extern int nxappearance_load(appearance_settings_t *out);
extern int nxappearance_save(const appearance_settings_t *settings);
extern void nxappearance_apply(const appearance_settings_t *settings);

/* ============ Panel Callbacks ============ */

void appearance_panel_init(settings_panel_t *panel) {
    panel->title = "Appearance";
    panel->icon = "appearance";
    
    nxappearance_load(&settings);
}

void appearance_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20;
    int x = panel->x + 20;
    int w = panel->width - 40;
    
    /* Header */
    panel_draw_header(x, y, "Appearance");
    y += 45;
    
    /* Theme Mode */
    panel_draw_label(x, y, "Appearance");
    
    /* Theme preview cards */
    int card_x = x + 150;
    const char *themes[] = {"Auto", "Light", "Dark"};
    for (int i = 0; i < 3; i++) {
        bool selected = ((i == 0 && settings.theme == THEME_AUTO) ||
                         (i == 1 && settings.theme == THEME_LIGHT) ||
                         (i == 2 && settings.theme == THEME_DARK));
        
        uint32_t border = selected ? 0x007AFF : 0x48484A;
        uint32_t bg = (i == 1) ? 0xE0E0E0 : 0x2A2A2A;
        
        panel_fill_rect(card_x, y, 80, 50, bg);
        panel_draw_rect(card_x, y, 80, 50, border);
        if (selected) {
            panel_draw_rect(card_x - 2, y - 2, 84, 54, 0x007AFF);
        }
        
        panel_draw_text(card_x + 25, y + 55, themes[i], 0xCCCCCC);
        card_x += 100;
    }
    y += 90;
    
    /* Liquid Glass */
    panel_draw_label(x, y, "Glass Style");
    panel_draw_text(x, y + 20, "Choose your preferred translucency", 0x666666);
    
    card_x = x + 150;
    const char *glass_names[] = {"Clear", "Tinted", "Frosted"};
    for (int i = 0; i < 3; i++) {
        bool selected = (settings.glass_style == (liquid_glass_t)i);
        uint32_t border = selected ? 0x007AFF : 0x48484A;
        
        panel_fill_rect(card_x, y, 80, 50, 0x2A2A2A);
        panel_draw_rect(card_x, y, 80, 50, border);
        
        panel_draw_text(card_x + 15, y + 55, glass_names[i], 0xCCCCCC);
        card_x += 100;
    }
    y += 90;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Theme section */
    panel_draw_subheader(x, y, "Theme");
    y += 35;
    
    /* Accent Color */
    panel_draw_label(x, y, "Color");
    int color_x = x + 150;
    for (int i = 0; i < ACCENT_COLOR_COUNT; i++) {
        bool selected = (settings.accent_color_index == i);
        
        panel_fill_circle(color_x + 12, y + 12, 12, accent_colors[i].color);
        if (selected) {
            panel_draw_circle(color_x + 12, y + 12, 15, 0x007AFF);
        }
        
        color_x += 35;
    }
    y += 40;
    
    /* Text highlight color */
    panel_draw_label(x, y, "Text highlight color");
    panel_draw_dropdown(x + 180, y, 120, "Automatic", NULL, 0);
    y += 35;
    
    /* Icon & widget style */
    panel_draw_label(x, y, "Icon & widget style");
    card_x = x + 180;
    const char *icon_names[] = {"Default", "Dark", "Clear", "Tinted"};
    for (int i = 0; i < 4; i++) {
        bool selected = (settings.icon_style == (icon_style_t)i);
        uint32_t border = selected ? 0x007AFF : 0x48484A;
        
        panel_fill_rect(card_x, y - 5, 50, 35, 0x2A2A2A);
        panel_draw_rect(card_x, y - 5, 50, 35, border);
        panel_draw_icon(card_x + 15, y, "folder", 20);
        
        panel_draw_text(card_x + 5, y + 35, icon_names[i], 0x888888);
        card_x += 70;
    }
    y += 60;
    
    /* Folder color */
    panel_draw_label(x, y, "Folder color");
    panel_draw_dropdown(x + 180, y, 120, "Automatic", NULL, 0);
    y += 40;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Window Controls */
    panel_draw_subheader(x, y, "Window Controls");
    y += 35;
    
    /* Close button position - RIGHT is NeolyxOS default */
    panel_draw_label(x, y, "Close button position");
    panel_draw_text(x, y + 18, "(NeolyxOS uses RIGHT side)", 0x666666);
    
    /* Preview window controls */
    int btn_preview_x = x + 250;
    
    /* NeolyxOS style: buttons on RIGHT */
    panel_fill_rect(btn_preview_x, y, 120, 30, 0x3A3A3A);
    /* Minimize */
    panel_fill_circle(btn_preview_x + 80, y + 15, 6, 0xFFCC00);
    /* Maximize */
    panel_fill_circle(btn_preview_x + 95, y + 15, 6, 0x34C759);
    /* Close (rightmost) */
    panel_fill_circle(btn_preview_x + 110, y + 15, 6, 0xFF3B30);
    
    panel_draw_text(btn_preview_x + 30, y + 35, "Right (NeolyxOS)", 0x888888);
    y += 55;
    
    /* Show window buttons */
    panel_draw_label(x, y, "Show window buttons");
    panel_draw_toggle(x + 200, y, settings.show_window_buttons);
    y += 40;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Hover Effects */
    panel_draw_subheader(x, y, "Hover Effects");
    y += 35;
    
    panel_draw_label(x, y, "Highlight on hover");
    panel_draw_toggle(x + 200, y, settings.hover_highlight);
    y += 35;
    
    panel_draw_label(x, y, "Scale on hover");
    panel_draw_toggle(x + 200, y, settings.hover_scale);
    if (settings.hover_scale) {
        panel_draw_slider(x + 260, y, 100, (settings.hover_scale_amount - 1.0f) / 0.2f, 0, 1);
    }
    y += 40;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Dock Settings */
    panel_draw_subheader(x, y, "Dock");
    y += 35;
    
    panel_draw_label(x, y, "Icon size");
    panel_draw_slider(x + 150, y, 150, (settings.dock_icon_size - 32) / 48.0f, 0, 1);
    char size_str[16];
    snprintf(size_str, sizeof(size_str), "%dpx", settings.dock_icon_size);
    panel_draw_text(x + 320, y, size_str, 0x888888);
    y += 35;
    
    panel_draw_label(x, y, "Magnification");
    panel_draw_toggle(x + 150, y, settings.dock_magnification);
    if (settings.dock_magnification) {
        panel_draw_slider(x + 210, y, 100, (settings.dock_magnification_amount - 1.0f) / 1.0f, 0, 1);
    }
    y += 35;
    
    panel_draw_label(x, y, "Auto-hide dock");
    panel_draw_toggle(x + 150, y, settings.dock_auto_hide);
    y += 40;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Search Bar (Anveshan) */
    panel_draw_subheader(x, y, "Search Bar");
    y += 35;
    
    panel_draw_label(x, y, "Width");
    panel_draw_slider(x + 150, y, 150, (settings.search_width - 280) / 320.0f, 0, 1);
    char width_str[16];
    snprintf(width_str, sizeof(width_str), "%dpx", settings.search_width);
    panel_draw_text(x + 320, y, width_str, 0x888888);
    y += 35;
    
    panel_draw_label(x, y, "Transparency");
    panel_draw_slider(x + 150, y, 150, settings.search_transparency / 100.0f, 0, 1);
    char trans_str[16];
    snprintf(trans_str, sizeof(trans_str), "%d%%", settings.search_transparency);
    panel_draw_text(x + 320, y, trans_str, 0x888888);
    y += 35;
    
    panel_draw_label(x, y, "Max suggestions");
    panel_draw_dropdown(x + 150, y, 80, 
        (settings.search_max_suggestions == 3) ? "3" :
        (settings.search_max_suggestions == 5) ? "5" :
        (settings.search_max_suggestions == 7) ? "7" : "10", NULL, 0);
    y += 40;
    
    panel_draw_label(x, y, "Search apps");
    panel_draw_toggle(x + 150, y, settings.search_apps);
    y += 30;
    
    panel_draw_label(x, y, "Search settings");
    panel_draw_toggle(x + 150, y, settings.search_settings);
    y += 30;
    
    panel_draw_label(x, y, "Search files");
    panel_draw_toggle(x + 150, y, settings.search_files);
    y += 30;
    
    panel_draw_label(x, y, "Voice search");
    panel_draw_toggle(x + 150, y, settings.search_voice);
    y += 40;
    
    /* Separator */
    panel_draw_line(x, y, x + w, y, 0x3A3A3A);
    y += 20;
    
    /* Accessibility */
    panel_draw_subheader(x, y, "Accessibility");
    y += 35;
    
    panel_draw_label(x, y, "Reduce motion");
    panel_draw_toggle(x + 200, y, settings.reduce_motion);
    y += 35;
    
    panel_draw_label(x, y, "Reduce transparency");
    panel_draw_toggle(x + 200, y, settings.reduce_transparency);
}

void appearance_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (event->type == EVT_CLICK) {
        /* Theme selection */
        int theme_y = panel->y + 65;
        int card_x = panel->x + 170;
        
        for (int i = 0; i < 3; i++) {
            if (event->y >= theme_y && event->y <= theme_y + 50 &&
                event->x >= card_x && event->x <= card_x + 80) {
                settings.theme = (theme_mode_t)i;
                nxappearance_apply(&settings);
                panel->needs_redraw = true;
                break;
            }
            card_x += 100;
        }
        
        /* Accent color selection */
        int color_y = panel->y + 295;
        int color_x = panel->x + 162;
        
        for (int i = 0; i < ACCENT_COLOR_COUNT; i++) {
            if (event->y >= color_y && event->y <= color_y + 24 &&
                event->x >= color_x && event->x <= color_x + 24) {
                settings.accent_color_index = i;
                nxappearance_apply(&settings);
                panel->needs_redraw = true;
                break;
            }
            color_x += 35;
        }
    }
}

void appearance_panel_cleanup(settings_panel_t *panel) {
    (void)panel;
    nxappearance_save(&settings);
}
