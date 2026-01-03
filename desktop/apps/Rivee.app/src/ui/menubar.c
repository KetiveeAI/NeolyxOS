/*
 * Rivee - Global Menu Bar Implementation
 * macOS-style top menu that's always visible
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "../include/menubar.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations for rendering */
extern void nxrender_fill_rect(void *surf, int x, int y, int w, int h, uint32_t color);
extern void nxrender_draw_text(void *surf, int x, int y, const char *text, uint32_t color, int size);
extern void nxrender_draw_icon(void *surf, int x, int y, const char *icon, int size);
extern int nxrender_text_width(const char *text, int size);
extern int nxrender_screen_width(void);

/* ============ Memory helpers ============ */

static void *rivee_alloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

#define rivee_free(ptr) do { if (ptr) free(ptr); (ptr) = NULL; } while(0)

/* ============ Menu Bar Creation ============ */

menubar_t *menubar_create(const char *app_name, const char *app_icon) {
    menubar_t *bar = rivee_alloc(sizeof(menubar_t));
    if (!bar) return NULL;
    
    strncpy(bar->app_name, app_name, sizeof(bar->app_name) - 1);
    if (app_icon) {
        strncpy(bar->app_icon, app_icon, sizeof(bar->app_icon) - 1);
    }
    
    bar->menus = NULL;
    bar->menu_count = 0;
    bar->active_menu = -1;
    bar->is_active = false;
    bar->battery_percent = 100;
    bar->wifi_connected = true;
    
    return bar;
}

void menubar_destroy(menubar_t *bar) {
    if (!bar) return;
    
    for (int i = 0; i < bar->menu_count; i++) {
        menu_t *menu = &bar->menus[i];
        menu_item_t *item = menu->items;
        while (item) {
            menu_item_t *next = item->next;
            rivee_free(item);
            item = next;
        }
    }
    
    rivee_free(bar->menus);
    rivee_free(bar);
}

/* ============ Menu Management ============ */

menu_t *menubar_add_menu(menubar_t *bar, const char *label) {
    if (!bar) return NULL;
    
    bar->menus = realloc(bar->menus, (bar->menu_count + 1) * sizeof(menu_t));
    menu_t *menu = &bar->menus[bar->menu_count++];
    
    memset(menu, 0, sizeof(menu_t));
    strncpy(menu->label, label, sizeof(menu->label) - 1);
    menu->items = NULL;
    menu->item_count = 0;
    menu->is_open = false;
    
    return menu;
}

static menu_item_t *menu_add_item_internal(menu_t *menu, const char *label, menu_item_type_t type) {
    menu_item_t *item = rivee_alloc(sizeof(menu_item_t));
    if (!item) return NULL;
    
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->type = type;
    item->enabled = true;
    item->checked = false;
    item->next = NULL;
    
    /* Add to end of list */
    if (!menu->items) {
        menu->items = item;
    } else {
        menu_item_t *last = menu->items;
        while (last->next) last = last->next;
        last->next = item;
    }
    menu->item_count++;
    
    return item;
}

menu_item_t *menu_add_item(menu_t *menu, const char *label, void (*action)(void *), void *ctx) {
    menu_item_t *item = menu_add_item_internal(menu, label, MENU_ITEM_NORMAL);
    if (item) {
        item->action = action;
        item->user_data = ctx;
    }
    return item;
}

menu_item_t *menu_add_item_with_shortcut(menu_t *menu, const char *label, 
                                          menu_shortcut_t shortcut,
                                          void (*action)(void *), void *ctx) {
    menu_item_t *item = menu_add_item(menu, label, action, ctx);
    if (item) {
        item->shortcut = shortcut;
    }
    return item;
}

void menu_add_separator(menu_t *menu) {
    menu_add_item_internal(menu, "", MENU_ITEM_SEPARATOR);
}

menu_item_t *menu_add_submenu(menu_t *menu, const char *label) {
    return menu_add_item_internal(menu, label, MENU_ITEM_SUBMENU);
}

void menu_add_toggle(menu_t *menu, const char *label, bool checked, void (*action)(void *), void *ctx) {
    menu_item_t *item = menu_add_item_internal(menu, label, MENU_ITEM_TOGGLE);
    if (item) {
        item->checked = checked;
        item->action = action;
        item->user_data = ctx;
    }
}

void menu_item_set_enabled(menu_item_t *item, bool enabled) {
    if (item) item->enabled = enabled;
}

void menu_item_set_checked(menu_item_t *item, bool checked) {
    if (item) item->checked = checked;
}

/* ============ Shortcut Formatting ============ */

static void format_shortcut(const menu_shortcut_t *sc, char *out, int max_len) {
    char buf[64] = "";
    
    if (sc->cmd) strcat(buf, "⌘");
    if (sc->shift) strcat(buf, "⇧");
    if (sc->alt) strcat(buf, "⌥");
    if (sc->ctrl) strcat(buf, "⌃");
    
    if (sc->key) {
        char key_str[2] = {sc->key, '\0'};
        if (sc->key >= 'a' && sc->key <= 'z') {
            key_str[0] = sc->key - 32; /* Uppercase */
        }
        strcat(buf, key_str);
    }
    
    strncpy(out, buf, max_len - 1);
}

/* ============ Rendering ============ */

void menubar_render(menubar_t *bar, void *surface) {
    if (!bar || !surface) return;
    
    int screen_w = nxrender_screen_width();
    
    /* Background */
    nxrender_fill_rect(surface, 0, 0, screen_w, MENUBAR_HEIGHT, MENUBAR_BG);
    
    int x = 8;
    
    /* App icon and name (Apple menu position) */
    if (bar->app_icon[0]) {
        nxrender_draw_icon(surface, x, 4, bar->app_icon, 16);
        x += 20;
    }
    nxrender_draw_text(surface, x, 4, bar->app_name, MENUBAR_TEXT, 13);
    x += nxrender_text_width(bar->app_name, 13) + MENU_ITEM_PADDING;
    
    /* Menus */
    for (int i = 0; i < bar->menu_count; i++) {
        menu_t *menu = &bar->menus[i];
        int text_w = nxrender_text_width(menu->label, 13);
        int item_w = text_w + MENU_ITEM_PADDING * 2;
        
        menu->x = x;
        menu->width = item_w;
        
        /* Highlight if active */
        if (bar->active_menu == i || menu->is_open) {
            nxrender_fill_rect(surface, x, 0, item_w, MENUBAR_HEIGHT, MENUBAR_ACTIVE);
        }
        
        nxrender_draw_text(surface, x + MENU_ITEM_PADDING, 4, menu->label, MENUBAR_TEXT, 13);
        x += item_w;
    }
    
    /* Right side - time, battery, wifi */
    int right_x = screen_w - 10;
    
    /* Time */
    right_x -= nxrender_text_width(bar->time_str, 12) + 8;
    nxrender_draw_text(surface, right_x, 5, bar->time_str, MENUBAR_TEXT, 12);
    
    /* Battery */
    right_x -= 30;
    char bat_str[8];
    snprintf(bat_str, sizeof(bat_str), "%d%%", bar->battery_percent);
    nxrender_draw_text(surface, right_x, 5, bat_str, MENUBAR_TEXT, 12);
    
    /* WiFi icon */
    right_x -= 20;
    nxrender_draw_icon(surface, right_x, 4, bar->wifi_connected ? "wifi" : "wifi_off", 16);
    
    /* Render open submenu */
    if (bar->active_menu >= 0 && bar->active_menu < bar->menu_count) {
        menu_t *menu = &bar->menus[bar->active_menu];
        if (menu->is_open) {
            menubar_render_submenu(menu, surface, menu->x, MENUBAR_HEIGHT);
        }
    }
}

void menubar_render_submenu(menu_t *menu, void *surface, int x, int y) {
    if (!menu || !surface) return;
    
    int item_height = 24;
    int total_height = 4; /* Padding */
    
    /* Calculate height */
    for (menu_item_t *item = menu->items; item; item = item->next) {
        if (item->type == MENU_ITEM_SEPARATOR) {
            total_height += 8;
        } else {
            total_height += item_height;
        }
    }
    total_height += 4; /* Bottom padding */
    
    /* Background with shadow */
    nxrender_fill_rect(surface, x + 3, y + 3, SUBMENU_WIDTH, total_height, 0x00000066);
    nxrender_fill_rect(surface, x, y, SUBMENU_WIDTH, total_height, SUBMENU_BG);
    
    /* Items */
    int item_y = y + 4;
    for (menu_item_t *item = menu->items; item; item = item->next) {
        if (item->type == MENU_ITEM_SEPARATOR) {
            nxrender_fill_rect(surface, x + 8, item_y + 3, SUBMENU_WIDTH - 16, 1, SUBMENU_BORDER);
            item_y += 8;
            continue;
        }
        
        uint32_t text_color = item->enabled ? MENUBAR_TEXT : DISABLED_COLOR;
        
        /* Checkmark for toggles */
        if (item->type == MENU_ITEM_TOGGLE && item->checked) {
            nxrender_draw_text(surface, x + 8, item_y + 4, "✓", text_color, 12);
        }
        
        /* Label */
        nxrender_draw_text(surface, x + 28, item_y + 4, item->label, text_color, 13);
        
        /* Shortcut */
        if (item->shortcut.key) {
            char shortcut_str[32];
            format_shortcut(&item->shortcut, shortcut_str, sizeof(shortcut_str));
            int sc_x = x + SUBMENU_WIDTH - nxrender_text_width(shortcut_str, 12) - 12;
            nxrender_draw_text(surface, sc_x, item_y + 5, shortcut_str, SHORTCUT_COLOR, 12);
        }
        
        /* Submenu arrow */
        if (item->type == MENU_ITEM_SUBMENU) {
            nxrender_draw_text(surface, x + SUBMENU_WIDTH - 16, item_y + 4, "▶", text_color, 10);
        }
        
        item_y += item_height;
    }
}

/* ============ Event Handling ============ */

bool menubar_handle_click(menubar_t *bar, int x, int y) {
    if (!bar) return false;
    
    /* Check if click is in menu bar */
    if (y >= 0 && y < MENUBAR_HEIGHT) {
        for (int i = 0; i < bar->menu_count; i++) {
            menu_t *menu = &bar->menus[i];
            if (x >= menu->x && x < menu->x + menu->width) {
                /* Toggle this menu */
                if (bar->active_menu == i) {
                    menu->is_open = !menu->is_open;
                    if (!menu->is_open) bar->active_menu = -1;
                } else {
                    if (bar->active_menu >= 0) {
                        bar->menus[bar->active_menu].is_open = false;
                    }
                    bar->active_menu = i;
                    menu->is_open = true;
                }
                return true;
            }
        }
        return false;
    }
    
    /* Check if click is in open submenu */
    if (bar->active_menu >= 0) {
        menu_t *menu = &bar->menus[bar->active_menu];
        if (menu->is_open) {
            if (x >= menu->x && x < menu->x + SUBMENU_WIDTH && y >= MENUBAR_HEIGHT) {
                /* Find clicked item */
                int item_y = MENUBAR_HEIGHT + 4;
                for (menu_item_t *item = menu->items; item; item = item->next) {
                    int item_h = (item->type == MENU_ITEM_SEPARATOR) ? 8 : 24;
                    
                    if (y >= item_y && y < item_y + item_h) {
                        if (item->enabled && item->type != MENU_ITEM_SEPARATOR) {
                            if (item->type == MENU_ITEM_TOGGLE) {
                                item->checked = !item->checked;
                            }
                            if (item->action) {
                                item->action(item->user_data);
                            }
                            menubar_close_all(bar);
                            return true;
                        }
                    }
                    item_y += item_h;
                }
            }
            /* Click outside - close */
            menubar_close_all(bar);
            return true;
        }
    }
    
    return false;
}

bool menubar_handle_key(menubar_t *bar, char key, bool cmd, bool shift, bool alt) {
    if (!bar || !cmd) return false; /* Only handle command shortcuts */
    
    for (int i = 0; i < bar->menu_count; i++) {
        for (menu_item_t *item = bar->menus[i].items; item; item = item->next) {
            menu_shortcut_t *sc = &item->shortcut;
            if (sc->cmd == cmd && sc->shift == shift && sc->alt == alt && sc->key == key) {
                if (item->enabled && item->action) {
                    item->action(item->user_data);
                    return true;
                }
            }
        }
    }
    return false;
}

void menubar_close_all(menubar_t *bar) {
    if (!bar) return;
    
    for (int i = 0; i < bar->menu_count; i++) {
        bar->menus[i].is_open = false;
    }
    bar->active_menu = -1;
}

/* ============ System Updates ============ */

void menubar_update_time(menubar_t *bar) {
    if (!bar) return;
    /* Would get system time - for now placeholder */
    strcpy(bar->time_str, "12:00 PM");
}

void menubar_update_battery(menubar_t *bar, int percent) {
    if (bar) bar->battery_percent = percent;
}

void menubar_update_wifi(menubar_t *bar, bool connected) {
    if (bar) bar->wifi_connected = connected;
}
