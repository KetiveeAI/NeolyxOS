/*
 * REOX Desktop Environment - Implementation
 * Window Manager, Taskbar, Dock for NeolyxOS
 */

#include "reox_desktop.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Taskbar Implementation
 * ============================================================================ */

rx_taskbar* taskbar_new(rx_taskbar_position position) {
    rx_taskbar* bar = (rx_taskbar*)calloc(1, sizeof(rx_taskbar));
    if (!bar) return NULL;
    
    bar->base.kind = RX_VIEW_CUSTOM;
    bar->base.box = box_new();
    bar->base.box.height = RX_DESKTOP_TASKBAR_HEIGHT;
    bar->base.visible = true;
    bar->base.enabled = true;
    
    bar->position = position;
    bar->background = color_hex(0x1C1C1E);
    bar->active_color = RX_COLOR_PRIMARY;
    bar->hover_color = color_hex(0x3A3A3C);
    
    /* Create components */
    bar->start_button = view_new(RX_VIEW_BOX);
    bar->start_button->box.width = 48;
    bar->start_button->box.height = 48;
    bar->start_button->box.background = bar->background;
    
    bar->window_list = hstack_new(4);
    bar->system_tray = hstack_new(8);
    
    bar->clock = text_view_new("00:00");
    if (bar->clock) {
        bar->clock->color = RX_COLOR_TEXT;
        bar->clock->font_size = 13;
    }
    
    return bar;
}

void taskbar_add_window(rx_taskbar* bar, rx_desktop_window* window) {
    if (!bar || !window) return;
    
    bar->items = (rx_taskbar_item*)realloc(
        bar->items,
        sizeof(rx_taskbar_item) * (bar->item_count + 1)
    );
    
    rx_taskbar_item* item = &bar->items[bar->item_count];
    item->window = window;
    item->pinned = false;
    
    /* Create button for window */
    item->button = view_new(RX_VIEW_BOX);
    item->button->box.width = 160;
    item->button->box.height = 36;
    item->button->box.background = bar->background;
    item->button->box.corner_radius = corners_all(4);
    
    view_add_child(bar->window_list, item->button);
    bar->item_count++;
}

void taskbar_remove_window(rx_taskbar* bar, rx_desktop_window* window) {
    if (!bar || !window) return;
    
    for (size_t i = 0; i < bar->item_count; i++) {
        if (bar->items[i].window == window) {
            view_remove_child(bar->window_list, bar->items[i].button);
            view_free(bar->items[i].button);
            
            /* Shift remaining items */
            for (size_t j = i; j < bar->item_count - 1; j++) {
                bar->items[j] = bar->items[j + 1];
            }
            bar->item_count--;
            break;
        }
    }
}

void taskbar_set_active(rx_taskbar* bar, rx_desktop_window* window) {
    if (!bar) return;
    
    for (size_t i = 0; i < bar->item_count; i++) {
        if (bar->items[i].button) {
            bar->items[i].button->box.background = 
                (bar->items[i].window == window) ? 
                    bar->active_color : bar->background;
        }
    }
}

void taskbar_update_clock(rx_taskbar* bar, const char* time_str) {
    if (bar && bar->clock) {
        text_view_set_text(bar->clock, time_str);
    }
}

/* ============================================================================
 * Dock Implementation
 * ============================================================================ */

rx_dock* dock_new(rx_dock_position position) {
    rx_dock* dock = (rx_dock*)calloc(1, sizeof(rx_dock));
    if (!dock) return NULL;
    
    dock->base.kind = RX_VIEW_CUSTOM;
    dock->base.box = box_new();
    dock->base.box.height = RX_DESKTOP_DOCK_SIZE + 16;
    dock->base.box.corner_radius = corners_all(12);
    dock->base.box.padding = insets_all(8);
    dock->base.visible = true;
    dock->base.enabled = true;
    
    dock->position = position;
    dock->icon_size = 48;
    dock->magnification = 1.5f;
    dock->auto_hide = false;
    dock->visible = true;
    dock->background = color_with_alpha(color_hex(0x1C1C1E), 200);
    dock->separator_color = color_hex(0x3A3A3C);
    
    /* Layout based on position */
    if (position == RX_DOCK_BOTTOM) {
        dock->base.layout = hstack(8);
    } else {
        dock->base.layout = vstack(8);
    }
    
    return dock;
}

int dock_add_app(rx_dock* dock, const char* app_id, const char* icon_path) {
    if (!dock) return -1;
    
    dock->items = (rx_dock_item*)realloc(
        dock->items,
        sizeof(rx_dock_item) * (dock->item_count + 1)
    );
    
    rx_dock_item* item = &dock->items[dock->item_count];
    item->app_id = app_id ? strdup(app_id) : NULL;
    item->icon_path = icon_path ? strdup(icon_path) : NULL;
    item->tooltip = app_id;
    item->running = false;
    item->pinned = true;
    item->window = NULL;
    
    /* Create icon view */
    item->icon_view = view_new(RX_VIEW_BOX);
    item->icon_view->box.width = dock->icon_size;
    item->icon_view->box.height = dock->icon_size;
    item->icon_view->box.corner_radius = corners_all(10);
    item->icon_view->box.background = color_hex(0x3A3A3C);
    
    view_add_child(&dock->base, item->icon_view);
    
    return (int)dock->item_count++;
}

void dock_remove_app(rx_dock* dock, const char* app_id) {
    if (!dock || !app_id) return;
    
    for (size_t i = 0; i < dock->item_count; i++) {
        if (dock->items[i].app_id && strcmp(dock->items[i].app_id, app_id) == 0) {
            view_remove_child(&dock->base, dock->items[i].icon_view);
            view_free(dock->items[i].icon_view);
            free((void*)dock->items[i].app_id);
            free((void*)dock->items[i].icon_path);
            
            /* Shift remaining */
            for (size_t j = i; j < dock->item_count - 1; j++) {
                dock->items[j] = dock->items[j + 1];
            }
            dock->item_count--;
            break;
        }
    }
}

void dock_set_running(rx_dock* dock, const char* app_id, bool running) {
    if (!dock || !app_id) return;
    
    for (size_t i = 0; i < dock->item_count; i++) {
        if (dock->items[i].app_id && strcmp(dock->items[i].app_id, app_id) == 0) {
            dock->items[i].running = running;
            break;
        }
    }
}

void dock_set_magnification(rx_dock* dock, float magnification) {
    if (dock) dock->magnification = magnification;
}

void dock_set_auto_hide(rx_dock* dock, bool auto_hide) {
    if (dock) dock->auto_hide = auto_hide;
}

/* ============================================================================
 * Menu Bar Implementation
 * ============================================================================ */

rx_menu_bar* menu_bar_new(void) {
    rx_menu_bar* bar = (rx_menu_bar*)calloc(1, sizeof(rx_menu_bar));
    if (!bar) return NULL;
    
    bar->base.kind = RX_VIEW_CUSTOM;
    bar->base.box = box_new();
    bar->base.box.height = RX_DESKTOP_MENU_BAR_HEIGHT;
    bar->base.visible = true;
    bar->base.enabled = true;
    bar->base.layout = hstack(16);
    bar->base.box.padding = insets_symmetric(0, 16);
    
    bar->background = color_with_alpha(color_hex(0x2C2C2E), 230);
    bar->text_color = RX_COLOR_TEXT;
    bar->active_index = -1;
    
    bar->clock = text_view_new("00:00");
    if (bar->clock) {
        bar->clock->font_size = 13;
    }
    
    bar->system_icons = hstack_new(12);
    
    return bar;
}

int menu_bar_add_menu(rx_menu_bar* bar, const char* title, rx_menu_view* menu) {
    if (!bar) return -1;
    
    bar->items = (rx_menu_bar_item*)realloc(
        bar->items,
        sizeof(rx_menu_bar_item) * (bar->item_count + 1)
    );
    
    bar->items[bar->item_count].title = title ? strdup(title) : NULL;
    bar->items[bar->item_count].menu = menu;
    
    return (int)bar->item_count++;
}

void menu_bar_set_app(rx_menu_bar* bar, const char* name, const char* icon) {
    if (!bar) return;
    bar->app_name = name ? strdup(name) : NULL;
    bar->app_icon = icon ? strdup(icon) : NULL;
}

/* ============================================================================
 * Desktop Implementation
 * ============================================================================ */

rx_desktop* desktop_create(int width, int height) {
    rx_desktop* d = (rx_desktop*)calloc(1, sizeof(rx_desktop));
    if (!d) return NULL;
    
    d->screen_width = width;
    d->screen_height = height;
    d->wallpaper_color = color_hex(0x1E1E2E);  /* Dark purple */
    
    /* Create wallpaper view */
    d->wallpaper = view_new(RX_VIEW_BOX);
    d->wallpaper->box.width = (float)width;
    d->wallpaper->box.height = (float)height;
    d->wallpaper->box.background = d->wallpaper_color;
    
    /* Create taskbar */
    d->taskbar = taskbar_new(RX_TASKBAR_BOTTOM);
    
    /* Create dock */
    d->dock = dock_new(RX_DOCK_BOTTOM);
    
    /* Create menu bar */
    d->menu_bar = menu_bar_new();
    
    /* Desktop icons container */
    d->icons_container = view_new(RX_VIEW_BOX);
    d->icons_container->layout = (rx_layout){ 
        RX_LAYOUT_VERTICAL, 
        RX_ALIGN_START, 
        RX_ALIGN_START, 
        16, 
        {16, 16, 16, 16} 
    };
    
    return d;
}

void desktop_destroy(rx_desktop* desktop) {
    if (!desktop) return;
    
    /* Free windows */
    for (size_t i = 0; i < desktop->window_count; i++) {
        if (desktop->windows[i]->window) {
            view_free(desktop->windows[i]->window->root_view);
        }
        free(desktop->windows[i]);
    }
    free(desktop->windows);
    
    view_free(desktop->wallpaper);
    view_free(desktop->icons_container);
    free(desktop);
}

rx_desktop_window* desktop_create_window(rx_desktop* d, const char* title,
                                          int width, int height) {
    if (!d) return NULL;
    
    rx_desktop_window* dw = (rx_desktop_window*)calloc(1, sizeof(rx_desktop_window));
    if (!dw) return NULL;
    
    /* Create underlying window */
    rx_app dummy = { .name = "app" };
    dw->window = app_create_window(&dummy, title, width, height);
    dw->state = RX_WINDOW_NORMAL;
    dw->focused = true;
    dw->visible = true;
    dw->z_order = (int)d->window_count;
    
    /* Center on screen */
    dw->window->position = point(
        (d->screen_width - width) / 2.0f,
        (d->screen_height - height) / 2.0f
    );
    
    /* Add to desktop */
    d->windows = (rx_desktop_window**)realloc(
        d->windows,
        sizeof(rx_desktop_window*) * (d->window_count + 1)
    );
    d->windows[d->window_count++] = dw;
    
    /* Add to taskbar */
    taskbar_add_window(d->taskbar, dw);
    
    /* Focus new window */
    desktop_focus_window(d, dw);
    
    return dw;
}

void desktop_close_window(rx_desktop* d, rx_desktop_window* window) {
    if (!d || !window) return;
    
    /* Remove from taskbar */
    taskbar_remove_window(d->taskbar, window);
    
    /* Remove from windows list */
    for (size_t i = 0; i < d->window_count; i++) {
        if (d->windows[i] == window) {
            for (size_t j = i; j < d->window_count - 1; j++) {
                d->windows[j] = d->windows[j + 1];
            }
            d->window_count--;
            break;
        }
    }
    
    /* Callback */
    if (d->on_window_close) {
        d->on_window_close(window, d->callback_data);
    }
    
    /* Focus next window */
    if (d->focused_window == window && d->window_count > 0) {
        desktop_focus_window(d, d->windows[d->window_count - 1]);
    }
    
    free(window);
}

void desktop_focus_window(rx_desktop* d, rx_desktop_window* window) {
    if (!d || !window) return;
    
    /* Unfocus current */
    if (d->focused_window) {
        d->focused_window->focused = false;
    }
    
    /* Focus new */
    window->focused = true;
    window->z_order = 999;  /* Bring to front */
    d->focused_window = window;
    
    /* Update taskbar */
    taskbar_set_active(d->taskbar, window);
    
    /* Callback */
    if (d->on_window_focus) {
        d->on_window_focus(window, d->callback_data);
    }
}

void desktop_minimize_window(rx_desktop* d, rx_desktop_window* window) {
    if (!window) return;
    window->restore_frame = window->window->root_view ? 
        window->window->root_view->box.frame : (rx_rect){0,0,0,0};
    window->state = RX_WINDOW_MINIMIZED;
    window->visible = false;
}

void desktop_maximize_window(rx_desktop* d, rx_desktop_window* window) {
    if (!d || !window) return;
    
    if (window->state != RX_WINDOW_MAXIMIZED) {
        window->restore_frame = window->window->root_view ?
            window->window->root_view->box.frame : (rx_rect){0,0,0,0};
    }
    
    window->state = RX_WINDOW_MAXIMIZED;
    window->window->size = size(
        (float)d->screen_width,
        (float)d->screen_height - RX_DESKTOP_TASKBAR_HEIGHT
    );
    window->window->position = point(0, 0);
}

void desktop_restore_window(rx_desktop* d, rx_desktop_window* window) {
    if (!window) return;
    
    window->state = RX_WINDOW_NORMAL;
    window->visible = true;
    
    if (window->window) {
        window->window->size = size(
            window->restore_frame.width,
            window->restore_frame.height
        );
        window->window->position = point(
            window->restore_frame.x,
            window->restore_frame.y
        );
    }
}

void desktop_show_all(rx_desktop* d) {
    if (!d) return;
    for (size_t i = 0; i < d->window_count; i++) {
        d->windows[i]->visible = true;
        d->windows[i]->state = RX_WINDOW_NORMAL;
    }
    d->show_desktop = false;
}

void desktop_minimize_all(rx_desktop* d) {
    if (!d) return;
    for (size_t i = 0; i < d->window_count; i++) {
        desktop_minimize_window(d, d->windows[i]);
    }
    d->show_desktop = true;
}

void desktop_tile_windows(rx_desktop* d, bool horizontal) {
    if (!d || d->window_count == 0) return;
    
    int visible_count = 0;
    for (size_t i = 0; i < d->window_count; i++) {
        if (d->windows[i]->visible) visible_count++;
    }
    
    if (visible_count == 0) return;
    
    float window_w, window_h;
    if (horizontal) {
        window_w = (float)d->screen_width / visible_count;
        window_h = (float)d->screen_height - RX_DESKTOP_TASKBAR_HEIGHT;
    } else {
        window_w = (float)d->screen_width;
        window_h = ((float)d->screen_height - RX_DESKTOP_TASKBAR_HEIGHT) / visible_count;
    }
    
    int idx = 0;
    for (size_t i = 0; i < d->window_count; i++) {
        if (d->windows[i]->visible) {
            d->windows[i]->window->size = size(window_w, window_h);
            d->windows[i]->window->position = horizontal ?
                point(window_w * idx, 0) :
                point(0, window_h * idx);
            idx++;
        }
    }
}

void desktop_set_wallpaper(rx_desktop* d, const char* path) {
    if (!d) return;
    d->wallpaper_path = path ? strdup(path) : NULL;
    /* TODO: Load image */
}

void desktop_set_wallpaper_color(rx_desktop* d, rx_color color) {
    if (!d) return;
    d->wallpaper_color = color;
    if (d->wallpaper) {
        d->wallpaper->box.background = color;
    }
}

void desktop_layout(rx_desktop* d) {
    if (!d) return;
    
    /* Layout taskbar at bottom */
    if (d->taskbar) {
        d->taskbar->base.box.frame = rect(
            0, 
            d->screen_height - RX_DESKTOP_TASKBAR_HEIGHT,
            d->screen_width,
            RX_DESKTOP_TASKBAR_HEIGHT
        );
    }
    
    /* Layout menu bar at top */
    if (d->menu_bar) {
        d->menu_bar->base.box.frame = rect(
            0, 0, d->screen_width, RX_DESKTOP_MENU_BAR_HEIGHT
        );
    }
    
    /* Layout dock centered at bottom */
    if (d->dock && d->dock->visible) {
        float dock_width = d->dock->item_count * (d->dock->icon_size + 8) + 16;
        d->dock->base.box.frame = rect(
            (d->screen_width - dock_width) / 2,
            d->screen_height - RX_DESKTOP_TASKBAR_HEIGHT - RX_DESKTOP_DOCK_SIZE - 24,
            dock_width,
            RX_DESKTOP_DOCK_SIZE + 16
        );
    }
    
    /* Layout windows */
    for (size_t i = 0; i < d->window_count; i++) {
        rx_desktop_window* dw = d->windows[i];
        if (dw->window && dw->window->root_view) {
            view_layout(dw->window->root_view, dw->window->size);
        }
    }
}

void desktop_render(rx_desktop* d, void* context) {
    if (!d) return;
    
    /* Render wallpaper */
    view_render(d->wallpaper, context);
    
    /* Render desktop icons */
    view_render(d->icons_container, context);
    
    /* Render windows (sorted by z-order) */
    for (size_t i = 0; i < d->window_count; i++) {
        if (d->windows[i]->visible && d->windows[i]->window) {
            view_render(d->windows[i]->window->root_view, context);
        }
    }
    
    /* Render dock */
    if (d->dock && d->dock->visible) {
        view_render(&d->dock->base, context);
    }
    
    /* Render taskbar */
    if (d->taskbar) {
        view_render(&d->taskbar->base, context);
    }
    
    /* Render menu bar */
    if (d->menu_bar) {
        view_render(&d->menu_bar->base, context);
    }
}

/* ============================================================================
 * Notification Center Implementation
 * ============================================================================ */

rx_notification_center* notification_center_new(void) {
    rx_notification_center* nc = (rx_notification_center*)calloc(1, sizeof(rx_notification_center));
    if (!nc) return NULL;
    
    nc->base.kind = RX_VIEW_CUSTOM;
    nc->base.box = box_new();
    nc->base.box.width = 320;
    nc->base.layout = vstack(8);
    nc->base.visible = false;
    
    return nc;
}

void notification_show(rx_notification_center* nc, rx_notification* notif) {
    if (!nc || !notif) return;
    
    nc->notifications = (rx_notification**)realloc(
        nc->notifications,
        sizeof(rx_notification*) * (nc->notification_count + 1)
    );
    nc->notifications[nc->notification_count++] = notif;
}

void notification_dismiss(rx_notification_center* nc, int index) {
    if (!nc || index < 0 || (size_t)index >= nc->notification_count) return;
    
    for (size_t i = (size_t)index; i < nc->notification_count - 1; i++) {
        nc->notifications[i] = nc->notifications[i + 1];
    }
    nc->notification_count--;
}

void notification_dismiss_all(rx_notification_center* nc) {
    if (!nc) return;
    nc->notification_count = 0;
}
