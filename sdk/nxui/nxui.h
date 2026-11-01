/*
 * NeolyxOS REOX SDK - NxUI
 *
 * The REOX-visible UI API.  REOX (reoxc 1.1.x) can express `int`, `float`,
 * `bool` and `string` in `extern fn` signatures but no pointers or function
 * types, so NxUI exposes nxrender through validated 64-bit integer handles
 * and routes button activity to exported REOX hook functions.
 *
 * Declare in REOX exactly as:
 *     extern fn nxui_label_create(text: string) -> int;
 * (`int` is int64_t, `float` is double, `string` is const char*.)
 *
 * Handles: 0 is never valid.  Every call checks the handle's slot, kind and
 * generation; a stale or foreign handle is rejected and logged.
 *
 * Hooks the application may export (all optional):
 *     pub fn rx_ui_on_action(action: int) -> void;   button with that action id clicked
 *     pub fn rx_ui_on_key(key: int) -> void;         ASCII key pressed in the window
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXUI_H
#define NXUI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t nxui_handle_t;

/* Diagnostics */
void nxui_log(const char* message);
int64_t nxui_error_count(void);

/* Window */
nxui_handle_t nxui_window_create(const char* title, int64_t width, int64_t height);
int64_t nxui_window_set_root(nxui_handle_t window, nxui_handle_t widget);
int64_t nxui_window_set_background(nxui_handle_t window, int64_t rgb);
/* Let the user resize (and tile) the window; windows are not resizable by
 * default. Call before nxui_run. 0, or -1 for a bad handle. */
int64_t nxui_window_set_resizable(nxui_handle_t window, bool resizable);

/* Containers */
nxui_handle_t nxui_vstack(int64_t gap);
nxui_handle_t nxui_hstack(int64_t gap);
nxui_handle_t nxui_grid(int64_t cols, int64_t gap);
int64_t nxui_grid_set_cell_size(nxui_handle_t grid, int64_t cell_w, int64_t cell_h);
int64_t nxui_add(nxui_handle_t container, nxui_handle_t child);
int64_t nxui_widget_set_background(nxui_handle_t widget, int64_t rgb);

/* Label */
nxui_handle_t nxui_label_create(const char* text);
int64_t nxui_label_set_text(nxui_handle_t label, const char* text);
/* current text; "" (and a logged error) for an invalid handle */
const char* nxui_label_get_text(nxui_handle_t label);
int64_t nxui_label_set_font_size(nxui_handle_t label, int64_t size);
int64_t nxui_label_set_color(nxui_handle_t label, int64_t rgb);
/* align: 0 left, 1 center, 2 right */
int64_t nxui_label_set_align(nxui_handle_t label, int64_t align);

/* Button */
nxui_handle_t nxui_button_create(const char* label);
int64_t nxui_button_set_action(nxui_handle_t button, int64_t action_id);
int64_t nxui_button_set_colors(nxui_handle_t button, int64_t bg_rgb, int64_t fg_rgb);
int64_t nxui_button_set_font_size(nxui_handle_t button, int64_t size);

/* Application
 *
 * nxui_run shows the window in its own window on the desktop's window
 * server (libnxwindow): the server draws the title bar and close button,
 * the app's widgets fill the content. It returns when the user closes the
 * window, after nxui_quit(), or at once (with a logged error) when no window
 * server is reachable. Returns the number of bridge errors during the run,
 * or -1 for a bad handle. An idle app draws nothing. */
int64_t nxui_run(nxui_handle_t window);
void nxui_quit(void);

/*
 * Text formatting.  Returned strings live in a ring of NXUI_FMT_SLOTS static
 * buffers: each result stays valid until NXUI_FMT_SLOTS further formatting
 * calls have been made.  Copy with nxui_label_set_text before that.
 */
#define NXUI_FMT_SLOTS 8
#define NXUI_FMT_LEN   64

/* value with up to `max_decimals` (0..15) fractional digits, trailing zeros trimmed;
 * NaN -> "NaN", infinities -> "Infinity"/"-Infinity", |v| >= 1e18 -> d.ddde+NN */
const char* nxui_format_number(double value, int64_t max_decimals);
/* exact rendering of a typed number: mantissa / 10^frac_digits, with an
 * explicit trailing "." when show_dot is set and frac_digits is 0 */
const char* nxui_format_input(int64_t mantissa, int64_t frac_digits, bool show_dot, bool negative);
const char* nxui_concat(const char* a, const char* b);

/* Hooks exported by the REOX application (weak; absent hooks are reported once). */
void rx_ui_on_action(int64_t action) __attribute__((weak));
void rx_ui_on_key(int64_t key) __attribute__((weak));

#ifdef __cplusplus
}
#endif
#endif /* NXUI_H */
