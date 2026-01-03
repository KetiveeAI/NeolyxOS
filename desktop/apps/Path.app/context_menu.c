/*
 * NeolyxOS Files.app - Context Menu
 * 
 * Right-click popup menu for file operations.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"
#include "../../include/nxicon.h"

/* ============ Colors ============ */

#define COLOR_MENU_BG       0x1E1E2E
#define COLOR_MENU_BORDER   0x45475A
#define COLOR_ITEM_HOVER    0x313244
#define COLOR_TEXT          0xCDD6F4
#define COLOR_TEXT_DIM      0x6C7086
#define COLOR_SEPARATOR     0x313244

/* ============ Menu Items ============ */

typedef enum {
    MENU_OPEN,
    MENU_OPEN_WITH,
    MENU_OPEN_TERMINAL,
    MENU_SEP1,
    MENU_GET_INFO,
    MENU_RENAME,
    MENU_SEP2,
    MENU_CUT,
    MENU_COPY,
    MENU_PASTE,
    MENU_SEP3,
    MENU_MOVE_TRASH,
    MENU_SEP4,
    MENU_NEW_FOLDER,
    MENU_NEW_FILE,
    MENU_SEP5,
    MENU_CHANGE_ICON,
    MENU_RESET_ICON,
    MENU_ITEM_COUNT
} menu_item_t;

static const char *menu_labels[] = {
    "Open",
    "Open With...",
    "Open in Terminal",
    NULL,  /* separator */
    "Get Info",
    "Rename",
    NULL,
    "Cut",
    "Copy",
    "Paste",
    NULL,
    "Move to Trash",
    NULL,
    "New Folder",
    "New File",
    NULL,
    "Change Icon...",
    "Reset Icon"
};

static const char *menu_shortcuts[] = {
    "Enter",
    NULL,
    NULL,
    NULL,
    "Cmd+I",
    "Enter",
    NULL,
    "Cmd+X",
    "Cmd+C",
    "Cmd+V",
    NULL,
    "Cmd+Del",
    NULL,
    "Cmd+Shift+N",
    NULL,
    NULL,
    NULL,
    NULL
};


/* ============ Context Menu State ============ */

typedef struct {
    int visible;
    int x;
    int y;
    int width;
    int height;
    int hover_index;
    
    /* Enabled states */
    int has_selection;
    int has_clipboard;
    int is_directory;
} context_menu_t;

/* ============ Drawing ============ */

void context_menu_draw(void *ctx, context_menu_t *menu) {
    if (!menu->visible) return;
    
    int row_height = 28;
    int sep_height = 9;
    int padding = 8;
    
    /* Calculate height */
    int h = padding * 2;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        h += menu_labels[i] ? row_height : sep_height;
    }
    menu->height = h;
    menu->width = 200;
    
    /* Shadow */
    nx_fill_rect(ctx, menu->x + 4, menu->y + 4, menu->width, menu->height, 0x11111B);
    
    /* Background */
    nx_fill_rect(ctx, menu->x, menu->y, menu->width, menu->height, COLOR_MENU_BG);
    
    /* Border */
    nx_draw_rect(ctx, menu->x, menu->y, menu->width, menu->height, COLOR_MENU_BORDER);
    
    int cy = menu->y + padding;
    
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (!menu_labels[i]) {
            /* Separator */
            cy += sep_height / 2;
            nx_fill_rect(ctx, menu->x + padding, cy, menu->width - padding * 2, 1, COLOR_SEPARATOR);
            cy += sep_height / 2;
            continue;
        }
        
        /* Check if enabled */
        int enabled = 1;
        if ((i == MENU_OPEN || i == MENU_GET_INFO || i == MENU_CUT || 
             i == MENU_COPY || i == MENU_MOVE_TRASH || i == MENU_RENAME ||
             i == MENU_OPEN_TERMINAL) && !menu->has_selection) {
            enabled = 0;
        }
        if (i == MENU_PASTE && !menu->has_clipboard) {
            enabled = 0;
        }
        /* Icon menu items only for directories */
        if ((i == MENU_CHANGE_ICON || i == MENU_RESET_ICON) && 
            (!menu->has_selection || !menu->is_directory)) {
            enabled = 0;
        }

        
        /* Hover highlight */
        if (menu->hover_index == i && enabled) {
            nx_fill_rect(ctx, menu->x + 4, cy, menu->width - 8, row_height, COLOR_ITEM_HOVER);
        }
        
        /* Label */
        uint32_t text_color = enabled ? COLOR_TEXT : COLOR_TEXT_DIM;
        nx_draw_text(ctx, menu_labels[i], menu->x + padding + 8, cy + 6, text_color);
        
        /* Shortcut */
        if (menu_shortcuts[i] && enabled) {
            int shortcut_x = menu->x + menu->width - 80;
            nx_draw_text(ctx, menu_shortcuts[i], shortcut_x, cy + 6, COLOR_TEXT_DIM);
        }
        
        cy += row_height;
    }
}

/* ============ Interaction ============ */

int context_menu_hit_test(context_menu_t *menu, int x, int y) {
    if (!menu->visible) return -1;
    if (x < menu->x || x > menu->x + menu->width) return -1;
    if (y < menu->y || y > menu->y + menu->height) return -1;
    
    int row_height = 28;
    int sep_height = 9;
    int padding = 8;
    
    int cy = menu->y + padding;
    
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (!menu_labels[i]) {
            cy += sep_height;
            continue;
        }
        
        if (y >= cy && y < cy + row_height) {
            return i;
        }
        
        cy += row_height;
    }
    
    return -1;
}

void context_menu_on_hover(context_menu_t *menu, int index) {
    menu->hover_index = index;
}

int context_menu_on_click(context_menu_t *menu, int index) {
    if (index < 0 || !menu_labels[index]) return -1;
    
    /* Check if enabled */
    if ((index == MENU_OPEN || index == MENU_GET_INFO || index == MENU_CUT ||
         index == MENU_COPY || index == MENU_MOVE_TRASH || index == MENU_RENAME ||
         index == MENU_OPEN_TERMINAL) 
        && !menu->has_selection) {
        return -1;
    }

    if (index == MENU_PASTE && !menu->has_clipboard) {
        return -1;
    }
    
    /* Icon menu items only for directories */
    if ((index == MENU_CHANGE_ICON || index == MENU_RESET_ICON) &&
        (!menu->has_selection || !menu->is_directory)) {
        return -1;
    }
    
    menu->visible = 0;
    return index;
}

/* ============ Control ============ */

void context_menu_show(context_menu_t *menu, int x, int y, 
                       int has_selection, int has_clipboard, int is_directory) {
    menu->visible = 1;
    menu->x = x;
    menu->y = y;
    menu->hover_index = -1;
    menu->has_selection = has_selection;
    menu->has_clipboard = has_clipboard;
    menu->is_directory = is_directory;
}

void context_menu_hide(context_menu_t *menu) {
    menu->visible = 0;
}

void context_menu_init(context_menu_t *menu) {
    menu->visible = 0;
    menu->x = 0;
    menu->y = 0;
    menu->width = 200;
    menu->height = 0;
    menu->hover_index = -1;
    menu->has_selection = 0;
    menu->has_clipboard = 0;
    menu->is_directory = 0;
}
