/*
 * NeolyxOS Files.app - Tab Bar
 * 
 * Multi-tab support like macOS Finder with tab creation, switching, and closing.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Configuration ============ */

#define TAB_MAX_COUNT       16
#define TAB_MAX_PATH        1024
#define TAB_MIN_WIDTH       100
#define TAB_MAX_WIDTH       200
#define TAB_HEIGHT          28

/* ============ Colors ============ */

#define COLOR_TAB_BG        0x181825
#define COLOR_TAB_ACTIVE    0x1E1E2E
#define COLOR_TAB_HOVER     0x313244
#define COLOR_TAB_BORDER    0x45475A
#define COLOR_TAB_TEXT      0xCDD6F4
#define COLOR_TAB_TEXT_DIM  0x6C7086
#define COLOR_TAB_CLOSE     0xF38BA8
#define COLOR_TAB_ADD       0x89B4FA

/* ============ Types ============ */

typedef struct file_entry file_entry_t;  /* From main.c */

/* Tab state */
typedef struct {
    int active;
    char path[TAB_MAX_PATH];
    char title[64];
    
    /* Tab-specific state */
    file_entry_t *entries;
    int entry_count;
    int selected_index;
    int scroll_offset;
    
    /* Navigation history per tab */
    char history[32][TAB_MAX_PATH];
    int history_count;
    int history_pos;
} files_tab_t;

/* Tab bar */
typedef struct {
    files_tab_t tabs[TAB_MAX_COUNT];
    int tab_count;
    int active_tab;
    int hover_tab;
    int hover_close;
    int height;
    int visible;
} tab_bar_t;

/* ============ Tab Management ============ */

void tab_bar_init(tab_bar_t *bar) {
    bar->tab_count = 0;
    bar->active_tab = -1;
    bar->hover_tab = -1;
    bar->hover_close = -1;
    bar->height = TAB_HEIGHT;
    bar->visible = 0;  /* Hidden with single tab */
    
    for (int i = 0; i < TAB_MAX_COUNT; i++) {
        bar->tabs[i].active = 0;
        bar->tabs[i].path[0] = '\0';
        bar->tabs[i].title[0] = '\0';
        bar->tabs[i].entries = NULL;
        bar->tabs[i].entry_count = 0;
        bar->tabs[i].selected_index = -1;
        bar->tabs[i].scroll_offset = 0;
        bar->tabs[i].history_count = 0;
        bar->tabs[i].history_pos = 0;
    }
}

int tab_bar_new_tab(tab_bar_t *bar, const char *path) {
    if (bar->tab_count >= TAB_MAX_COUNT) return -1;
    
    /* Find first free slot */
    int idx = -1;
    for (int i = 0; i < TAB_MAX_COUNT; i++) {
        if (!bar->tabs[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;
    
    files_tab_t *tab = &bar->tabs[idx];
    tab->active = 1;
    tab->selected_index = -1;
    tab->scroll_offset = 0;
    tab->history_count = 0;
    tab->history_pos = 0;
    
    /* Set path */
    int i = 0;
    while (path && path[i] && i < TAB_MAX_PATH - 1) {
        tab->path[i] = path[i];
        i++;
    }
    tab->path[i] = '\0';
    
    /* Extract title from path */
    int last_slash = 0;
    for (int j = 0; tab->path[j]; j++) {
        if (tab->path[j] == '/') last_slash = j;
    }
    i = 0;
    const char *name = &tab->path[last_slash + (last_slash > 0 ? 1 : 0)];
    while (name[i] && i < 63) {
        tab->title[i] = name[i];
        i++;
    }
    tab->title[i] = '\0';
    if (tab->title[0] == '\0') {
        tab->title[0] = '/';
        tab->title[1] = '\0';
    }
    
    bar->tab_count++;
    bar->active_tab = idx;
    bar->visible = (bar->tab_count > 1);
    
    return idx;
}

void tab_bar_close_tab(tab_bar_t *bar, int idx) {
    if (idx < 0 || idx >= TAB_MAX_COUNT) return;
    if (!bar->tabs[idx].active) return;
    if (bar->tab_count <= 1) return;  /* Keep at least one tab */
    
    bar->tabs[idx].active = 0;
    bar->tabs[idx].path[0] = '\0';
    bar->tabs[idx].title[0] = '\0';
    bar->tab_count--;
    
    /* Switch to another tab if this was active */
    if (bar->active_tab == idx) {
        for (int i = 0; i < TAB_MAX_COUNT; i++) {
            if (bar->tabs[i].active) {
                bar->active_tab = i;
                break;
            }
        }
    }
    
    bar->visible = (bar->tab_count > 1);
}

void tab_bar_switch_tab(tab_bar_t *bar, int idx) {
    if (idx < 0 || idx >= TAB_MAX_COUNT) return;
    if (!bar->tabs[idx].active) return;
    bar->active_tab = idx;
}

files_tab_t* tab_bar_get_active(tab_bar_t *bar) {
    if (bar->active_tab < 0 || bar->active_tab >= TAB_MAX_COUNT) return NULL;
    files_tab_t *tab = &bar->tabs[bar->active_tab];
    return tab->active ? tab : NULL;
}

void tab_bar_update_title(tab_bar_t *bar, int idx, const char *path) {
    if (idx < 0 || idx >= TAB_MAX_COUNT) return;
    if (!bar->tabs[idx].active) return;
    
    files_tab_t *tab = &bar->tabs[idx];
    
    /* Update path */
    int i = 0;
    while (path && path[i] && i < TAB_MAX_PATH - 1) {
        tab->path[i] = path[i];
        i++;
    }
    tab->path[i] = '\0';
    
    /* Update title */
    int last_slash = 0;
    for (int j = 0; tab->path[j]; j++) {
        if (tab->path[j] == '/') last_slash = j;
    }
    i = 0;
    const char *name = &tab->path[last_slash + (last_slash > 0 ? 1 : 0)];
    while (name[i] && i < 63) {
        tab->title[i] = name[i];
        i++;
    }
    tab->title[i] = '\0';
    if (tab->title[0] == '\0') {
        tab->title[0] = '/';
        tab->title[1] = '\0';
    }
}

/* ============ Drawing ============ */

void tab_bar_draw(void *ctx, tab_bar_t *bar, int x, int y, int width) {
    if (!bar->visible) return;
    
    int h = bar->height;
    
    /* Background */
    nx_fill_rect(ctx, x, y, width, h, COLOR_TAB_BG);
    
    /* Bottom border */
    nx_fill_rect(ctx, x, y + h - 1, width, 1, COLOR_TAB_BORDER);
    
    /* Calculate tab width */
    int active_count = 0;
    for (int i = 0; i < TAB_MAX_COUNT; i++) {
        if (bar->tabs[i].active) active_count++;
    }
    
    int add_btn_width = 28;
    int available = width - add_btn_width - 8;
    int tab_width = available / (active_count > 0 ? active_count : 1);
    if (tab_width > TAB_MAX_WIDTH) tab_width = TAB_MAX_WIDTH;
    if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;
    
    int cx = x + 4;
    
    /* Draw tabs */
    for (int i = 0; i < TAB_MAX_COUNT; i++) {
        if (!bar->tabs[i].active) continue;
        
        int is_active = (i == bar->active_tab);
        int is_hover = (i == bar->hover_tab);
        
        /* Tab background */
        uint32_t bg_color = is_active ? COLOR_TAB_ACTIVE 
                         : is_hover ? COLOR_TAB_HOVER 
                         : COLOR_TAB_BG;
        nx_fill_rect(ctx, cx, y, tab_width, h, bg_color);
        
        /* Tab border */
        if (is_active) {
            nx_fill_rect(ctx, cx, y, 1, h, COLOR_TAB_BORDER);
            nx_fill_rect(ctx, cx + tab_width - 1, y, 1, h, COLOR_TAB_BORDER);
        }
        
        /* Tab title */
        uint32_t text_color = is_active ? COLOR_TAB_TEXT : COLOR_TAB_TEXT_DIM;
        nx_draw_text(ctx, bar->tabs[i].title, cx + 8, y + 6, text_color);
        
        /* Close button */
        int close_x = cx + tab_width - 20;
        int close_y = y + 8;
        int is_close_hover = (bar->hover_close == i);
        if (is_close_hover || is_hover) {
            nx_draw_text(ctx, "x", close_x, close_y, 
                        is_close_hover ? COLOR_TAB_CLOSE : COLOR_TAB_TEXT_DIM);
        }
        
        cx += tab_width + 2;
    }
    
    /* Add tab button */
    int add_x = cx + 4;
    nx_fill_rect(ctx, add_x, y + 4, add_btn_width - 8, h - 8, COLOR_TAB_HOVER);
    nx_draw_text(ctx, "+", add_x + 6, y + 6, COLOR_TAB_ADD);
}

/* ============ Interaction ============ */

typedef enum {
    TAB_HIT_NONE,
    TAB_HIT_TAB,
    TAB_HIT_CLOSE,
    TAB_HIT_ADD
} tab_hit_type_t;

typedef struct {
    tab_hit_type_t type;
    int tab_index;
} tab_hit_result_t;

tab_hit_result_t tab_bar_hit_test(tab_bar_t *bar, int mx, int my, int bar_x, int bar_y, int width) {
    tab_hit_result_t result = { TAB_HIT_NONE, -1 };
    
    if (!bar->visible) return result;
    if (my < bar_y || my > bar_y + bar->height) return result;
    
    /* Calculate tab width */
    int active_count = 0;
    for (int i = 0; i < TAB_MAX_COUNT; i++) {
        if (bar->tabs[i].active) active_count++;
    }
    
    int add_btn_width = 28;
    int available = width - add_btn_width - 8;
    int tab_width = available / (active_count > 0 ? active_count : 1);
    if (tab_width > TAB_MAX_WIDTH) tab_width = TAB_MAX_WIDTH;
    if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;
    
    int cx = bar_x + 4;
    
    for (int i = 0; i < TAB_MAX_COUNT; i++) {
        if (!bar->tabs[i].active) continue;
        
        if (mx >= cx && mx < cx + tab_width) {
            /* Check close button */
            int close_x = cx + tab_width - 20;
            if (mx >= close_x && mx < close_x + 16) {
                result.type = TAB_HIT_CLOSE;
                result.tab_index = i;
                return result;
            }
            
            /* Hit the tab */
            result.type = TAB_HIT_TAB;
            result.tab_index = i;
            return result;
        }
        
        cx += tab_width + 2;
    }
    
    /* Check add button */
    int add_x = cx + 4;
    if (mx >= add_x && mx < add_x + add_btn_width) {
        result.type = TAB_HIT_ADD;
        return result;
    }
    
    return result;
}

void tab_bar_on_hover(tab_bar_t *bar, tab_hit_result_t hit) {
    bar->hover_tab = (hit.type == TAB_HIT_TAB || hit.type == TAB_HIT_CLOSE) 
                   ? hit.tab_index : -1;
    bar->hover_close = (hit.type == TAB_HIT_CLOSE) ? hit.tab_index : -1;
}

int tab_bar_on_click(tab_bar_t *bar, tab_hit_result_t hit, const char *current_path) {
    switch (hit.type) {
        case TAB_HIT_TAB:
            tab_bar_switch_tab(bar, hit.tab_index);
            return 1;  /* Switched tab */
            
        case TAB_HIT_CLOSE:
            tab_bar_close_tab(bar, hit.tab_index);
            return 2;  /* Closed tab */
            
        case TAB_HIT_ADD:
            tab_bar_new_tab(bar, current_path);
            return 3;  /* New tab */
            
        default:
            return 0;
    }
}

/* ============ Keyboard Shortcuts ============ */

int tab_bar_on_key(tab_bar_t *bar, uint16_t keycode, uint16_t mods, const char *current_path) {
    int ctrl = mods & 0x02;
    int shift = mods & 0x01;
    
    if (ctrl) {
        switch (keycode & 0xFF) {
            case 't':  /* Ctrl+T: New tab */
                return tab_bar_new_tab(bar, current_path);
                
            case 'w':  /* Ctrl+W: Close tab */
                if (bar->tab_count > 1) {
                    tab_bar_close_tab(bar, bar->active_tab);
                    return 1;
                }
                break;
                
            case 9:    /* Ctrl+Tab: Next tab */
                if (bar->tab_count > 1) {
                    int next = (bar->active_tab + 1) % TAB_MAX_COUNT;
                    while (!bar->tabs[next].active) {
                        next = (next + 1) % TAB_MAX_COUNT;
                    }
                    tab_bar_switch_tab(bar, next);
                    return 1;
                }
                break;
        }
        
        /* Ctrl+Shift+Tab: Previous tab */
        if (shift && keycode == 9 && bar->tab_count > 1) {
            int prev = (bar->active_tab - 1 + TAB_MAX_COUNT) % TAB_MAX_COUNT;
            while (!bar->tabs[prev].active) {
                prev = (prev - 1 + TAB_MAX_COUNT) % TAB_MAX_COUNT;
            }
            tab_bar_switch_tab(bar, prev);
            return 1;
        }
    }
    
    return 0;
}
