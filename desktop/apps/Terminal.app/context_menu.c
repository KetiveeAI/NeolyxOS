/*
 * NeolyxOS Terminal.app - Context Menu
 * 
 * Right-click context menu implementation using ReoxUI.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* ============ Menu Types ============ */

typedef void (*menu_callback_t)(void *context);

typedef struct {
    const char *label;
    const char *shortcut;      /* e.g. "Ctrl+C" */
    menu_callback_t callback;
    void *context;
    uint8_t enabled;
    uint8_t separator_after;
} menu_item_t;

typedef struct {
    const char *title;
    menu_item_t *items;
    int item_count;
    int x, y;
    int width, height;
    int selected_index;
    int visible;
} context_menu_t;

/* ============ Menu Item Definitions ============ */

static void menu_copy(void *ctx);
static void menu_paste(void *ctx);
static void menu_select_all(void *ctx);
static void menu_clear(void *ctx);
static void menu_new_tab(void *ctx);
static void menu_close_tab(void *ctx);

static menu_item_t terminal_menu_items[] = {
    { "Copy",       "Ctrl+C", menu_copy,      NULL, 1, 0 },
    { "Paste",      "Ctrl+V", menu_paste,     NULL, 1, 0 },
    { "Select All", "Ctrl+A", menu_select_all, NULL, 1, 1 },
    { "Clear",      "Ctrl+K", menu_clear,     NULL, 1, 1 },
    { "New Tab",    "Ctrl+T", menu_new_tab,   NULL, 1, 0 },
    { "Close Tab",  "Ctrl+W", menu_close_tab, NULL, 1, 0 },
};

#define MENU_ITEM_COUNT (sizeof(terminal_menu_items) / sizeof(terminal_menu_items[0]))

/* ============ Context Menu State ============ */

static context_menu_t g_context_menu = {
    .title = NULL,
    .items = terminal_menu_items,
    .item_count = MENU_ITEM_COUNT,
    .x = 0,
    .y = 0,
    .width = 180,
    .height = 0,
    .selected_index = -1,
    .visible = 0
};

/* ============ Menu Functions ============ */

void context_menu_show(int x, int y, void *app_context) {
    g_context_menu.x = x;
    g_context_menu.y = y;
    g_context_menu.visible = 1;
    g_context_menu.selected_index = -1;
    
    /* Update context for all items */
    for (int i = 0; i < g_context_menu.item_count; i++) {
        g_context_menu.items[i].context = app_context;
    }
    
    /* Calculate height */
    int item_height = 28;
    int separator_height = 8;
    g_context_menu.height = 4;  /* Padding */
    
    for (int i = 0; i < g_context_menu.item_count; i++) {
        g_context_menu.height += item_height;
        if (g_context_menu.items[i].separator_after) {
            g_context_menu.height += separator_height;
        }
    }
    g_context_menu.height += 4;  /* Padding */
}

void context_menu_hide(void) {
    g_context_menu.visible = 0;
    g_context_menu.selected_index = -1;
}

int context_menu_is_visible(void) {
    return g_context_menu.visible;
}

/* Handle mouse move over menu */
void context_menu_mouse_move(int x, int y) {
    if (!g_context_menu.visible) return;
    
    /* Check if inside menu bounds */
    if (x < g_context_menu.x || x > g_context_menu.x + g_context_menu.width ||
        y < g_context_menu.y || y > g_context_menu.y + g_context_menu.height) {
        g_context_menu.selected_index = -1;
        return;
    }
    
    /* Find which item mouse is over */
    int item_height = 28;
    int separator_height = 8;
    int cy = g_context_menu.y + 4;
    
    for (int i = 0; i < g_context_menu.item_count; i++) {
        if (y >= cy && y < cy + item_height) {
            if (g_context_menu.items[i].enabled) {
                g_context_menu.selected_index = i;
            }
            return;
        }
        cy += item_height;
        if (g_context_menu.items[i].separator_after) {
            cy += separator_height;
        }
    }
    
    g_context_menu.selected_index = -1;
}

/* Handle click on menu */
void context_menu_click(int x, int y) {
    context_menu_mouse_move(x, y);
    
    if (g_context_menu.selected_index >= 0) {
        menu_item_t *item = &g_context_menu.items[g_context_menu.selected_index];
        if (item->callback && item->enabled) {
            item->callback(item->context);
        }
    }
    
    context_menu_hide();
}

/* Render the menu */
void context_menu_render(void) {
    if (!g_context_menu.visible) return;
    
    /* TODO: Render using ReoxUI */
    /* 1. Draw shadow (offset rectangle) */
    /* 2. Draw menu background (rounded rect) */
    /* 3. Draw each item with hover highlight */
    /* 4. Draw separators */
    /* 5. Draw shortcut text right-aligned */
    
    int x = g_context_menu.x;
    int y = g_context_menu.y;
    int w = g_context_menu.width;
    int h = g_context_menu.height;
    
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    
    /* Style constants */
    uint32_t bg_color = 0x2D2D2D;
    uint32_t hover_color = 0x0A82FF;
    uint32_t text_color = 0xE0E0E0;
    uint32_t shortcut_color = 0x888888;
    uint32_t disabled_color = 0x555555;
    uint32_t separator_color = 0x444444;
    
    (void)bg_color;
    (void)hover_color;
    (void)text_color;
    (void)shortcut_color;
    (void)disabled_color;
    (void)separator_color;
    
    /* reox_draw_rounded_rect(x, y, w, h, 8, bg_color); */
    
    int item_height = 28;
    int separator_height = 8;
    int cy = y + 4;
    
    for (int i = 0; i < g_context_menu.item_count; i++) {
        menu_item_t *item = &g_context_menu.items[i];
        
        /* Draw hover highlight */
        if (i == g_context_menu.selected_index) {
            /* reox_draw_rect(x + 4, cy, w - 8, item_height, hover_color); */
        }
        
        /* Draw label */
        uint32_t label_color = item->enabled ? text_color : disabled_color;
        /* reox_draw_text(item->label, x + 12, cy + 6, label_color); */
        (void)label_color;
        
        /* Draw shortcut */
        if (item->shortcut) {
            /* reox_draw_text(item->shortcut, x + w - 80, cy + 6, shortcut_color); */
        }
        
        cy += item_height;
        
        /* Draw separator */
        if (item->separator_after) {
            /* reox_draw_line(x + 8, cy + 4, x + w - 8, cy + 4, separator_color); */
            cy += separator_height;
        }
    }
}

/* ============ Menu Action Callbacks ============ */

static void menu_copy(void *ctx) {
    /* Forward to terminal_copy() */
    (void)ctx;
}

static void menu_paste(void *ctx) {
    /* Forward to terminal_paste() */
    (void)ctx;
}

static void menu_select_all(void *ctx) {
    /* Select all text in terminal */
    (void)ctx;
}

static void menu_clear(void *ctx) {
    /* Clear terminal screen */
    (void)ctx;
}

static void menu_new_tab(void *ctx) {
    /* Create new terminal tab */
    (void)ctx;
}

static void menu_close_tab(void *ctx) {
    /* Close current tab */
    (void)ctx;
}
