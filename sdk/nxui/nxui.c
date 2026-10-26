/*
 * NeolyxOS REOX SDK - NxUI implementation
 *
 * Integer-handle facade over gui/nxrender_c reox_ffi plus the NeolyxOS
 * platform host (framebuffer, input, present, sleep via nxsyscall.h).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxui.h"
#include "reox_ffi.h"
#include <stddef.h>

#ifdef __NEOLYXOS__
#include "nxsyscall.h"
#else
#include <stdio.h>
#endif

/* ============ Logging ============ */

static int64_t g_nxui_errors = 0;

static void nxui_out(const char* msg) {
    if (!msg) return;
#ifdef __NEOLYXOS__
    nx_debug_print(msg);
#else
    fputs(msg, stderr);
#endif
}

static void nxui_fail(const char* fn, const char* reason) {
    char buf[192];
    size_t pos = 0;
    const char* parts[5] = { "[NXUI] ", fn ? fn : "?", ": ", reason ? reason : "error", "\n" };
    for (int i = 0; i < 5; i++) {
        const char* s = parts[i];
        while (*s && pos + 1 < sizeof buf) buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    g_nxui_errors++;
    nxui_out(buf);
}

void nxui_log(const char* message) {
    if (!message) { nxui_fail("nxui_log", "message is NULL"); return; }
    nxui_out(message);
}

int64_t nxui_error_count(void) {
    return g_nxui_errors + (int64_t)reox_ffi_error_count();
}

/* ============ Handle table ============ */

typedef enum {
    NXUI_KIND_NONE = 0,
    NXUI_KIND_WINDOW,
    NXUI_KIND_CONTAINER,
    NXUI_KIND_LABEL,
    NXUI_KIND_BUTTON
} nxui_kind_t;

#define NXUI_MAX_HANDLES 512

typedef struct {
    void* ptr;
    nxui_kind_t kind;
    uint16_t generation;
    int64_t action;          /* buttons: action id passed to rx_ui_on_action */
} nxui_slot_t;

static nxui_slot_t g_slots[NXUI_MAX_HANDLES];
static uint32_t g_slot_count = 0;

static nxui_handle_t make_handle(uint32_t index, uint16_t gen) {
    return ((int64_t)gen << 32) | (int64_t)(index + 1);
}

static nxui_handle_t register_handle(void* ptr, nxui_kind_t kind, const char* fn) {
    if (!ptr) return 0;   /* the FFI already logged why */
    for (uint32_t i = 0; i < g_slot_count; i++) {
        if (g_slots[i].kind == NXUI_KIND_NONE) {
            g_slots[i].ptr = ptr;
            g_slots[i].kind = kind;
            g_slots[i].generation++;
            g_slots[i].action = -1;
            return make_handle(i, g_slots[i].generation);
        }
    }
    if (g_slot_count >= NXUI_MAX_HANDLES) {
        nxui_fail(fn, "handle table full (NXUI_MAX_HANDLES)");
        return 0;
    }
    uint32_t i = g_slot_count++;
    g_slots[i].ptr = ptr;
    g_slots[i].kind = kind;
    g_slots[i].generation = 1;
    g_slots[i].action = -1;
    return make_handle(i, 1);
}

static nxui_slot_t* resolve(nxui_handle_t h, nxui_kind_t kind, const char* fn) {
    if (h <= 0) { nxui_fail(fn, "invalid handle (zero or negative)"); return NULL; }
    uint32_t index = (uint32_t)(h & 0xFFFFFFFFu);
    uint16_t gen = (uint16_t)((h >> 32) & 0xFFFF);
    if (index == 0 || index > g_slot_count) { nxui_fail(fn, "handle out of range"); return NULL; }
    nxui_slot_t* s = &g_slots[index - 1];
    if (s->kind == NXUI_KIND_NONE || s->generation != gen) { nxui_fail(fn, "stale handle"); return NULL; }
    if (kind != NXUI_KIND_NONE && s->kind != kind) { nxui_fail(fn, "handle is of the wrong widget kind"); return NULL; }
    return s;
}

static bool check_text(const char* s, const char* fn) {
    if (!s) { nxui_fail(fn, "text is NULL"); return false; }
    return true;
}

static bool check_rgb(int64_t rgb, const char* fn) {
    if (rgb < 0 || rgb > 0xFFFFFF) { nxui_fail(fn, "rgb must be 0x000000..0xFFFFFF"); return false; }
    return true;
}

static reox_color_t rgb_to_color(int64_t rgb) {
    return (reox_color_t){ (uint8_t)((rgb >> 16) & 0xFF), (uint8_t)((rgb >> 8) & 0xFF), (uint8_t)(rgb & 0xFF), 255 };
}

static bool check_int_range(int64_t v, int64_t lo, int64_t hi, const char* fn, const char* what) {
    if (v < lo || v > hi) { nxui_fail(fn, what); return false; }
    return true;
}

/* ============ Window ============ */

nxui_handle_t nxui_window_create(const char* title, int64_t width, int64_t height) {
    if (!check_text(title, "nxui_window_create")) return 0;
    if (!check_int_range(width, 1, 16384, "nxui_window_create", "width out of range")) return 0;
    if (!check_int_range(height, 1, 16384, "nxui_window_create", "height out of range")) return 0;
    return register_handle(reox_window_create(title, (int)width, (int)height), NXUI_KIND_WINDOW, "nxui_window_create");
}

int64_t nxui_window_set_root(nxui_handle_t window, nxui_handle_t widget) {
    nxui_slot_t* w = resolve(window, NXUI_KIND_WINDOW, "nxui_window_set_root");
    nxui_slot_t* r = resolve(widget, NXUI_KIND_NONE, "nxui_window_set_root");
    if (!w || !r) return -1;
    if (r->kind == NXUI_KIND_WINDOW) { nxui_fail("nxui_window_set_root", "root must be a widget, not a window"); return -1; }
    reox_window_set_root(w->ptr, r->ptr);
    return 0;
}

int64_t nxui_window_set_background(nxui_handle_t window, int64_t rgb) {
    nxui_slot_t* w = resolve(window, NXUI_KIND_WINDOW, "nxui_window_set_background");
    if (!w || !check_rgb(rgb, "nxui_window_set_background")) return -1;
    reox_window_set_background(w->ptr, rgb_to_color(rgb));
    return 0;
}

/* ============ Containers ============ */

nxui_handle_t nxui_vstack(int64_t gap) {
    if (!check_int_range(gap, 0, 1024, "nxui_vstack", "gap out of range")) return 0;
    return register_handle(reox_vstack((int)gap), NXUI_KIND_CONTAINER, "nxui_vstack");
}

nxui_handle_t nxui_hstack(int64_t gap) {
    if (!check_int_range(gap, 0, 1024, "nxui_hstack", "gap out of range")) return 0;
    return register_handle(reox_hstack((int)gap), NXUI_KIND_CONTAINER, "nxui_hstack");
}

nxui_handle_t nxui_grid(int64_t cols, int64_t gap) {
    if (!check_int_range(cols, 1, 64, "nxui_grid", "cols out of range")) return 0;
    if (!check_int_range(gap, 0, 1024, "nxui_grid", "gap out of range")) return 0;
    return register_handle(reox_grid((int)cols, (int)gap), NXUI_KIND_CONTAINER, "nxui_grid");
}

int64_t nxui_grid_set_cell_size(nxui_handle_t grid, int64_t cell_w, int64_t cell_h) {
    nxui_slot_t* g = resolve(grid, NXUI_KIND_CONTAINER, "nxui_grid_set_cell_size");
    if (!g) return -1;
    if (!check_int_range(cell_w, 1, 16384, "nxui_grid_set_cell_size", "cell width out of range")) return -1;
    if (!check_int_range(cell_h, 1, 16384, "nxui_grid_set_cell_size", "cell height out of range")) return -1;
    uint32_t before = reox_ffi_error_count();
    reox_grid_set_cell_size(g->ptr, (int)cell_w, (int)cell_h);
    return reox_ffi_error_count() == before ? 0 : -1;
}

int64_t nxui_add(nxui_handle_t container, nxui_handle_t child) {
    nxui_slot_t* c = resolve(container, NXUI_KIND_CONTAINER, "nxui_add");
    nxui_slot_t* k = resolve(child, NXUI_KIND_NONE, "nxui_add");
    if (!c || !k) return -1;
    if (k->kind == NXUI_KIND_WINDOW) { nxui_fail("nxui_add", "a window cannot be a child"); return -1; }
    uint32_t before = reox_ffi_error_count();
    reox_container_add(c->ptr, k->ptr);
    return reox_ffi_error_count() == before ? 0 : -1;
}

int64_t nxui_widget_set_background(nxui_handle_t widget, int64_t rgb) {
    nxui_slot_t* w = resolve(widget, NXUI_KIND_CONTAINER, "nxui_widget_set_background");
    if (!w || !check_rgb(rgb, "nxui_widget_set_background")) return -1;
    uint32_t before = reox_ffi_error_count();
    reox_widget_set_background(w->ptr, rgb_to_color(rgb));
    return reox_ffi_error_count() == before ? 0 : -1;
}

/* ============ Label ============ */

nxui_handle_t nxui_label_create(const char* text) {
    if (!check_text(text, "nxui_label_create")) return 0;
    return register_handle(reox_label_create(text), NXUI_KIND_LABEL, "nxui_label_create");
}

int64_t nxui_label_set_text(nxui_handle_t label, const char* text) {
    nxui_slot_t* l = resolve(label, NXUI_KIND_LABEL, "nxui_label_set_text");
    if (!l || !check_text(text, "nxui_label_set_text")) return -1;
    reox_label_set_text(l->ptr, text);
    return 0;
}

const char* nxui_label_get_text(nxui_handle_t label) {
    nxui_slot_t* l = resolve(label, NXUI_KIND_LABEL, "nxui_label_get_text");
    if (!l) return "";
    return reox_label_get_text(l->ptr);
}

int64_t nxui_label_set_font_size(nxui_handle_t label, int64_t size) {
    nxui_slot_t* l = resolve(label, NXUI_KIND_LABEL, "nxui_label_set_font_size");
    if (!l || !check_int_range(size, 1, 256, "nxui_label_set_font_size", "size out of range")) return -1;
    reox_label_set_font_size(l->ptr, (int)size);
    return 0;
}

int64_t nxui_label_set_color(nxui_handle_t label, int64_t rgb) {
    nxui_slot_t* l = resolve(label, NXUI_KIND_LABEL, "nxui_label_set_color");
    if (!l || !check_rgb(rgb, "nxui_label_set_color")) return -1;
    reox_label_set_color(l->ptr, rgb_to_color(rgb));
    return 0;
}

int64_t nxui_label_set_align(nxui_handle_t label, int64_t align) {
    nxui_slot_t* l = resolve(label, NXUI_KIND_LABEL, "nxui_label_set_align");
    if (!l || !check_int_range(align, 0, 2, "nxui_label_set_align", "align must be 0, 1 or 2")) return -1;
    reox_label_set_align(l->ptr, (int)align);
    return 0;
}

/* ============ Button ============ */

static bool g_warned_no_action_hook = false;

static void nxui_button_trampoline(void* data) {
    nxui_slot_t* s = (nxui_slot_t*)data;
    if (!s || s->kind != NXUI_KIND_BUTTON) {
        nxui_fail("nxui_button_trampoline", "callback data does not name a live button");
        return;
    }
    if (!rx_ui_on_action) {
        if (!g_warned_no_action_hook) {
            g_warned_no_action_hook = true;
            nxui_fail("nxui_button_trampoline", "application exports no rx_ui_on_action(action: int)");
        }
        return;
    }
    rx_ui_on_action(s->action);
}

nxui_handle_t nxui_button_create(const char* label) {
    if (!check_text(label, "nxui_button_create")) return 0;
    return register_handle(reox_button_create(label), NXUI_KIND_BUTTON, "nxui_button_create");
}

int64_t nxui_button_set_action(nxui_handle_t button, int64_t action_id) {
    nxui_slot_t* b = resolve(button, NXUI_KIND_BUTTON, "nxui_button_set_action");
    if (!b) return -1;
    if (action_id < 0) { nxui_fail("nxui_button_set_action", "action id must be >= 0"); return -1; }
    b->action = action_id;
    reox_button_set_on_click(b->ptr, nxui_button_trampoline, b);
    return 0;
}

int64_t nxui_button_set_colors(nxui_handle_t button, int64_t bg_rgb, int64_t fg_rgb) {
    nxui_slot_t* b = resolve(button, NXUI_KIND_BUTTON, "nxui_button_set_colors");
    if (!b || !check_rgb(bg_rgb, "nxui_button_set_colors") || !check_rgb(fg_rgb, "nxui_button_set_colors")) return -1;
    reox_button_set_colors(b->ptr, rgb_to_color(bg_rgb), rgb_to_color(fg_rgb));
    return 0;
}

int64_t nxui_button_set_font_size(nxui_handle_t button, int64_t size) {
    nxui_slot_t* b = resolve(button, NXUI_KIND_BUTTON, "nxui_button_set_font_size");
    if (!b || !check_int_range(size, 1, 256, "nxui_button_set_font_size", "size out of range")) return -1;
    reox_button_set_font_size(b->ptr, (int)size);
    return 0;
}

/* ============ Platform host (NeolyxOS) ============ */

#ifdef __NEOLYXOS__

static fb_info_t g_fb_info;
static void* g_fb_ptr = NULL;

static void* host_map_framebuffer(uint32_t* w, uint32_t* h, uint32_t* pitch) {
    if (!w || !h || !pitch) return NULL;
    if (nx_fb_info(&g_fb_info) != 0 || g_fb_info.base_addr == 0) {
        nxui_fail("host_map_framebuffer", "SYS_FB_INFO failed");
        return NULL;
    }
    if (g_fb_info.bpp != 32) {
        nxui_fail("host_map_framebuffer", "only 32bpp framebuffers are supported");
        return NULL;
    }
    g_fb_ptr = nx_fb_map();
    if (!g_fb_ptr) {
        nxui_fail("host_map_framebuffer", "SYS_FB_MAP failed");
        return NULL;
    }
    *w = g_fb_info.width;
    *h = g_fb_info.height;
    *pitch = g_fb_info.pitch;
    return g_fb_ptr;
}

static int host_poll_input(reox_ffi_input_event_t* ev) {
    if (!ev) return -1;
    input_event_t raw;
    int n = nx_input_poll(&raw);
    if (n <= 0) return n;
    ev->dx = ev->dy = 0;
    ev->buttons = 0;
    ev->keycode = ev->scancode = 0;
    ev->pressed = false;
    switch (raw.type) {
    case INPUT_TYPE_MOUSE_MOVE:
        ev->type = REOX_INPUT_MOUSE_MOVE;
        ev->dx = raw.mouse.dx; ev->dy = raw.mouse.dy; ev->buttons = raw.mouse.buttons;
        return 1;
    case INPUT_TYPE_MOUSE_BUTTON:
        ev->type = REOX_INPUT_MOUSE_BUTTON;
        ev->dx = raw.mouse.dx; ev->dy = raw.mouse.dy; ev->buttons = raw.mouse.buttons;
        return 1;
    case INPUT_TYPE_KEYBOARD:
        ev->type = REOX_INPUT_KEY;
        ev->keycode = raw.keyboard.ascii;
        ev->scancode = raw.keyboard.scancode;
        ev->pressed = raw.keyboard.pressed != 0;
        if (ev->pressed && rx_ui_on_key && raw.keyboard.ascii != 0) {
            rx_ui_on_key((int64_t)raw.keyboard.ascii);
        }
        return 1;
    default:
        nxui_fail("host_poll_input", "kernel delivered unknown input type");
        return 0;
    }
}

static int host_present(void* fb) {
    (void)fb;
    /* Rendering goes straight to the mapped framebuffer; flip with the
     * front buffer so the kernel's cursor layer composes over it. */
    return nx_fb_flip(0) == 0 ? 0 : -1;
}

static void host_set_cursor(int32_t x, int32_t y, bool visible) {
    nx_cursor_set(x, y, visible ? 1 : 0);
}

static void host_sleep_ms(uint32_t ms) {
    nx_sleep(ms);
}

static void host_log(const char* msg) {
    nx_debug_print(msg);
}

static int nxui_install_host(void) {
    nx_input_register(INPUT_TYPE_MOUSE_BUTTON);
    nx_input_register(INPUT_TYPE_KEYBOARD);
    reox_ffi_host_t host = {
        .map_framebuffer = host_map_framebuffer,
        .poll_input = host_poll_input,
        .present = host_present,
        .set_cursor = host_set_cursor,
        .sleep_ms = host_sleep_ms,
        .log = host_log,
    };
    return reox_ffi_set_host(&host);
}

#else /* development host: no framebuffer/input; the loop refuses to start */

static int nxui_install_host(void) {
    reox_ffi_set_log_sink(nxui_out);
    nxui_fail("nxui_run", "no NeolyxOS platform host on this build; UI cannot run here");
    return -1;
}

#endif

/* ============ Application ============ */

int64_t nxui_run(nxui_handle_t window) {
    nxui_slot_t* w = resolve(window, NXUI_KIND_WINDOW, "nxui_run");
    if (!w) return -1;
    if (nxui_install_host() != 0) return -1;
    uint32_t before = reox_ffi_error_count();
    reox_app_run(w->ptr);
    return (int64_t)(reox_ffi_error_count() - before);
}

void nxui_quit(void) {
    reox_app_quit();
}

/* ============ Formatting ============ */

static char g_fmt_ring[NXUI_FMT_SLOTS][NXUI_FMT_LEN];
static uint32_t g_fmt_next = 0;

static char* fmt_slot(void) {
    char* s = g_fmt_ring[g_fmt_next];
    g_fmt_next = (g_fmt_next + 1) % NXUI_FMT_SLOTS;
    s[0] = '\0';
    return s;
}

static size_t put_str(char* dst, size_t pos, const char* s) {
    while (*s && pos + 1 < NXUI_FMT_LEN) dst[pos++] = *s++;
    dst[pos] = '\0';
    return pos;
}

/* Writes the decimal digits of an unsigned value; returns new position. */
static size_t put_u64(char* dst, size_t pos, uint64_t v, int min_digits) {
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && n < 20);
    while (n < min_digits && n < 20) tmp[n++] = '0';
    while (n > 0 && pos + 1 < NXUI_FMT_LEN) dst[pos++] = tmp[--n];
    dst[pos] = '\0';
    return pos;
}

const char* nxui_format_number(double value, int64_t max_decimals) {
    char* out = fmt_slot();
    size_t pos = 0;
    if (max_decimals < 0 || max_decimals > 15) {
        nxui_fail("nxui_format_number", "max_decimals must be 0..15");
        max_decimals = max_decimals < 0 ? 0 : 15;
    }
    if (value != value) return put_str(out, 0, "NaN"), out;
    if (value > 1.7976931348623157e308) return put_str(out, 0, "Infinity"), out;
    if (value < -1.7976931348623157e308) return put_str(out, 0, "-Infinity"), out;

    bool negative = value < 0;
    double mag = negative ? -value : value;

    if (mag >= 1e18) {
        /* d.ddddde+NN */
        int exp10 = 0;
        while (mag >= 10.0 && exp10 < 400) { mag /= 10.0; exp10++; }
        if (negative) pos = put_str(out, pos, "-");
        uint64_t scaled = (uint64_t)(mag * 100000.0 + 0.5);
        if (scaled >= 1000000ULL) { scaled /= 10; exp10++; }
        pos = put_u64(out, pos, scaled / 100000ULL, 1);
        pos = put_str(out, pos, ".");
        pos = put_u64(out, pos, scaled % 100000ULL, 5);
        pos = put_str(out, pos, "e+");
        pos = put_u64(out, pos, (uint64_t)exp10, 2);
        return out;
    }

    uint64_t scale = 1;
    for (int64_t i = 0; i < max_decimals; i++) scale *= 10ULL;
    double scaled_d = mag * (double)scale + 0.5;
    if (scaled_d >= 1.8e19) {
        /* precision request not representable at this magnitude: fall back */
        return nxui_format_number(negative ? -mag : mag, max_decimals > 0 ? max_decimals - 1 : 0);
    }
    uint64_t scaled = (uint64_t)scaled_d;
    uint64_t ipart = scaled / scale;
    uint64_t fpart = scaled % scale;

    if (negative && (ipart != 0 || fpart != 0)) pos = put_str(out, pos, "-");
    pos = put_u64(out, pos, ipart, 1);
    if (fpart != 0) {
        char frac[20];
        int n = (int)max_decimals;
        for (int i = n - 1; i >= 0; i--) { frac[i] = (char)('0' + fpart % 10); fpart /= 10; }
        while (n > 0 && frac[n - 1] == '0') n--;
        frac[n] = '\0';
        pos = put_str(out, pos, ".");
        pos = put_str(out, pos, frac);
    }
    return out;
}

const char* nxui_format_input(int64_t mantissa, int64_t frac_digits, bool show_dot, bool negative) {
    char* out = fmt_slot();
    size_t pos = 0;
    if (mantissa < 0) { nxui_fail("nxui_format_input", "mantissa must be >= 0"); mantissa = -mantissa; }
    if (frac_digits < 0 || frac_digits > 18) { nxui_fail("nxui_format_input", "frac_digits must be 0..18"); frac_digits = 0; }
    uint64_t scale = 1;
    for (int64_t i = 0; i < frac_digits; i++) scale *= 10ULL;
    uint64_t m = (uint64_t)mantissa;
    if (negative) pos = put_str(out, pos, "-");
    pos = put_u64(out, pos, m / scale, 1);
    if (frac_digits > 0) {
        pos = put_str(out, pos, ".");
        pos = put_u64(out, pos, m % scale, (int)frac_digits);
    } else if (show_dot) {
        pos = put_str(out, pos, ".");
    }
    return out;
}

const char* nxui_concat(const char* a, const char* b) {
    char* out = fmt_slot();
    if (!a || !b) { nxui_fail("nxui_concat", "argument is NULL"); return out; }
    size_t pos = put_str(out, 0, a);
    pos = put_str(out, pos, b);
    (void)pos;
    return out;
}
