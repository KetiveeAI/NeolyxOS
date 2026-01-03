/*
 * NeolyxOS - NXRender Window Manager
 * Window creation and management for desktop environment
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_WINDOW_H
#define NXRENDER_WINDOW_H

#include "nxgfx/nxgfx.h"
#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_window nx_window_t;
typedef struct nx_window_manager nx_window_manager_t;

/* Window flags */
typedef enum {
    NX_WINDOW_NORMAL     = 0,
    NX_WINDOW_BORDERLESS = 1 << 0,
    NX_WINDOW_RESIZABLE  = 1 << 1,
    NX_WINDOW_MODAL      = 1 << 2,
    NX_WINDOW_TOPMOST    = 1 << 3,
    NX_WINDOW_MINIMIZED  = 1 << 4,
    NX_WINDOW_MAXIMIZED  = 1 << 5
} nx_window_flags_t;

/* Window style */
typedef struct {
    nx_color_t title_bar_color;
    nx_color_t title_text_color;
    nx_color_t border_color;
    nx_color_t background_color;
    uint32_t title_bar_height;
    uint32_t border_width;
    uint32_t corner_radius;
} nx_window_style_t;

/* Window structure */
struct nx_window {
    char* title;
    nx_rect_t frame;           /* Outer frame including title bar */
    nx_rect_t content_rect;    /* Inner content area */
    nx_window_flags_t flags;
    nx_window_style_t style;
    nx_widget_t* root_widget;  /* Root widget for content */
    bool focused;
    bool visible;
    bool dirty;
    int32_t z_order;
    void* user_data;
    nx_window_t* next;
};

/* Window manager */
struct nx_window_manager {
    nx_context_t* ctx;
    nx_window_t* windows;      /* Linked list sorted by z-order */
    nx_window_t* focused;
    nx_window_t* dragging;
    nx_point_t drag_offset;
    uint32_t screen_width;
    uint32_t screen_height;
};

/* Default window style */
nx_window_style_t nx_window_default_style(void);

/* Window manager */
nx_window_manager_t* nx_wm_create(nx_context_t* ctx);
void nx_wm_destroy(nx_window_manager_t* wm);

/* Window lifecycle */
nx_window_t* nx_window_create(const char* title, nx_rect_t frame, nx_window_flags_t flags);
void nx_window_destroy(nx_window_t* win);

/* Window manager operations */
void nx_wm_add_window(nx_window_manager_t* wm, nx_window_t* win);
void nx_wm_remove_window(nx_window_manager_t* wm, nx_window_t* win);
void nx_wm_focus_window(nx_window_manager_t* wm, nx_window_t* win);
nx_window_t* nx_wm_window_at(nx_window_manager_t* wm, nx_point_t pos);

/* Window operations */
void nx_window_set_title(nx_window_t* win, const char* title);
void nx_window_set_root_widget(nx_window_t* win, nx_widget_t* widget);
void nx_window_move(nx_window_t* win, nx_point_t pos);
void nx_window_resize(nx_window_t* win, nx_size_t size);
void nx_window_show(nx_window_t* win);
void nx_window_hide(nx_window_t* win);
void nx_window_minimize(nx_window_t* win);
void nx_window_maximize(nx_window_t* win, nx_window_manager_t* wm);

/* Rendering */
void nx_wm_render(nx_window_manager_t* wm);
void nx_window_render(nx_window_t* win, nx_context_t* ctx);

/* Event handling */
bool nx_wm_handle_event(nx_window_manager_t* wm, nx_event_t* event);

#ifdef __cplusplus
}
#endif
#endif
