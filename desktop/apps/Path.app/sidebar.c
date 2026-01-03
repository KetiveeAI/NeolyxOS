/*
 * NeolyxOS Files.app - Sidebar
 * 
 * Favorites, volumes, and quick access panel.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Colors ============ */

#define COLOR_SIDEBAR_BG    0x181825
#define COLOR_SEPARATOR     0x313244
#define COLOR_SECTION       0x6C7086
#define COLOR_ITEM          0xCDD6F4
#define COLOR_ITEM_HOVER    0x45475A
#define COLOR_ITEM_ACTIVE   0x585B70
#define COLOR_ICON_FOLDER   0x89B4FA
#define COLOR_ICON_DISK     0xF5E0DC

/* ============ Sidebar Items ============ */

typedef struct {
    char name[64];
    char path[256];
    uint32_t icon_color;
    int is_volume;
} sidebar_item_t;

/* ============ Sidebar State ============ */

typedef struct {
    sidebar_item_t favorites[16];
    int favorites_count;
    
    sidebar_item_t volumes[8];
    int volumes_count;
    
    int hover_index;       /* -1 = none */
    int active_index;      /* Currently selected */
    int width;
} sidebar_t;

/* ============ Drawing ============ */

void sidebar_draw(void *ctx, sidebar_t *sidebar, int x, int y, int height) {
    int w = sidebar->width;
    
    /* Background */
    nx_fill_rect(ctx, x, y, w, height, COLOR_SIDEBAR_BG);
    
    /* Right border */
    nx_fill_rect(ctx, x + w - 1, y, 1, height, COLOR_SEPARATOR);
    
    int row_height = 28;
    int section_height = 24;
    int padding = 12;
    int icon_size = 16;
    int cy = y + 8;
    
    /* === Favorites Section === */
    nx_draw_text(ctx, "Favorites", x + padding, cy + 4, COLOR_SECTION);
    cy += section_height;
    
    for (int i = 0; i < sidebar->favorites_count; i++) {
        sidebar_item_t *item = &sidebar->favorites[i];
        
        /* Highlight */
        if (sidebar->active_index == i) {
            nx_fill_rect(ctx, x + 4, cy, w - 8, row_height, COLOR_ITEM_ACTIVE);
        } else if (sidebar->hover_index == i) {
            nx_fill_rect(ctx, x + 4, cy, w - 8, row_height, COLOR_ITEM_HOVER);
        }
        
        /* Icon */
        nx_fill_rect(ctx, x + padding, cy + 6, icon_size, icon_size, item->icon_color);
        
        /* Label */
        nx_draw_text(ctx, item->name, x + padding + icon_size + 8, cy + 6, COLOR_ITEM);
        
        cy += row_height;
    }
    
    /* Separator */
    cy += 8;
    nx_fill_rect(ctx, x + padding, cy, w - padding * 2, 1, COLOR_SEPARATOR);
    cy += 16;
    
    /* === Volumes Section === */
    nx_draw_text(ctx, "Locations", x + padding, cy + 4, COLOR_SECTION);
    cy += section_height;
    
    int vol_offset = sidebar->favorites_count;
    
    for (int i = 0; i < sidebar->volumes_count; i++) {
        sidebar_item_t *item = &sidebar->volumes[i];
        int idx = vol_offset + i;
        
        /* Highlight */
        if (sidebar->active_index == idx) {
            nx_fill_rect(ctx, x + 4, cy, w - 8, row_height, COLOR_ITEM_ACTIVE);
        } else if (sidebar->hover_index == idx) {
            nx_fill_rect(ctx, x + 4, cy, w - 8, row_height, COLOR_ITEM_HOVER);
        }
        
        /* Disk icon */
        nx_fill_rect(ctx, x + padding, cy + 6, icon_size, icon_size, COLOR_ICON_DISK);
        
        /* Label */
        nx_draw_text(ctx, item->name, x + padding + icon_size + 8, cy + 6, COLOR_ITEM);
        
        cy += row_height;
    }
}

/* ============ Interaction ============ */

int sidebar_hit_test(sidebar_t *sidebar, int x, int y, int sidebar_x, int sidebar_y) {
    if (x < sidebar_x || x > sidebar_x + sidebar->width) return -1;
    
    int row_height = 28;
    int section_height = 24;
    int cy = sidebar_y + 8 + section_height;
    
    /* Favorites */
    for (int i = 0; i < sidebar->favorites_count; i++) {
        if (y >= cy && y < cy + row_height) {
            return i;
        }
        cy += row_height;
    }
    
    /* Skip separator */
    cy += 8 + 16 + section_height;
    
    /* Volumes */
    for (int i = 0; i < sidebar->volumes_count; i++) {
        if (y >= cy && y < cy + row_height) {
            return sidebar->favorites_count + i;
        }
        cy += row_height;
    }
    
    return -1;
}

void sidebar_on_hover(sidebar_t *sidebar, int index) {
    sidebar->hover_index = index;
}

const char* sidebar_on_click(sidebar_t *sidebar, int index) {
    if (index < 0) return NULL;
    
    sidebar->active_index = index;
    
    if (index < sidebar->favorites_count) {
        return sidebar->favorites[index].path;
    } else {
        int vol_idx = index - sidebar->favorites_count;
        if (vol_idx < sidebar->volumes_count) {
            return sidebar->volumes[vol_idx].path;
        }
    }
    
    return NULL;
}

/* ============ Initialization ============ */

void sidebar_init(sidebar_t *sidebar, int width) {
    sidebar->width = width;
    sidebar->hover_index = -1;
    sidebar->active_index = 0;
    sidebar->favorites_count = 0;
    sidebar->volumes_count = 0;
}

void sidebar_add_favorite(sidebar_t *sidebar, const char *name, const char *path) {
    if (sidebar->favorites_count >= 16) return;
    
    sidebar_item_t *item = &sidebar->favorites[sidebar->favorites_count++];
    
    int i = 0;
    while (name[i] && i < 63) { item->name[i] = name[i]; i++; }
    item->name[i] = '\0';
    
    i = 0;
    while (path[i] && i < 255) { item->path[i] = path[i]; i++; }
    item->path[i] = '\0';
    
    item->icon_color = COLOR_ICON_FOLDER;
    item->is_volume = 0;
}

void sidebar_add_volume(sidebar_t *sidebar, const char *name, const char *path) {
    if (sidebar->volumes_count >= 8) return;
    
    sidebar_item_t *item = &sidebar->volumes[sidebar->volumes_count++];
    
    int i = 0;
    while (name[i] && i < 63) { item->name[i] = name[i]; i++; }
    item->name[i] = '\0';
    
    i = 0;
    while (path[i] && i < 255) { item->path[i] = path[i]; i++; }
    item->path[i] = '\0';
    
    item->icon_color = COLOR_ICON_DISK;
    item->is_volume = 1;
}
