/*
 * NeolyxOS - REOX FFI Bridge
 * Maps REOX extern functions to nxrender widgets
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef REOX_FFI_H
#define REOX_FFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles for REOX */
typedef void* reox_window_t;
typedef void* reox_widget_t;
typedef void* reox_button_t;
typedef void* reox_label_t;
typedef void* reox_textfield_t;
typedef void* reox_slider_t;
typedef void* reox_checkbox_t;

/* Color type for REOX */
typedef struct {
    uint8_t r, g, b, a;
} reox_color_t;

/* Window functions - maps to window.reox extern fn */
reox_window_t reox_window_create(const char* title, int w, int h);
void reox_window_set_title(reox_window_t win, const char* title);
void reox_window_set_size(reox_window_t win, int w, int h);
void reox_window_set_position(reox_window_t win, int x, int y);
void reox_window_set_resizable(reox_window_t win, bool resizable);
void reox_window_maximize(reox_window_t win);
void reox_window_minimize(reox_window_t win);
void reox_window_restore(reox_window_t win);
void reox_window_fullscreen(reox_window_t win);
void reox_window_center(reox_window_t win);
void reox_window_close(reox_window_t win);
const char* reox_window_get_title(reox_window_t win);
int reox_window_get_width(reox_window_t win);
int reox_window_get_height(reox_window_t win);
bool reox_window_is_visible(reox_window_t win);
bool reox_window_is_focused(reox_window_t win);
void reox_window_set_root(reox_window_t win, reox_widget_t widget);

/* Button widget - maps to controls.reox */
reox_button_t reox_button_create(const char* label);
void reox_button_set_label(reox_button_t btn, const char* label);
void reox_button_set_enabled(reox_button_t btn, bool enabled);
void reox_button_set_on_click(reox_button_t btn, void (*callback)(void*), void* data);

/* Label widget */
reox_label_t reox_label_create(const char* text);
void reox_label_set_text(reox_label_t lbl, const char* text);
void reox_label_set_color(reox_label_t lbl, reox_color_t color);
void reox_label_set_font_size(reox_label_t lbl, int size);

/* TextField widget */
reox_textfield_t reox_textfield_create(const char* placeholder);
void reox_textfield_set_text(reox_textfield_t tf, const char* text);
const char* reox_textfield_get_text(reox_textfield_t tf);
void reox_textfield_set_placeholder(reox_textfield_t tf, const char* placeholder);
void reox_textfield_set_password(reox_textfield_t tf, bool is_password);
void reox_textfield_set_on_change(reox_textfield_t tf, void (*callback)(const char*, void*), void* data);

/* Slider widget */
reox_slider_t reox_slider_create(float min, float max);
void reox_slider_set_value(reox_slider_t sl, float value);
float reox_slider_get_value(reox_slider_t sl);
void reox_slider_set_on_change(reox_slider_t sl, void (*callback)(float, void*), void* data);

/* Checkbox widget */
reox_checkbox_t reox_checkbox_create(const char* label);
void reox_checkbox_set_checked(reox_checkbox_t cb, bool checked);
bool reox_checkbox_is_checked(reox_checkbox_t cb);
void reox_checkbox_set_on_change(reox_checkbox_t cb, void (*callback)(bool, void*), void* data);

/* Layout helpers */
reox_widget_t reox_hstack(int gap);
reox_widget_t reox_vstack(int gap);
void reox_container_add(reox_widget_t container, reox_widget_t child);

/* Application lifecycle */
void reox_app_run(reox_window_t main_window);
void reox_app_quit(void);

/* Utility */
void reox_print(const char* text);

#ifdef __cplusplus
}
#endif
#endif /* REOX_FFI_H */
