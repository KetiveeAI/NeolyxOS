/*
 * NeolyxOS - Overview (Mission Control), see overview.h
 *
 * Windows are drawn from the window manager's snapshot cache, so the
 * grid costs one scaled blit per window per frame regardless of how much
 * the apps draw. Snapshots are leased while Overview is open and refreshed
 * at most one window per frame.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/overview.h"
#include "../include/window_manager.h"
#include "../include/stage.h"
#include "../include/desktop_shell.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxicons.h"
#include "../include/nxsyscall.h"

#define OV_TILE_W     150.0f
#define OV_TILE_H      94.0f
#define OV_TILE_GAP    18.0f
#define OV_STRIP_Y     38.0f        /* top of the space tiles */
#define OV_LABEL_H     30.0f        /* icon + title under each window */
#define OV_GAP         36.0f        /* between windows in the grid */
#define OV_MAX_SCALE   0.80f
#define OV_DRAG_PX     6

typedef struct {
    uint32_t   id;
    nxc_rect_t from;     /* where the window is on the desktop */
    nxc_rect_t to;       /* its grid slot */
    nxc_rect_t delta;    /* relayout offset, decays to zero */
    nxc_rect_t real;     /* the window frame (layout sizes and order use it) */
} ov_item_t;

static ov_item_t g_it[MAX_WINDOWS];
static int g_n = 0;
static int g_active = 0;           /* visible (entering, shown or leaving) */
static int g_leaving = 0;
static nxui_spring_t g_t;          /* 0 desktop .. 1 overview */
static int g_hover = -1;           /* item under the pointer */
static int g_space_hover = -1;     /* space tile under the pointer, WM_MAX_SPACES = "+" */
static int g_space_close_hover = 0;

/* Pointer press / drag */
static int g_press = -1;
static int g_press_space = -2;     /* -2 none, -1 background, 0.. tile, WM_MAX_SPACES "+" */
static int g_press_close = 0;
static int32_t g_press_x = 0, g_press_y = 0;
static int g_dragging = 0;
static int32_t g_ptr_x = 0, g_ptr_y = 0;

/* Mini wallpaper for the space tiles (built once, rebuilt on appearance change) */
static nxc_surface_t g_mini;
static int g_mini_ok = 0;
static int g_mini_failed = 0;
static int g_mini_appearance = -1;

static const desktop_window_t *ov_window(uint32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == id) return w;
    }
    return NULL;
}

static nxc_rect_t lerp_rect(nxc_rect_t a, nxc_rect_t b, float t) {
    return nxc_rect(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t);
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Desktop rect of a window: its frame, or its Stage thumbnail if parked. */
static nxc_rect_t ov_desktop_rect(const desktop_window_t *w) {
    nxc_rect_t r;
    if ((wm_window_hidden(w->id) & WM_HIDE_STAGE) && stage_window_thumb_rect(w->id, &r)) return r;
    int32_t x, y;
    uint32_t ww, wh;
    if (wm_shell_get_frame(w->id, &x, &y, &ww, &wh) != 0) {
        x = w->x; y = w->y; ww = w->width; wh = w->height;
    }
    return nxc_rect((float)x, (float)y, (float)ww, (float)wh + (float)WM_TITLEBAR_H);
}

static nxc_rect_t ov_frame_rect(const desktop_window_t *w) {
    int32_t x, y;
    uint32_t ww, wh;
    if (wm_shell_get_frame(w->id, &x, &y, &ww, &wh) != 0) {
        x = w->x; y = w->y; ww = w->width; wh = w->height;
    }
    return nxc_rect((float)x, (float)y, (float)ww, (float)wh + (float)WM_TITLEBAR_H);
}

/* ---- Space tiles ---- */

static int ov_tile_count(void) {
    int n = wm_space_count();
    return n < WM_MAX_SPACES ? n + 1 : n;    /* "+" while more fit */
}

static nxc_rect_t ov_tile_rect(int i) {
    int n = ov_tile_count();
    float plus_w = 64.0f;
    int spaces = wm_space_count();
    float total = (float)spaces * OV_TILE_W + (float)(n - 1) * OV_TILE_GAP + (n > spaces ? plus_w : 0.0f);
    float x = ((float)nxui_width() - total) * 0.5f + (float)i * (OV_TILE_W + OV_TILE_GAP);
    if (i >= spaces) return nxc_rect((float)(int)x, OV_STRIP_Y, plus_w, OV_TILE_H);
    return nxc_rect((float)(int)x, OV_STRIP_Y, OV_TILE_W, OV_TILE_H);
}

/* Tile index under (x, y): 0..spaces-1, WM_MAX_SPACES for "+", or -1. */
static int ov_tile_at(int32_t x, int32_t y) {
    int n = ov_tile_count(), spaces = wm_space_count();
    for (int i = 0; i < n; i++) {
        if (nxglass_hit(ov_tile_rect(i), x, y)) return i < spaces ? i : WM_MAX_SPACES;
    }
    return -1;
}

static nxc_rect_t ov_tile_close_rect(int i) {
    nxc_rect_t r = ov_tile_rect(i);
    return nxc_rect(r.x - 7.0f, r.y - 7.0f, 18.0f, 18.0f);
}

static void ov_build_mini(void) {
    if (g_mini_failed) return;
    if (g_mini_ok && g_mini_appearance == (int)nxui_appearance()) return;
    const nxc_surface_t *wall = nxui_wallpaper();
    if (!wall || !wall->pixels) return;
    if (!g_mini.pixels) {
        if (nxc_surface_create(&g_mini, (int32_t)OV_TILE_W, (int32_t)OV_TILE_H, 0) != NXC_OK) {
            g_mini_failed = 1;
            nx_debug_print("[OVERVIEW] mini wallpaper allocation failed; plain tiles\n");
            return;
        }
    }
    nxc_canvas_t mc;
    if (nxc_canvas_init(&mc, &g_mini) != NXC_OK) return;
    nxc_draw_surface(&mc, wall, nxc_irect(0, 0, wall->width, wall->height),
                     nxc_rect(0.0f, 0.0f, OV_TILE_W, OV_TILE_H), 255, NXC_FILTER_SMOOTH);
    g_mini_ok = 1;
    g_mini_appearance = (int)nxui_appearance();
}

/* ---- Layout ---- */

static void ov_layout(void) {
    if (g_n <= 0) return;
    float ax = 48.0f, ay = OV_STRIP_Y + OV_TILE_H + 44.0f;
    float aw = (float)nxui_width() - 96.0f;
    float ah = (float)nxui_height() - (float)WM_DOCK_RESERVE - ay - 8.0f;
    if (aw < 100.0f || ah < 100.0f) return;

    /* Rows by the windows' vertical position, columns by horizontal. */
    int order[MAX_WINDOWS];
    for (int i = 0; i < g_n; i++) order[i] = i;
    for (int i = 1; i < g_n; i++) {
        int k = order[i], j = i - 1;
        float cy = g_it[k].real.y + g_it[k].real.h * 0.5f;
        while (j >= 0 && g_it[order[j]].real.y + g_it[order[j]].real.h * 0.5f > cy) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }

    /* Pick the column count that gives the largest uniform scale. */
    int best_cols = 1;
    float best_s = 0.0f;
    for (int cols = 1; cols <= g_n; cols++) {
        int rows = (g_n + cols - 1) / cols;
        float cw = (aw - OV_GAP * (float)(cols - 1)) / (float)cols;
        float ch = (ah - OV_GAP * (float)(rows - 1)) / (float)rows - OV_LABEL_H;
        if (cw <= 10.0f || ch <= 10.0f) continue;
        float s = OV_MAX_SCALE;
        for (int i = 0; i < g_n; i++) {
            float sx = cw / g_it[i].real.w, sy = ch / g_it[i].real.h;
            if (sx < s) s = sx;
            if (sy < s) s = sy;
        }
        if (s > best_s + 0.001f) { best_s = s; best_cols = cols; }
    }
    if (best_s <= 0.0f) best_s = 0.1f;
    int cols = best_cols, rows = (g_n + cols - 1) / cols;
    float row_h = (ah - OV_GAP * (float)(rows - 1)) / (float)rows;
    float total_h = row_h * (float)rows + OV_GAP * (float)(rows - 1);
    float y0 = ay + (ah - total_h) * 0.5f;

    for (int r = 0; r < rows; r++) {
        int first = r * cols, last = first + cols;
        if (last > g_n) last = g_n;
        /* Sort the row left to right */
        for (int i = first + 1; i < last; i++) {
            int k = order[i], j = i - 1;
            float cx = g_it[k].real.x + g_it[k].real.w * 0.5f;
            while (j >= first && g_it[order[j]].real.x + g_it[order[j]].real.w * 0.5f > cx) { order[j + 1] = order[j]; j--; }
            order[j + 1] = k;
        }
        float row_w = 0.0f;
        for (int i = first; i < last; i++) row_w += g_it[order[i]].real.w * best_s;
        row_w += OV_GAP * (float)(last - first - 1);
        float x = ax + (aw - row_w) * 0.5f;
        float cell_y = y0 + (float)r * (row_h + OV_GAP);
        for (int i = first; i < last; i++) {
            ov_item_t *it = &g_it[order[i]];
            float w = it->real.w * best_s, h = it->real.h * best_s;
            float y = cell_y + (row_h - OV_LABEL_H - h) * 0.5f;
            it->to = nxc_rect((float)(int)x, (float)(int)y, (float)(int)w, (float)(int)h);
            x += w + OV_GAP;
        }
    }
}

/* Collect the current space's windows (shown, or parked by Stage). */
static void ov_gather(void) {
    uint32_t ids[MAX_WINDOWS];
    int n = wm_shell_window_order(ids, MAX_WINDOWS);
    g_n = 0;
    for (int i = n - 1; i >= 0; i--) {          /* bottom first */
        const desktop_window_t *w = ov_window(ids[i]);
        if (!w || w->minimized) continue;
        uint8_t hid = wm_window_hidden(w->id);
        if (hid & ~WM_HIDE_STAGE) continue;    /* other space or closing */
        ov_item_t *it = &g_it[g_n++];
        it->id = w->id;
        it->from = ov_desktop_rect(w);
        it->real = ov_frame_rect(w);
        it->to = it->from;
        it->delta = nxc_rect(0.0f, 0.0f, 0.0f, 0.0f);
        wm_snapshot_request(w->id);
    }
    ov_layout();
}

/* Re-run the layout keeping on-screen positions continuous. */
static void ov_relayout(void) {
    nxc_rect_t old[MAX_WINDOWS];
    for (int i = 0; i < g_n; i++) old[i] = g_it[i].to;
    ov_layout();
    for (int i = 0; i < g_n; i++) {
        g_it[i].delta.x += old[i].x - g_it[i].to.x;
        g_it[i].delta.y += old[i].y - g_it[i].to.y;
        g_it[i].delta.w += old[i].w - g_it[i].to.w;
        g_it[i].delta.h += old[i].h - g_it[i].to.h;
    }
}

static void ov_remove_item(int idx) {
    if (idx < 0 || idx >= g_n) return;
    for (int i = idx; i < g_n - 1; i++) g_it[i] = g_it[i + 1];
    g_n--;
    g_hover = -1;
    ov_relayout();
}

/* ---- Enter / leave ---- */

static void ov_leave(uint32_t focus_id) {
    if (!g_active || g_leaving) return;
    g_leaving = 1;
    g_dragging = 0;
    g_press = -1;
    g_press_space = -2;
    if (focus_id) desktop_activate_window(focus_id);
    /* Fly back to wherever the windows are now (activation may move them) */
    for (int i = 0; i < g_n; i++) {
        const desktop_window_t *w = ov_window(g_it[i].id);
        if (w) g_it[i].from = ov_desktop_rect(w);
    }
    nx_debug_print("[OVERVIEW] leave\n");
}

void overview_toggle(void) {
    if (g_active && !g_leaving) {
        ov_leave(0);
        return;
    }
    if (g_active && g_leaving) {
        g_leaving = 0;            /* reverse a leave in progress */
        return;
    }
    g_active = 1;
    g_leaving = 0;
    g_t.value = 0.0f;
    g_t.velocity = 0.0f;
    g_hover = -1;
    g_space_hover = -1;
    g_press = -1;
    g_press_space = -2;
    g_dragging = 0;
    ov_build_mini();
    ov_gather();
    nx_debug_print("[OVERVIEW] enter\n");
}

int overview_active(void) { return g_active; }

void overview_tick(float dt) {
    if (!g_active) return;
    nxui_spring_step(&g_t, g_leaving ? 0.0f : 1.0f, 240.0f, 31.0f, dt);
    for (int i = 0; i < g_n; i++) {
        ov_item_t *it = &g_it[i];
        it->delta.x = nxui_approach(it->delta.x, 0.0f, 0.07f, dt);
        it->delta.y = nxui_approach(it->delta.y, 0.0f, 0.07f, dt);
        it->delta.w = nxui_approach(it->delta.w, 0.0f, 0.07f, dt);
        it->delta.h = nxui_approach(it->delta.h, 0.0f, 0.07f, dt);
        /* Keep the thumbnails current while visible (lease renews each frame) */
        wm_snapshot_keep_fresh(it->id, 1000);
    }
    if (g_leaving && g_t.value < 0.01f) {
        g_active = 0;
        g_leaving = 0;
        g_n = 0;
        g_t.value = 0.0f;
        g_t.velocity = 0.0f;
    }
}

void overview_on_window_destroyed(uint32_t window_id) {
    if (!g_active) return;
    for (int i = 0; i < g_n; i++) {
        if (g_it[i].id == window_id) {
            if (g_press == i) { g_press = -1; g_dragging = 0; }
            else if (g_press > i) g_press--;
            ov_remove_item(i);
            return;
        }
    }
}

/* Current on-screen rect of item i. */
static nxc_rect_t ov_item_rect(int i, float t) {
    ov_item_t *it = &g_it[i];
    nxc_rect_t r = lerp_rect(it->from, it->to, t);
    r.x += it->delta.x * t;
    r.y += it->delta.y * t;
    r.w += it->delta.w * t;
    r.h += it->delta.h * t;
    return r;
}

static nxc_rect_t ov_drag_rect(int i) {
    nxc_rect_t r = g_it[i].to;
    float s = 0.55f;
    float gx = (float)(g_press_x) - r.x, gy = (float)(g_press_y) - r.y;  /* grab point */
    return nxc_rect((float)g_ptr_x - gx * s, (float)g_ptr_y - gy * s, r.w * s, r.h * s);
}

/* ---- Input ---- */

int overview_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_active) return 0;
    g_ptr_x = x;
    g_ptr_y = y;
    if (g_leaving) return 1;
    float t = clamp01(g_t.value);
    int pressed = (buttons & 0x01) && !(prev_buttons & 0x01);
    int released = !(buttons & 0x01) && (prev_buttons & 0x01);

    /* Hover */
    g_space_hover = ov_tile_at(x, y);
    g_space_close_hover = 0;
    if (g_space_hover >= 0 && g_space_hover < WM_MAX_SPACES && wm_space_count() > 1 &&
        nxglass_hit(ov_tile_close_rect(g_space_hover), x, y)) g_space_close_hover = 1;
    g_hover = -1;
    if (!g_dragging) {
        for (int i = g_n - 1; i >= 0; i--) {
            if (nxglass_hit(ov_item_rect(i, t), x, y)) { g_hover = i; break; }
        }
    }

    if (pressed) {
        g_press_x = x;
        g_press_y = y;
        g_press = g_hover;
        g_press_space = g_hover >= 0 ? -2 : (g_space_hover >= 0 ? g_space_hover : -1);
        g_press_close = g_space_close_hover;
        g_dragging = 0;
        return 1;
    }
    if ((buttons & 0x01) && g_press >= 0 && !g_dragging) {
        int32_t dx = x - g_press_x, dy = y - g_press_y;
        if (dx > OV_DRAG_PX || dx < -OV_DRAG_PX || dy > OV_DRAG_PX || dy < -OV_DRAG_PX) g_dragging = 1;
    }
    if (!released) return 1;

    if (g_dragging && g_press >= 0 && g_press < g_n) {
        /* Drop on a space (or "+") moves the window there */
        int tile = ov_tile_at(x, y);
        uint32_t id = g_it[g_press].id;
        int dest = -1;
        if (tile == WM_MAX_SPACES) dest = wm_space_add();
        else if (tile >= 0) dest = tile;
        if (dest >= 0 && dest != wm_space_current() && wm_window_set_space(id, dest) == 0) {
            nx_debug_print("[OVERVIEW] window moved to another space\n");
            ov_remove_item(g_press);
        }
        g_dragging = 0;
        g_press = -1;
        return 1;
    }
    if (g_press >= 0 && g_press == g_hover && g_press < g_n) {
        ov_leave(g_it[g_press].id);                     /* click a window: focus it */
    } else if (g_press_space >= 0 && g_press_space == g_space_hover) {
        if (g_press_space == WM_MAX_SPACES) {
            wm_space_add();
        } else if (g_press_close && g_space_close_hover) {
            int was = wm_space_current();
            wm_space_remove(g_press_space);
            if (wm_space_current() != was || g_press_space == was) ov_gather();
        } else if (g_press_space != wm_space_current()) {
            wm_space_switch(g_press_space);
            ov_gather();
            for (int i = 0; i < g_n; i++) g_it[i].from = g_it[i].to;   /* already in the grid */
            ov_leave(0);
        } else {
            ov_leave(0);
        }
    } else if (g_press_space == -1) {
        ov_leave(0);                                    /* empty space: back to the desktop */
    }
    g_press = -1;
    g_press_space = -2;
    return 1;
}

int overview_handle_key(uint8_t scancode, uint8_t pressed) {
    if (!g_active) return 0;
    if (!pressed) return 1;
    if (scancode == 0x01) ov_leave(0);                       /* Esc */
    else if (scancode == 0x1C) ov_leave(g_hover >= 0 && g_hover < g_n ? g_it[g_hover].id : 0);   /* Return */
    return 1;
}

/* ---- Drawing ---- */

static void ov_draw_tile(nxc_canvas_t *c, int i, float t) {
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = ov_tile_rect(i);
    int spaces = wm_space_count();
    if (i >= spaces) {
        /* "+" tile */
        int hot = g_space_hover == WM_MAX_SPACES;
        nxc_fill_rrect(c, r, 10.0f, hot ? th->control_fill_hover : th->control_fill);
        nxc_stroke_rrect(c, r, 10.0f, 1.0f, th->separator);
        nxsym_draw(c, NXSYM_PLUS, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 18.0f, th->text_primary, 0.0f);
        return;
    }
    if (g_mini_ok) nxc_draw_surface_rrect(c, &g_mini, nxc_irect(0, 0, g_mini.width, g_mini.height), r, 10.0f, 255,
                                          NXC_FILTER_NEAREST);
    else nxc_fill_rrect(c, r, 10.0f, th->window_bg);
    /* Its windows, to scale */
    float sx = r.w / (float)nxui_width(), sy = r.h / (float)nxui_height();
    uint32_t ids[MAX_WINDOWS];
    int n = wm_shell_window_order(ids, MAX_WINDOWS);
    nxc_save(c);
    nxc_clip_rrect(c, r, 10.0f);
    for (int k = n - 1; k >= 0; k--) {
        const desktop_window_t *w = ov_window(ids[k]);
        if (!w || w->minimized || wm_window_space(w->id) != i) continue;
        if (wm_window_hidden(w->id) & WM_HIDE_STAGE) continue;
        nxc_rect_t wr = nxc_rect(r.x + (float)w->x * sx, r.y + (float)w->y * sy, (float)w->width * sx,
                                 ((float)w->height + (float)WM_TITLEBAR_H) * sy);
        nxc_fill_rrect(c, wr, 2.0f, NXC_WITH_ALPHA(th->window_bg, 235));
        nxc_fill_rect(c, nxc_rect(wr.x, wr.y, wr.w, 2.5f), th->window_titlebar);
    }
    nxc_restore(c);
    int cur = i == wm_space_current();
    if (cur) nxc_stroke_rrect(c, nxc_rect(r.x - 2.0f, r.y - 2.0f, r.w + 4.0f, r.h + 4.0f), 12.0f, 2.5f, th->accent);
    else if (g_space_hover == i) nxc_stroke_rrect(c, r, 10.0f, 1.5f, th->text_secondary);
    else nxc_stroke_rrect(c, r, 10.0f, 1.0f, th->separator);
    /* Label */
    char label[16] = "Desktop ";
    label[8] = (char)('1' + i);
    label[9] = '\0';
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x, r.y + r.h + 4.0f, r.w, 16.0f), label,
                      cur ? th->text_primary : th->text_secondary, NXC_ALIGN_CENTER);
    /* Remove button on hover (never for the last space) */
    if (g_space_hover == i && spaces > 1 && t > 0.9f) {
        nxc_rect_t cr = ov_tile_close_rect(i);
        nxc_fill_circle(c, cr.x + 9.0f, cr.y + 9.0f, 9.0f, g_space_close_hover ? th->destructive : NXC_ARGB(220, 60, 60, 66));
        nxsym_draw(c, NXSYM_CLOSE, cr.x + 9.0f, cr.y + 9.0f, 8.0f, NXC_RGB(255, 255, 255), 0.0f);
    }
}

static void ov_draw_label(nxc_canvas_t *c, int i, nxc_rect_t r, float a) {
    const desktop_window_t *w = ov_window(g_it[i].id);
    if (!w || a <= 0.02f) return;
    const nxui_theme_t *th = nxui_theme();
    float tw = nxui_text_width(NXUI_TEXT_BODY_MEDIUM, w->title);
    float pw = tw + 22.0f + 24.0f;
    if (pw > r.w + 40.0f) pw = r.w + 40.0f;
    nxc_rect_t pill = nxc_rect(r.x + (r.w - pw) * 0.5f, r.y + r.h + 8.0f, pw, 24.0f);
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(a * 255.0f));
    nxc_fill_rrect(c, pill, 12.0f, i == g_hover ? NXC_WITH_ALPHA(th->accent, 230) : NXC_ARGB(150, 20, 22, 28));
    nxapp_icon_draw(c, nxapp_icon_from_name(w->title), pill.x + 5.0f, pill.y + 3.0f, 18.0f, 255);
    nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(pill.x + 27.0f, pill.y, pill.w - 33.0f, pill.h), w->title,
                      NXC_RGB(255, 255, 255), NXC_ALIGN_LEFT);
    nxc_restore(c);
}

void overview_render(nxc_canvas_t *c) {
    if (!c || !g_active) return;
    const nxui_theme_t *th = nxui_theme();
    float t = clamp01(g_t.value);
    float W = (float)nxui_width(), H = (float)nxui_height();

    /* Dim the desktop */
    nxc_fill_rect(c, nxc_rect(0.0f, 0.0f, W, H), NXC_ARGB((uint8_t)(150.0f * t), 4, 6, 12));

    /* Spaces strip slides down from the top: glass bar, cached once settled */
    float strip_off = (float)(int)(-(1.0f - t) * (OV_TILE_H + 60.0f));
    nxc_save(c);
    nxc_translate(c, 0.0f, strip_off);
    nxc_set_alpha(c, (uint8_t)(t * 255.0f));
    nxc_rect_t bar = nxc_rect(0.0f, 0.0f, W, OV_STRIP_Y + OV_TILE_H + 30.0f);
    nxglass_panel(c, bar, 0.0f, NXGLASS_THIN, 0, t < 0.995f || g_leaving);
    nxc_fill_rect(c, bar, NXC_ARGB(40, 0, 0, 0));
    nxc_fill_rect(c, nxc_rect(0.0f, bar.h - 1.0f, W, 1.0f), th->separator);
    for (int i = 0; i < ov_tile_count(); i++) ov_draw_tile(c, i, t);
    nxc_restore(c);

    /* Windows in z-order (bottom first) so the exit lands naturally */
    int order[MAX_WINDOWS];
    uint8_t z[MAX_WINDOWS];
    for (int i = 0; i < g_n; i++) {
        const desktop_window_t *w = ov_window(g_it[i].id);
        order[i] = i;
        z[i] = w ? w->z_order : 0;
    }
    for (int i = 1; i < g_n; i++) {
        int k = order[i], j = i - 1;
        while (j >= 0 && z[order[j]] > z[k]) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    float label_a = clamp01((t - 0.6f) / 0.4f);
    for (int oi = 0; oi < g_n; oi++) {
        int i = order[oi];
        if (g_dragging && i == g_press) continue;
        nxc_rect_t r = ov_item_rect(i, t);
        if (r.w < 4.0f || r.h < 4.0f) continue;
        float rad = th->radius_window * (r.w / (g_it[i].real.w > 1.0f ? g_it[i].real.w : 1.0f));
        if (rad < 4.0f) rad = 4.0f;
        nxui_elevation(c, r, rad, 3);
        wm_shell_draw_window_image(c, g_it[i].id, r, 255);
        if (i == g_hover && t > 0.9f) {
            nxc_stroke_rrect(c, nxc_rect(r.x - 4.0f, r.y - 4.0f, r.w + 8.0f, r.h + 8.0f), rad + 4.0f, 3.0f, th->accent);
        }
        ov_draw_label(c, i, r, label_a);
    }

    /* Dragged window follows the pointer, smaller */
    if (g_dragging && g_press >= 0 && g_press < g_n) {
        nxc_rect_t r = ov_drag_rect(g_press);
        nxui_elevation(c, r, 6.0f, 4);
        wm_shell_draw_window_image(c, g_it[g_press].id, r, 235);
        int tile = ov_tile_at(g_ptr_x, g_ptr_y);
        if (tile >= 0) {
            nxc_rect_t tr = ov_tile_rect(tile == WM_MAX_SPACES ? ov_tile_count() - 1 : tile);
            nxc_stroke_rrect(c, nxc_rect(tr.x - 3.0f, tr.y - 3.0f, tr.w + 6.0f, tr.h + 6.0f), 13.0f, 3.0f, th->accent);
        }
    }

    if (g_n == 0 && t > 0.5f) {
        nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(0.0f, H * 0.5f - 20.0f, W, 40.0f), "No windows on this desktop",
                          NXC_WITH_ALPHA(th->text_secondary, (uint8_t)(255.0f * label_a)), NXC_ALIGN_CENTER);
    }
}
