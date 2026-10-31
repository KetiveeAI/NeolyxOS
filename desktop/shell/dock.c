/*
 * NeolyxOS - Dock (see dock.h)
 *
 * This file owns the dock's configuration (validated, persisted through
 * nxconfig), the item model derived from the shell's window table, the
 * dock layout with spring-driven magnification, input dispatch and the
 * actions items perform. The context menu lives in dock_menu.c and the
 * taskbar layout in taskbar_bar.c.
 *
 * Layout model (dock layout). Positions are computed along the dock's
 * main axis (x for a bottom dock, y for a side dock) and a cross axis
 * measured from the screen edge. Each frame:
 *   1. the rest layout places items at the configured icon size (shrunk
 *      if the row would not fit), centered along the edge;
 *   2. magnification scales items by a smooth bump around the pointer's
 *      position in the rest layout, so the pointer->item mapping never
 *      depends on the magnified sizes (no feedback, no jitter);
 *   3. the magnified row is anchored so the point under the pointer stays
 *      under the pointer, and returns to the rest layout as the blend
 *      falls to zero;
 *   4. every item's displayed position and size follow those targets
 *      through critically damped nxui springs.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "dock_internal.h"
#include "../include/nxconfig.h"
#include <stddef.h>

extern void serial_puts(const char *s);

/* ============ Logging ============ */

void dock_log(const char *msg) {
    serial_puts("[DOCK] ");
    serial_puts(msg ? msg : "(null)");
    serial_puts("\n");
}

void dock_log2(const char *msg, const char *arg) {
    serial_puts("[DOCK] ");
    serial_puts(msg ? msg : "(null)");
    serial_puts(arg ? arg : "(null)");
    serial_puts("\n");
}

static void dock_log_int(const char *msg, int v) {
    char buf[16];
    int i = 15, neg = v < 0;
    unsigned u = neg ? (unsigned)(-(v + 1)) + 1u : (unsigned)v;
    buf[i] = '\0';
    do { buf[--i] = (char)('0' + u % 10u); u /= 10u; } while (u && i > 1);
    if (neg) buf[--i] = '-';
    dock_log2(msg, &buf[i]);
}

/* ============ String helpers ============ */

void dock_strlcpy(char *dst, const char *src, int cap) {
    if (!dst || cap <= 0) return;
    int i = 0;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

int dock_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static uint32_t hash_ci(const char *s) {
    uint32_t h = 2166136261u;
    while (s && *s) { h ^= (uint8_t)lower(*s++); h *= 16777619u; }
    return h;
}

/* A label is a pinnable app name: 1..31 bytes, no control characters,
 * no ',' (the pinned list separator). UTF-8 bytes are allowed. */
static int label_valid(const char *s) {
    if (!s || !s[0]) return 0;
    int n = 0;
    for (; s[n]; n++) {
        uint8_t ch = (uint8_t)s[n];
        if (n >= 31 || ch < 0x20 || ch == 0x7F || ch == ',') return 0;
    }
    return 1;
}

/* ============ App identity ============ */

/* Icon kind for an app or window name. Aliases cover names the icon
 * table does not know; everything else goes through nxapp_icon_from_name. */
static int kind_for_name(const char *name) {
    static const struct { const char *name; nxapp_icon_t kind; } alias[] = {
        { "Task Manager", NXAPP_MONITOR }, { "Tasks", NXAPP_MONITOR },
        { "Activity Monitor", NXAPP_MONITOR },
    };
    if (!name) return NXAPP_GENERIC;
    for (unsigned i = 0; i < sizeof(alias) / sizeof(alias[0]); i++)
        if (dock_streq_ci(name, alias[i].name)) return alias[i].kind;
    return (int)nxapp_icon_from_name(name);
}

static int same_app(const char *la, int ka, const char *lb, int kb) {
    if (ka != NXAPP_GENERIC || kb != NXAPP_GENERIC) return ka == kb;
    return dock_streq_ci(la, lb);
}

static uint32_t key_for_app(const char *label, int kind) {
    if (kind != NXAPP_GENERIC) return 0x01000000u | (uint32_t)kind;
    return 0x40000000u | (hash_ci(label) & 0x3FFFFFFFu);
}

#define KEY_SEP    0xFFFFFFF0u
#define KEY_TRASH  0xFFFFFFF1u
#define KEY_MINI(id) (0x80000000u | ((id) & 0x0FFFFFFFu))

/* ============ State ============ */

static dock_state_t g_dock;
static int g_dock_initialized = 0;
static int g_pin_kind[DOCK_MAX_ICONS];

typedef struct {
    int             icon_size;
    int             mag_on;
    float           mag_size;
    int             autohide;
    dock_position_t pos;
    dock_layout_t   layout;
} dock_cfg_t;

static dock_cfg_t g_cfg = {
    DOCK_ICON_SIZE_DEFAULT, 1, DOCK_MAG_SIZE_DEFAULT, 0, DOCK_POS_BOTTOM, DOCK_LAYOUT_DOCK
};
static nxc_config_t *g_conf;            /* loaded once (nxconfig has 4 slots) */
static dock_hide_window_fn g_hide_fn;

dock_model_t g_dock_model;
static uint32_t g_model_sig;
static int g_model_dirty = 1;

/* Per-item animation state, keyed by item key. */
typedef struct {
    uint32_t      key;
    int           used;
    int           seen;
    nxui_spring_t pos;      /* main-axis start */
    nxui_spring_t size;
    float         bounce;   /* seconds since bounce start, < 0 = none */
} dock_anim_t;

static dock_anim_t g_anim[DOCK_MAX_ITEMS];
static int g_anim_reset = 1;

/* Layout of the last frame (dock layout), per model item. */
static float g_tpos[DOCK_MAX_ITEMS], g_tsize[DOCK_MAX_ITEMS];   /* targets */
static float g_rpos[DOCK_MAX_ITEMS], g_rsize[DOCK_MAX_ITEMS];   /* rest */
static float g_base = DOCK_ICON_SIZE_DEFAULT;
static float g_magblend;                 /* 0..1, spring-driven */
static nxui_spring_t g_mag_spring;
static float g_last_pm = -1.0f;           /* pointer main coord at last inside frame */
static nxui_spring_t g_hide_spring;       /* 0 shown .. 1 hidden */
static float g_hide_delay;
static float g_tip_alpha;
static int   g_tip_item = -1;
static nxc_rect_t g_last_shelf;
static int   g_shelf_stable;

/* Pointer interaction */
static int   g_press_item = -1;
static int   g_press_x, g_press_y;
static int   g_dragging;
static int   g_drag_pin = -1;           /* pinned index being dragged */
static int   g_drag_insert = -1;
static float g_drag_grab;               /* pointer offset inside the icon */
static int   g_mouse_x, g_mouse_y;

/* Geometry constants (dock layout) */
#define SHELF_PAD      9.0f     /* inner padding around icons */
#define SEP_W          13.0f
#define SPRING_K       1400.0f
#define SPRING_D       74.8f    /* 2 * sqrt(SPRING_K): critically damped */
#define MAG_SPAN       2.6f     /* magnification reach, in icon slots */

/* ============ Configuration ============ */

dock_layout_t   dock_cfg_layout(void)   { return g_cfg.layout; }
dock_position_t dock_cfg_position(void) { return g_cfg.pos; }

static const char *pos_name(dock_position_t p) {
    return p == DOCK_POS_LEFT ? "left" : (p == DOCK_POS_RIGHT ? "right" : "bottom");
}

static nxc_config_t *conf(void) {
    static int failed_logged = 0;
    if (!g_conf) {
        g_conf = nxc_config_load(NXC_USER_DEFAULTS);
        if (!g_conf && !failed_logged) {
            dock_log("warning: user config unavailable, settings will not persist");
            failed_logged = 1;
        }
    }
    return g_conf;
}

/* Write the pinned list as "Files,Browser,..." */
static int persist_pins(nxc_config_t *cf) {
    char buf[NXC_MAX_VALUE_LEN];
    int n = 0;
    for (int i = 0; i < g_dock.icon_count; i++) {
        const char *l = g_dock.icons[i].label;
        int len = 0;
        while (l[len]) len++;
        if (n + len + 2 >= (int)sizeof(buf)) {
            dock_log("warning: pinned list too long to save in full");
            break;
        }
        if (n) buf[n++] = ',';
        for (int k = 0; k < len; k++) buf[n++] = l[k];
    }
    buf[n] = '\0';
    return nxc_set_string(cf, "dock", "pinned", buf);
}

/* Save everything the dock owns. DOCK_OK or DOCK_ERR_PERSIST. */
static int persist(void) {
    nxc_config_t *cf = conf();
    if (!cf) return DOCK_ERR_PERSIST;
    int err = 0;
    err |= nxc_set_int(cf, "dock", "icon_size", g_cfg.icon_size);
    err |= nxc_set_bool(cf, "dock", "magnification", g_cfg.mag_on);
    err |= nxc_set_int(cf, "dock", "magnification_size", (int)(g_cfg.mag_size * 100.0f + 0.5f));
    err |= nxc_set_bool(cf, "dock", "autohide", g_cfg.autohide);
    err |= nxc_set_string(cf, "dock", "layout", g_cfg.layout == DOCK_LAYOUT_TASKBAR ? "taskbar" : "dock");
    err |= nxc_set_string(cf, "desktop", "dock_position", pos_name(g_cfg.pos));
    err |= persist_pins(cf);
    if (err || nxc_config_save(cf) != 0) {
        dock_log("error: saving dock settings failed");
        return DOCK_ERR_PERSIST;
    }
    return DOCK_OK;
}

/* Mirror the dock fields into nxappearance so other readers agree. */
static void mirror_appearance(void) {
    appearance_settings_t *app = nxappearance_get();
    if (!app) return;
    app->dock_icon_size = g_cfg.icon_size;
    app->dock_magnification = g_cfg.mag_on ? true : false;
    app->dock_magnification_amount = g_cfg.mag_size;
    app->dock_auto_hide = g_cfg.autohide ? true : false;
}

static void announce(void) {
    int h = g_cfg.layout == DOCK_LAYOUT_TASKBAR ? DOCK_TASKBAR_HEIGHT
                                                : g_cfg.icon_size + (int)(2.0f * SHELF_PAD) + DOCK_MARGIN_BOTTOM;
    nxevent_publish_dock_resize(g_cfg.icon_size, h, pos_name(g_cfg.pos));
}

static int apply_and_save(void) {
    mirror_appearance();
    announce();
    return persist();
}

/* Load the saved settings; invalid values fall back to defaults (logged). */
static void load_config(void) {
    nxc_config_t *cf = conf();
    if (!cf) return;
    int sz = nxc_get_int(cf, "dock", "icon_size", DOCK_ICON_SIZE_DEFAULT);
    if (sz >= DOCK_ICON_SIZE_MIN && sz <= DOCK_ICON_SIZE_MAX) g_cfg.icon_size = sz;
    else dock_log_int("warning: ignoring saved icon_size ", sz);
    g_cfg.mag_on = nxc_get_bool(cf, "dock", "magnification", 1) ? 1 : 0;
    int mp = nxc_get_int(cf, "dock", "magnification_size", (int)(DOCK_MAG_SIZE_DEFAULT * 100.0f + 0.5f));
    if (mp >= 100 && mp <= 200) g_cfg.mag_size = (float)mp / 100.0f;
    else dock_log_int("warning: ignoring saved magnification_size ", mp);
    g_cfg.autohide = nxc_get_bool(cf, "dock", "autohide", 0) ? 1 : 0;
    const char *lay = nxc_get_string(cf, "dock", "layout", "dock");
    if (dock_streq_ci(lay, "taskbar")) g_cfg.layout = DOCK_LAYOUT_TASKBAR;
    else if (dock_streq_ci(lay, "dock")) g_cfg.layout = DOCK_LAYOUT_DOCK;
    else dock_log2("warning: ignoring saved layout ", lay);
    const char *pos = nxc_get_string(cf, "desktop", "dock_position", "bottom");
    if (dock_streq_ci(pos, "left")) g_cfg.pos = DOCK_POS_LEFT;
    else if (dock_streq_ci(pos, "right")) g_cfg.pos = DOCK_POS_RIGHT;
    else if (dock_streq_ci(pos, "bottom")) g_cfg.pos = DOCK_POS_BOTTOM;
    else dock_log2("warning: ignoring saved dock_position ", pos);
}

/* Pinned apps from "pinned" (comma separated); defaults when absent. */
static void load_pins(void) {
    nxc_config_t *cf = conf();
    const char *list = cf ? nxc_get_string(cf, "dock", "pinned", NULL) : NULL;
    if (list && list[0]) {
        char name[32];
        int n = 0;
        for (const char *p = list;; p++) {
            if (*p == ',' || *p == '\0') {
                while (n > 0 && name[n - 1] == ' ') n--;
                name[n] = '\0';
                if (n > 0 && dock_add_icon(hash_ci(name), name, 0) < 0)
                    dock_log2("warning: skipping saved pin ", name);
                n = 0;
                if (*p == '\0') break;
            } else if (n < 31 && !(n == 0 && *p == ' ')) {
                name[n++] = *p;
            }
        }
        if (g_dock.icon_count > 0) return;
        dock_log("warning: saved pinned list empty or invalid, using defaults");
    }
    static const char *defaults[] = {
        "Files", "Browser", "Messages", "Notes", "Photos", "Music", "Mail", "Settings", "Shell"
    };
    for (unsigned i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        dock_add_icon(hash_ci(defaults[i]), defaults[i], 0);
}

/* ============ Settings API ============ */

int dock_set_magnification(int on, float size) {
    if (on != 0 && on != 1) { dock_log_int("rejected magnification on=", on); return DOCK_ERR_INVALID; }
    if (!(size >= DOCK_MAG_SIZE_MIN && size <= DOCK_MAG_SIZE_MAX)) {   /* also rejects NaN */
        dock_log("rejected magnification size (valid 1.0..2.0)");
        return DOCK_ERR_INVALID;
    }
    g_cfg.mag_on = on;
    g_cfg.mag_size = size;
    dock_log_int("magnification -> ", on ? (int)(size * 100.0f + 0.5f) : 0);
    return apply_and_save();
}

int dock_set_icon_size(int px) {
    if (px < DOCK_ICON_SIZE_MIN || px > DOCK_ICON_SIZE_MAX) {
        dock_log_int("rejected icon size ", px);
        return DOCK_ERR_INVALID;
    }
    g_cfg.icon_size = px;
    dock_log_int("icon size -> ", px);
    return apply_and_save();
}

int dock_set_autohide(int on) {
    if (on != 0 && on != 1) { dock_log_int("rejected autohide ", on); return DOCK_ERR_INVALID; }
    g_cfg.autohide = on;
    g_hide_delay = 0.0f;
    dock_log(on ? "autohide -> on" : "autohide -> off");
    return apply_and_save();
}

int dock_set_position(dock_position_t pos) {
    if (pos != DOCK_POS_BOTTOM && pos != DOCK_POS_LEFT && pos != DOCK_POS_RIGHT) {
        dock_log_int("rejected position ", (int)pos);
        return DOCK_ERR_INVALID;
    }
    if (pos != g_cfg.pos) g_anim_reset = 1;
    g_cfg.pos = pos;
    g_dock.position = pos;
    dock_menu_close();
    dock_log2("position -> ", pos_name(pos));
    return apply_and_save();
}

int dock_set_layout(dock_layout_t layout) {
    if (layout != DOCK_LAYOUT_DOCK && layout != DOCK_LAYOUT_TASKBAR) {
        dock_log_int("rejected layout ", (int)layout);
        return DOCK_ERR_INVALID;
    }
    if (layout != g_cfg.layout) g_anim_reset = 1;
    g_cfg.layout = layout;
    dock_menu_close();
    g_dragging = 0;
    g_press_item = -1;
    dock_log(layout == DOCK_LAYOUT_TASKBAR ? "layout -> taskbar" : "layout -> dock");
    return apply_and_save();
}

int dock_get_magnification(int *on, float *size) {
    if (!on || !size) return DOCK_ERR_INVALID;
    *on = g_cfg.mag_on;
    *size = g_cfg.mag_size;
    return DOCK_OK;
}
int dock_get_icon_size(void) { return g_cfg.icon_size; }
int dock_get_autohide(void) { return g_cfg.autohide; }
dock_position_t dock_get_position(void) { return g_cfg.pos; }
dock_layout_t dock_get_layout(void) { return g_cfg.layout; }

void dock_update_settings(void) {
    appearance_settings_t *app = nxappearance_get();
    g_dock.settings = app;
    if (!app) return;
    if (app->dock_icon_size >= DOCK_ICON_SIZE_MIN && app->dock_icon_size <= DOCK_ICON_SIZE_MAX)
        g_cfg.icon_size = app->dock_icon_size;
    else dock_log_int("warning: ignoring appearance dock_icon_size ", app->dock_icon_size);
    g_cfg.mag_on = app->dock_magnification ? 1 : 0;
    float m = app->dock_magnification_amount;
    if (m >= DOCK_MAG_SIZE_MIN && m <= DOCK_MAG_SIZE_MAX) g_cfg.mag_size = m;
    g_cfg.autohide = app->dock_auto_hide ? 1 : 0;
}

void dock_set_hide_handler(dock_hide_window_fn fn) {
    g_hide_fn = fn;
    dock_log(fn ? "hide handler registered" : "hide handler cleared");
}

int dock_hide_available(void) { return g_hide_fn != NULL; }

int dock_hide_window(uint32_t id) {
    if (!g_hide_fn || id == 0) return -1;
    int r = g_hide_fn(id);
    if (r != 0) dock_log("warning: hide handler failed");
    return r;
}

/* ============ Pinned apps ============ */

int dock_add_icon(uint32_t app_id, const char *label, uint32_t color) {
    if (!label_valid(label)) {
        dock_log("rejected pin: invalid label");
        return -1;
    }
    int kind = kind_for_name(label);
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (same_app(g_dock.icons[i].label, g_pin_kind[i], label, kind)) return i;
    }
    if (g_dock.icon_count >= DOCK_MAX_ICONS) {
        dock_log2("rejected pin (dock full): ", label);
        return -1;
    }
    dock_icon_t *icon = &g_dock.icons[g_dock.icon_count];
    for (int i = 0; i < (int)sizeof(dock_icon_t); i++) ((char *)icon)[i] = 0;
    icon->app_id = app_id;
    icon->color = color;
    dock_strlcpy(icon->label, label, (int)sizeof(icon->label));
    g_pin_kind[g_dock.icon_count] = kind;
    g_model_dirty = 1;
    return g_dock.icon_count++;
}

static void remove_pin_at(int idx) {
    if (idx < 0 || idx >= g_dock.icon_count) return;
    for (int j = idx; j < g_dock.icon_count - 1; j++) {
        g_dock.icons[j] = g_dock.icons[j + 1];
        g_pin_kind[j] = g_pin_kind[j + 1];
    }
    g_dock.icon_count--;
    g_model_dirty = 1;
}

void dock_remove_icon(uint32_t app_id) {
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].app_id == app_id) {
            dock_log2("unpinned ", g_dock.icons[i].label);
            remove_pin_at(i);
            return;
        }
    }
}

void dock_set_running(uint32_t app_id, int running) {
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].app_id == app_id) {
            g_dock.icons[i].running = running ? 1 : 0;
            return;
        }
    }
}

static dock_anim_t *anim_find(uint32_t key) {
    for (int i = 0; i < DOCK_MAX_ITEMS; i++)
        if (g_anim[i].used && g_anim[i].key == key) return &g_anim[i];
    return NULL;
}

static void bounce_key(uint32_t key) {
    appearance_settings_t *app = nxappearance_get();
    if (app && app->reduce_motion) return;
    dock_anim_t *a = anim_find(key);
    if (a) a->bounce = 0.0f;
}

void dock_start_bounce(uint32_t app_id) {
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].app_id == app_id) {
            bounce_key(key_for_app(g_dock.icons[i].label, g_pin_kind[i]));
            return;
        }
    }
}

#define BOUNCE_TIME 0.9f

float dock_bounce_lift(uint32_t key, float amplitude) {
    dock_anim_t *a = anim_find(key);
    if (!a || a->bounce < 0.0f) return 0.0f;
    /* Two decaying hops: 0..0.5 full height, 0.5..1 at 40 %. */
    float t = a->bounce / BOUNCE_TIME;
    if (t >= 1.0f) return 0.0f;
    float hop = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    float amp = t < 0.5f ? 1.0f : 0.4f;
    return amplitude * amp * 4.0f * hop * (1.0f - hop);
}

/* ============ Model ============ */

void dock_model_invalidate(void) { g_model_dirty = 1; }

static uint32_t window_signature(void) {
    uint32_t sig = 2166136261u;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        uint32_t v = 0;
        if (w) {
            v = w->id * 2654435761u ^ ((uint32_t)w->minimized << 1) ^ ((uint32_t)w->visible << 2) ^
                ((uint32_t)w->focused << 3) ^ ((uint32_t)w->title[0] << 8) ^ ((uint32_t)w->title[1] << 16);
        }
        sig = (sig ^ v) * 16777619u;
    }
    return sig;
}

static int add_item(dock_model_t *m, dock_item_type_t type) {
    if (m->nitems >= DOCK_MAX_ITEMS) return -1;
    dock_item_t *it = &m->items[m->nitems];
    for (int i = 0; i < (int)sizeof(*it); i++) ((char *)it)[i] = 0;
    it->type = type;
    it->pin = -1;
    it->kind = NXAPP_GENERIC;
    return m->nitems++;
}

void dock_model_refresh(void) {
    uint32_t sig = window_signature();
    if (!g_model_dirty && sig == g_model_sig) return;
    g_model_sig = sig;
    g_model_dirty = 0;

    dock_model_t *m = &g_dock_model;
    m->nitems = 0;
    m->nwins = 0;
    for (int i = 0; i < g_dock.icon_count; i++) {
        int k = add_item(m, DOCK_IT_APP);
        if (k < 0) break;
        dock_item_t *it = &m->items[k];
        it->pin = i;
        it->kind = g_pin_kind[i];
        dock_strlcpy(it->label, g_dock.icons[i].label, (int)sizeof(it->label));
        it->key = key_for_app(it->label, it->kind);
    }

    /* Windows: attach to their app item (adding running apps as needed). */
    uint8_t front_z[DOCK_MAX_ITEMS] = { 0 };
    int trash_wins = 0, trash_min = 0, trash_focus = 0;
    uint32_t trash_front = 0;
    for (int s = 0; s < MAX_WINDOWS; s++) {
        const desktop_window_t *w = desktop_window_slot(s);
        if (!w || (!w->visible && !w->minimized)) continue;
        dock_win_t *dw = &m->wins[m->nwins++];
        dw->id = w->id;
        dw->minimized = w->minimized ? 1 : 0;
        dw->focused = (w->focused && w->visible && !w->minimized) ? 1 : 0;
        dock_strlcpy(dw->title, w->title, (int)sizeof(dw->title));
        dw->kind = kind_for_name(w->title);
        dw->item = -1;
        if (dw->kind == NXAPP_TRASH) {
            trash_wins++;
            trash_min += dw->minimized;
            trash_focus |= dw->focused;
            if (!trash_front || !dw->minimized) trash_front = dw->id;
            continue;
        }
        int found = -1;
        for (int k = 0; k < m->nitems; k++) {
            if (m->items[k].type == DOCK_IT_APP &&
                same_app(m->items[k].label, m->items[k].kind, dw->title, dw->kind)) { found = k; break; }
        }
        if (found < 0) {
            found = add_item(m, DOCK_IT_APP);
            if (found < 0) continue;
            dock_item_t *it = &m->items[found];
            it->kind = dw->kind;
            dock_strlcpy(it->label, dw->title, (int)sizeof(it->label));
            it->key = key_for_app(it->label, it->kind);
        }
        dock_item_t *it = &m->items[found];
        dw->item = found;
        it->nwin++;
        it->nmin += dw->minimized;
        it->focused |= dw->focused;
        /* Frontmost: highest z among visible windows, else any minimized. */
        uint8_t z = dw->minimized ? 0 : (uint8_t)(w->z_order + 1);
        if (!it->front_id || z > front_z[found]) {
            it->front_id = w->id;
            front_z[found] = z;
        }
    }

    /* Separator, minimized windows, Trash */
    int sep = add_item(m, DOCK_IT_SEP);
    if (sep >= 0) m->items[sep].key = KEY_SEP;
    int shown = 0;
    for (int i = 0; i < m->nwins && shown < DOCK_MAX_MINI; i++) {
        if (!m->wins[i].minimized) continue;
        int k = add_item(m, DOCK_IT_MINI);
        if (k < 0) break;
        dock_item_t *it = &m->items[k];
        it->win_id = m->wins[i].id;
        it->kind = m->wins[i].kind;
        dock_strlcpy(it->label, m->wins[i].title, (int)sizeof(it->label));
        it->key = KEY_MINI(it->win_id);
        shown++;
    }
    int tr = add_item(m, DOCK_IT_TRASH);
    if (tr >= 0) {
        dock_item_t *it = &m->items[tr];
        it->kind = NXAPP_TRASH;
        dock_strlcpy(it->label, "Trash", (int)sizeof(it->label));
        it->key = KEY_TRASH;
        it->nwin = trash_wins;
        it->nmin = trash_min;
        it->focused = trash_focus;
        it->front_id = trash_front;
    }
}

/* ============ Actions ============ */

static dock_item_t *item_at(int idx) {
    if (idx < 0 || idx >= g_dock_model.nitems) return NULL;
    return &g_dock_model.items[idx];
}

void dock_item_launch(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it || (it->type != DOCK_IT_APP && it->type != DOCK_IT_TRASH)) return;
    dock_log2("launch ", it->label);
    uint32_t app_id = it->pin >= 0 ? g_dock.icons[it->pin].app_id : hash_ci(it->label);
    nxevent_publish_app(NX_EVENT_APP_LAUNCHED, app_id, it->label, "/Applications/");
    if (it->nwin == 0) bounce_key(it->key);
}

void dock_item_primary(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it) return;
    switch (it->type) {
        case DOCK_IT_MINI:
            if (desktop_activate_window(it->win_id) != 0) dock_log("warning: minimized window vanished");
            break;
        case DOCK_IT_APP:
        case DOCK_IT_TRASH:
            if (it->front_id) {
                if (desktop_activate_window(it->front_id) != 0) {
                    dock_log("warning: window vanished, launching instead");
                    dock_item_launch(idx);
                }
            } else {
                dock_item_launch(idx);
            }
            break;
        default:
            break;
    }
}

void dock_item_show_all(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it || it->nwin == 0) return;
    /* Every window of the app comes forward; the frontmost ends on top. */
    for (int i = 0; i < g_dock_model.nwins; i++) {
        const dock_win_t *w = &g_dock_model.wins[i];
        if (w->item == idx && w->id != it->front_id) desktop_activate_window(w->id);
    }
    if (it->front_id) desktop_activate_window(it->front_id);
    dock_log2("show all windows of ", it->label);
}

void dock_item_hide(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it || !g_hide_fn) return;
    for (int i = 0; i < g_dock_model.nwins; i++) {
        const dock_win_t *w = &g_dock_model.wins[i];
        if (w->item == idx && !w->minimized) dock_hide_window(w->id);
    }
    dock_log2("hide ", it->label);
}

void dock_item_quit(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it) return;
    int n = 0;
    for (int i = 0; i < g_dock_model.nwins; i++) {
        const dock_win_t *w = &g_dock_model.wins[i];
        int mine = (it->type == DOCK_IT_TRASH) ? (w->kind == NXAPP_TRASH) : (w->item == idx);
        if (mine && desktop_close_window(w->id) == 0) n++;
    }
    dock_log2("quit ", it->label);
    if (n == 0) dock_log("warning: quit found no windows");
}

void dock_item_keep(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it || it->type != DOCK_IT_APP || it->pin >= 0) return;
    if (dock_add_icon(hash_ci(it->label), it->label, 0) < 0) return;
    dock_log2("kept in dock: ", it->label);
    if (persist() != DOCK_OK) dock_log("warning: pinned list not saved");
}

void dock_item_unpin(int idx) {
    dock_item_t *it = item_at(idx);
    if (!it || it->type != DOCK_IT_APP || it->pin < 0) return;
    dock_log2("removed from dock: ", it->label);
    remove_pin_at(it->pin);
    if (persist() != DOCK_OK) dock_log("warning: pinned list not saved");
}

/* ============ Init ============ */

void dock_init(uint32_t screen_w, uint32_t screen_h) {
    if (g_dock_initialized) return;
    if (screen_w < 320 || screen_h < 240 || screen_w > 16384 || screen_h > 16384) {
        dock_log("error: dock_init with invalid screen size");
        return;
    }
    dock_log("init");
    g_dock.screen_width = screen_w;
    g_dock.screen_height = screen_h;
    g_dock.icon_count = 0;
    g_dock.hover_idx = -1;
    g_dock.pressed_idx = -1;
    g_dock.minimize_anim_idx = -1;
    g_dock.visible = 1;
    g_dock.settings = nxappearance_get();
    if (g_dock.settings) dock_update_settings();      /* appearance defaults first */
    load_config();                                    /* saved values win */
    g_dock.position = g_cfg.pos;
    mirror_appearance();
    load_pins();
    for (int i = 0; i < DOCK_MAX_ITEMS; i++) g_anim[i].used = 0;
    g_hide_spring.value = 0.0f;
    g_hide_spring.velocity = 0.0f;
    nxevent_subscribe_range(NX_EVENT_DOCK_RESIZE, NX_EVENT_DOCK_ICON_REMOVED, dock_event_handler, NULL);
    g_dock_initialized = 1;
    dock_log_int("ready, pinned apps: ", g_dock.icon_count);
    dock_log2("layout: ", g_cfg.layout == DOCK_LAYOUT_TASKBAR ? "taskbar" : "dock");
}

dock_state_t *dock_get_state(void) { return &g_dock; }

/* ============ Animation tick ============ */

void dock_tick(void) {
    if (!g_dock_initialized) return;
    float dt = nxui_frame_dt();
    for (int i = 0; i < DOCK_MAX_ITEMS; i++) {
        if (g_anim[i].used && g_anim[i].bounce >= 0.0f) {
            g_anim[i].bounce += dt;
            if (g_anim[i].bounce >= BOUNCE_TIME) g_anim[i].bounce = -1.0f;
        }
    }
}

/* ============ Geometry helpers (dock layout) ============ */

/* Pointer coordinates in dock space: main axis, and distance from edge. */
static void to_dock_space(int x, int y, float *main, float *cross) {
    float W = (float)g_dock.screen_width, H = (float)g_dock.screen_height;
    switch (g_cfg.pos) {
        case DOCK_POS_LEFT:  *main = (float)y; *cross = (float)x; break;
        case DOCK_POS_RIGHT: *main = (float)y; *cross = W - 1.0f - (float)x; break;
        default:             *main = (float)x; *cross = H - 1.0f - (float)y; break;
    }
}

/* Rect in screen space for main [m, m+len], cross [c, c+thick] (from edge). */
static nxc_rect_t to_screen(float m, float len, float c, float thick) {
    float W = (float)g_dock.screen_width, H = (float)g_dock.screen_height;
    switch (g_cfg.pos) {
        case DOCK_POS_LEFT:  return nxc_rect(c, m, thick, len);
        case DOCK_POS_RIGHT: return nxc_rect(W - c - thick, m, thick, len);
        default:             return nxc_rect(m, H - c - thick, len, thick);
    }
}

static float item_extent(const dock_item_t *it, float base) {
    return it->type == DOCK_IT_SEP ? SEP_W : base;
}

static float bump(float d) {
    /* 1 at d = 0, 0 at |d| >= MAG_SPAN, smooth (C1) in between. */
    if (d < 0.0f) d = -d;
    if (d >= MAG_SPAN) return 0.0f;
    float t = d / MAG_SPAN;
    float u = 1.0f - t * t;
    return u * u;
}

/* Thickness of the shelf and the cross offset of icons (dock layout). */
static float shelf_thick(void) { return g_base + 2.0f * SHELF_PAD; }
static float shelf_margin(void) { return (float)DOCK_MARGIN_BOTTOM; }
static float hide_shift(void) {
    float v = g_hide_spring.value;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    float span = g_cfg.layout == DOCK_LAYOUT_TASKBAR ? (float)DOCK_TASKBAR_HEIGHT + 4.0f
                                                     : shelf_margin() + shelf_thick() + 8.0f;
    return v * span;
}

/* Item under the pointer in the target layout, -1 if none. */
static int dock_hit(int x, int y) {
    if (g_cfg.layout != DOCK_LAYOUT_DOCK) return -1;
    float pm, pc;
    to_dock_space(x, y, &pm, &pc);
    pc += hide_shift();
    float reach = shelf_margin() + shelf_thick() + (g_cfg.mag_on ? (g_cfg.mag_size - 1.0f) * g_base * g_magblend : 0.0f);
    if (pc < 0.0f || pc > reach) return -1;
    for (int i = 0; i < g_dock_model.nitems; i++) {
        if (g_dock_model.items[i].type == DOCK_IT_SEP) continue;
        float a = g_tpos[i] - DOCK_ICON_GAP * 0.5f, b = g_tpos[i] + g_tsize[i] + DOCK_ICON_GAP * 0.5f;
        if (pm >= a && pm < b) return i;
    }
    return -1;
}

/* Whether the pointer is over the shelf area (incl. magnified overhang). */
static int dock_over(int x, int y) {
    if (g_cfg.layout != DOCK_LAYOUT_DOCK || g_dock_model.nitems == 0) return 0;
    float pm, pc;
    to_dock_space(x, y, &pm, &pc);
    pc += hide_shift();
    float reach = shelf_margin() + shelf_thick() + (g_cfg.mag_on ? (g_cfg.mag_size - 1.0f) * g_base * g_magblend : 0.0f);
    int last = g_dock_model.nitems - 1;
    float a = g_tpos[0] - SHELF_PAD, b = g_tpos[last] + g_tsize[last] + SHELF_PAD;
    return pc >= 0.0f && pc <= reach && pm >= a && pm <= b;
}

int dock_minimize_target(int *out_x, int *out_y) {
    if (!out_x || !out_y) return -1;
    if (g_cfg.layout == DOCK_LAYOUT_TASKBAR) {
        nxc_rect_t r = taskbar_rect();
        *out_x = (int)(r.x + r.w * 0.5f);
        *out_y = (int)(r.y + r.h * 0.5f);
        return 0;
    }
    /* The minimized section ends just before the Trash. */
    int tr = g_dock_model.nitems - 1;
    if (tr < 0) {
        *out_x = (int)g_dock.screen_width / 2;
        *out_y = (int)g_dock.screen_height - 40;
        return 0;
    }
    float m = g_tpos[tr] + g_tsize[tr] * 0.5f;
    nxc_rect_t r = to_screen(m, 1.0f, shelf_margin() + shelf_thick() * 0.5f, 1.0f);
    *out_x = (int)r.x;
    *out_y = (int)r.y;
    return 0;
}

/* ============ Rendering (dock layout) ============ */

static dock_anim_t *anim_get(uint32_t key, float pos, float size) {
    dock_anim_t *a = anim_find(key);
    if (a) return a;
    for (int i = 0; i < DOCK_MAX_ITEMS; i++) {
        if (!g_anim[i].used) {
            a = &g_anim[i];
            a->used = 1;
            a->key = key;
            a->pos.value = pos;
            a->pos.velocity = 0.0f;
            a->size.value = g_anim_reset ? size : 0.0f;     /* new items grow in */
            a->size.velocity = 0.0f;
            a->bounce = -1.0f;
            return a;
        }
    }
    return NULL;
}

/* Draw a minimized window as a small glass window card with its app icon. */
static void draw_mini_window(nxc_canvas_t *c, const dock_item_t *it, float x, float y, float s) {
    const nxui_theme_t *th = nxui_theme();
    float inset = s * 0.06f;
    nxc_rect_t r = nxc_rect(x + inset, y + s * 0.12f, s - 2.0f * inset, s * 0.76f);
    float rad = s * 0.09f;
    nxc_fill_rrect(c, r, rad, th->window_bg);
    nxc_save(c);
    nxc_clip_rrect(c, r, rad);
    nxc_fill_rect(c, nxc_rect(r.x, r.y, r.w, s * 0.13f), th->window_titlebar);
    nxc_restore(c);
    float dr = s * 0.022f + 0.6f, dy = r.y + s * 0.065f;
    nxc_fill_circle(c, r.x + s * 0.07f, dy, dr, th->traffic_close);
    nxc_fill_circle(c, r.x + s * 0.13f, dy, dr, th->traffic_minimize);
    nxc_fill_circle(c, r.x + s * 0.19f, dy, dr, th->traffic_zoom);
    nxc_stroke_rrect(c, r, rad, 1.0f, th->window_border);
    float is = s * 0.42f;
    nxapp_icon_draw(c, (nxapp_icon_t)it->kind, x + (s - is) * 0.5f, r.y + s * 0.18f, is, 255);
}

static void draw_item(nxc_canvas_t *c, const dock_item_t *it, nxc_rect_t r, uint8_t alpha) {
    if (it->type == DOCK_IT_MINI) {
        if (alpha < 255) { nxc_save(c); nxc_set_alpha(c, alpha); }
        draw_mini_window(c, it, r.x, r.y, r.w);
        if (alpha < 255) nxc_restore(c);
        return;
    }
    nxapp_icon_draw(c, (nxapp_icon_t)it->kind, r.x, r.y, r.w, alpha);
}

static void render_tooltip(nxc_canvas_t *c, const dock_item_t *it, nxc_rect_t icon, float alpha) {
    if (alpha < 0.02f || !it->label[0]) return;
    const nxui_theme_t *th = nxui_theme();
    float tw = (float)(int)(nxui_text_width(NXUI_TEXT_BODY_MEDIUM, it->label) + 28.0f);
    if (tw > 260.0f) tw = 260.0f;
    float th_ = 26.0f, gap = 10.0f;
    nxc_rect_t tip;
    switch (g_cfg.pos) {
        case DOCK_POS_LEFT:
            tip = nxc_rect(icon.x + icon.w + gap, icon.y + icon.h * 0.5f - th_ * 0.5f, tw, th_); break;
        case DOCK_POS_RIGHT:
            tip = nxc_rect(icon.x - gap - tw, icon.y + icon.h * 0.5f - th_ * 0.5f, tw, th_); break;
        default:
            tip = nxc_rect(icon.x + icon.w * 0.5f - tw * 0.5f, icon.y - gap - th_, tw, th_); break;
    }
    float W = (float)g_dock.screen_width;
    if (tip.x < 6.0f) tip.x = 6.0f;
    if (tip.x + tip.w > W - 6.0f) tip.x = W - 6.0f - tip.w;
    tip.x = (float)(int)tip.x;
    tip.y = (float)(int)tip.y;
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(alpha * 255.0f));
    nxglass_panel(c, tip, 9.0f, NXGLASS_REGULAR, 1, 1);
    nxc_stroke_rrect(c, tip, 9.0f, 1.0f, th->window_border);
    nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(tip.x + 6.0f, tip.y, tip.w - 12.0f, tip.h),
                      it->label, th->text_primary, NXC_ALIGN_CENTER);
    nxc_restore(c);
}

static void render_dock(nxc_canvas_t *c, int mx, int my) {
    const nxui_theme_t *th = nxui_theme();
    dock_model_t *m = &g_dock_model;
    int n = m->nitems;
    if (n <= 0) return;
    float dt = nxui_frame_dt();
    float W = (float)g_dock.screen_width, H = (float)g_dock.screen_height;
    int side = g_cfg.pos != DOCK_POS_BOTTOM;
    float area0 = side ? th->menubar_height + 8.0f : 8.0f;
    float area1 = side ? H - 8.0f : W - 8.0f;

    /* 1. Rest layout (shrink to fit). */
    float base = (float)g_cfg.icon_size;
    float fixed = 0.0f;
    int nicons = 0;
    for (int i = 0; i < n; i++) {
        if (m->items[i].type == DOCK_IT_SEP) fixed += SEP_W; else nicons++;
    }
    float gaps = (float)(n - 1) * DOCK_ICON_GAP + 2.0f * SHELF_PAD;
    float avail = area1 - area0;
    if (nicons > 0 && fixed + gaps + base * (float)nicons > avail) {
        base = (avail - fixed - gaps) / (float)nicons;
        if (base < 20.0f) base = 20.0f;
    }
    g_base = base;
    float row = fixed + gaps - 2.0f * SHELF_PAD + base * (float)nicons;
    float start = (area0 + area1) * 0.5f - row * 0.5f;
    float p = start;
    for (int i = 0; i < n; i++) {
        g_rpos[i] = p;
        g_rsize[i] = item_extent(&m->items[i], base);
        p += g_rsize[i] + DOCK_ICON_GAP;
    }

    /* Autohide: reveal at the edge, hide after the pointer leaves. */
    float pm, pc;
    to_dock_space(mx, my, &pm, &pc);
    int menu_open = dock_menu_is_open();
    if (g_cfg.autohide) {
        int shown = g_hide_spring.value < 0.5f;
        int near = shown ? (pc <= shelf_margin() + shelf_thick() + 24.0f &&
                            pm >= g_rpos[0] - 40.0f && pm <= g_rpos[n - 1] + g_rsize[n - 1] + 40.0f)
                         : (pc <= 2.0f && pm >= area0 && pm <= area1);
        if (near || menu_open || g_dragging) g_hide_delay = 0.0f;
        else g_hide_delay += dt;
        float target = (near || menu_open || g_dragging || g_hide_delay < 0.45f) ? 0.0f : 1.0f;
        nxui_spring_step(&g_hide_spring, target, 260.0f, 32.2f, dt);
    } else {
        nxui_spring_step(&g_hide_spring, 0.0f, 260.0f, 32.2f, dt);
    }
    float shift = hide_shift();
    g_dock.visible = g_hide_spring.value < 0.995f;
    if (!g_dock.visible) {
        g_dock.hover_idx = -1;
        g_tip_alpha = 0.0f;
        for (int i = 0; i < n; i++) { g_tpos[i] = g_rpos[i]; g_tsize[i] = g_rsize[i]; }
        return;
    }

    /* 2. Magnification blend from the pointer's rest-layout position. */
    float margin = shelf_margin(), thick = shelf_thick();
    int over = !menu_open && !g_dragging && pc + shift >= 0.0f &&
               pc + shift <= margin + thick + (g_cfg.mag_size - 1.0f) * base * g_magblend + 2.0f &&
               pm >= g_rpos[0] - SHELF_PAD && pm <= g_rpos[n - 1] + g_rsize[n - 1] + SHELF_PAD;
    float mag = g_cfg.mag_on ? g_cfg.mag_size : 1.0f;
    nxui_spring_step(&g_mag_spring, (over && mag > 1.0f) ? 1.0f : 0.0f, 520.0f, 45.6f, dt);
    g_magblend = g_mag_spring.value < 0.0f ? 0.0f : (g_mag_spring.value > 1.0f ? 1.0f : g_mag_spring.value);
    if (over) g_last_pm = pm;
    float anchor = g_last_pm;

    /* Drag-to-reorder: the dragged pin moves to its insertion slot. */
    int order[DOCK_MAX_ITEMS];
    for (int i = 0; i < n; i++) order[i] = i;
    int drag_item = -1;
    if (g_dragging && g_drag_pin >= 0 && g_drag_pin < g_dock.icon_count) {
        int np = g_dock.icon_count;
        int ins = (int)((pm - g_drag_grab + base * 0.5f - g_rpos[0]) / (base + DOCK_ICON_GAP));
        if (ins < 0) ins = 0;
        if (ins > np - 1) ins = np - 1;
        g_drag_insert = ins;
        drag_item = g_drag_pin;          /* pinned items are model items 0..np-1 */
        int k = 0;
        for (int i = 0; i < np; i++) {
            if (i == g_drag_pin) continue;
            if (k == ins) order[k++] = g_drag_pin;
            order[k++] = i;
        }
        if (k == ins) order[k++] = g_drag_pin;
    }

    /* 3. Target sizes and anchored magnified positions (in display order). */
    float tsz[DOCK_MAX_ITEMS], tps[DOCK_MAX_ITEMS];
    float slot = base + DOCK_ICON_GAP;
    for (int k = 0; k < n; k++) {
        int i = order[k];
        float ext = g_rsize[k];             /* rest extent of slot k */
        if (m->items[i].type == DOCK_IT_SEP) { tsz[k] = SEP_W; continue; }
        float s = base;
        if (anchor >= 0.0f && g_magblend > 0.0f && mag > 1.0f) {
            float d = (anchor - (g_rpos[k] + ext * 0.5f)) / slot;
            s = base * (1.0f + (mag - 1.0f) * bump(d) * g_magblend);
        }
        tsz[k] = s;
    }
    /* Positions: contiguous from a shift S chosen so the anchor point stays put. */
    float acc = 0.0f;
    for (int k = 0; k < n; k++) { tps[k] = acc; acc += tsz[k] + DOCK_ICON_GAP; }
    float total = acc - DOCK_ICON_GAP;
    float S = start;
    if (anchor >= 0.0f && g_magblend > 0.0f) {
        float half = DOCK_ICON_GAP * 0.5f;
        if (anchor <= g_rpos[0] - half) {
            S = start;
        } else if (anchor >= g_rpos[n - 1] + g_rsize[n - 1] + half) {
            S = start + row - total;
        } else {
            for (int k = 0; k < n; k++) {
                float a = g_rpos[k] - half, b = g_rpos[k] + g_rsize[k] + half;
                if (anchor >= a && anchor < b) {
                    float f = (anchor - a) / (b - a);
                    float ma = tps[k] - half, mb = tps[k] + tsz[k] + half;
                    S = anchor - (ma + f * (mb - ma));
                    break;
                }
            }
        }
    }
    if (S < area0) S = area0;
    if (S + total > area1) S = area1 - total;

    /* Map back to model order and drive the springs. */
    for (int i = 0; i < DOCK_MAX_ITEMS; i++) g_anim[i].seen = 0;
    float dpos[DOCK_MAX_ITEMS], dsz[DOCK_MAX_ITEMS];
    for (int k = 0; k < n; k++) {
        int i = order[k];
        g_tpos[i] = S + tps[k];
        g_tsize[i] = tsz[k];
        if (i == drag_item) {
            g_tpos[i] = pm - g_drag_grab;
            g_tsize[i] = base;
        }
        dock_anim_t *a = anim_get(m->items[i].key, g_tpos[i], g_tsize[i]);
        if (!a) { dpos[i] = g_tpos[i]; dsz[i] = g_tsize[i]; continue; }
        a->seen = 1;
        if (g_anim_reset || i == drag_item) {
            a->pos.value = g_tpos[i];
            a->pos.velocity = 0.0f;
        } else {
            nxui_spring_step(&a->pos, g_tpos[i], SPRING_K, SPRING_D, dt);
        }
        nxui_spring_step(&a->size, g_tsize[i], SPRING_K, SPRING_D, dt);
        dpos[i] = a->pos.value;
        dsz[i] = a->size.value < 0.0f ? 0.0f : a->size.value;
    }
    for (int i = 0; i < DOCK_MAX_ITEMS; i++) if (g_anim[i].used && !g_anim[i].seen) g_anim[i].used = 0;
    g_anim_reset = 0;

    /* Shelf: spans the displayed row; cached while it does not move. */
    float lo = dpos[order[0]], hi = dpos[order[n - 1]] + dsz[order[n - 1]];
    if (drag_item >= 0) {      /* the dragged icon floats; the shelf keeps the row */
        lo = S; hi = S + total;
    }
    nxc_rect_t shelf = to_screen(lo - SHELF_PAD, hi - lo + 2.0f * SHELF_PAD, margin - shift, thick);
    shelf.x = (float)(int)(shelf.x + 0.5f);
    shelf.y = (float)(int)(shelf.y + 0.5f);
    shelf.w = (float)(int)(shelf.w + 0.5f);
    shelf.h = (float)(int)(shelf.h + 0.5f);
    int same = shelf.x == g_last_shelf.x && shelf.y == g_last_shelf.y &&
               shelf.w == g_last_shelf.w && shelf.h == g_last_shelf.h;
    g_shelf_stable = same ? g_shelf_stable + 1 : 0;
    g_last_shelf = shelf;
    float radius = th->radius_dock;
    if (radius > thick * 0.5f) radius = thick * 0.5f;
    nxglass_panel(c, shelf, radius, NXGLASS_THIN, 2, g_shelf_stable < 2);
    nxc_stroke_rrect(c, shelf, radius, 1.0f, th->highlight);

    g_dock.x = (int)shelf.x;
    g_dock.y = (int)shelf.y;
    g_dock.width = (int)shelf.w;
    g_dock.height = (int)shelf.h;

    /* Hover (target layout) */
    int hover = (over && !g_dragging) ? dock_hit(mx, my) : -1;
    g_dock.hover_idx = hover;

    /* Items */
    float icon_c0 = margin + SHELF_PAD - shift;     /* icon edge nearest the screen edge */
    for (int k = 0; k < n; k++) {
        int i = order[k];
        if (i == drag_item) continue;
        const dock_item_t *it = &m->items[i];
        if (it->type == DOCK_IT_SEP) {
            float sx = dpos[i] + SEP_W * 0.5f;
            nxc_rect_t line = to_screen(sx, 1.0f, margin - shift + 10.0f, thick - 20.0f);
            nxc_fill_rect(c, line, th->separator);
            continue;
        }
        float sz = dsz[i];
        if (sz < 1.0f) continue;
        float lift = dock_bounce_lift(it->key, base * 0.55f);
        nxc_rect_t r = to_screen(dpos[i], sz, icon_c0 + lift, sz);
        int pressed = (i == g_press_item && (g_dock.pressed_idx == i));
        draw_item(c, it, r, 255);
        if (pressed) nxc_fill_rrect(c, r, sz * 0.225f, NXC_ARGB(70, 0, 0, 0));
        /* Running indicator: windows open (minimized ones count). */
        if (it->type != DOCK_IT_MINI && it->nwin > 0) {
            float dm = dpos[i] + sz * 0.5f;
            nxc_rect_t d = to_screen(dm, 1.0f, margin - shift + 4.5f, 1.0f);
            nxc_fill_circle(c, d.x, d.y, 2.2f, NXC_WITH_ALPHA(th->text_primary, it->focused ? 255 : 190));
        }
    }

    /* Dragged icon on top */
    if (drag_item >= 0) {
        nxc_rect_t r = to_screen(pm - g_drag_grab, base, icon_c0 + 6.0f, base);
        draw_item(c, &m->items[drag_item], r, 225);
    }

    /* Tooltip */
    if (hover >= 0) g_tip_item = hover;
    float tip_target = (hover >= 0 && !menu_open && !g_dragging) ? 1.0f : 0.0f;
    g_tip_alpha = nxui_approach(g_tip_alpha, tip_target, hover >= 0 ? 0.05f : 0.03f, dt);
    if (g_tip_item >= 0 && g_tip_item < n && g_tip_alpha > 0.02f) {
        int i = g_tip_item;
        float sz = dsz[i];
        float lift = dock_bounce_lift(m->items[i].key, base * 0.55f);
        nxc_rect_t r = to_screen(dpos[i], sz, icon_c0 + lift, sz);
        render_tooltip(c, &m->items[i], r, g_tip_alpha);
    }
}

void dock_render(nxc_canvas_t *c, int mouse_x, int mouse_y) {
    if (!c || !g_dock_initialized) return;
    g_mouse_x = mouse_x;
    g_mouse_y = mouse_y;
    dock_model_refresh();
    if (g_cfg.layout == DOCK_LAYOUT_TASKBAR) {
        float dt = nxui_frame_dt();
        float target = 0.0f;
        if (g_cfg.autohide) {
            int shown = g_hide_spring.value < 0.5f;
            int near = shown ? taskbar_contains(mouse_x, mouse_y) || mouse_y >= (int)g_dock.screen_height - 2
                             : mouse_y >= (int)g_dock.screen_height - 2;
            if (near || dock_menu_is_open()) g_hide_delay = 0.0f; else g_hide_delay += dt;
            target = (near || dock_menu_is_open() || g_hide_delay < 0.45f) ? 0.0f : 1.0f;
        }
        nxui_spring_step(&g_hide_spring, target, 260.0f, 32.2f, dt);
        g_dock.visible = g_hide_spring.value < 0.995f;
        if (g_dock.visible) taskbar_render(c, mouse_x, mouse_y, g_hide_spring.value);
        nxc_rect_t r = taskbar_rect();
        g_dock.x = (int)r.x; g_dock.y = (int)r.y; g_dock.width = (int)r.w; g_dock.height = (int)r.h;
    } else {
        render_dock(c, mouse_x, mouse_y);
    }
    dock_menu_render(c, mouse_x, mouse_y);
}

/* ============ Input ============ */

static void open_menu_for(int item) {
    if (item < 0 || item >= g_dock_model.nitems) return;
    float base = g_base;
    float m = g_rpos[item] + g_rsize[item] * 0.5f;
    float reach = shelf_margin() + shelf_thick() + 6.0f;
    nxc_rect_t a = to_screen(m, 1.0f, reach, 1.0f);
    (void)base;
    switch (g_cfg.pos) {
        case DOCK_POS_LEFT:  dock_menu_open(item, a.x, a.y, DOCK_MENU_RIGHT); break;
        case DOCK_POS_RIGHT: dock_menu_open(item, a.x + 1.0f, a.y, DOCK_MENU_LEFT); break;
        default:             dock_menu_open(item, a.x, a.y + 1.0f, DOCK_MENU_UP); break;
    }
}

static void commit_drag(void) {
    int from = g_drag_pin, to = g_drag_insert;
    g_dragging = 0;
    g_drag_pin = -1;
    if (from < 0 || from >= g_dock.icon_count || to < 0 || to >= g_dock.icon_count || from == to) return;
    dock_icon_t moved = g_dock.icons[from];
    int mk = g_pin_kind[from];
    if (from < to) {
        for (int j = from; j < to; j++) { g_dock.icons[j] = g_dock.icons[j + 1]; g_pin_kind[j] = g_pin_kind[j + 1]; }
    } else {
        for (int j = from; j > to; j--) { g_dock.icons[j] = g_dock.icons[j - 1]; g_pin_kind[j] = g_pin_kind[j - 1]; }
    }
    g_dock.icons[to] = moved;
    g_pin_kind[to] = mk;
    g_model_dirty = 1;
    dock_log2("reordered ", moved.label);
    if (persist() != DOCK_OK) dock_log("warning: pinned order not saved");
}

int dock_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_dock_initialized) return 0;
    g_mouse_x = mouse_x;
    g_mouse_y = mouse_y;
    if (dock_menu_is_open()) return dock_menu_handle_mouse(mouse_x, mouse_y, buttons, prev_buttons);
    if (g_cfg.layout == DOCK_LAYOUT_TASKBAR) return taskbar_handle_mouse(mouse_x, mouse_y, buttons, prev_buttons);
    if (!g_dock.visible) return 0;

    int lpress = (buttons & 0x01) && !(prev_buttons & 0x01);
    int lrelease = !(buttons & 0x01) && (prev_buttons & 0x01);
    int rpress = (buttons & 0x02) && !(prev_buttons & 0x02);

    /* A press that started on the dock owns the pointer until release. */
    if (g_press_item >= 0) {
        if ((buttons & 0x01) && !g_dragging) {
            int dx = mouse_x - g_press_x, dy = mouse_y - g_press_y;
            const dock_item_t *it = &g_dock_model.items[g_press_item < g_dock_model.nitems ? g_press_item : 0];
            if ((dx * dx + dy * dy) > 36 && it->type == DOCK_IT_APP && it->pin >= 0) {
                float pm, pc;
                to_dock_space(g_press_x, g_press_y, &pm, &pc);
                g_dragging = 1;
                g_drag_pin = it->pin;
                g_drag_insert = it->pin;
                g_drag_grab = pm - g_tpos[g_press_item];
                if (g_drag_grab < 0.0f || g_drag_grab > g_base) g_drag_grab = g_base * 0.5f;
                g_dock.pressed_idx = -1;
                dock_log2("drag ", it->label);
            }
        }
        if (lrelease) {
            int item = g_press_item;
            g_press_item = -1;
            g_dock.pressed_idx = -1;
            if (g_dragging) {
                commit_drag();
            } else if (dock_hit(mouse_x, mouse_y) == item) {
                dock_item_primary(item);
            }
        }
        return 1;
    }

    if (!dock_over(mouse_x, mouse_y)) return 0;
    int hit = dock_hit(mouse_x, mouse_y);
    if (lpress && hit >= 0) {
        g_press_item = hit;
        g_dock.pressed_idx = hit;
        g_press_x = mouse_x;
        g_press_y = mouse_y;
    } else if (rpress && hit >= 0) {
        open_menu_for(hit);
    }
    return 1;
}

int dock_handle_key(uint8_t scancode, uint8_t pressed) {
    if (!g_dock_initialized || !pressed) return 0;
    if (scancode == 0x01) {
        if (dock_menu_is_open()) { dock_menu_close(); return 1; }
        if (g_dragging) { g_dragging = 0; g_drag_pin = -1; g_press_item = -1; dock_log("drag cancelled"); return 1; }
    }
    return 0;
}

/* ============ Event handler ============ */

void dock_event_handler(const nx_event_t *event, void *userdata) {
    (void)userdata;
    if (!event || event->source_id == NX_COMPONENT_DOCK) return;   /* our own announcements */
    switch (event->type) {
        case NX_EVENT_DOCK_RESIZE:
            if (event->data && event->data_size >= sizeof(nx_dock_data_t)) {
                const nx_dock_data_t *d = (const nx_dock_data_t *)event->data;
                if (d->icon_size != g_cfg.icon_size) dock_set_icon_size(d->icon_size);
                if (dock_streq_ci(d->position, "left")) dock_set_position(DOCK_POS_LEFT);
                else if (dock_streq_ci(d->position, "right")) dock_set_position(DOCK_POS_RIGHT);
                else if (dock_streq_ci(d->position, "bottom")) dock_set_position(DOCK_POS_BOTTOM);
            } else {
                dock_update_settings();
            }
            break;
        case NX_EVENT_DOCK_AUTOHIDE:
            dock_update_settings();
            break;
        case NX_EVENT_DOCK_ICON_ADDED:
            if (event->data && event->data_size >= sizeof(nx_app_data_t)) {
                const nx_app_data_t *a = (const nx_app_data_t *)event->data;
                char name[32];
                dock_strlcpy(name, a->app_name, (int)sizeof(name));
                if (dock_add_icon(a->app_id, name, 0) >= 0 && persist() != DOCK_OK)
                    dock_log("warning: pinned list not saved");
            }
            break;
        case NX_EVENT_DOCK_ICON_REMOVED:
            if (event->data && event->data_size >= sizeof(nx_app_data_t)) {
                const nx_app_data_t *a = (const nx_app_data_t *)event->data;
                dock_remove_icon(a->app_id);
                if (persist() != DOCK_OK) dock_log("warning: pinned list not saved");
            }
            break;
        default:
            break;
    }
}

#ifdef NX_HOST_PREVIEW
/* PREVIEW ONLY: void(void) entry points for the preview script's "call". */
void dock_preview_layout_taskbar(void) { dock_set_layout(DOCK_LAYOUT_TASKBAR); }
void dock_preview_layout_dock(void)    { dock_set_layout(DOCK_LAYOUT_DOCK); }
void dock_preview_position_left(void)  { dock_set_position(DOCK_POS_LEFT); }
void dock_preview_position_right(void) { dock_set_position(DOCK_POS_RIGHT); }
void dock_preview_autohide_on(void)    { dock_set_autohide(1); }
void dock_preview_mag_large(void)      { dock_set_magnification(1, 2.0f); }
void dock_preview_invalid_args(void) {
    /* Each call must be rejected and leave the settings unchanged. */
    volatile float zero = 0.0f;
    int bad = 0;
    bad += dock_set_icon_size(8) != DOCK_ERR_INVALID;
    bad += dock_set_icon_size(400) != DOCK_ERR_INVALID;
    bad += dock_set_magnification(2, 1.5f) != DOCK_ERR_INVALID;
    bad += dock_set_magnification(1, 0.5f) != DOCK_ERR_INVALID;
    bad += dock_set_magnification(1, zero / zero) != DOCK_ERR_INVALID;
    bad += dock_set_autohide(-1) != DOCK_ERR_INVALID;
    bad += dock_set_position((dock_position_t)7) != DOCK_ERR_INVALID;
    bad += dock_set_layout((dock_layout_t)3) != DOCK_ERR_INVALID;
    bad += dock_get_magnification(NULL, NULL) != DOCK_ERR_INVALID;
    dock_log(bad ? "PREVIEW validation check FAILED" : "PREVIEW validation check passed");
}
#endif
