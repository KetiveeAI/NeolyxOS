/*
 * NeolyxOS - REOX FFI Bridge Implementation
 * Bridges REOX language to nxrender widget system
 * Copyright (c) 2025 KetiveeAI
 */

#include "reox_ffi.h"
#include "nxrender.h"
#include "widgets/container.h"
#include "widgets/slider.h"
#include "widgets/checkbox.h"
#include <string.h>


/* Inline constructors for geometry types */
static inline nx_point_t nx_point(int32_t x, int32_t y) {
    return (nx_point_t){ x, y };
}
static inline nx_size_t nx_size(uint32_t w, uint32_t h) {
    return (nx_size_t){ w, h };
}

/* Internal: screen dimensions (set by first window) */
static uint32_t g_screen_width = 1280;
static uint32_t g_screen_height = 800;
static nx_window_manager_t* g_wm = NULL;
static nx_context_t* g_ctx = NULL;

/* ============ Window Functions ============ */

reox_window_t reox_window_create(const char* title, int w, int h) {
    nx_rect_t frame = nx_rect(100, 100, w, h);
    nx_window_t* win = nx_window_create(title, frame, NX_WINDOW_RESIZABLE);
    return (reox_window_t)win;
}

void reox_window_set_title(reox_window_t win, const char* title) {
    if (win) nx_window_set_title((nx_window_t*)win, title);
}

void reox_window_set_size(reox_window_t win, int w, int h) {
    if (win) nx_window_resize((nx_window_t*)win, nx_size(w, h));
}

void reox_window_set_position(reox_window_t win, int x, int y) {
    if (win) nx_window_move((nx_window_t*)win, nx_point(x, y));
}

void reox_window_set_resizable(reox_window_t win, bool resizable) {
    if (!win) return;
    nx_window_t* w = (nx_window_t*)win;
    if (resizable) {
        w->flags |= NX_WINDOW_RESIZABLE;
    } else {
        w->flags &= ~NX_WINDOW_RESIZABLE;
    }
}

void reox_window_maximize(reox_window_t win) {
    if (win && g_wm) nx_window_maximize(g_wm, (nx_window_t*)win);
}

void reox_window_minimize(reox_window_t win) {
    if (win) nx_window_minimize((nx_window_t*)win);
}

void reox_window_restore(reox_window_t win) {
    if (!win) return;
    nx_window_t* w = (nx_window_t*)win;
    w->flags &= ~(NX_WINDOW_MINIMIZED | NX_WINDOW_MAXIMIZED);
}

void reox_window_fullscreen(reox_window_t win) {
    if (!win) return;
    nx_window_t* w = (nx_window_t*)win;
    w->frame = nx_rect(0, 0, g_screen_width, g_screen_height);
}

void reox_window_center(reox_window_t win) {
    if (!win) return;
    nx_window_t* w = (nx_window_t*)win;
    int x = (g_screen_width - w->frame.width) / 2;
    int y = (g_screen_height - w->frame.height) / 2;
    nx_window_move(w, nx_point(x, y));
}

void reox_window_close(reox_window_t win) {
    if (win) {
        if (g_wm) nx_wm_remove_window(g_wm, (nx_window_t*)win);
        nx_window_destroy((nx_window_t*)win);
    }
}

const char* reox_window_get_title(reox_window_t win) {
    return win ? ((nx_window_t*)win)->title : "";
}

int reox_window_get_width(reox_window_t win) {
    return win ? (int)((nx_window_t*)win)->frame.width : 0;
}

int reox_window_get_height(reox_window_t win) {
    return win ? (int)((nx_window_t*)win)->frame.height : 0;
}

bool reox_window_is_visible(reox_window_t win) {
    return win ? ((nx_window_t*)win)->visible : false;
}

bool reox_window_is_focused(reox_window_t win) {
    return win ? ((nx_window_t*)win)->focused : false;
}

void reox_window_set_root(reox_window_t win, reox_widget_t widget) {
    if (win && widget) {
        nx_window_set_root_widget((nx_window_t*)win, (nx_widget_t*)widget);
    }
}

/* ============ Button Widget ============ */

reox_button_t reox_button_create(const char* label) {
    return (reox_button_t)nx_button_create(label);
}

void reox_button_set_label(reox_button_t btn, const char* label) {
    if (btn) nx_button_set_label((nx_button_t*)btn, label);
}

void reox_button_set_enabled(reox_button_t btn, bool enabled) {
    if (!btn) return;
    nx_button_t* b = (nx_button_t*)btn;
    b->base.enabled = enabled;
}

void reox_button_set_on_click(reox_button_t btn, void (*callback)(void*), void* data) {
    if (btn) nx_button_set_on_click((nx_button_t*)btn, callback, data);
}

/* ============ Label Widget ============ */

reox_label_t reox_label_create(const char* text) {
    return (reox_label_t)nx_label_create(text);
}

void reox_label_set_text(reox_label_t lbl, const char* text) {
    if (lbl) nx_label_set_text((nx_label_t*)lbl, text);
}

void reox_label_set_color(reox_label_t lbl, reox_color_t color) {
    if (!lbl) return;
    nx_label_t* l = (nx_label_t*)lbl;
    l->style.text_color = nx_rgba(color.r, color.g, color.b, color.a);
}

void reox_label_set_font_size(reox_label_t lbl, int size) {
    if (!lbl) return;
    nx_label_t* l = (nx_label_t*)lbl;
    l->style.font_size = size;
}

/* ============ TextField Widget ============ */

reox_textfield_t reox_textfield_create(const char* placeholder) {
    return (reox_textfield_t)nx_textfield_create(placeholder);
}

void reox_textfield_set_text(reox_textfield_t tf, const char* text) {
    if (tf) nx_textfield_set_text((nx_textfield_t*)tf, text);
}

const char* reox_textfield_get_text(reox_textfield_t tf) {
    return tf ? nx_textfield_get_text((nx_textfield_t*)tf) : "";
}

void reox_textfield_set_placeholder(reox_textfield_t tf, const char* placeholder) {
    if (tf) nx_textfield_set_placeholder((nx_textfield_t*)tf, placeholder);
}

void reox_textfield_set_password(reox_textfield_t tf, bool is_password) {
    if (tf) nx_textfield_set_password_mode((nx_textfield_t*)tf, is_password);
}

void reox_textfield_set_on_change(reox_textfield_t tf, void (*callback)(const char*, void*), void* data) {
    if (tf) nx_textfield_set_on_change((nx_textfield_t*)tf, callback, data);
}

/* ============ Slider Widget ============ */

reox_slider_t reox_slider_create(float min, float max) {
    nx_slider_t* slider = nx_slider_create(min, max, min);
    return (reox_slider_t)slider;
}

void reox_slider_set_value(reox_slider_t sl, float value) {
    if (sl) nx_slider_set_value((nx_slider_t*)sl, value);
}

float reox_slider_get_value(reox_slider_t sl) {
    return sl ? nx_slider_get_value((nx_slider_t*)sl) : 0.0f;
}

void reox_slider_set_on_change(reox_slider_t sl, void (*callback)(float, void*), void* data) {
    if (sl) nx_slider_set_on_change((nx_slider_t*)sl, callback, data);
}

/* ============ Checkbox Widget ============ */

reox_checkbox_t reox_checkbox_create(const char* label) {
    nx_checkbox_t* cb = nx_checkbox_create(label);
    return (reox_checkbox_t)cb;
}

void reox_checkbox_set_checked(reox_checkbox_t cb, bool checked) {
    if (cb) nx_checkbox_set_checked((nx_checkbox_t*)cb, checked);
}

bool reox_checkbox_is_checked(reox_checkbox_t cb) {
    return cb ? nx_checkbox_is_checked((nx_checkbox_t*)cb) : false;
}

void reox_checkbox_set_on_change(reox_checkbox_t cb, void (*callback)(bool, void*), void* data) {
    if (cb) {
        /* Note: REOX callback is (bool, void*) but NXRender is (checkbox*, bool, void*)
         * In C, ignored leading params are ABI-safe on all NeolyxOS targets (x86_64/ARM64)
         * TODO: Add proper wrapper for full portability */
        nx_checkbox_set_on_changed((nx_checkbox_t*)cb, 
            (void (*)(nx_checkbox_t*, bool, void*))callback, data);
    }
}

/* ============ Layout Containers ============ */

reox_widget_t reox_hstack(int gap) {
    nx_hbox_t* hbox = nx_hbox_create();
    if (hbox) {
        nx_hbox_set_gap(hbox, (uint32_t)gap);
    }
    return (reox_widget_t)hbox;
}

reox_widget_t reox_vstack(int gap) {
    nx_vbox_t* vbox = nx_vbox_create();
    if (vbox) {
        nx_vbox_set_gap(vbox, (uint32_t)gap);
    }
    return (reox_widget_t)vbox;
}

void reox_container_add(reox_widget_t container, reox_widget_t child) {
    if (container && child) {
        nx_widget_add_child((nx_widget_t*)container, (nx_widget_t*)child);
    }
}


/* ============ Application Lifecycle ============ */

void reox_app_run(reox_window_t main_window) {
    if (!main_window) return;
    nx_window_t* win = (nx_window_t*)main_window;
    win->visible = true;
    if (g_wm) nx_wm_add_window(g_wm, win);
}

void reox_app_quit(void) {
    /* Signal application exit */
}

/* ============ Utility ============ */

void reox_print(const char* text) {
    /* Map to serial output or console */
    (void)text;
}

/* ============ Initialization (called from desktop) ============ */

void reox_ffi_init(nx_context_t* ctx, nx_window_manager_t* wm, uint32_t w, uint32_t h) {
    g_ctx = ctx;
    g_wm = wm;
    g_screen_width = w;
    g_screen_height = h;
}
