/*
 * Settings - Icons Panel
 * Folder icons, app icons, and custom icon management
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include "../../../include/nxicon.h"

/* ============ Colors ============ */

#define COLOR_BG            0x1E1E2E
#define COLOR_CARD          0x313244
#define COLOR_TEXT          0xCDD6F4
#define COLOR_TEXT_DIM      0x6C7086
#define COLOR_PRIMARY       0x89B4FA
#define COLOR_BORDER        0x45475A

/* ============ Icon Theme Options ============ */

static const char *icon_themes[] = {
    "Default",
    "Warm Pastel",
    "Vibrant",
    "Monochrome",
    "Classic Blue"
};
#define ICON_THEME_COUNT 5

/* ============ Icon Settings ============ */

typedef struct {
    int theme_index;
    int folder_icon_size;       /* 32, 48, 64, 80 */
    int sidebar_icon_size;      /* 16, 20, 24 */
    int show_folder_badges;     /* Show count badges */
    int use_custom_icons;       /* Allow user custom icons */
    int animate_icons;          /* Hover animation */
} icon_settings_t;

static icon_settings_t settings = {
    .theme_index = 1,        /* Warm Pastel */
    .folder_icon_size = 48,
    .sidebar_icon_size = 20,
    .show_folder_badges = 1,
    .use_custom_icons = 1,
    .animate_icons = 1
};

/* ============ Panel State ============ */

static struct {
    int scroll_y;
    int selected_preview;
    int hover_item;
} panel_state = {0};

/* ============ Drawing Helpers ============ */

static void draw_section_header(void *ctx, int x, int y, const char *title) {
    /* Card header with title */
    nx_draw_text(ctx, title, x, y, COLOR_TEXT);
}

static void draw_icon_preview(void *ctx, int x, int y, int size) {
    /* Draw folder icon preview */
    const char *icon_path = NXICON_DOCUMENTS;
    if (nx_draw_nxi(ctx, icon_path, x, y, size, size) != 0) {
        nx_fill_rect(ctx, x, y, size, size, NXICON_COLOR_FOLDER);
    }
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

void icons_panel_init(settings_panel_t *panel) {
    panel_state.scroll_y = 0;
    panel_state.selected_preview = 0;
    panel_state.hover_item = -1;
}

void icons_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20 - panel_state.scroll_y;
    int x = panel->x + 24;
    int w = panel->width - 48;
    
    /* Header */
    nx_draw_text(ctx, "Icons", x, y, COLOR_TEXT);
    y += 45;
    
    /* ============ Icon Theme ============ */
    draw_section_header(ctx, x, y, "Icon Theme");
    y += 30;
    
    /* Theme dropdown */
    nx_draw_text(ctx, "Folder Style", x, y + 6, COLOR_TEXT);
    draw_dropdown(ctx, x + 150, y, 180, icon_themes[settings.theme_index]);
    y += 40;
    
    /* Preview grid */
    nx_draw_text(ctx, "Preview", x, y, COLOR_TEXT_DIM);
    y += 25;
    
    int preview_x = x;
    const char *preview_icons[] = {
        NXICON_DOCUMENTS, NXICON_DOWNLOADS, NXICON_MUSIC,
        NXICON_VIDEO, NXICON_CAMERA, NXICON_DESKTOP
    };
    for (int i = 0; i < 6; i++) {
        if (nx_draw_nxi(ctx, preview_icons[i], preview_x, y, 48, 48) != 0) {
            nx_fill_rect(ctx, preview_x, y, 48, 48, NXICON_COLOR_FOLDER);
        }
        preview_x += 60;
    }
    y += 70;
    
    /* ============ Icon Sizes ============ */
    draw_section_header(ctx, x, y, "Icon Sizes");
    y += 30;
    
    /* Folder icon size */
    nx_draw_text(ctx, "Folder Icons", x, y + 6, COLOR_TEXT);
    float size_val = (settings.folder_icon_size - 32) / 48.0f;
    draw_slider(ctx, x + 150, y, 150, size_val);
    char size_str[16];
    snprintf(size_str, sizeof(size_str), "%dpx", settings.folder_icon_size);
    nx_draw_text(ctx, size_str, x + 320, y + 6, COLOR_TEXT_DIM);
    y += 40;
    
    /* Sidebar icon size */
    nx_draw_text(ctx, "Sidebar Icons", x, y + 6, COLOR_TEXT);
    float sidebar_val = (settings.sidebar_icon_size - 16) / 8.0f;
    draw_slider(ctx, x + 150, y, 150, sidebar_val);
    snprintf(size_str, sizeof(size_str), "%dpx", settings.sidebar_icon_size);
    nx_draw_text(ctx, size_str, x + 320, y + 6, COLOR_TEXT_DIM);
    y += 50;
    
    /* ============ Behavior ============ */
    draw_section_header(ctx, x, y, "Behavior");
    y += 30;
    
    /* Show folder badges */
    nx_draw_text(ctx, "Show Item Count", x, y + 6, COLOR_TEXT);
    draw_toggle(ctx, x + 250, y, settings.show_folder_badges);
    y += 40;
    
    /* Allow custom icons */
    nx_draw_text(ctx, "Allow Custom Icons", x, y + 6, COLOR_TEXT);
    draw_toggle(ctx, x + 250, y, settings.use_custom_icons);
    y += 40;
    
    /* Animate icons */
    nx_draw_text(ctx, "Hover Animation", x, y + 6, COLOR_TEXT);
    draw_toggle(ctx, x + 250, y, settings.animate_icons);
    y += 50;
    
    /* ============ Custom Icons ============ */
    draw_section_header(ctx, x, y, "Custom Folder Icons");
    y += 25;
    nx_draw_text(ctx, "Right-click a folder in Path and select 'Change Icon...'", 
                 x, y, COLOR_TEXT_DIM);
    y += 25;
    nx_draw_text(ctx, "to assign a custom icon.", x, y, COLOR_TEXT_DIM);
}

void icons_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (!event) return;
    
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
            
            /* Theme dropdown (y=95) */
            if (my >= y + 75 && my < y + 103 && mx >= x + 150 && mx < x + 330) {
                settings.theme_index = (settings.theme_index + 1) % ICON_THEME_COUNT;
            }
            
            /* Folder size slider (y=215) */
            if (my >= y + 195 && my < y + 227 && mx >= x + 150 && mx < x + 300) {
                float val = (float)(mx - x - 150) / 150.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings.folder_icon_size = 32 + (int)(val * 48);
            }
            
            /* Sidebar size slider (y=255) */
            if (my >= y + 235 && my < y + 267 && mx >= x + 150 && mx < x + 300) {
                float val = (float)(mx - x - 150) / 150.0f;
                if (val < 0) val = 0; if (val > 1) val = 1;
                settings.sidebar_icon_size = 16 + (int)(val * 8);
            }
            
            /* Toggle: Show item count (y=315) */
            if (my >= y + 295 && my < y + 325 && mx >= x + 250 && mx < x + 294) {
                settings.show_folder_badges = !settings.show_folder_badges;
            }
            
            /* Toggle: Allow custom (y=355) */
            if (my >= y + 335 && my < y + 365 && mx >= x + 250 && mx < x + 294) {
                settings.use_custom_icons = !settings.use_custom_icons;
            }
            
            /* Toggle: Hover animation (y=395) */
            if (my >= y + 375 && my < y + 405 && mx >= x + 250 && mx < x + 294) {
                settings.animate_icons = !settings.animate_icons;
            }
            break;
        }
        
        default:
            break;
    }
}

void icons_panel_cleanup(settings_panel_t *panel) {
    /* Nothing to clean up */
}
