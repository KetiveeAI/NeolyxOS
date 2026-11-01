/*
 * NeolyxOS - REOX FFI Bridge Implementation
 * Bridges REOX language to nxrender widget system
 * Copyright (c) 2025 KetiveeAI
 *
 * Two operating modes:
 *   Embedded   - reox_ffi_init() was called by the desktop shell with its
 *                context and window manager.  reox_app_run() registers the
 *                window and returns; the shell's loop renders and dispatches.
 *   Standalone - a platform host (reox_ffi_set_host) supplies framebuffer,
 *                input, present and sleep.  reox_app_run() owns the loop
 *                until reox_app_quit() or the window close control is hit.
 *
 * Every public function validates its arguments and reports a rejected call
 * through reox_ffi_fail(); nothing returns NULL or a default silently.
 */

#include "reox_ffi.h"
#include "nxrender.h"
#include "widgets/container.h"
#include "widgets/slider.h"
#include "widgets/checkbox.h"
#include <string.h>
#include <stdlib.h>

/* ============ Limits ============ */

#define REOX_MAX_DIMENSION   16384
#define REOX_MIN_DIMENSION   1
#define REOX_MAX_TEXT_LEN    4096
#define REOX_MAX_FONT_SIZE   256
#define REOX_MAX_GAP         1024
#define REOX_MAX_GRID_COLS   64
#define REOX_MAX_CHECKBOX_CB 64
#define REOX_FRAME_SLEEP_MS  16
#define REOX_LOG_BUF         256

/* ============ Bridge state ============ */

static uint32_t g_screen_width = 1280;
static uint32_t g_screen_height = 800;
static nx_window_manager_t* g_wm = NULL;
static nx_context_t* g_ctx = NULL;
static bool g_embedded = false;
static bool g_owns_wm = false;

static reox_ffi_host_t g_host;
static bool g_has_host = false;

static volatile bool g_quit_requested = false;
static bool g_running = false;

static void (*g_log_sink)(const char* msg) = NULL;
static char g_last_error[REOX_LOG_BUF];
static uint32_t g_error_count = 0;

static nx_color_t g_desktop_bg = { 18, 18, 22, 255 };

/* vtables of containers created through this bridge (for type checks) */
static const nx_widget_vtable_t* g_vbox_vtable = NULL;
static const nx_widget_vtable_t* g_hbox_vtable = NULL;

/* ============ Diagnostics ============ */

static void reox_log_raw(const char* msg) {
    if (!msg) return;
    if (g_host.log && g_has_host) {
        g_host.log(msg);
    } else if (g_log_sink) {
        g_log_sink(msg);
    }
}

static void append_str(char* dst, size_t cap, size_t* pos, const char* s) {
    if (!dst || !pos || !s) return;
    while (*s && *pos + 1 < cap) {
        dst[(*pos)++] = *s++;
    }
    dst[*pos] = '\0';
}

/* Record and emit a validation failure: "[REOX_FFI] fn: reason". */
static void reox_ffi_fail(const char* fn, const char* reason) {
    char buf[REOX_LOG_BUF];
    size_t pos = 0;
    buf[0] = '\0';
    append_str(buf, sizeof buf, &pos, "[REOX_FFI] ");
    append_str(buf, sizeof buf, &pos, fn ? fn : "?");
    append_str(buf, sizeof buf, &pos, ": ");
    append_str(buf, sizeof buf, &pos, reason ? reason : "error");
    append_str(buf, sizeof buf, &pos, "\n");

    memcpy(g_last_error, buf, pos + 1);
    if (g_error_count < UINT32_MAX) g_error_count++;
    reox_log_raw(buf);
}

void reox_ffi_set_log_sink(void (*sink)(const char* msg)) {
    g_log_sink = sink;
}

const char* reox_ffi_last_error(void) {
    return g_last_error;
}

uint32_t reox_ffi_error_count(void) {
    return g_error_count;
}

/* ============ Validation helpers ============ */

static bool valid_dimension(int v) {
    return v >= REOX_MIN_DIMENSION && v <= REOX_MAX_DIMENSION;
}

static bool valid_text(const char* s, const char* fn) {
    if (!s) {
        reox_ffi_fail(fn, "text is NULL");
        return false;
    }
    size_t n = 0;
    while (s[n] != '\0') {
        if (++n > REOX_MAX_TEXT_LEN) {
            reox_ffi_fail(fn, "text exceeds REOX_MAX_TEXT_LEN");
            return false;
        }
    }
    return true;
}

static inline nx_point_t nx_point(int32_t x, int32_t y) {
    return (nx_point_t){ x, y };
}
static inline nx_size_t nx_size(uint32_t w, uint32_t h) {
    return (nx_size_t){ w, h };
}
static inline nx_color_t to_nx_color(reox_color_t c) {
    return nx_rgba(c.r, c.g, c.b, c.a);
}

/* ============ Window Functions ============ */

reox_window_t reox_window_create(const char* title, int w, int h) {
    if (!valid_text(title, "reox_window_create")) return NULL;
    if (!valid_dimension(w) || !valid_dimension(h)) {
        reox_ffi_fail("reox_window_create", "width/height out of range");
        return NULL;
    }
    int32_t x = 0, y = 0;
    if ((uint32_t)w < g_screen_width)  x = (int32_t)((g_screen_width - (uint32_t)w) / 2);
    if ((uint32_t)h < g_screen_height) y = (int32_t)((g_screen_height - (uint32_t)h) / 2);
    nx_window_t* win = nx_window_create(title, nx_rect(x, y, (uint32_t)w, (uint32_t)h), NX_WINDOW_RESIZABLE);
    if (!win) {
        reox_ffi_fail("reox_window_create", "nx_window_create failed (out of memory)");
        return NULL;
    }
    return (reox_window_t)win;
}

void reox_window_set_title(reox_window_t win, const char* title) {
    if (!win) { reox_ffi_fail("reox_window_set_title", "window is NULL"); return; }
    if (!valid_text(title, "reox_window_set_title")) return;
    nx_window_set_title((nx_window_t*)win, title);
}

void reox_window_set_size(reox_window_t win, int w, int h) {
    if (!win) { reox_ffi_fail("reox_window_set_size", "window is NULL"); return; }
    if (!valid_dimension(w) || !valid_dimension(h)) {
        reox_ffi_fail("reox_window_set_size", "width/height out of range");
        return;
    }
    nx_window_resize((nx_window_t*)win, nx_size((uint32_t)w, (uint32_t)h));
}

void reox_window_set_position(reox_window_t win, int x, int y) {
    if (!win) { reox_ffi_fail("reox_window_set_position", "window is NULL"); return; }
    if (x < -REOX_MAX_DIMENSION || x > REOX_MAX_DIMENSION ||
        y < -REOX_MAX_DIMENSION || y > REOX_MAX_DIMENSION) {
        reox_ffi_fail("reox_window_set_position", "position out of range");
        return;
    }
    nx_window_move((nx_window_t*)win, nx_point(x, y));
}

void reox_window_set_resizable(reox_window_t win, bool resizable) {
    if (!win) { reox_ffi_fail("reox_window_set_resizable", "window is NULL"); return; }
    nx_window_t* w = (nx_window_t*)win;
    if (resizable) {
        w->flags |= NX_WINDOW_RESIZABLE;
    } else {
        w->flags &= ~NX_WINDOW_RESIZABLE;
    }
}

void reox_window_maximize(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_maximize", "window is NULL"); return; }
    if (!g_wm) { reox_ffi_fail("reox_window_maximize", "no window manager yet (call before reox_app_run)"); return; }
    nx_window_maximize(g_wm, (nx_window_t*)win);
}

void reox_window_minimize(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_minimize", "window is NULL"); return; }
    nx_window_minimize((nx_window_t*)win);
}

void reox_window_restore(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_restore", "window is NULL"); return; }
    nx_window_restore((nx_window_t*)win);
}

void reox_window_fullscreen(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_fullscreen", "window is NULL"); return; }
    nx_window_t* w = (nx_window_t*)win;
    nx_window_move(w, nx_point(0, 0));
    nx_window_resize(w, nx_size(g_screen_width, g_screen_height));
}

void reox_window_center(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_center", "window is NULL"); return; }
    nx_window_t* w = (nx_window_t*)win;
    int32_t x = 0, y = 0;
    if (w->frame.width < g_screen_width)   x = (int32_t)((g_screen_width - w->frame.width) / 2);
    if (w->frame.height < g_screen_height) y = (int32_t)((g_screen_height - w->frame.height) / 2);
    nx_window_move(w, nx_point(x, y));
}

void reox_window_close(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_close", "window is NULL"); return; }
    if (g_wm) nx_wm_remove_window(g_wm, (nx_window_t*)win);
    nx_window_destroy((nx_window_t*)win);
    if (g_running && g_wm && nx_wm_window_count(g_wm) == 0) {
        g_quit_requested = true;
    }
}

const char* reox_window_get_title(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_get_title", "window is NULL"); return ""; }
    const char* t = ((nx_window_t*)win)->title;
    return t ? t : "";
}

int reox_window_get_width(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_get_width", "window is NULL"); return -1; }
    return (int)((nx_window_t*)win)->frame.width;
}

int reox_window_get_height(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_get_height", "window is NULL"); return -1; }
    return (int)((nx_window_t*)win)->frame.height;
}

bool reox_window_is_visible(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_is_visible", "window is NULL"); return false; }
    return ((nx_window_t*)win)->visible;
}

bool reox_window_is_focused(reox_window_t win) {
    if (!win) { reox_ffi_fail("reox_window_is_focused", "window is NULL"); return false; }
    return ((nx_window_t*)win)->focused;
}

void reox_window_set_root(reox_window_t win, reox_widget_t widget) {
    if (!win) { reox_ffi_fail("reox_window_set_root", "window is NULL"); return; }
    if (!widget) { reox_ffi_fail("reox_window_set_root", "widget is NULL"); return; }
    nx_window_set_root_widget((nx_window_t*)win, (nx_widget_t*)widget);
}

void reox_window_set_background(reox_window_t win, reox_color_t color) {
    if (!win) { reox_ffi_fail("reox_window_set_background", "window is NULL"); return; }
    nx_window_t* w = (nx_window_t*)win;
    w->style.background_color = to_nx_color(color);
    w->dirty = true;
}

/* ============ Button Widget ============ */

reox_button_t reox_button_create(const char* label) {
    if (!valid_text(label, "reox_button_create")) return NULL;
    nx_button_t* btn = nx_button_create(label);
    if (!btn) {
        reox_ffi_fail("reox_button_create", "nx_button_create failed (out of memory)");
        return NULL;
    }
    return (reox_button_t)btn;
}

void reox_button_set_label(reox_button_t btn, const char* label) {
    if (!btn) { reox_ffi_fail("reox_button_set_label", "button is NULL"); return; }
    if (!valid_text(label, "reox_button_set_label")) return;
    nx_button_set_label((nx_button_t*)btn, label);
}

void reox_button_set_enabled(reox_button_t btn, bool enabled) {
    if (!btn) { reox_ffi_fail("reox_button_set_enabled", "button is NULL"); return; }
    nx_button_t* b = (nx_button_t*)btn;
    b->base.enabled = enabled;
    if (enabled) {
        nx_widget_clear_state(&b->base, NX_WIDGET_DISABLED);
    } else {
        nx_widget_set_state(&b->base, NX_WIDGET_DISABLED);
    }
}

void reox_button_set_on_click(reox_button_t btn, void (*callback)(void*), void* data) {
    if (!btn) { reox_ffi_fail("reox_button_set_on_click", "button is NULL"); return; }
    if (!callback) { reox_ffi_fail("reox_button_set_on_click", "callback is NULL"); return; }
    nx_button_set_on_click((nx_button_t*)btn, callback, data);
}

void reox_button_set_colors(reox_button_t btn, reox_color_t background, reox_color_t text) {
    if (!btn) { reox_ffi_fail("reox_button_set_colors", "button is NULL"); return; }
    nx_button_t* b = (nx_button_t*)btn;
    nx_button_style_t style = b->style;
    style.background = to_nx_color(background);
    style.text_color = to_nx_color(text);
    /* Derive hover/pressed shades so the state feedback stays visible. */
    uint8_t hr = (uint8_t)(background.r + (255 - background.r) / 6);
    uint8_t hg = (uint8_t)(background.g + (255 - background.g) / 6);
    uint8_t hb = (uint8_t)(background.b + (255 - background.b) / 6);
    style.background_hover = nx_rgba(hr, hg, hb, background.a);
    style.background_pressed = nx_rgba((uint8_t)(background.r * 3 / 4),
                                       (uint8_t)(background.g * 3 / 4),
                                       (uint8_t)(background.b * 3 / 4), background.a);
    nx_button_set_style(b, style);
}

void reox_button_set_font_size(reox_button_t btn, int size) {
    if (!btn) { reox_ffi_fail("reox_button_set_font_size", "button is NULL"); return; }
    if (size < 1 || size > REOX_MAX_FONT_SIZE) { reox_ffi_fail("reox_button_set_font_size", "size out of range"); return; }
    nx_button_t* b = (nx_button_t*)btn;
    nx_button_style_t style = b->style;
    style.font_size = (uint32_t)size;
    nx_button_set_style(b, style);
}

/* ============ Label Widget ============ */

reox_label_t reox_label_create(const char* text) {
    if (!valid_text(text, "reox_label_create")) return NULL;
    nx_label_t* lbl = nx_label_create(text);
    if (!lbl) {
        reox_ffi_fail("reox_label_create", "nx_label_create failed (out of memory)");
        return NULL;
    }
    return (reox_label_t)lbl;
}

void reox_label_set_text(reox_label_t lbl, const char* text) {
    if (!lbl) { reox_ffi_fail("reox_label_set_text", "label is NULL"); return; }
    if (!valid_text(text, "reox_label_set_text")) return;
    nx_label_set_text((nx_label_t*)lbl, text);
}

const char* reox_label_get_text(reox_label_t lbl) {
    if (!lbl) { reox_ffi_fail("reox_label_get_text", "label is NULL"); return ""; }
    const char* t = ((nx_label_t*)lbl)->text;
    return t ? t : "";
}

void reox_label_set_color(reox_label_t lbl, reox_color_t color) {
    if (!lbl) { reox_ffi_fail("reox_label_set_color", "label is NULL"); return; }
    nx_label_t* l = (nx_label_t*)lbl;
    nx_label_style_t style = l->style;
    style.text_color = to_nx_color(color);
    nx_label_set_style(l, style);
}

void reox_label_set_font_size(reox_label_t lbl, int size) {
    if (!lbl) { reox_ffi_fail("reox_label_set_font_size", "label is NULL"); return; }
    if (size < 1 || size > REOX_MAX_FONT_SIZE) { reox_ffi_fail("reox_label_set_font_size", "size out of range"); return; }
    nx_label_t* l = (nx_label_t*)lbl;
    nx_label_style_t style = l->style;
    style.font_size = (uint32_t)size;
    nx_label_set_style(l, style);
}

void reox_label_set_align(reox_label_t lbl, int align) {
    if (!lbl) { reox_ffi_fail("reox_label_set_align", "label is NULL"); return; }
    if (align < 0 || align > 2) { reox_ffi_fail("reox_label_set_align", "align must be 0, 1 or 2"); return; }
    nx_label_t* l = (nx_label_t*)lbl;
    nx_label_style_t style = l->style;
    style.align = (align == 0) ? NX_TEXT_ALIGN_LEFT : (align == 1) ? NX_TEXT_ALIGN_CENTER : NX_TEXT_ALIGN_RIGHT;
    nx_label_set_style(l, style);
}

/* ============ TextField Widget ============ */

reox_textfield_t reox_textfield_create(const char* placeholder) {
    if (!valid_text(placeholder, "reox_textfield_create")) return NULL;
    nx_textfield_t* tf = nx_textfield_create(placeholder);
    if (!tf) {
        reox_ffi_fail("reox_textfield_create", "nx_textfield_create failed (out of memory)");
        return NULL;
    }
    return (reox_textfield_t)tf;
}

void reox_textfield_set_text(reox_textfield_t tf, const char* text) {
    if (!tf) { reox_ffi_fail("reox_textfield_set_text", "textfield is NULL"); return; }
    if (!valid_text(text, "reox_textfield_set_text")) return;
    nx_textfield_set_text((nx_textfield_t*)tf, text);
}

const char* reox_textfield_get_text(reox_textfield_t tf) {
    if (!tf) { reox_ffi_fail("reox_textfield_get_text", "textfield is NULL"); return ""; }
    const char* t = nx_textfield_get_text((nx_textfield_t*)tf);
    return t ? t : "";
}

void reox_textfield_set_placeholder(reox_textfield_t tf, const char* placeholder) {
    if (!tf) { reox_ffi_fail("reox_textfield_set_placeholder", "textfield is NULL"); return; }
    if (!valid_text(placeholder, "reox_textfield_set_placeholder")) return;
    nx_textfield_set_placeholder((nx_textfield_t*)tf, placeholder);
}

void reox_textfield_set_password(reox_textfield_t tf, bool is_password) {
    if (!tf) { reox_ffi_fail("reox_textfield_set_password", "textfield is NULL"); return; }
    nx_textfield_set_password_mode((nx_textfield_t*)tf, is_password);
}

void reox_textfield_set_on_change(reox_textfield_t tf, void (*callback)(const char*, void*), void* data) {
    if (!tf) { reox_ffi_fail("reox_textfield_set_on_change", "textfield is NULL"); return; }
    if (!callback) { reox_ffi_fail("reox_textfield_set_on_change", "callback is NULL"); return; }
    nx_textfield_set_on_change((nx_textfield_t*)tf, callback, data);
}

/* ============ Slider Widget ============ */

reox_slider_t reox_slider_create(float min, float max) {
    if (!(min < max)) {   /* also rejects NaN */
        reox_ffi_fail("reox_slider_create", "min must be less than max");
        return NULL;
    }
    nx_slider_t* slider = nx_slider_create(min, max, min);
    if (!slider) {
        reox_ffi_fail("reox_slider_create", "nx_slider_create failed (out of memory)");
        return NULL;
    }
    return (reox_slider_t)slider;
}

void reox_slider_set_value(reox_slider_t sl, float value) {
    if (!sl) { reox_ffi_fail("reox_slider_set_value", "slider is NULL"); return; }
    if (value != value) { reox_ffi_fail("reox_slider_set_value", "value is NaN"); return; }
    nx_slider_set_value((nx_slider_t*)sl, value);
}

float reox_slider_get_value(reox_slider_t sl) {
    if (!sl) { reox_ffi_fail("reox_slider_get_value", "slider is NULL"); return 0.0f; }
    return nx_slider_get_value((nx_slider_t*)sl);
}

void reox_slider_set_on_change(reox_slider_t sl, void (*callback)(float, void*), void* data) {
    if (!sl) { reox_ffi_fail("reox_slider_set_on_change", "slider is NULL"); return; }
    if (!callback) { reox_ffi_fail("reox_slider_set_on_change", "callback is NULL"); return; }
    nx_slider_set_on_change((nx_slider_t*)sl, callback, data);
}

/* ============ Checkbox Widget ============ */

/*
 * nxrender reports (checkbox*, bool, data); REOX expects (bool, data).
 * A fixed-size registry keyed by checkbox pointer adapts the signature
 * without casting between incompatible function types.
 */
typedef struct {
    nx_checkbox_t* cb;
    void (*callback)(bool, void*);
    void* data;
} reox_checkbox_binding_t;

static reox_checkbox_binding_t g_checkbox_bindings[REOX_MAX_CHECKBOX_CB];

static void reox_checkbox_trampoline(nx_checkbox_t* cb, bool checked, void* user_data) {
    (void)user_data;
    if (!cb) return;
    for (size_t i = 0; i < REOX_MAX_CHECKBOX_CB; i++) {
        if (g_checkbox_bindings[i].cb == cb && g_checkbox_bindings[i].callback) {
            g_checkbox_bindings[i].callback(checked, g_checkbox_bindings[i].data);
            return;
        }
    }
    reox_ffi_fail("reox_checkbox_trampoline", "no binding registered for checkbox");
}

reox_checkbox_t reox_checkbox_create(const char* label) {
    if (!valid_text(label, "reox_checkbox_create")) return NULL;
    nx_checkbox_t* cb = nx_checkbox_create(label);
    if (!cb) {
        reox_ffi_fail("reox_checkbox_create", "nx_checkbox_create failed (out of memory)");
        return NULL;
    }
    return (reox_checkbox_t)cb;
}

void reox_checkbox_set_checked(reox_checkbox_t cb, bool checked) {
    if (!cb) { reox_ffi_fail("reox_checkbox_set_checked", "checkbox is NULL"); return; }
    nx_checkbox_set_checked((nx_checkbox_t*)cb, checked);
}

bool reox_checkbox_is_checked(reox_checkbox_t cb) {
    if (!cb) { reox_ffi_fail("reox_checkbox_is_checked", "checkbox is NULL"); return false; }
    return nx_checkbox_is_checked((nx_checkbox_t*)cb);
}

void reox_checkbox_set_on_change(reox_checkbox_t cb, void (*callback)(bool, void*), void* data) {
    if (!cb) { reox_ffi_fail("reox_checkbox_set_on_change", "checkbox is NULL"); return; }
    if (!callback) { reox_ffi_fail("reox_checkbox_set_on_change", "callback is NULL"); return; }
    nx_checkbox_t* box = (nx_checkbox_t*)cb;
    reox_checkbox_binding_t* slot = NULL;
    for (size_t i = 0; i < REOX_MAX_CHECKBOX_CB; i++) {
        if (g_checkbox_bindings[i].cb == box) { slot = &g_checkbox_bindings[i]; break; }
    }
    if (!slot) {
        for (size_t i = 0; i < REOX_MAX_CHECKBOX_CB; i++) {
            if (g_checkbox_bindings[i].cb == NULL) { slot = &g_checkbox_bindings[i]; break; }
        }
    }
    if (!slot) {
        reox_ffi_fail("reox_checkbox_set_on_change", "checkbox binding table full (REOX_MAX_CHECKBOX_CB)");
        return;
    }
    slot->cb = box;
    slot->callback = callback;
    slot->data = data;
    nx_checkbox_set_on_changed(box, reox_checkbox_trampoline, NULL);
}

/* ============ Layout Containers ============ */

reox_widget_t reox_hstack(int gap) {
    if (gap < 0 || gap > REOX_MAX_GAP) { reox_ffi_fail("reox_hstack", "gap out of range"); return NULL; }
    nx_hbox_t* hbox = nx_hbox_create();
    if (!hbox) { reox_ffi_fail("reox_hstack", "nx_hbox_create failed (out of memory)"); return NULL; }
    nx_hbox_set_gap(hbox, (uint32_t)gap);
    nx_hbox_set_align(hbox, NX_ALIGN_STRETCH);
    g_hbox_vtable = hbox->base.vtable;
    return (reox_widget_t)hbox;
}

reox_widget_t reox_vstack(int gap) {
    if (gap < 0 || gap > REOX_MAX_GAP) { reox_ffi_fail("reox_vstack", "gap out of range"); return NULL; }
    nx_vbox_t* vbox = nx_vbox_create();
    if (!vbox) { reox_ffi_fail("reox_vstack", "nx_vbox_create failed (out of memory)"); return NULL; }
    nx_vbox_set_gap(vbox, (uint32_t)gap);
    nx_vbox_set_align(vbox, NX_ALIGN_STRETCH);
    g_vbox_vtable = vbox->base.vtable;
    return (reox_widget_t)vbox;
}

/* ---- Grid container (equal cells, row-major) ---- */

typedef struct {
    nx_widget_t base;
    uint32_t cols;
    uint32_t gap;
    uint32_t cell_w;
    uint32_t cell_h;
    nx_color_t background;
} reox_grid_widget_t;

static uint32_t grid_rows(const reox_grid_widget_t* g) {
    if (g->base.child_count == 0) return 0;
    return (uint32_t)((g->base.child_count + g->cols - 1) / g->cols);
}

static void grid_render(nx_widget_t* self, nx_context_t* ctx) {
    reox_grid_widget_t* g = (reox_grid_widget_t*)self;
    if (g->background.a > 0) nxgfx_fill_rect(ctx, self->bounds, g->background);
}

static void grid_layout(nx_widget_t* self, nx_rect_t bounds) {
    reox_grid_widget_t* g = (reox_grid_widget_t*)self;
    self->bounds = bounds;
    uint32_t rows = grid_rows(g);
    if (rows == 0) return;
    uint32_t gaps_w = g->gap * (g->cols - 1);
    uint32_t gaps_h = g->gap * (rows - 1);
    uint32_t cw = (bounds.width > gaps_w) ? (bounds.width - gaps_w) / g->cols : 0;
    uint32_t ch = (bounds.height > gaps_h) ? (bounds.height - gaps_h) / rows : 0;
    for (size_t i = 0; i < self->child_count; i++) {
        uint32_t r = (uint32_t)(i / g->cols);
        uint32_t c = (uint32_t)(i % g->cols);
        nx_rect_t cell = {
            bounds.x + (int32_t)(c * (cw + g->gap)),
            bounds.y + (int32_t)(r * (ch + g->gap)),
            cw, ch
        };
        nx_widget_layout(self->children[i], cell);
    }
}

static nx_size_t grid_measure(nx_widget_t* self, nx_size_t available) {
    (void)available;
    reox_grid_widget_t* g = (reox_grid_widget_t*)self;
    uint32_t rows = grid_rows(g);
    if (rows == 0) return (nx_size_t){0, 0};
    return (nx_size_t){
        g->cols * g->cell_w + g->gap * (g->cols - 1),
        rows * g->cell_h + g->gap * (rows - 1)
    };
}

static void grid_destroy(nx_widget_t* self) { free(self); }

static const nx_widget_vtable_t GRID_VTABLE = {
    .render = grid_render, .layout = grid_layout,
    .measure = grid_measure, .handle_event = NULL, .destroy = grid_destroy
};

reox_widget_t reox_grid(int cols, int gap) {
    if (cols < 1 || cols > REOX_MAX_GRID_COLS) { reox_ffi_fail("reox_grid", "cols out of range"); return NULL; }
    if (gap < 0 || gap > REOX_MAX_GAP) { reox_ffi_fail("reox_grid", "gap out of range"); return NULL; }
    reox_grid_widget_t* g = (reox_grid_widget_t*)calloc(1, sizeof(reox_grid_widget_t));
    if (!g) { reox_ffi_fail("reox_grid", "out of memory"); return NULL; }
    nx_widget_init(&g->base, &GRID_VTABLE);
    g->cols = (uint32_t)cols;
    g->gap = (uint32_t)gap;
    g->cell_w = 64;
    g->cell_h = 48;
    g->background = (nx_color_t){0, 0, 0, 0};
    return (reox_widget_t)g;
}

void reox_grid_set_cell_size(reox_widget_t grid, int cell_w, int cell_h) {
    if (!grid) { reox_ffi_fail("reox_grid_set_cell_size", "grid is NULL"); return; }
    nx_widget_t* w = (nx_widget_t*)grid;
    if (w->vtable != &GRID_VTABLE) { reox_ffi_fail("reox_grid_set_cell_size", "widget is not a grid"); return; }
    if (!valid_dimension(cell_w) || !valid_dimension(cell_h)) { reox_ffi_fail("reox_grid_set_cell_size", "cell size out of range"); return; }
    reox_grid_widget_t* g = (reox_grid_widget_t*)grid;
    g->cell_w = (uint32_t)cell_w;
    g->cell_h = (uint32_t)cell_h;
}

void reox_container_add(reox_widget_t container, reox_widget_t child) {
    if (!container) { reox_ffi_fail("reox_container_add", "container is NULL"); return; }
    if (!child) { reox_ffi_fail("reox_container_add", "child is NULL"); return; }
    if (container == child) { reox_ffi_fail("reox_container_add", "cannot add a widget to itself"); return; }
    nx_widget_t* c = (nx_widget_t*)child;
    if (c->parent) { reox_ffi_fail("reox_container_add", "child already has a parent"); return; }
    nx_widget_add_child((nx_widget_t*)container, c);
}

void reox_widget_set_background(reox_widget_t widget, reox_color_t color) {
    if (!widget) { reox_ffi_fail("reox_widget_set_background", "widget is NULL"); return; }
    nx_widget_t* w = (nx_widget_t*)widget;
    nx_color_t c = to_nx_color(color);
    if (w->vtable == &GRID_VTABLE) {
        ((reox_grid_widget_t*)w)->background = c;
        return;
    }
    if (g_vbox_vtable && w->vtable == g_vbox_vtable) { ((nx_vbox_t*)w)->background = c; return; }
    if (g_hbox_vtable && w->vtable == g_hbox_vtable) { ((nx_hbox_t*)w)->background = c; return; }
    reox_ffi_fail("reox_widget_set_background", "widget is not a container (vstack/hstack/grid)");
}

/* ============ Platform host ============ */

int reox_ffi_set_host(const reox_ffi_host_t* host) {
    if (!host) { reox_ffi_fail("reox_ffi_set_host", "host is NULL"); return -1; }
    if (!host->map_framebuffer || !host->poll_input || !host->present || !host->sleep_ms) {
        reox_ffi_fail("reox_ffi_set_host", "map_framebuffer, poll_input, present and sleep_ms are mandatory");
        return -1;
    }
    g_host = *host;
    g_has_host = true;
    return 0;
}

void reox_ffi_init(void* nx_context, void* nx_window_manager, uint32_t w, uint32_t h) {
    if (!nx_context || !nx_window_manager) {
        reox_ffi_fail("reox_ffi_init", "context or window manager is NULL");
        return;
    }
    if (w < REOX_MIN_DIMENSION || w > REOX_MAX_DIMENSION || h < REOX_MIN_DIMENSION || h > REOX_MAX_DIMENSION) {
        reox_ffi_fail("reox_ffi_init", "screen size out of range");
        return;
    }
    g_ctx = (nx_context_t*)nx_context;
    g_wm = (nx_window_manager_t*)nx_window_manager;
    g_screen_width = w;
    g_screen_height = h;
    g_embedded = true;
    g_owns_wm = false;
}

/* ============ Standalone run loop ============ */

typedef struct {
    int32_t x, y;
    uint32_t buttons;
} reox_pointer_t;

static bool point_in_close_control(const nx_window_t* win, nx_point_t p) {
    if (!win || (win->flags & NX_WINDOW_BORDERLESS)) return false;
    int32_t cx = win->frame.x + (int32_t)win->frame.width - 20;
    int32_t cy = win->frame.y + 16;
    int32_t dx = p.x - cx, dy = p.y - cy;
    return (dx * dx + dy * dy) <= (10 * 10);
}

/* Forward an event to the focused window's root widget in local coordinates. */
static void dispatch_to_focused(nx_event_t* ev) {
    if (!g_wm || !g_wm->focused || !g_wm->focused->root_widget) return;
    nx_window_t* win = g_wm->focused;
    if (!win->visible) return;
    nx_event_t local = *ev;
    local.pos.x -= win->content_rect.x;
    local.pos.y -= win->content_rect.y;
    nx_widget_handle_event(win->root_widget, &local);
}

static void handle_host_event(const reox_ffi_input_event_t* in, reox_pointer_t* ptr) {
    nx_event_t ev;
    memset(&ev, 0, sizeof ev);

    switch (in->type) {
    case REOX_INPUT_MOUSE_MOVE:
    case REOX_INPUT_MOUSE_BUTTON:
    case REOX_INPUT_POINTER_ABS: {
        bool moved;
        if (in->type == REOX_INPUT_POINTER_ABS) {
            /* Windowed: content-local, and outside the window while a
             * button is held there, so it is not clamped */
            if (in->x < -REOX_MAX_DIMENSION || in->x > REOX_MAX_DIMENSION ||
                in->y < -REOX_MAX_DIMENSION || in->y > REOX_MAX_DIMENSION) {
                reox_ffi_fail("reox_app_run", "host delivered a pointer position out of range");
                break;
            }
            moved = in->x != ptr->x || in->y != ptr->y;
            ptr->x = in->x;
            ptr->y = in->y;
        } else {
            int64_t nx = (int64_t)ptr->x + in->dx;
            int64_t ny = (int64_t)ptr->y + in->dy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx >= (int64_t)g_screen_width)  nx = (int64_t)g_screen_width - 1;
            if (ny >= (int64_t)g_screen_height) ny = (int64_t)g_screen_height - 1;
            ptr->x = (int32_t)nx;
            ptr->y = (int32_t)ny;
            moved = in->dx != 0 || in->dy != 0 || in->type == REOX_INPUT_MOUSE_MOVE;
        }
        ev.pos = nx_point(ptr->x, ptr->y);

        if (moved) {
            ev.type = NX_EVENT_MOUSE_MOVE;
            if (!nx_wm_handle_event(g_wm, &ev)) dispatch_to_focused(&ev);
        }

        uint32_t was = ptr->buttons & 1u;
        uint32_t now = in->buttons & 1u;
        if (now && !was) {
            nx_window_t* hit = nx_wm_window_at(g_wm, ev.pos);
            if (hit && point_in_close_control(hit, ev.pos)) {
                reox_log_raw("[REOX_FFI] window close control activated\n");
                g_quit_requested = true;
            } else {
                ev.type = NX_EVENT_MOUSE_DOWN;
                ev.button = NX_MOUSE_LEFT;
                nx_wm_handle_event(g_wm, &ev);
            }
        } else if (!now && was) {
            ev.type = NX_EVENT_MOUSE_UP;
            ev.button = NX_MOUSE_LEFT;
            if (!nx_wm_handle_event(g_wm, &ev)) dispatch_to_focused(&ev);
        }
        ptr->buttons = in->buttons;
        break;
    }
    case REOX_INPUT_KEY:
        ev.type = in->pressed ? NX_EVENT_KEY_DOWN : NX_EVENT_KEY_UP;
        ev.keycode = in->keycode ? in->keycode : in->scancode;
        if (in->keycode >= 32 && in->keycode < 127) {
            ev.text[0] = (char)in->keycode;
            ev.text[1] = '\0';
        }
        ev.pos = nx_point(ptr->x, ptr->y);
        if (g_wm->focused && g_wm->focused->root_widget) {
            nx_widget_handle_event(g_wm->focused->root_widget, &ev);
        }
        break;
    case REOX_INPUT_CLOSE:
        reox_log_raw("[REOX_FFI] window close requested\n");
        g_quit_requested = true;
        break;
    default:
        reox_ffi_fail("reox_app_run", "host delivered unknown input event type");
        break;
    }
}

static void render_frame(void* framebuffer, reox_pointer_t* ptr) {
    nxgfx_clear(g_ctx, g_desktop_bg);
    nx_wm_render(g_wm);
    nxgfx_present(g_ctx);
    if (g_host.set_cursor) g_host.set_cursor(ptr->x, ptr->y, true);
    if (g_host.present(framebuffer) != 0) {
        reox_ffi_fail("reox_app_run", "host present failed");
    }
}

/* Windowed: the window is the whole buffer, without chrome. */
static void windowed_fit(nx_window_t* win, uint32_t w, uint32_t h) {
    win->flags |= NX_WINDOW_BORDERLESS;
    win->flags &= ~(NX_WINDOW_MINIMIZED | NX_WINDOW_MAXIMIZED);
    win->frame = nx_rect(0, 0, w, h);
    win->content_rect = win->frame;
    if (win->root_widget) nx_widget_layout(win->root_widget, win->content_rect);
    win->dirty = true;
}

/* Windowed: switch drawing to the buffer a RESIZE event carries.
 * Returns the new buffer, or NULL (logged; the loop stops). */
static void* windowed_resize(const reox_ffi_input_event_t* in, nx_window_t* win) {
    if (!in->framebuffer || !valid_dimension((int)in->width) || !valid_dimension((int)in->height) ||
        in->pitch < in->width * 4u) {
        reox_ffi_fail("reox_app_run", "host delivered an invalid RESIZE buffer");
        return NULL;
    }
    nx_context_t* ctx = nxgfx_init(in->framebuffer, in->width, in->height, in->pitch);
    if (!ctx) {
        reox_ffi_fail("reox_app_run", "nxgfx_init failed for the resized buffer");
        return NULL;
    }
    nxgfx_destroy(g_ctx);
    g_ctx = ctx;
    g_wm->ctx = ctx;
    g_screen_width = in->width;
    g_screen_height = in->height;
    g_wm->screen_width = in->width;
    g_wm->screen_height = in->height;
    windowed_fit(win, in->width, in->height);
    return in->framebuffer;
}

/* Windowed: render after input or a resize, sleep otherwise. */
static void windowed_loop(void* fb, nx_window_t* win, reox_pointer_t* ptr) {
    bool dirty = true;
    while (!g_quit_requested) {
        if (dirty) {
            render_frame(fb, ptr);
            dirty = false;
        }
        reox_ffi_input_event_t ev;
        memset(&ev, 0, sizeof ev);
        int got;
        if (g_host.wait_event) {
            got = g_host.wait_event(&ev, REOX_WAIT_FOREVER);
        } else {
            got = g_host.poll_input(&ev);
            if (got == 0) g_host.sleep_ms(REOX_FRAME_SLEEP_MS);
        }
        uint32_t guard = 0;
        while (got > 0 && guard++ < 256 && !g_quit_requested) {
            if (ev.type == REOX_INPUT_RESIZE) {
                void* nfb = windowed_resize(&ev, win);
                if (!nfb) { g_quit_requested = true; break; }
                fb = nfb;
            } else {
                handle_host_event(&ev, ptr);
            }
            dirty = true;
            memset(&ev, 0, sizeof ev);
            got = g_host.poll_input(&ev);
        }
        if (got < 0) {
            reox_ffi_fail("reox_app_run", "host input failed; stopping");
            break;
        }
    }
}

void reox_app_run(reox_window_t main_window) {
    if (!main_window) { reox_ffi_fail("reox_app_run", "main window is NULL"); return; }
    nx_window_t* win = (nx_window_t*)main_window;

    if (g_embedded) {
        if (!g_wm) { reox_ffi_fail("reox_app_run", "embedded mode without window manager"); return; }
        nx_window_show(win);
        nx_wm_add_window(g_wm, win);
        nx_wm_focus_window(g_wm, win);
        return;
    }

    if (g_running) { reox_ffi_fail("reox_app_run", "already running"); return; }
    if (!g_has_host) {
        reox_ffi_fail("reox_app_run", "no platform host registered (reox_ffi_set_host) and not embedded");
        return;
    }

    uint32_t fb_w = 0, fb_h = 0, fb_pitch = 0;
    void* fb = g_host.map_framebuffer(&fb_w, &fb_h, &fb_pitch);
    if (!fb || fb_w < REOX_MIN_DIMENSION || fb_h < REOX_MIN_DIMENSION ||
        fb_w > REOX_MAX_DIMENSION || fb_h > REOX_MAX_DIMENSION || fb_pitch < fb_w * 4) {
        reox_ffi_fail("reox_app_run", "host returned an invalid framebuffer");
        return;
    }
    g_screen_width = fb_w;
    g_screen_height = fb_h;

    g_ctx = nxgfx_init(fb, fb_w, fb_h, fb_pitch);
    if (!g_ctx) { reox_ffi_fail("reox_app_run", "nxgfx_init failed"); return; }
    g_wm = nx_wm_create(g_ctx);
    if (!g_wm) {
        reox_ffi_fail("reox_app_run", "nx_wm_create failed");
        nxgfx_destroy(g_ctx);
        g_ctx = NULL;
        return;
    }
    g_owns_wm = true;
    g_wm->screen_width = fb_w;
    g_wm->screen_height = fb_h;

    if (g_host.windowed) {
        /* The window server draws the chrome; the window fills its buffer */
        windowed_fit(win, fb_w, fb_h);
    } else {
        /* Re-centre now that the real screen size is known. */
        reox_window_center(win);
    }
    nx_window_show(win);
    nx_wm_add_window(g_wm, win);
    nx_wm_focus_window(g_wm, win);

    reox_pointer_t ptr = { (int32_t)(fb_w / 2), (int32_t)(fb_h / 2), 0 };
    if (g_host.windowed) { ptr.x = -1; ptr.y = -1; }    /* not over the window yet */
    g_quit_requested = false;
    g_running = true;
    reox_log_raw(g_host.windowed ? "[REOX_FFI] windowed loop start\n" : "[REOX_FFI] standalone loop start\n");

    if (g_host.windowed) windowed_loop(fb, win, &ptr);

    while (!g_host.windowed && !g_quit_requested) {
        reox_ffi_input_event_t ev;
        int polled;
        uint32_t guard = 0;
        while ((polled = g_host.poll_input(&ev)) > 0 && guard++ < 256) {
            handle_host_event(&ev, &ptr);
        }
        if (polled < 0) {
            reox_ffi_fail("reox_app_run", "host input poll failed; stopping");
            break;
        }
        render_frame(fb, &ptr);
        g_host.sleep_ms(REOX_FRAME_SLEEP_MS);
    }

    reox_log_raw(g_host.windowed ? "[REOX_FFI] windowed loop end\n" : "[REOX_FFI] standalone loop end\n");
    g_running = false;

    /* Windows still registered are owned by the caller; detach without destroying. */
    while (g_wm->windows) nx_wm_remove_window(g_wm, g_wm->windows);
    nx_wm_destroy(g_wm);
    g_wm = NULL;
    nxgfx_destroy(g_ctx);
    g_ctx = NULL;
    g_owns_wm = false;
}

void reox_app_quit(void) {
    g_quit_requested = true;
    if (!g_running && !g_embedded) {
        reox_log_raw("[REOX_FFI] reox_app_quit called before reox_app_run; next run exits immediately\n");
    }
}

/* ============ Utility ============ */

void reox_print(const char* text) {
    if (!valid_text(text, "reox_print")) return;
    reox_log_raw(text);
}
