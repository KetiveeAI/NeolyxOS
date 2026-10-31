/*
 * NeolyxOS - Stage, see stage.h
 *
 * State:
 *   g_stage       the group on stage (shown windows)
 *   g_groups[]    parked groups, most recently used first; the first
 *                 STAGE_MAX_GROUPS are drawn in the strip
 *   g_fl[]        windows flying between a thumbnail and the stage
 *
 * Parked windows carry WM_HIDE_STAGE, so the shell neither draws nor hit
 * tests them; their frames are kept, so a group comes back where it was.
 *
 * The strip layer (all visible thumbnails, tilted, with app icons) is one
 * cached surface. Its signature folds in the group membership, snapshot
 * generations and appearance; the layer is rebuilt only when that changes.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/stage.h"
#include "../include/window_manager.h"
#include "../include/desktop_shell.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxicons.h"
#include "../include/nxsyscall.h"

#define ST_STRIP_X      10.0f
#define ST_CELL_H       108.0f
#define ST_THUMB_W      148.0f
#define ST_THUMB_H      88.0f
#define ST_THUMB_X      24.0f     /* thumb box left inside the strip */
#define ST_SLICES       24        /* vertical slices of the tilt */
#define ST_TILT         0.16f     /* right edge height loss */
#define ST_DRAG_PX      6
#define ST_FAN          3         /* windows drawn per group thumbnail */

typedef struct {
    uint32_t ids[STAGE_GROUP_MAX];
    int n;
} st_group_t;

typedef struct {
    uint32_t id;
    nxc_rect_t from, to;
    nxui_spring_t s;
    int to_stage;            /* lands on stage (unhide) or in the strip */
} st_flight_t;

typedef struct {
    uint32_t id;
    int32_t x, y;
    uint32_t w, h;
} st_saved_t;

static int g_on = 0;
static int g_exiting = 0;
static int g_space = 0;
static st_group_t g_stage;
static st_group_t g_groups[MAX_WINDOWS];
static int g_ngroups = 0;
static st_flight_t g_fl[MAX_WINDOWS];
static int g_nfl = 0;
static st_saved_t g_saved[MAX_WINDOWS];
static int g_nsaved = 0;
static nxui_spring_t g_appear;

/* Strip layer cache */
static nxc_surface_t g_layer;
static int g_layer_failed = 0;
static uint32_t g_layer_sig = 0;
static int g_layer_valid = 0;
static int32_t g_layer_w = 0, g_layer_h = 0;

/* Pointer */
static int g_hover = -1;
static int g_press = -1;
static int32_t g_press_x = 0, g_press_y = 0;
static int g_drag = 0;
static int32_t g_ptr_x = 0, g_ptr_y = 0;

/* ---- helpers ---- */

static const desktop_window_t *st_window(uint32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == id) return w;
    }
    return NULL;
}

static int st_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static float st_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static int group_has(const st_group_t *g, uint32_t id) {
    for (int i = 0; i < g->n; i++) if (g->ids[i] == id) return 1;
    return 0;
}

static void group_add(st_group_t *g, uint32_t id) {
    if (id == 0 || group_has(g, id) || g->n >= STAGE_GROUP_MAX) return;
    g->ids[g->n++] = id;
}

static int group_remove(st_group_t *g, uint32_t id) {
    int o = 0, hit = 0;
    for (int i = 0; i < g->n; i++) {
        if (g->ids[i] == id) { hit = 1; continue; }
        g->ids[o++] = g->ids[i];
    }
    g->n = o;
    return hit;
}

static void groups_remove_at(int gi) {
    if (gi < 0 || gi >= g_ngroups) return;
    for (int i = gi; i < g_ngroups - 1; i++) g_groups[i] = g_groups[i + 1];
    g_ngroups--;
}

static void groups_insert_front(const st_group_t *g) {
    if (g->n == 0) return;
    if (g_ngroups >= MAX_WINDOWS) g_ngroups = MAX_WINDOWS - 1;   /* drops the oldest */
    for (int i = g_ngroups; i > 0; i--) g_groups[i] = g_groups[i - 1];
    g_groups[0] = *g;
    g_ngroups++;
}

static int group_index_of(uint32_t id) {
    for (int gi = 0; gi < g_ngroups; gi++) if (group_has(&g_groups[gi], id)) return gi;
    return -1;
}

static int flying(uint32_t id) {
    for (int i = 0; i < g_nfl; i++) if (g_fl[i].id == id) return 1;
    return 0;
}

static int group_flying(const st_group_t *g) {
    for (int i = 0; i < g->n; i++) if (flying(g->ids[i])) return 1;
    return 0;
}

static int visible_groups(void) {
    return g_ngroups < STAGE_MAX_GROUPS ? g_ngroups : STAGE_MAX_GROUPS;
}

/* Strip panel rect (screen) for k visible groups. */
static nxc_rect_t strip_rect(int k) {
    float top = (float)WM_MENUBAR_H + 8.0f, bottom = (float)nxui_height() - (float)WM_DOCK_RESERVE - 8.0f;
    float h = (float)k * ST_CELL_H + 16.0f;
    float y = top + ((bottom - top) - h) * 0.5f;
    return nxc_rect(ST_STRIP_X, (float)(int)y, (float)STAGE_STRIP_W, (float)(int)h);
}

/* Thumbnail box of visible group gi (screen, untilted). */
static nxc_rect_t group_box(int gi) {
    nxc_rect_t s = strip_rect(visible_groups());
    return nxc_rect(s.x + ST_THUMB_X, s.y + 8.0f + (float)gi * ST_CELL_H + (ST_CELL_H - ST_THUMB_H) * 0.5f,
                    ST_THUMB_W, ST_THUMB_H);
}

/* Front window rect inside a box, keeping its aspect ratio. */
static nxc_rect_t fit_in_box(nxc_rect_t box, uint32_t id) {
    const desktop_window_t *w = st_window(id);
    float ww = w ? (float)w->width : 4.0f, wh = w ? (float)w->height + (float)WM_TITLEBAR_H : 3.0f;
    float s = box.w / ww;
    if (wh * s > box.h) s = box.h / wh;
    float dw = ww * s, dh = wh * s;
    return nxc_rect(box.x, box.y + (box.h - dh) * 0.5f, dw, dh);
}

static nxc_rect_t window_rect(uint32_t id) {
    int32_t x = 0, y = 0;
    uint32_t w = 0, h = 0;
    wm_shell_get_frame(id, &x, &y, &w, &h);
    return nxc_rect((float)x, (float)y, (float)w, (float)h + (float)WM_TITLEBAR_H);
}

/* The area right of the strip where stage windows sit. */
static nxc_rect_t stage_area(void) {
    float x = ST_STRIP_X + (float)STAGE_STRIP_W + 18.0f;
    float y = (float)WM_MENUBAR_H + 10.0f;
    return nxc_rect(x, y, (float)nxui_width() - x - 18.0f,
                    (float)nxui_height() - (float)WM_DOCK_RESERVE - y - 6.0f);
}

static void add_flight(uint32_t id, nxc_rect_t from, nxc_rect_t to, int to_stage) {
    for (int i = 0; i < g_nfl; i++) {
        if (g_fl[i].id == id) {      /* retarget a window already in flight */
            g_fl[i].to = to;
            g_fl[i].to_stage = to_stage;
            return;
        }
    }
    if (g_nfl >= MAX_WINDOWS) return;
    st_flight_t *f = &g_fl[g_nfl++];
    f->id = id;
    f->from = from;
    f->to = to;
    f->s.value = 0.0f;
    f->s.velocity = 0.0f;
    f->to_stage = to_stage;
    wm_snapshot_request(id);
}

/* Place a group that comes on stage: keep its frames unless they collide
 * with the strip, else center the front window and cascade the rest. */
static void place_on_stage(const st_group_t *g, int animate) {
    if (g->n == 0) return;
    nxc_rect_t area = stage_area();
    int collide = 0;
    for (int i = 0; i < g->n; i++) {
        nxc_rect_t r = window_rect(g->ids[i]);
        if (r.x < area.x - 2.0f || r.x + r.w > (float)nxui_width()) collide = 1;
    }
    if (!collide) return;
    nxc_rect_t f0 = window_rect(g->ids[0]);
    for (int i = 0; i < g->n; i++) {
        nxc_rect_t r = window_rect(g->ids[i]);
        float w = r.w, h = r.h;
        if (w > area.w) w = area.w;
        if (h > area.h) h = area.h;
        float x = area.x + (area.w - (f0.w < area.w ? f0.w : area.w)) * 0.5f + 34.0f * (float)i;
        float y = area.y + (area.h - (f0.h < area.h ? f0.h : area.h)) * 0.4f + 28.0f * (float)i;
        if (x + w > area.x + area.w) x = area.x + area.w - w;
        if (y + h > area.y + area.h) y = area.y + area.h - h;
        if (y < area.y) y = area.y;
        wm_shell_set_frame(g->ids[i], (int32_t)x, (int32_t)y, (uint32_t)w, (uint32_t)(h - (float)WM_TITLEBAR_H),
                           animate);
    }
}

/* Park the on-stage group in the strip (front), flying into its slot. */
static void park_stage_group(void) {
    if (g_stage.n == 0) return;
    st_group_t out = g_stage;
    g_stage.n = 0;
    groups_insert_front(&out);
    nxc_rect_t box = group_box(0);
    for (int i = 0; i < out.n; i++) {
        uint32_t id = out.ids[i];
        nxc_rect_t from = window_rect(id);
        const desktop_window_t *w = st_window(id);
        if (w && w->visible) add_flight(id, from, fit_in_box(box, id), 0);
        wm_window_set_hidden(id, WM_HIDE_STAGE, 1);
    }
}

/* Bring strip group gi on stage; merge=1 adds it to the current group. */
static void bring_group(int gi, int merge) {
    if (gi < 0 || gi >= g_ngroups) return;
    st_group_t in = g_groups[gi];
    nxc_rect_t box = group_box(gi < STAGE_MAX_GROUPS ? gi : 0);
    nxc_rect_t froms[STAGE_GROUP_MAX];
    for (int i = 0; i < in.n; i++) froms[i] = fit_in_box(box, in.ids[i]);
    groups_remove_at(gi);

    if (merge && g_stage.n > 0) {
        /* Cascade the newcomers off the current front window */
        nxc_rect_t f0 = window_rect(g_stage.ids[0]);
        nxc_rect_t area = stage_area();
        for (int i = 0; i < in.n; i++) {
            nxc_rect_t r = window_rect(in.ids[i]);
            float x = f0.x + 40.0f * (float)(g_stage.n + i), y = f0.y + 30.0f * (float)(g_stage.n + i);
            if (x + r.w > area.x + area.w) x = area.x + area.w - r.w;
            if (y + r.h > area.y + area.h) y = area.y + area.h - r.h;
            if (x < area.x) x = area.x;
            if (y < area.y) y = area.y;
            wm_shell_set_frame(in.ids[i], (int32_t)x, (int32_t)y, (uint32_t)r.w,
                               (uint32_t)(r.h - (float)WM_TITLEBAR_H), 0);
            group_add(&g_stage, in.ids[i]);
        }
        nx_debug_print("[STAGE] group merged onto stage\n");
    } else {
        park_stage_group();
        g_stage = in;
        place_on_stage(&g_stage, 0);
        nx_debug_print("[STAGE] group swapped onto stage\n");
    }
    for (int i = 0; i < in.n; i++) {
        add_flight(in.ids[i], froms[i], window_rect(in.ids[i]), 1);
        wm_shell_raise(in.ids[i]);   /* z-order now; focus follows on landing */
    }
}

/* ---- enter / leave ---- */

static void stage_enter(void) {
    uint32_t ids[MAX_WINDOWS];
    int n = wm_shell_window_order(ids, MAX_WINDOWS);
    g_space = wm_space_current();
    g_stage.n = 0;
    g_ngroups = 0;
    g_nfl = 0;
    g_nsaved = 0;
    uint32_t focus = desktop_focused_window_id();
    for (int i = 0; i < n; i++) {
        const desktop_window_t *w = st_window(ids[i]);
        if (!w || w->minimized || wm_window_hidden(w->id) != 0) continue;
        st_saved_t *s = &g_saved[g_nsaved++];
        s->id = w->id; s->x = w->x; s->y = w->y; s->w = w->width; s->h = w->height;
        if (focus == 0) focus = w->id;
    }
    /* Front group: the focused window; the rest grouped by app, MRU first */
    for (int i = 0; i < n; i++) {
        const desktop_window_t *w = st_window(ids[i]);
        if (!w || w->minimized || wm_window_hidden(w->id) != 0) continue;
        if (w->id == focus) { group_add(&g_stage, w->id); continue; }
        int gi = -1;
        for (int k = 0; k < g_ngroups; k++) {
            const desktop_window_t *f = st_window(g_groups[k].ids[0]);
            if (f && st_streq_ci(f->title, w->title)) { gi = k; break; }
        }
        if (gi < 0) {
            if (g_ngroups >= MAX_WINDOWS) continue;
            gi = g_ngroups++;
            g_groups[gi].n = 0;
        }
        group_add(&g_groups[gi], w->id);
    }
    g_on = 1;
    g_exiting = 0;
    g_layer_valid = 0;
    /* Parked windows fly into the strip */
    for (int gi = 0; gi < g_ngroups; gi++) {
        for (int i = 0; i < g_groups[gi].n; i++) {
            uint32_t id = g_groups[gi].ids[i];
            if (gi < STAGE_MAX_GROUPS) add_flight(id, window_rect(id), fit_in_box(group_box(gi), id), 0);
            wm_window_set_hidden(id, WM_HIDE_STAGE, 1);
        }
    }
    place_on_stage(&g_stage, 1);
    nx_debug_print("[STAGE] on\n");
}

static const st_saved_t *saved_of(uint32_t id) {
    for (int i = 0; i < g_nsaved; i++) if (g_saved[i].id == id) return &g_saved[i];
    return NULL;
}

static void stage_leave(int animate) {
    /* Every parked window flies back to the frame it had before Stage */
    for (int gi = 0; gi < g_ngroups; gi++) {
        nxc_rect_t box = group_box(gi < STAGE_MAX_GROUPS ? gi : 0);
        for (int i = 0; i < g_groups[gi].n; i++) {
            uint32_t id = g_groups[gi].ids[i];
            const st_saved_t *s = saved_of(id);
            if (s) wm_shell_set_frame(id, s->x, s->y, s->w, s->h, 0);
            if (animate && gi < STAGE_MAX_GROUPS) add_flight(id, fit_in_box(box, id), window_rect(id), 1);
            else wm_window_set_hidden(id, WM_HIDE_STAGE, 0);
        }
    }
    for (int i = 0; i < g_stage.n; i++) {
        const st_saved_t *s = saved_of(g_stage.ids[i]);
        if (s) wm_shell_set_frame(g_stage.ids[i], s->x, s->y, s->w, s->h, animate);
    }
    if (!animate) {
        for (int i = 0; i < g_nfl; i++) wm_window_set_hidden(g_fl[i].id, WM_HIDE_STAGE, 0);
        g_nfl = 0;
    }
    g_ngroups = 0;
    g_stage.n = 0;
    g_on = 0;
    g_exiting = animate;
    g_press = -1;
    g_drag = 0;
    g_hover = -1;
    nx_debug_print("[STAGE] off\n");
}

void stage_toggle(void) {
    if (g_on) stage_leave(1);
    else stage_enter();
}

int stage_active(void) { return g_on; }

/* ---- lifecycle hooks ---- */

void stage_on_window_created(uint32_t window_id) {
    if (!g_on) return;
    park_stage_group();
    g_stage.n = 0;
    group_add(&g_stage, window_id);
    place_on_stage(&g_stage, 0);
}

void stage_on_window_destroyed(uint32_t window_id) {
    if (!g_on && !g_exiting) return;
    group_remove(&g_stage, window_id);
    for (int gi = g_ngroups - 1; gi >= 0; gi--) {
        if (group_remove(&g_groups[gi], window_id) && g_groups[gi].n == 0) groups_remove_at(gi);
    }
    int o = 0;
    for (int i = 0; i < g_nfl; i++) if (g_fl[i].id != window_id) g_fl[o++] = g_fl[i];
    g_nfl = o;
    o = 0;
    for (int i = 0; i < g_nsaved; i++) if (g_saved[i].id != window_id) g_saved[o++] = g_saved[i];
    g_nsaved = o;
    g_press = -1;
    g_drag = 0;
}

void stage_on_window_activated(uint32_t window_id) {
    if (!g_on) return;
    if (group_has(&g_stage, window_id)) return;
    int gi = group_index_of(window_id);
    if (gi >= 0) {
        bring_group(gi, 0);
        return;
    }
    /* Not tracked (restored from the dock): it becomes the stage group */
    const desktop_window_t *w = st_window(window_id);
    if (!w || wm_window_space(window_id) != g_space) return;
    park_stage_group();
    g_stage.n = 0;
    group_add(&g_stage, window_id);
    wm_window_set_hidden(window_id, WM_HIDE_STAGE, 0);
    place_on_stage(&g_stage, 1);
}

int stage_window_thumb_rect(uint32_t window_id, nxc_rect_t *r) {
    if (!g_on || !r) return 0;
    int gi = group_index_of(window_id);
    if (gi < 0 || gi >= STAGE_MAX_GROUPS) return 0;
    *r = fit_in_box(group_box(gi), window_id);
    return 1;
}

/* ---- tick ---- */

void stage_tick(float dt) {
    if (!g_on && !g_exiting) return;
    nxui_spring_step(&g_appear, g_on ? 1.0f : 0.0f, 260.0f, 32.0f, dt);

    /* Flights */
    int o = 0;
    for (int i = 0; i < g_nfl; i++) {
        st_flight_t *f = &g_fl[i];
        nxui_spring_step(&f->s, 1.0f, 230.0f, 30.0f, dt);
        if (f->s.value > 0.995f) {
            if (f->to_stage) {
                wm_window_set_hidden(f->id, WM_HIDE_STAGE, 0);
                if (g_on && g_stage.n > 0 && f->id == g_stage.ids[0]) wm_shell_raise(f->id);
            }
            continue;
        }
        g_fl[o++] = *f;
    }
    g_nfl = o;

    if (g_exiting) {
        if (g_nfl == 0 && g_appear.value < 0.02f) {
            g_exiting = 0;
            g_appear.value = 0.0f;
            g_appear.velocity = 0.0f;
        }
        return;
    }

    /* A space switch ends Stage at once (Stage works on one space) */
    if (wm_space_current() != g_space) {
        stage_leave(0);
        return;
    }

    /* Minimized windows leave their groups (they live in the dock) */
    for (int i = g_stage.n - 1; i >= 0; i--) {
        const desktop_window_t *w = st_window(g_stage.ids[i]);
        if (!w || w->minimized) group_remove(&g_stage, g_stage.ids[i]);
    }
    for (int gi = g_ngroups - 1; gi >= 0; gi--) {
        for (int i = g_groups[gi].n - 1; i >= 0; i--) {
            const desktop_window_t *w = st_window(g_groups[gi].ids[i]);
            if (!w || w->minimized) {
                wm_window_set_hidden(g_groups[gi].ids[i], WM_HIDE_STAGE, 0);
                group_remove(&g_groups[gi], g_groups[gi].ids[i]);
            }
        }
        if (g_groups[gi].n == 0) groups_remove_at(gi);
    }
    /* Empty stage: the most recent group steps up */
    if (g_stage.n == 0 && g_ngroups > 0 && g_nfl == 0) bring_group(0, 0);
}

/* ---- strip drawing ---- */

/* Draw a window image tilted about its vertical axis (right edge recedes)
 * by slicing it into vertical strips of decreasing height. */
static void draw_tilted(nxc_canvas_t *c, uint32_t id, nxc_rect_t r, uint8_t alpha) {
    const nxc_surface_t *surf = NULL;
    nxc_irect_t src;
    if (!wm_snapshot_get(id, &surf, &src) || src.w < ST_SLICES) {
        wm_shell_draw_window_image(c, id, r, alpha);
        return;
    }
    float cy = r.y + r.h * 0.5f;
    for (int i = 0; i < ST_SLICES; i++) {
        float u0 = (float)i / (float)ST_SLICES, u1 = (float)(i + 1) / (float)ST_SLICES;
        float um = (u0 + u1) * 0.5f;
        float h = r.h * (1.0f - ST_TILT * um);
        int32_t sx0 = src.x + (int32_t)((float)src.w * u0), sx1 = src.x + (int32_t)((float)src.w * u1);
        if (sx1 <= sx0) continue;
        /* Columns get narrower toward the far edge too (foreshortening) */
        float x0 = r.x + r.w * (u0 - 0.06f * u0 * u0), x1 = r.x + r.w * (u1 - 0.06f * u1 * u1);
        nxc_draw_surface(c, surf, nxc_irect(sx0, src.y, sx1 - sx0, src.h),
                         nxc_rect(x0, cy - h * 0.5f, x1 - x0 + 0.6f, h), alpha, NXC_FILTER_BILINEAR);
    }
}

static uint32_t sig_mix(uint32_t h, uint32_t v) {
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

static uint32_t layer_signature(void) {
    uint32_t h = 2166136261u;
    int k = visible_groups();
    h = sig_mix(h, (uint32_t)k);
    h = sig_mix(h, (uint32_t)nxui_appearance());
    h = sig_mix(h, (uint32_t)nxui_height());
    for (int gi = 0; gi < k; gi++) {
        const st_group_t *g = &g_groups[gi];
        h = sig_mix(h, (uint32_t)g->n);
        h = sig_mix(h, (uint32_t)group_flying(g));
        for (int i = 0; i < g->n && i < ST_FAN; i++) {
            h = sig_mix(h, g->ids[i]);
            h = sig_mix(h, wm_snapshot_generation(g->ids[i]));
        }
    }
    return h;
}

/* Draw the visible groups into canvas c, whose origin is the strip's
 * top-left (layer) or the screen origin (fallback, ox/oy = strip pos). */
static void draw_groups(nxc_canvas_t *c, float ox, float oy) {
    nxc_rect_t strip = strip_rect(visible_groups());
    int k = visible_groups();
    for (int gi = 0; gi < k; gi++) {
        const st_group_t *g = &g_groups[gi];
        if (g->n == 0 || group_flying(g)) continue;
        nxc_rect_t box = group_box(gi);
        box.x += ox - strip.x;
        box.y += oy - strip.y;
        int fan = g->n < ST_FAN ? g->n : ST_FAN;
        for (int i = fan - 1; i >= 0; i--) {          /* back windows first */
            nxc_rect_t r = fit_in_box(box, g->ids[i]);
            float s = 1.0f - 0.08f * (float)i;
            r = nxc_rect(r.x + 10.0f * (float)i, r.y + r.h * (1.0f - s) * 0.5f - 7.0f * (float)i, r.w * s, r.h * s);
            nxui_elevation(c, r, 6.0f, i == 0 ? 2 : 1);
            draw_tilted(c, g->ids[i], r, (uint8_t)(255 - 50 * i));
        }
        const desktop_window_t *w = st_window(g->ids[0]);
        if (w) {
            nxc_rect_t r = fit_in_box(box, g->ids[0]);
            nxapp_icon_draw(c, nxapp_icon_from_name(w->title), r.x - 12.0f, r.y + r.h - 22.0f, 32.0f, 255);
        }
    }
}

static int layer_ensure(void) {
    if (g_layer.pixels) return 1;
    if (g_layer_failed) return 0;
    int32_t h = (int32_t)nxui_height() - WM_MENUBAR_H - WM_DOCK_RESERVE;
    if (h < 64) return 0;
    if (nxc_surface_create(&g_layer, STAGE_STRIP_W, h, 0) != NXC_OK) {
        g_layer_failed = 1;
        nx_debug_print("[STAGE] strip layer allocation failed; drawing thumbnails live\n");
        return 0;
    }
    g_layer_w = STAGE_STRIP_W;
    g_layer_h = h;
    return 1;
}

void stage_render(nxc_canvas_t *c) {
    if (!c || (!g_on && !g_exiting)) return;
    int k = visible_groups();
    float a = st_clamp01(g_appear.value);
    if (a < 0.02f) return;

    /* Lease fresh snapshots for what the strip shows (one capture/frame) */
    for (int gi = 0; gi < k; gi++) {
        for (int i = 0; i < g_groups[gi].n && i < ST_FAN; i++)
            wm_snapshot_keep_fresh(g_groups[gi].ids[i], STAGE_REFRESH_MS);
    }
    if (k == 0) return;

    nxc_rect_t strip = strip_rect(k);
    float off = -(1.0f - a) * (strip.w + 20.0f);
    nxc_rect_t panel = nxc_rect(strip.x + off, strip.y, strip.w, strip.h);
    int settled = a > 0.995f;
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(a * 255.0f));
    nxglass_panel(c, panel, 20.0f, NXGLASS_REGULAR, 2, settled ? 0 : 1);

    if (layer_ensure() && (int32_t)strip.h <= g_layer_h) {
        uint32_t sig = layer_signature();
        if (!g_layer_valid || sig != g_layer_sig) {
            nxc_canvas_t lc;
            if (nxc_canvas_init(&lc, &g_layer) == NXC_OK) {
                nxc_surface_clear(&g_layer, 0);
                draw_groups(&lc, 0.0f, 0.0f);
                g_layer_sig = sig;
                g_layer_valid = 1;
            }
        }
        nxc_draw_surface(c, &g_layer, nxc_irect(0, 0, g_layer_w, (int32_t)strip.h),
                         nxc_rect(panel.x, panel.y, (float)g_layer_w, strip.h), 255, NXC_FILTER_NEAREST);
    } else {
        draw_groups(c, panel.x, panel.y);   /* no layer memory: live fallback */
    }

    /* Hover ring (live, so hovering never rebuilds the layer) */
    if (g_hover >= 0 && g_hover < k && !g_drag && settled) {
        nxc_rect_t box = group_box(g_hover);
        const nxui_theme_t *th = nxui_theme();
        nxc_stroke_rrect(c, nxc_rect(box.x - 10.0f, box.y - 8.0f, box.w + 22.0f, box.h + 16.0f), 12.0f, 1.5f,
                         NXC_WITH_ALPHA(th->accent, 170));
    }
    nxc_restore(c);
}

void stage_render_overlay(nxc_canvas_t *c) {
    if (!c || (!g_on && !g_exiting)) return;
    for (int i = 0; i < g_nfl; i++) {
        st_flight_t *f = &g_fl[i];
        float t = st_clamp01(f->s.value);
        nxc_rect_t r = nxc_rect(f->from.x + (f->to.x - f->from.x) * t, f->from.y + (f->to.y - f->from.y) * t,
                                f->from.w + (f->to.w - f->from.w) * t, f->from.h + (f->to.h - f->from.h) * t);
        if (r.w < 2.0f || r.h < 2.0f) continue;
        nxui_elevation(c, r, 8.0f, 3);
        wm_shell_draw_window_image(c, f->id, r, 255);
    }
    if (g_on && g_drag && g_press >= 0 && g_press < g_ngroups && g_groups[g_press].n > 0) {
        uint32_t id = g_groups[g_press].ids[0];
        nxc_rect_t box = group_box(g_press);
        nxc_rect_t r = fit_in_box(box, id);
        r.x += (float)(g_ptr_x - g_press_x);
        r.y += (float)(g_ptr_y - g_press_y);
        nxui_elevation(c, r, 6.0f, 4);
        wm_shell_draw_window_image(c, id, r, 235);
        if ((float)g_ptr_x > ST_STRIP_X + (float)STAGE_STRIP_W) {
            const nxui_theme_t *th = nxui_theme();
            nxc_rect_t area = stage_area();
            nxc_stroke_rrect(c, area, 14.0f, 2.0f, NXC_WITH_ALPHA(th->accent, 140));
        }
    }
}

/* ---- input ---- */

static int group_at(int32_t x, int32_t y) {
    int k = visible_groups();
    for (int gi = 0; gi < k; gi++) {
        nxc_rect_t b = group_box(gi);
        if (nxglass_hit(nxc_rect(b.x - 12.0f, b.y - 10.0f, b.w + 24.0f, b.h + 20.0f), x, y)) return gi;
    }
    return -1;
}

int stage_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_on) return 0;
    g_ptr_x = x;
    g_ptr_y = y;
    int pressed = (buttons & 0x01) && !(prev_buttons & 0x01);
    int released = !(buttons & 0x01) && (prev_buttons & 0x01);
    int k = visible_groups();
    nxc_rect_t strip = strip_rect(k);
    int in_strip = k > 0 && nxglass_hit(strip, x, y);
    g_hover = group_at(x, y);

    if (g_press >= 0) {
        if (buttons & 0x01) {
            int32_t dx = x - g_press_x, dy = y - g_press_y;
            if (!g_drag && (dx > ST_DRAG_PX || dx < -ST_DRAG_PX || dy > ST_DRAG_PX || dy < -ST_DRAG_PX)) g_drag = 1;
            return 1;
        }
        if (released) {
            int gi = g_press;
            int was_drag = g_drag;
            g_press = -1;
            g_drag = 0;
            if (gi < g_ngroups) {
                if (was_drag) {
                    if ((float)x > ST_STRIP_X + (float)STAGE_STRIP_W) bring_group(gi, 1);   /* drop on stage */
                } else if (group_at(x, y) == gi) {
                    bring_group(gi, 0);                                                    /* click: swap */
                }
            }
            return 1;
        }
    }
    if (pressed && g_hover >= 0) {
        g_press = g_hover;
        g_press_x = x;
        g_press_y = y;
        g_drag = 0;
        return 1;
    }
    return in_strip;
}
