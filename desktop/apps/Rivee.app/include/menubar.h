/*
 * Rivee - Global Menu Bar (macOS-style)
 * Always displays at top of screen
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef RIVEE_MENUBAR_H
#define RIVEE_MENUBAR_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Menu Bar Constants ============ */

#define MENUBAR_HEIGHT      24
#define MENU_ITEM_PADDING   12
#define SUBMENU_WIDTH       220
#define SEPARATOR_HEIGHT    1

/* ============ Menu Colors (Dark Theme) ============ */

#define MENUBAR_BG          0x1E1E1E
#define MENUBAR_TEXT        0xFFFFFF
#define MENUBAR_HOVER       0x3A3A3A
#define MENUBAR_ACTIVE      0x007AFF
#define SUBMENU_BG          0x2D2D2D
#define SUBMENU_BORDER      0x4A4A4A
#define SUBMENU_HIGHLIGHT   0x0A84FF
#define SHORTCUT_COLOR      0x888888
#define DISABLED_COLOR      0x666666

/* ============ Menu Item Types ============ */

typedef enum {
    MENU_ITEM_NORMAL,
    MENU_ITEM_SEPARATOR,
    MENU_ITEM_TOGGLE,
    MENU_ITEM_SUBMENU
} menu_item_type_t;

/* ============ Menu Shortcut ============ */

typedef struct {
    bool cmd;       /* Command/Super key */
    bool shift;
    bool alt;
    bool ctrl;
    char key;       /* Main key */
} menu_shortcut_t;

#define SHORTCUT_CMD(k)         ((menu_shortcut_t){true, false, false, false, k})
#define SHORTCUT_CMD_SHIFT(k)   ((menu_shortcut_t){true, true, false, false, k})
#define SHORTCUT_CMD_ALT(k)     ((menu_shortcut_t){true, false, true, false, k})

/* ============ Menu Item ============ */

typedef struct menu_item {
    char label[64];
    char icon[32];              /* Icon name from resources */
    menu_item_type_t type;
    menu_shortcut_t shortcut;
    bool enabled;
    bool checked;               /* For toggle items */
    void (*action)(void *ctx);  /* Callback */
    void *user_data;
    
    struct menu_item *submenu;  /* For MENU_ITEM_SUBMENU */
    int submenu_count;
    
    struct menu_item *next;
} menu_item_t;

/* ============ Menu ============ */

typedef struct {
    char label[32];
    menu_item_t *items;
    int item_count;
    bool is_open;
    int x;
    int width;
} menu_t;

/* ============ Menu Bar ============ */

typedef struct {
    menu_t *menus;
    int menu_count;
    int active_menu;            /* -1 = none */
    bool is_active;
    
    /* App info on left */
    char app_name[32];
    char app_icon[64];
    
    /* Right side items */
    char time_str[16];
    int battery_percent;
    bool wifi_connected;
} menubar_t;

/* ============ Menu Bar API ============ */

/* Initialize menu bar */
menubar_t *menubar_create(const char *app_name, const char *app_icon);
void menubar_destroy(menubar_t *bar);

/* Add menus */
menu_t *menubar_add_menu(menubar_t *bar, const char *label);
menu_item_t *menu_add_item(menu_t *menu, const char *label, void (*action)(void *), void *ctx);
menu_item_t *menu_add_item_with_shortcut(menu_t *menu, const char *label, 
                                          menu_shortcut_t shortcut,
                                          void (*action)(void *), void *ctx);
void menu_add_separator(menu_t *menu);
menu_item_t *menu_add_submenu(menu_t *menu, const char *label);
void menu_add_toggle(menu_t *menu, const char *label, bool checked, void (*action)(void *), void *ctx);

/* Item operations */
void menu_item_set_enabled(menu_item_t *item, bool enabled);
void menu_item_set_checked(menu_item_t *item, bool checked);

/* Rendering */
void menubar_render(menubar_t *bar, void *surface);
void menubar_render_submenu(menu_t *menu, void *surface, int x, int y);

/* Event handling */
bool menubar_handle_click(menubar_t *bar, int x, int y);
bool menubar_handle_key(menubar_t *bar, char key, bool cmd, bool shift, bool alt);
void menubar_close_all(menubar_t *bar);

/* System time update */
void menubar_update_time(menubar_t *bar);
void menubar_update_battery(menubar_t *bar, int percent);
void menubar_update_wifi(menubar_t *bar, bool connected);

#endif /* RIVEE_MENUBAR_H */
