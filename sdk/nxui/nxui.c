/*
 * NeolyxOS REOX SDK - NxUI implementation
 *
 * Integer-handle facade over gui/nxrender_c reox_ffi plus the NeolyxOS
 * platform host: every NxUI app runs in its own window on the desktop's
 * window server through libnxwindow (sdk/nxwindow/nxwindow.h), spec
 * docs/desktop/proposals/libnxwindow.md, Decision 3.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxui.h"
#include "reox_ffi.h"
#include <stddef.h>

#ifdef __NEOLYXOS__
#include "nxsyscall.h"
#include "nxwindow.h"
#include "nxgfx/nxgfx.h"
#include <stdlib.h>
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
    bool resizable;          /* windows: the user may resize it (default no) */
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
            g_slots[i].resizable = false;
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
    g_slots[i].resizable = false;
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

int64_t nxui_window_set_resizable(nxui_handle_t window, bool resizable) {
    nxui_slot_t* w = resolve(window, NXUI_KIND_WINDOW, "nxui_window_set_resizable");
    if (!w) return -1;
    w->resizable = resizable;
    reox_window_set_resizable(w->ptr, resizable);
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

/* ============ Platform host (NeolyxOS): a window on the window server ============ */

#ifdef __NEOLYXOS__

#define NXUI_EVQ        64      /* translated events waiting for the bridge */
#define NXUI_FRAME_MS   1000    /* the server presents within a tick or two */

typedef struct {
    char     title[NXW_TITLE_MAX];
    uint32_t width, height;
    bool     resizable;
} nxui_win_req_t;

static nxui_win_req_t g_req;            /* the window nxui_run is showing */
static nxw_window_t* g_win = NULL;
static nxw_frame_t g_frame;             /* the buffer the bridge draws into */
static bool g_frame_begun = false;
static bool g_resize_queued = false;    /* a REOX_INPUT_RESIZE is in g_evq */
static reox_ffi_input_event_t g_evq[NXUI_EVQ];
static uint32_t g_evq_head = 0, g_evq_count = 0;
static uint64_t g_evq_drops = 0;

static void nxui_fail_code(const char* fn, const char* reason, int code) {
    char msg[96];
    size_t pos = 0;
    const char* r = reason ? reason : "error";
    while (*r && pos + 1 < sizeof msg) msg[pos++] = *r++;
    const char* sep = " (";
    while (*sep && pos + 1 < sizeof msg) msg[pos++] = *sep++;
    char digits[16];
    int n = 0;
    uint32_t u = code < 0 ? (uint32_t)(-(int64_t)code) : (uint32_t)code;
    do { digits[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 15);
    if (code < 0 && pos + 1 < sizeof msg) msg[pos++] = '-';
    while (n > 0 && pos + 1 < sizeof msg) msg[pos++] = digits[--n];
    if (pos + 1 < sizeof msg) msg[pos++] = ')';
    msg[pos] = '\0';
    nxui_fail(fn, msg);
}

static void evq_push(const reox_ffi_input_event_t* ev) {
    if (g_evq_count == NXUI_EVQ) {
        /* Full: the oldest pointer motion goes first, else the oldest event */
        uint32_t victim = 0;
        for (uint32_t k = 0; k < g_evq_count; k++) {
            reox_ffi_input_event_t* e = &g_evq[(g_evq_head + k) % NXUI_EVQ];
            if (e->type == REOX_INPUT_POINTER_ABS) { victim = k; break; }
        }
        if (g_evq[(g_evq_head + victim) % NXUI_EVQ].type == REOX_INPUT_RESIZE) g_resize_queued = false;
        for (uint32_t k = victim; k + 1 < g_evq_count; k++)
            g_evq[(g_evq_head + k) % NXUI_EVQ] = g_evq[(g_evq_head + k + 1) % NXUI_EVQ];
        g_evq_count--;
        if (g_evq_drops++ == 0) nxui_fail("nxui_run", "input queue full: dropping events");
    }
    g_evq[(g_evq_head + g_evq_count) % NXUI_EVQ] = *ev;
    g_evq_count++;
}

static void evq_clear(void) {
    g_evq_head = g_evq_count = 0;
    g_resize_queued = false;
}

/* Turn one libnxwindow event into bridge events (queued). Returns 1 when
 * it was the FRAME_DONE of our window. */
static int translate(const nxw_event_t* e) {
    reox_ffi_input_event_t ev;
    for (size_t i = 0; i < sizeof ev; i++) ((volatile uint8_t*)&ev)[i] = 0;
    if (e->type != NXW_EV_DISCONNECTED && e->type != NXW_EV_ERROR && e->window != g_win) return 0;
    switch (e->type) {
    case NXW_EV_FRAME:
        return 1;
    case NXW_EV_CONFIGURE:
        /* A new size: one RESIZE, which gets its buffer when the bridge
         * takes it (nxw_frame_begin then makes the new buffer). Focus
         * and tiling changes alone need no redraw. */
        if ((e->configure.width != g_frame.width || e->configure.height != g_frame.height) && !g_resize_queued) {
            ev.type = REOX_INPUT_RESIZE;
            evq_push(&ev);
            g_resize_queued = true;
        }
        return 0;
    case NXW_EV_KEY:
        ev.type = REOX_INPUT_KEY;
        ev.keycode = e->key.ascii;
        ev.scancode = e->key.scancode;
        ev.pressed = e->key.pressed != 0;
        evq_push(&ev);
        return 0;
    case NXW_EV_POINTER:
        if (e->pointer.kind == NXW_PTR_WHEEL) return 0;     /* nxrender widgets do not scroll yet */
        ev.type = REOX_INPUT_POINTER_ABS;
        ev.x = e->pointer.kind == NXW_PTR_LEAVE ? -1 : e->pointer.x;
        ev.y = e->pointer.kind == NXW_PTR_LEAVE ? -1 : e->pointer.y;
        ev.buttons = e->pointer.buttons & (NXW_BUTTON_LEFT | NXW_BUTTON_RIGHT | NXW_BUTTON_MIDDLE);
        evq_push(&ev);
        return 0;
    case NXW_EV_CLOSE:
        ev.type = REOX_INPUT_CLOSE;
        evq_push(&ev);
        return 0;
    case NXW_EV_ERROR:
        nxui_fail_code("nxui_run", "the window server refused a request", e->error.code);
        return 0;
    case NXW_EV_DISCONNECTED:
        nxui_fail("nxui_run", "the window server went away; closing the app");
        g_frame_begun = false;
        ev.type = REOX_INPUT_CLOSE;
        evq_push(&ev);
        return 0;
    default:
        return 0;
    }
}

/* Hand the oldest queued event to the bridge. A RESIZE gets its buffer
 * here. Returns 1, or 0 when nothing could be delivered. */
static int evq_deliver(reox_ffi_input_event_t* out) {
    while (g_evq_count > 0) {
        *out = g_evq[g_evq_head];
        g_evq_head = (g_evq_head + 1) % NXUI_EVQ;
        g_evq_count--;
        if (out->type == REOX_INPUT_RESIZE) {
            g_resize_queued = false;
            nxw_frame_t f;
            int r = nxw_frame_begin(g_win, &f);
            if (r != 0) {
                /* Logged by libnxwindow; keep drawing into the current buffer */
                nxui_fail_code("nxui_run", "no buffer for the new window size", r);
                continue;
            }
            g_frame = f;
            g_frame_begun = true;
            out->framebuffer = f.pixels;
            out->width = f.width;
            out->height = f.height;
            out->pitch = f.stride;
        } else if (out->type == REOX_INPUT_KEY && out->pressed && out->keycode != 0 && rx_ui_on_key) {
            rx_ui_on_key((int64_t)out->keycode);
        }
        return 1;
    }
    return 0;
}

static void* host_map_framebuffer(uint32_t* w, uint32_t* h, uint32_t* pitch) {
    if (!w || !h || !pitch) return NULL;
    nxw_display_info_t info;
    int r = nxw_connect(&info);
    if (r != 0) {
        nxui_fail_code("nxui_run", "cannot reach the window server (is the desktop running?)", r);
        return NULL;
    }
    nxw_window_desc_t d;
    d.title = g_req.title;
    d.width = g_req.width <= info.max_size ? g_req.width : info.max_size;
    d.height = g_req.height <= info.max_size ? g_req.height : info.max_size;
    d.x = NXW_POS_AUTO;
    d.y = NXW_POS_AUTO;
    d.flags = g_req.resizable ? NXW_WIN_RESIZABLE : 0;
    r = nxw_window_create(&d, &g_win);
    if (r != 0) {
        nxui_fail_code("nxui_run", "the window server refused the window", r);
        nxw_disconnect();
        g_win = NULL;
        return NULL;
    }
    r = nxw_frame_begin(g_win, &g_frame);
    if (r != 0) {
        nxui_fail_code("nxui_run", "no buffer for the window", r);
        nxw_disconnect();
        g_win = NULL;
        return NULL;
    }
    g_frame_begun = true;
    *w = g_frame.width;
    *h = g_frame.height;
    *pitch = g_frame.stride;
    return g_frame.pixels;
}

/* ============ Memory pressure (K2) ============ */
/*
 * When the kernel reports WARN or CRITICAL, NxUI gives the unused top of
 * its heap back with nx_malloc_trim(). The pressure handle is waited on
 * next to the window server connection, so an idle app reacts too. If the
 * kernel has no pressure notices (ABI < 1.3) or the handle cannot be
 * opened, the app runs as before and the reason is logged once.
 */
static nx_handle_t g_pressure = NX_HANDLE_INVALID;
static bool g_pressure_tried = false;
static int g_pressure_level = NX_MEM_NORMAL;

static void nxui_out_dec(const char* before, uint64_t v, const char* after) {
    char buf[128];
    size_t pos = 0;
    const char* s = before ? before : "";
    while (*s && pos + 1 < sizeof buf) buf[pos++] = *s++;
    char digits[21];
    int n = 0;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 20);
    while (n > 0 && pos + 1 < sizeof buf) buf[pos++] = digits[--n];
    s = after ? after : "";
    while (*s && pos + 1 < sizeof buf) buf[pos++] = *s++;
    buf[pos] = '\0';
    nxui_out(buf);
}

static void nxui_pressure_trim(int level) {
    uint64_t released = nx_malloc_trim();
    nxui_out_dec(level >= (int)NX_MEM_CRITICAL ? "[NXUI] memory CRITICAL: returned "
                                          : "[NXUI] memory WARN: returned ",
                 released / 1024u, " KB of heap to the system\n");
}

static void nxui_pressure_close(void) {
    if (g_pressure != NX_HANDLE_INVALID) nx_handle_close(g_pressure);
    g_pressure = NX_HANDLE_INVALID;
}

/* Read the level after the handle became READABLE; trim when it rose to
 * WARN or above. */
static void nxui_pressure_changed(void) {
    int lvl = nx_mem_pressure_read(g_pressure);
    if (lvl < 0) {
        nxui_fail_code("nxui_run", "memory pressure read failed; no heap trims from now on", lvl);
        nxui_pressure_close();
        return;
    }
    int was = g_pressure_level;
    g_pressure_level = lvl;
    if (lvl >= (int)NX_MEM_WARN && lvl > was) nxui_pressure_trim(lvl);
}

static void nxui_pressure_open(void) {
    if (g_pressure_tried) return;
    g_pressure_tried = true;
    nx_abi_info_t abi;
    int r = nx_abi_query(&abi);
    if (r != 0 || !(abi.flags & NX_ABI_FLAG_MEMCLASS)) {
        nxui_out("[NXUI] kernel has no memory pressure notices; heap is not trimmed on WARN\n");
        return;
    }
    nx_handle_t h = nx_mem_pressure_open();
    if (h <= 0) {
        nxui_fail_code("nxui_run", "cannot open a memory pressure handle; heap is not trimmed on WARN", (int)h);
        return;
    }
    g_pressure = h;
    int lvl = nx_mem_pressure_read(h);
    if (lvl < 0) {
        nxui_fail_code("nxui_run", "memory pressure read failed; heap is not trimmed on WARN", lvl);
        nxui_pressure_close();
        return;
    }
    g_pressure_level = (int)NX_MEM_NORMAL;
    if (lvl >= (int)NX_MEM_WARN) nxui_pressure_trim(lvl);
    g_pressure_level = lvl;
}

/* nxw_wait_event that also wakes for memory pressure changes. Same
 * returns: 1 an event, 0 timeout, negative errno. */
static int nxui_wait(nxw_event_t* e, uint64_t ms) {
    if (g_pressure == NX_HANDLE_INVALID) return nxw_wait_event(e, ms);
    uint64_t start = nx_gettime();
    for (;;) {
        int r = nxw_poll_event(e);
        if (r != 0) return r;
        uint64_t left = NX_WAIT_FOREVER;
        if (ms != NX_WAIT_FOREVER) {
            uint64_t spent = nx_gettime() - start;
            if (spent >= ms) return 0;
            left = ms - spent;
        }
        nx_wait_item_t it[2];
        it[0].handle = nxw_connection_handle();
        it[0].signals = NX_SIGNAL_READABLE | NX_SIGNAL_PEER_CLOSED;
        it[1].handle = g_pressure;
        it[1].signals = NX_SIGNAL_READABLE;
        for (int i = 0; i < 2; i++) { it[i].observed = 0; it[i].reserved = 0; }
        int w = nx_obj_wait(it, 2, left);
        if (w == -NXW_ETIMEDOUT) return 0;
        if (w < 0) {
            /* Keep the app working: drop the pressure handle and wait on
             * the connection alone */
            nxui_fail_code("nxui_run", "waiting with the memory pressure handle failed; no heap trims from now on", w);
            nxui_pressure_close();
            return nxw_wait_event(e, left);
        }
        if (it[1].observed & NX_SIGNAL_READABLE) nxui_pressure_changed();
        /* connection readable or closed: the next poll reports it */
    }
}

static int host_poll_input(reox_ffi_input_event_t* ev) {
    if (!ev) return -1;
    if (evq_deliver(ev)) return 1;
    nxw_event_t e;
    int r;
    while ((r = nxw_poll_event(&e)) == 1) {
        translate(&e);
        if (evq_deliver(ev)) return 1;
    }
    return 0;           /* -NXW_ENOTCONN: the CLOSE was queued and delivered */
}

static int host_wait_event(reox_ffi_input_event_t* ev, uint32_t timeout_ms) {
    if (!ev) return -1;
    if (evq_deliver(ev)) return 1;
    uint64_t limit = timeout_ms == REOX_WAIT_FOREVER ? NX_WAIT_FOREVER : (uint64_t)timeout_ms;
    uint64_t start = nx_gettime();
    for (;;) {
        uint64_t left = limit;
        if (limit != NX_WAIT_FOREVER) {
            uint64_t spent = nx_gettime() - start;
            if (spent >= limit) return 0;
            left = limit - spent;
        }
        nxw_event_t e;
        int r = nxui_wait(&e, left);
        if (r == 0) return 0;
        if (r < 0) {
            nxui_fail_code("nxui_run", "waiting for window events failed", r);
            return -1;
        }
        translate(&e);
        if (evq_deliver(ev)) return 1;
    }
}

/* Commit the whole buffer and wait until it is free again (FRAME_DONE).
 * Events that arrive meanwhile are queued for the bridge. */
static int host_present(void* fb) {
    (void)fb;
    if (!g_frame_begun) {
        nxui_fail("nxui_run", "present without a buffer");
        return -1;
    }
    int r = nxw_frame_commit(g_win, NULL, 0);
    g_frame_begun = false;
    if (r != 0) {
        nxui_fail_code("nxui_run", "commit failed", r);
        return -1;
    }
    uint64_t start = nx_gettime();
    for (;;) {
        uint64_t spent = nx_gettime() - start;
        if (spent >= NXUI_FRAME_MS) {
            nxui_fail("nxui_run", "no FRAME_DONE within 1 s");
            return -1;
        }
        nxw_event_t e;
        r = nxui_wait(&e, NXUI_FRAME_MS - spent);
        if (r < 0) return -1;           /* disconnected: CLOSE is queued */
        if (r == 1 && translate(&e)) break;
    }
    /* A new size gets its buffer when the RESIZE is delivered */
    if (g_resize_queued) return 0;
    r = nxw_frame_begin(g_win, &g_frame);
    if (r != 0) {
        nxui_fail_code("nxui_run", "the buffer did not come back", r);
        return -1;
    }
    g_frame_begun = true;
    return 0;
}

static void host_sleep_ms(uint32_t ms) {
    nx_sleep(ms);
}

static void host_log(const char* msg) {
    nx_debug_print(msg);
}

/* Copy the title, cut at 63 bytes on a UTF-8 character boundary. */
static void req_title(const char* t) {
    size_t n = 0;
    while (t[n] && n < NXW_TITLE_MAX - 1) n++;
    if (t[n] != '\0') {
        while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--;   /* not inside a character */
        nxui_fail("nxui_run", "window title longer than 63 bytes; shortened");
    }
    for (size_t i = 0; i < n; i++) g_req.title[i] = t[i];
    for (size_t i = n; i < NXW_TITLE_MAX; i++) g_req.title[i] = '\0';
}

/*
 * UI typeface (owner decision, R1): NX Sans Text is read from the boot
 * disk once per process and handed to NXGFX, which then draws outline
 * text through NXCanvas. If the file is missing or rejected the app still
 * runs, with the 8x16 bitmap font, and the reason is logged.
 */
#define NXUI_FONT_PATH       "/System/Fonts/NXSans-Text.ttf"
#define NXUI_FONT_MAX_BYTES  (4u * 1024u * 1024u)

static bool g_font_tried = false;

static void nxui_load_ui_font(void) {
    if (g_font_tried) return;
    g_font_tried = true;

    int fd = nx_open(NXUI_FONT_PATH, O_RDONLY);
    if (fd < 0) {
        nxui_fail_code("nxui_run", "cannot open " NXUI_FONT_PATH "; using the bitmap font", fd);
        return;
    }
    int64_t size = nx_seek(fd, 0, SEEK_END);
    if (size <= 0 || (uint64_t)size > NXUI_FONT_MAX_BYTES || nx_seek(fd, 0, SEEK_SET) != 0) {
        nx_close(fd);
        nxui_fail_code("nxui_run", "font file size not acceptable; using the bitmap font", (int)size);
        return;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)size);
    if (!data) {
        nx_close(fd);
        nxui_fail("nxui_run", "no memory for the UI font; using the bitmap font");
        return;
    }
    int64_t done = 0;
    while (done < size) {
        int64_t n = nx_read(fd, data + done, (uint64_t)(size - done));
        if (n <= 0) break;
        done += n;
    }
    nx_close(fd);
    if (done != size) {
        free(data);
        nxui_fail("nxui_run", "short read on the UI font; using the bitmap font");
        return;
    }
    if (nxgfx_font_register(data, (uint32_t)size) != 0) {
        free(data);
        nxui_fail("nxui_run", "UI font rejected by NXGFX; using the bitmap font");
        return;
    }
    /* data stays allocated for the life of the process: the face points into it */
    nxui_out("[NXUI] UI font: NX Sans Text (outline)\n");
}

static int nxui_install_host(const nxui_slot_t* w) {
    const char* title = reox_window_get_title(w->ptr);
    int width = reox_window_get_width(w->ptr);
    int height = reox_window_get_height(w->ptr);
    if (width < 1 || height < 1) { nxui_fail("nxui_run", "window has no size"); return -1; }
    req_title(title ? title : "");
    g_req.width = (uint32_t)width;
    g_req.height = (uint32_t)height;
    g_req.resizable = w->resizable;
    nxgfx_set_log_sink(host_log);
    nxui_load_ui_font();
    nxui_pressure_open();
    g_win = NULL;
    g_frame_begun = false;
    evq_clear();
    reox_ffi_host_t host = {
        .map_framebuffer = host_map_framebuffer,
        .poll_input = host_poll_input,
        .present = host_present,
        .set_cursor = NULL,             /* the window server draws the cursor */
        .sleep_ms = host_sleep_ms,
        .log = host_log,
        .windowed = true,
        .wait_event = host_wait_event,
    };
    return reox_ffi_set_host(&host);
}

static void nxui_remove_host(void) {
    if (g_win) nxw_window_destroy(g_win);
    g_win = NULL;
    g_frame_begun = false;
    evq_clear();
    nxw_disconnect();
}

#else /* development host: no window server; the loop refuses to start */

static int nxui_install_host(const nxui_slot_t* w) {
    (void)w;
    reox_ffi_set_log_sink(nxui_out);
    nxui_fail("nxui_run", "no NeolyxOS platform host on this build; UI cannot run here");
    return -1;
}

static void nxui_remove_host(void) {}

#endif

/* ============ Application ============ */

int64_t nxui_run(nxui_handle_t window) {
    nxui_slot_t* w = resolve(window, NXUI_KIND_WINDOW, "nxui_run");
    if (!w) return -1;
    if (nxui_install_host(w) != 0) return -1;
    uint32_t before = reox_ffi_error_count();
    reox_app_run(w->ptr);
    nxui_remove_host();
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
