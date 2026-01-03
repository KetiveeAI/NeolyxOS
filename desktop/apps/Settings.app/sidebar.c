/*
 * NeolyxOS Settings.app - Sidebar Navigation
 * 
 * Panel navigation sidebar similar to Path.app structure.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: LicenseRef-KetiveeAI-Proprietary
 */

#include <stdint.h>
#include <stddef.h>
#include "settings.h"

/* ============ Colors ============ */

#define COLOR_SIDEBAR_BG    0x1C1C1E
#define COLOR_ITEM_HOVER    0x2C2C2E
#define COLOR_ITEM_ACTIVE   0x3A3A3C
#define COLOR_TEXT          0xFFFFFF
#define COLOR_TEXT_DIM      0xAEAEB2
#define COLOR_SEPARATOR     0x3A3A3C

/* ============ Sidebar Items ============ */

typedef struct {
    const char *name;
    const char *icon;
    panel_id_t panel;
} sidebar_item_t;

static const sidebar_item_t sidebar_items[] = {
    { "System",      "system",      PANEL_SYSTEM },
    { "Network",     "network",     PANEL_NETWORK },
    { "Bluetooth",   "bluetooth",   PANEL_BLUETOOTH },
    { "Display",     "display",     PANEL_DISPLAY },
    { "Sound",       "sound",       PANEL_SOUND },
    { "Storage",     "storage",     PANEL_STORAGE },
    { NULL,          NULL,          0 },  /* Separator */
    { "Accounts",    "accounts",    PANEL_ACCOUNTS },
    { "Security",    "security",    PANEL_SECURITY },
    { "Privacy",     "privacy",     PANEL_PRIVACY },
    { NULL,          NULL,          0 },  /* Separator */
    { "Appearance",  "appearance",  PANEL_APPEARANCE },
    { "Icons",       "icons",       PANEL_ICONS },
    { "Windows",     "windows",     PANEL_WINDOWS },
    { "Power",       "power",       PANEL_POWER },
    { NULL,          NULL,          0 },  /* Separator */
    { "Processes",   "processes",   PANEL_PROCESSES },
    { "Startup",     "startup",     PANEL_STARTUP },
    { "Scheduler",   "scheduler",   PANEL_SCHEDULER },
    { "Extensions",  "extensions",  PANEL_EXTENSIONS },
    { NULL,          NULL,          0 },  /* Separator */
    { "Updates",     "updates",     PANEL_UPDATES },
    { "Bootloader",  "bootloader",  PANEL_BOOTLOADER },
    { "Developer",   "developer",   PANEL_DEVELOPER },
    { NULL,          NULL,          0 },  /* Separator */
    { "About",       "about",       PANEL_ABOUT },
};

#define SIDEBAR_ITEM_COUNT (sizeof(sidebar_items) / sizeof(sidebar_items[0]))

/* ============ Sidebar State ============ */

typedef struct {
    int width;
    int hover_index;
    int active_index;
    int scroll_y;
} settings_sidebar_t;

/* ============ Functions ============ */

void settings_sidebar_init(settings_sidebar_t *sidebar, int width) {
    sidebar->width = width;
    sidebar->hover_index = -1;
    sidebar->active_index = 0;
    sidebar->scroll_y = 0;
}

void settings_sidebar_draw(void *ctx, settings_sidebar_t *sidebar, int x, int y, int height) {
    int row_height = 32;
    int sep_height = 12;
    int padding = 12;
    
    /* Background */
    /* nx_fill_rect would be called here */
    
    int cy = y + padding - sidebar->scroll_y;
    
    for (int i = 0; i < (int)SIDEBAR_ITEM_COUNT; i++) {
        const sidebar_item_t *item = &sidebar_items[i];
        
        if (!item->name) {
            /* Separator */
            cy += sep_height / 2;
            /* Draw separator line */
            cy += sep_height / 2;
            continue;
        }
        
        /* Skip if out of view */
        if (cy + row_height < y || cy > y + height) {
            cy += row_height;
            continue;
        }
        
        /* Draw item background if hovered or active */
        if (i == sidebar->active_index) {
            /* Active background */
        } else if (i == sidebar->hover_index) {
            /* Hover background */
        }
        
        /* Draw icon (16x16) */
        /* Draw label */
        
        cy += row_height;
    }
}

int settings_sidebar_hit_test(settings_sidebar_t *sidebar, int mx, int my, int sx, int sy) {
    if (mx < sx || mx > sx + sidebar->width) return -1;
    
    int row_height = 32;
    int sep_height = 12;
    int padding = 12;
    
    int cy = sy + padding - sidebar->scroll_y;
    
    for (int i = 0; i < (int)SIDEBAR_ITEM_COUNT; i++) {
        const sidebar_item_t *item = &sidebar_items[i];
        
        if (!item->name) {
            cy += sep_height;
            continue;
        }
        
        if (my >= cy && my < cy + row_height) {
            return i;
        }
        
        cy += row_height;
    }
    
    return -1;
}

void settings_sidebar_on_hover(settings_sidebar_t *sidebar, int index) {
    sidebar->hover_index = index;
}

panel_id_t settings_sidebar_on_click(settings_sidebar_t *sidebar, int index) {
    if (index < 0 || index >= (int)SIDEBAR_ITEM_COUNT) {
        return PANEL_SYSTEM;
    }
    
    if (!sidebar_items[index].name) {
        return sidebar_items[sidebar->active_index].panel;
    }
    
    sidebar->active_index = index;
    return sidebar_items[index].panel;
}
