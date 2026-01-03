
 * REOX Desktop Environment
 * Window Manager, Taskbar, Dock for NeolyxOS
 * 
 * This provides the core desktop UI components that REOX apps
 * are displayed within.
 */

#ifndef REOX_DESKTOP_H
#define REOX_DESKTOP_H

#include "reox_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Desktop Configuration
 * ============================================================================ */

#define RX_DESKTOP_TASKBAR_HEIGHT 48
#define RX_DESKTOP_DOCK_SIZE 64
#define RX_DESKTOP_MENU_BAR_HEIGHT 28

typedef enum rx_dock_position {
    RX_DOCK_BOTTOM,
    RX_DOCK_LEFT,
    RX_DOCK_RIGHT,
} rx_dock_position;

typedef enum rx_taskbar_position {
    RX_TASKBAR_TOP,
    RX_TASKBAR_BOTTOM,
} rx_taskbar_position;

/* ============================================================================
 * Desktop Window
 * ============================================================================ */

typedef enum rx_window_state {
    RX_WINDOW_NORMAL,
    RX_WINDOW_MAXIMIZED,
    RX_WINDOW_MINIMIZED,
    RX_WINDOW_FULLSCREEN,
} rx_window_state;

typedef struct rx_desktop_window {
    rx_window* window;
    rx_window_state state;
    rx_rect restore_frame;      /* Frame before maximize/minimize */
    const char* app_name;
    const char* icon_path;
    bool focused;
    bool visible;
    int z_order;
} rx_desktop_window;

/* Window chrome (title bar, buttons) */
typedef struct rx_window_chrome {
    rx_view base;
    rx_desktop_window* window;
    rx_button_view* close_btn;
    rx_button_view* minimize_btn;
    rx_button_view* maximize_btn;
    rx_text_view* title_label;
    bool dragging;
    rx_point drag_offset;
} rx_window_chrome;

/* ============================================================================
 * Taskbar
 * ============================================================================ */

typedef struct rx_taskbar_item {
    rx_desktop_window* window;
    rx_view* button;
    bool pinned;
} rx_taskbar_item;

typedef struct rx_taskbar {
    rx_view base;
    rx_taskbar_position position;
    rx_taskbar_item* items;
    size_t item_count;
    
    /* Components */
    rx_view* start_button;
    rx_view* window_list;
    rx_view* system_tray;
    rx_text_view* clock;
    
    /* Colors */
    rx_color background;
    rx_color active_color;
    rx_color hover_color;
} rx_taskbar;

extern rx_taskbar* taskbar_new(rx_taskbar_position position);
extern void taskbar_add_window(rx_taskbar* bar, rx_desktop_window* window);
extern void taskbar_remove_window(rx_taskbar* bar, rx_desktop_window* window);
extern void taskbar_set_active(rx_taskbar* bar, rx_desktop_window* window);
extern void taskbar_update_clock(rx_taskbar* bar, const char* time_str);

/* ============================================================================
 * Dock (macOS-style)
 * ============================================================================ */

typedef struct rx_dock_item {
    const char* app_id;
    const char* icon_path;
    const char* tooltip;
    rx_view* icon_view;
    bool running;               /* Show running indicator */
    bool pinned;                /* Stays in dock when closed */
    rx_desktop_window* window;  /* Associated window if running */
} rx_dock_item;

typedef struct rx_dock {
    rx_view base;
    rx_dock_position position;
    rx_dock_item* items;
    size_t item_count;
    
    /* Appearance */
    float icon_size;
    float magnification;        /* Hover magnification (1.0 = none) */
    bool auto_hide;
    bool visible;
    
    /* Colors */
    rx_color background;
    rx_color separator_color;
} rx_dock;

extern rx_dock* dock_new(rx_dock_position position);
extern int dock_add_app(rx_dock* dock, const char* app_id, const char* icon_path);
extern void dock_remove_app(rx_dock* dock, const char* app_id);
extern void dock_set_running(rx_dock* dock, const char* app_id, bool running);
extern void dock_set_magnification(rx_dock* dock, float magnification);
extern void dock_set_auto_hide(rx_dock* dock, bool auto_hide);

/* ============================================================================
 * Menu Bar
 * ============================================================================ */

typedef struct rx_menu_bar_item {
    const char* title;
    rx_menu_view* menu;
} rx_menu_bar_item;

typedef struct rx_menu_bar {
    rx_view base;
    rx_menu_bar_item* items;
    size_t item_count;
    int active_index;
    
    /* App info (left side) */
    const char* app_name;
    const char* app_icon;
    
    /* System area (right side) */
    rx_text_view* clock;
    rx_view* system_icons;
    
    rx_color background;
    rx_color text_color;
} rx_menu_bar;

extern rx_menu_bar* menu_bar_new(void);
extern int menu_bar_add_menu(rx_menu_bar* bar, const char* title, rx_menu_view* menu);
extern void menu_bar_set_app(rx_menu_bar* bar, const char* name, const char* icon);

/* ============================================================================
 * Desktop (Window Manager)
 * ============================================================================ */

typedef void (*rx_window_callback)(rx_desktop_window* window, void* user_data);

typedef struct rx_desktop {
    /* Screen info */
    int screen_width;
    int screen_height;
    
    /* Background */
    rx_view* wallpaper;
    rx_color wallpaper_color;
    const char* wallpaper_path;
    
    /* Chrome elements */
    rx_taskbar* taskbar;
    rx_dock* dock;
    rx_menu_bar* menu_bar;
    
    /* Windows */
    rx_desktop_window** windows;
    size_t window_count;
    rx_desktop_window* focused_window;
    
    /* Desktop icons */
    rx_view* icons_container;
    
    /* State */
    bool show_desktop;          /* All windows minimized */
    int next_window_id;
    
    /* Callbacks */
    rx_window_callback on_window_focus;
    rx_window_callback on_window_close;
    void* callback_data;
} rx_desktop;

/* Desktop lifecycle */
extern rx_desktop* desktop_create(int width, int height);
extern void desktop_destroy(rx_desktop* desktop);

/* Window management */
extern rx_desktop_window* desktop_create_window(rx_desktop* d, const char* title, 
                                                 int width, int height);
extern void desktop_close_window(rx_desktop* d, rx_desktop_window* window);
extern void desktop_focus_window(rx_desktop* d, rx_desktop_window* window);
extern void desktop_minimize_window(rx_desktop* d, rx_desktop_window* window);
extern void desktop_maximize_window(rx_desktop* d, rx_desktop_window* window);
extern void desktop_restore_window(rx_desktop* d, rx_desktop_window* window);

/* Desktop actions */
extern void desktop_show_all(rx_desktop* d);
extern void desktop_minimize_all(rx_desktop* d);
extern void desktop_tile_windows(rx_desktop* d, bool horizontal);

/* Appearance */
extern void desktop_set_wallpaper(rx_desktop* d, const char* path);
extern void desktop_set_wallpaper_color(rx_desktop* d, rx_color color);

/* Rendering */
extern void desktop_layout(rx_desktop* d);
extern void desktop_render(rx_desktop* d, void* context);
extern void desktop_handle_event(rx_desktop* d, void* event);

/* ============================================================================
 * Notification Center
 * ============================================================================ */

typedef enum rx_notification_type {
    RX_NOTIF_INFO,
    RX_NOTIF_WARNING,
    RX_NOTIF_ERROR,
    RX_NOTIF_SUCCESS,
} rx_notification_type;

typedef struct rx_notification {
    const char* title;
    const char* message;
    const char* icon;
    rx_notification_type type;
    float duration;             /* Seconds, 0 = persistent */
    void (*on_click)(void*);
    void* user_data;
} rx_notification;

typedef struct rx_notification_center {
    rx_view base;
    rx_notification** notifications;
    size_t notification_count;
    bool visible;
} rx_notification_center;

extern rx_notification_center* notification_center_new(void);
extern void notification_show(rx_notification_center* nc, rx_notification* notif);
extern void notification_dismiss(rx_notification_center* nc, int index);
extern void notification_dismiss_all(rx_notification_center* nc);

#ifdef __cplusplus
}
#endif

#endif /* REOX_DESKTOP_H */
