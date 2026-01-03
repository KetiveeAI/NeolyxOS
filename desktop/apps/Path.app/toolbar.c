/*
 * NeolyxOS Files.app - Toolbar
 * 
 * Navigation buttons, path bar, search, and view controls.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Colors ============ */

#define COLOR_TOOLBAR_BG    0x1E1E2E
#define COLOR_BUTTON        0x313244
#define COLOR_BUTTON_HOVER  0x45475A
#define COLOR_BUTTON_ACTIVE 0x585B70
#define COLOR_BUTTON_DIS    0x26283B
#define COLOR_ICON          0xCDD6F4
#define COLOR_ICON_DIS      0x45475A
#define COLOR_PATH_BG       0x181825
#define COLOR_SEPARATOR     0x313244
#define COLOR_TEXT          0xCDD6F4

/* ============ Button IDs ============ */

typedef enum {
    BTN_BACK,
    BTN_FORWARD,
    BTN_UP,
    BTN_VIEW_ICONS,
    BTN_VIEW_LIST,
    BTN_VIEW_COLUMNS,
    BTN_VIEW_GALLERY,
    BTN_SEARCH,
    BTN_COUNT
} toolbar_button_t;

/* ============ Toolbar State ============ */

typedef struct {
    int can_back;
    int can_forward;
    int can_up;
    int view_mode;     /* 0=icons, 1=list, 2=columns, 3=gallery */
    int hover_button;  /* -1 = none */
    int height;
    
    char path[1024];
    char search_query[256];
    int search_active;
} toolbar_t;

/* ============ Drawing ============ */

void toolbar_draw(void *ctx, toolbar_t *toolbar, int x, int y, int width) {
    int h = toolbar->height;
    int btn_size = 28;
    int btn_padding = 4;
    int gap = 8;
    
    /* Background */
    nx_fill_rect(ctx, x, y, width, h, COLOR_TOOLBAR_BG);
    
    /* Bottom border */
    nx_fill_rect(ctx, x, y + h - 1, width, 1, COLOR_SEPARATOR);
    
    int cx = x + 8;
    int cy = y + (h - btn_size) / 2;
    
    /* === Navigation Buttons === */
    
    /* Back */
    uint32_t back_color = toolbar->can_back 
        ? (toolbar->hover_button == BTN_BACK ? COLOR_BUTTON_HOVER : COLOR_BUTTON)
        : COLOR_BUTTON_DIS;
    nx_fill_rect(ctx, cx, cy, btn_size, btn_size, back_color);
    nx_draw_text(ctx, "<", cx + 10, cy + 6, toolbar->can_back ? COLOR_ICON : COLOR_ICON_DIS);
    cx += btn_size + btn_padding;
    
    /* Forward */
    uint32_t fwd_color = toolbar->can_forward
        ? (toolbar->hover_button == BTN_FORWARD ? COLOR_BUTTON_HOVER : COLOR_BUTTON)
        : COLOR_BUTTON_DIS;
    nx_fill_rect(ctx, cx, cy, btn_size, btn_size, fwd_color);
    nx_draw_text(ctx, ">", cx + 10, cy + 6, toolbar->can_forward ? COLOR_ICON : COLOR_ICON_DIS);
    cx += btn_size + gap;
    
    /* Up */
    uint32_t up_color = toolbar->can_up
        ? (toolbar->hover_button == BTN_UP ? COLOR_BUTTON_HOVER : COLOR_BUTTON)
        : COLOR_BUTTON_DIS;
    nx_fill_rect(ctx, cx, cy, btn_size, btn_size, up_color);
    nx_draw_text(ctx, "^", cx + 10, cy + 6, toolbar->can_up ? COLOR_ICON : COLOR_ICON_DIS);
    cx += btn_size + gap * 2;
    
    /* === Path Bar === */
    int path_width = width - cx - 200;  /* Leave room for view buttons and search */
    if (path_width > 400) path_width = 400;
    if (path_width < 100) path_width = 100;
    
    nx_fill_rect(ctx, cx, cy, path_width, btn_size, COLOR_PATH_BG);
    nx_draw_text(ctx, toolbar->path, cx + 8, cy + 6, COLOR_TEXT);
    cx += path_width + gap * 2;
    
    /* === View Buttons === */
    int view_btn_w = 26;
    const char *view_labels[] = { "G", "L", "C", "I" };  /* Grid, List, Columns, Gallery icons */
    
    for (int i = 0; i < 4; i++) {
        int is_active = (toolbar->view_mode == i);
        int is_hover = (toolbar->hover_button == BTN_VIEW_ICONS + i);
        
        uint32_t btn_col = is_active ? COLOR_BUTTON_ACTIVE 
                         : is_hover ? COLOR_BUTTON_HOVER 
                         : COLOR_BUTTON;
        
        nx_fill_rect(ctx, cx, cy, view_btn_w, btn_size, btn_col);
        nx_draw_text(ctx, view_labels[i], cx + 8, cy + 6, COLOR_ICON);
        cx += view_btn_w + 2;
    }
    
    cx += gap;
    
    /* === Search === */
    int search_width = width - cx - 12;
    if (search_width > 180) search_width = 180;
    if (search_width > 0) {
        nx_fill_rect(ctx, cx, cy, search_width, btn_size, COLOR_PATH_BG);
        if (toolbar->search_query[0]) {
            nx_draw_text(ctx, toolbar->search_query, cx + 8, cy + 6, COLOR_TEXT);
        } else {
            nx_draw_text(ctx, "Search", cx + 8, cy + 6, COLOR_ICON_DIS);
        }
    }
}

/* ============ Interaction ============ */

int toolbar_hit_test(toolbar_t *toolbar, int x, int y, int toolbar_x, int toolbar_y, int width) {
    int h = toolbar->height;
    int btn_size = 28;
    int btn_padding = 4;
    int gap = 8;
    
    if (y < toolbar_y || y > toolbar_y + h) return -1;
    
    int cy = toolbar_y + (h - btn_size) / 2;
    int cx = toolbar_x + 8;
    
    /* Back */
    if (x >= cx && x < cx + btn_size && y >= cy && y < cy + btn_size) return BTN_BACK;
    cx += btn_size + btn_padding;
    
    /* Forward */
    if (x >= cx && x < cx + btn_size && y >= cy && y < cy + btn_size) return BTN_FORWARD;
    cx += btn_size + gap;
    
    /* Up */
    if (x >= cx && x < cx + btn_size && y >= cy && y < cy + btn_size) return BTN_UP;
    cx += btn_size + gap * 2;
    
    /* Skip path bar */
    int path_width = width - cx - 200;
    if (path_width > 400) path_width = 400;
    if (path_width < 100) path_width = 100;
    cx += path_width + gap * 2;
    
    /* View buttons */
    int view_btn_w = 26;
    for (int i = 0; i < 4; i++) {
        if (x >= cx && x < cx + view_btn_w && y >= cy && y < cy + btn_size) {
            return BTN_VIEW_ICONS + i;
        }
        cx += view_btn_w + 2;
    }
    
    /* Search */
    if (x >= cx) return BTN_SEARCH;
    
    return -1;
}

void toolbar_on_hover(toolbar_t *toolbar, int button) {
    toolbar->hover_button = button;
}

/* ============ Initialization ============ */

void toolbar_init(toolbar_t *toolbar, int height) {
    toolbar->height = height;
    toolbar->can_back = 0;
    toolbar->can_forward = 0;
    toolbar->can_up = 1;
    toolbar->view_mode = 0;  /* Icons */
    toolbar->hover_button = -1;
    toolbar->search_active = 0;
    toolbar->path[0] = '/';
    toolbar->path[1] = '\0';
    toolbar->search_query[0] = '\0';
}

void toolbar_set_path(toolbar_t *toolbar, const char *path) {
    int i = 0;
    while (path[i] && i < 1023) {
        toolbar->path[i] = path[i];
        i++;
    }
    toolbar->path[i] = '\0';
    
    /* Can go up if not at root */
    toolbar->can_up = (path[0] == '/' && path[1] != '\0');
}

void toolbar_set_navigation(toolbar_t *toolbar, int can_back, int can_forward) {
    toolbar->can_back = can_back;
    toolbar->can_forward = can_forward;
}

void toolbar_set_view(toolbar_t *toolbar, int mode) {
    toolbar->view_mode = mode;
}
