/*
 * NeolyxOS - REOX FFI Bridge
 * Maps REOX extern functions to nxrender widgets
 * Copyright (c) 2025 KetiveeAI
 *
 * Every entry point validates its arguments.  A rejected call is reported
 * through the log sink (reox_ffi_set_log_sink / host->log) and recorded in
 * reox_ffi_last_error(); nothing fails silently.
 *
 * Handles are nxrender object pointers typed as void*.  The REOX-facing
 * layer (sdk/nxui) wraps them in validated integer handles; C callers that
 * use this header directly must pass handles obtained from this API only.
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
void reox_window_set_background(reox_window_t win, reox_color_t color);

/* Button widget - maps to controls.reox */
reox_button_t reox_button_create(const char* label);
void reox_button_set_label(reox_button_t btn, const char* label);
void reox_button_set_enabled(reox_button_t btn, bool enabled);
void reox_button_set_on_click(reox_button_t btn, void (*callback)(void*), void* data);
void reox_button_set_colors(reox_button_t btn, reox_color_t background, reox_color_t text);
void reox_button_set_font_size(reox_button_t btn, int size);

/* Label widget */
reox_label_t reox_label_create(const char* text);
void reox_label_set_text(reox_label_t lbl, const char* text);
const char* reox_label_get_text(reox_label_t lbl);
void reox_label_set_color(reox_label_t lbl, reox_color_t color);
void reox_label_set_font_size(reox_label_t lbl, int size);
/* align: 0 = left, 1 = center, 2 = right */
void reox_label_set_align(reox_label_t lbl, int align);

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
/* Equal-cell grid: children fill cells row-major, `cols` per row. */
reox_widget_t reox_grid(int cols, int gap);
void reox_grid_set_cell_size(reox_widget_t grid, int cell_w, int cell_h);
void reox_container_add(reox_widget_t container, reox_widget_t child);
void reox_widget_set_background(reox_widget_t widget, reox_color_t color);

/* Application lifecycle */
void reox_app_run(reox_window_t main_window);
void reox_app_quit(void);

/* Utility */
void reox_print(const char* text);

/* ============ Platform integration ============ */

/* Input events delivered by the host (platform layer) to the bridge loop. */
enum {
    REOX_INPUT_NONE = 0,
    REOX_INPUT_MOUSE_MOVE = 1,
    REOX_INPUT_MOUSE_BUTTON = 2,
    REOX_INPUT_KEY = 3
};

typedef struct reox_ffi_input_event {
    uint32_t type;       /* REOX_INPUT_* */
    int32_t  dx, dy;     /* relative mouse motion */
    uint32_t buttons;    /* bit0 left, bit1 right, bit2 middle */
    uint32_t keycode;    /* ASCII when printable, else 0 */
    uint32_t scancode;   /* raw scancode */
    bool     pressed;    /* key pressed (true) or released */
} reox_ffi_input_event_t;

/*
 * Host services for the standalone (own-process) run loop.  Provided by the
 * platform layer (sdk/nxui on NeolyxOS).  map_framebuffer, poll_input,
 * present and sleep_ms are mandatory; set_cursor and log are optional.
 */
typedef struct reox_ffi_host {
    void* (*map_framebuffer)(uint32_t* width, uint32_t* height, uint32_t* pitch);
    int   (*poll_input)(reox_ffi_input_event_t* ev);     /* >0 event, 0 none, <0 error */
    int   (*present)(void* framebuffer);                  /* 0 on success */
    void  (*set_cursor)(int32_t x, int32_t y, bool visible);
    void  (*sleep_ms)(uint32_t ms);
    void  (*log)(const char* msg);
} reox_ffi_host_t;

/* Register the host; returns 0, or -1 if a mandatory callback is missing. */
int reox_ffi_set_host(const reox_ffi_host_t* host);

/*
 * Embedded mode (called by the desktop shell): hand the bridge an existing
 * nx_context_t / nx_window_manager_t.  reox_app_run then only registers the
 * window and returns; the desktop loop drives rendering and input.
 */
void reox_ffi_init(void* nx_context, void* nx_window_manager, uint32_t screen_w, uint32_t screen_h);

/* Diagnostics */
void reox_ffi_set_log_sink(void (*sink)(const char* msg));
const char* reox_ffi_last_error(void);
uint32_t reox_ffi_error_count(void);

#ifdef __cplusplus
}
#endif
#endif /* REOX_FFI_H */
