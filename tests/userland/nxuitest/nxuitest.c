/*
 * NeolyxOS nxuitest - an NxUI app in its own window
 *
 * Checks docs/desktop/proposals/libnxwindow.md (week 4, step 2): NxUI, the
 * UI layer REOX apps use, runs in a window on the desktop's window server
 * through libnxwindow. This program uses the NxUI C API (sdk/nxui/nxui.h)
 * exactly as REOX code does, so it needs no REOX compiler:
 *
 *   - builds a window with a title label, a status label, a 3x2 grid of
 *     buttons and a "keys" label, and makes it resizable;
 *   - checks NxUI's handle validation (bad handles are refused);
 *   - runs it with nxui_run: the window server draws the title bar and the
 *     close button, clicks on the buttons call rx_ui_on_action and update
 *     the status label, typed keys call rx_ui_on_key and show in the keys
 *     label;
 *   - nxui_run returns when the window is closed; the test then checks
 *     that no bridge error happened during the run.
 *
 * Output: "[NXUITEST] PASS/FAIL ..." lines, the actions and keys it saw,
 * and "[NXUITEST] RESULT: PASS" or "RESULT: FAIL". Exit code 0 or 1.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "nxsyscall.h"
#include "nxui.h"

static int g_failed;
static nxui_handle_t g_status, g_keys;
static int64_t g_clicks, g_keycount;
static char g_typed[24];
static int g_typed_len;

static void say(const char *a, int64_t v, const char *b) {
    char line[160];
    int n = 0;
    const char *p = "[NXUITEST] ";
    while (*p && n < 150) line[n++] = *p++;
    while (a && *a && n < 150) line[n++] = *a++;
    if (v != INT64_MIN) {
        char d[24];
        int k = 0;
        uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
        do { d[k++] = (char)('0' + u % 10); u /= 10; } while (u && k < 22);
        if (v < 0 && n < 150) line[n++] = '-';
        while (k > 0 && n < 150) line[n++] = d[--k];
    }
    while (b && *b && n < 150) line[n++] = *b++;
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

static void check(int ok, const char *what, int64_t got) {
    say(ok ? "PASS " : "FAIL ", INT64_MIN, what);
    say("  got ", got, "");
    if (!ok) g_failed = 1;
}

/* Called by NxUI when a button with an action id is clicked */
void rx_ui_on_action(int64_t action) {
    g_clicks++;
    say("action ", action, "");
    const char *s = nxui_concat("Button ", nxui_format_number((double)action, 0));
    s = nxui_concat(s, ", clicks ");
    s = nxui_concat(s, nxui_format_number((double)g_clicks, 0));
    nxui_label_set_text(g_status, s);
}

/* Called by NxUI for every typed character */
void rx_ui_on_key(int64_t key) {
    g_keycount++;
    say("key ", key, "");
    if (key >= 32 && key < 127) {
        if (g_typed_len >= (int)sizeof(g_typed) - 1) g_typed_len = 0;
        g_typed[g_typed_len++] = (char)key;
        g_typed[g_typed_len] = '\0';
        nxui_label_set_text(g_keys, nxui_concat("Keys: ", g_typed));
    }
}

int main(void) {
    say("started, PID ", syscall0(SYS_GETPID), "");

    /* Handle validation: each bad call is refused (and logged by NxUI) */
    check(nxui_window_set_resizable(0, true) == -1, "a zero window handle is refused", 0);
    check(nxui_window_set_resizable(0x700000001LL, true) == -1, "a stale window handle is refused", 0);

    nxui_handle_t win = nxui_window_create("NxUI test", 360, 300);
    check(win > 0, "nxui_window_create", win);
    if (win <= 0) { say("RESULT: FAIL", INT64_MIN, ""); return 1; }
    nxui_handle_t lbl = nxui_label_create("x");
    check(nxui_window_set_resizable(lbl, true) == -1, "a label is not a window", 0);
    check(nxui_window_set_resizable(win, true) == 0, "nxui_window_set_resizable", 0);
    int64_t baseline = nxui_error_count();

    nxui_handle_t root = nxui_vstack(10);
    nxui_widget_set_background(root, 0x1C2230);
    nxui_handle_t title = nxui_label_create("NxUI in its own window");
    nxui_label_set_font_size(title, 18);
    nxui_label_set_color(title, 0xF0F0F0);
    g_status = nxui_label_create("Click a button");
    nxui_label_set_color(g_status, 0x9FD0FF);
    nxui_handle_t grid = nxui_grid(3, 8);
    nxui_grid_set_cell_size(grid, 100, 44);
    static const char *names[6] = { "One", "Two", "Three", "Four", "Five", "Six" };
    for (int i = 0; i < 6; i++) {
        nxui_handle_t b = nxui_button_create(names[i]);
        nxui_button_set_action(b, i + 1);
        nxui_button_set_colors(b, i % 2 ? 0x3A4A6A : 0x5A3A6A, 0xFFFFFF);
        nxui_add(grid, b);
    }
    g_keys = nxui_label_create("Keys: ");
    nxui_label_set_color(g_keys, 0xE0C080);
    nxui_add(root, title);
    nxui_add(root, g_status);
    nxui_add(root, grid);
    nxui_add(root, g_keys);
    nxui_window_set_root(win, root);
    nxui_window_set_background(win, 0x1C2230);
    check(nxui_error_count() == baseline, "the UI builds without errors", nxui_error_count() - baseline);

    say("window open: click the buttons, type, resize it, then close it", INT64_MIN, "");
    int64_t run_errors = nxui_run(win);
    check(run_errors == 0, "nxui_run: no bridge errors during the run", run_errors);
    say("clicks seen ", g_clicks, "");
    say("keys seen ", g_keycount, "");

    say(g_failed ? "RESULT: FAIL" : "RESULT: PASS", INT64_MIN, "");
    return g_failed ? 1 : 0;
}
