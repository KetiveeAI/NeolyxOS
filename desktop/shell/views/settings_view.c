/*
 * NeolyxOS - System Settings app view (shell-hosted, see app_view.h)
 *
 * Owns the Settings window: the glass sidebar (search field + grouped
 * pane list), the detail toolbar (back/forward + title), navigation
 * history, keyboard focus and input routing. Panes draw into the detail
 * area through settings_pane_t (settings_pane.h); most are written on
 * the sui layout in settings_ui.c, the Network pane belongs to the
 * Networking area and is only registered here.
 *
 * Keyboard (window focused):
 *   Up/Down        move through the sidebar (sidebar focus)
 *   Right/Enter    move focus into the pane; Left/Esc returns
 *   Tab/Shift+Tab  cycle sidebar -> pane -> search
 *   typing         goes to the search field (focused, or from the sidebar)
 *   Ctrl+F         focus search
 *   Alt+Left/Right or Ctrl+[ / Ctrl+]   back / forward
 *   Up/Down/PgUp/PgDn/Home/End/Space    scroll the pane (pane focus)
 * Pointer: click, drag-scroll (sidebar and pane), scrollbar drag, and
 * the scroll wheel (input_event_t.mouse.wheel) over the sidebar or pane.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "control_center.h"
#include "settings_internal.h"
#include "nxframe.h"

extern int desktop_launch_app(const char *name);

/* ============ Pane table ============ */

typedef struct {
    const settings_pane_t *pane;
    int                    group;
    settings_tile_t        tile;
    const char            *keywords;   /* extra search terms */
} pane_entry_t;

#define TILE_SYM(s, col)  { STILE_SYM, (s), 0, (col) }
#define TILE_TXT(t, col)  { STILE_TEXT, 0, (t), (col) }
#define TILE_KIND(k, col) { (k), 0, 0, (col) }

static const pane_entry_t g_panes[] = {
    { &settings_pane_about,         0, TILE_SYM(NXSYM_INFO,      0xFF0A84FF), "about this system computer version kernel processor cpu cores memory ram display resolution storage disk hardware specs configuration" },
    { &settings_pane_network,       0, TILE_SYM(NXSYM_WIFI,      0xFF0A84FF), "wifi wi-fi ethernet internet ip dns bluetooth" },
    { &settings_pane_notifications, 1, TILE_SYM(NXSYM_BELL,      0xFFFF453A), "alerts banners do not disturb focus" },
    { &settings_pane_sound,         1, TILE_SYM(NXSYM_VOLUME,    0xFFFF375F), "volume audio output input alert mute speaker" },
    { &settings_pane_general,       2, TILE_SYM(NXSYM_GEAR,      0xFF8E8E93), "about version processor memory system info" },
    { &settings_pane_appearance,    2, TILE_SYM(NXSYM_MOON,      0xFF5E5CE6), "dark light mode theme accent color sidebar" },
    { &settings_pane_display,       2, TILE_SYM(NXSYM_BRIGHTNESS,0xFF32ADE6), "night mode warmth resolution screen monitor" },
    { &settings_pane_wallpaper,     2, TILE_SYM(NXSYM_SUN,       0xFF30B0C7), "background desktop picture" },
    { &settings_pane_dock,          2, TILE_SYM(NXSYM_GRID,      0xFF636366), "taskbar icon size magnification autohide hide" },
    { &settings_pane_privacy,       3, TILE_SYM(NXSYM_SHIELD,    0xFF0A84FF), "security firewall lock password permissions" },
    { &settings_pane_users,         3, TILE_SYM(NXSYM_USER,      0xFF5AC8FA), "accounts login password user" },
    { &settings_pane_keyboard,      4, TILE_SYM(NXSYM_KEYBOARD,  0xFF8E8E93), "key repeat shortcuts input layout" },
    { &settings_pane_mouse,         4, TILE_KIND(STILE_MOUSE,    0xFF8E8E93), "trackpad pointer cursor tracking scroll click" },
    { &settings_pane_datetime,      4, TILE_SYM(NXSYM_CLOCK,     0xFF0A84FF), "clock time zone calendar 24-hour" },
    { &settings_pane_storage,       5, TILE_SYM(NXSYM_FOLDER,    0xFF8E8E93), "disk space volume memory" },
    { &settings_pane_fonts,         5, TILE_TXT("Aa",            0xFFAF52DE), "font manager typeface text" },
    { &settings_pane_update,        5, TILE_KIND(STILE_UPDATE,   0xFF8E8E93), "software update upgrade version" },
    { &settings_pane_developer,     5, TILE_TXT("{ }",           0xFF3A3A3C), "debug logs task manager diagnostics" },
};
#define PANE_COUNT ((int)(sizeof(g_panes) / sizeof(g_panes[0])))

/* Rail group labels, indexed by pane_entry_t.group. */
static const char *const g_group_names[] = {
    "This system", "Attention", "Look and feel", "People and privacy", "Input and time", "Maintenance",
};
#define GROUP_COUNT ((int)(sizeof(g_group_names) / sizeof(g_group_names[0])))

/* Accepted aliases for settings_view_show_pane / control center ids. */
static const struct { const char *alias; const char *id; } g_aliases[] = {
    { "system", "about" }, { "info", "about" }, { "security", "privacy" }, { "trackpad", "mouse" },
    { "taskbar", "dock" }, { "accounts", "users" }, { "date", "datetime" },
    { "time", "datetime" }, { "updates", "update" }, { "displays", "display" },
    { "bluetooth", "network" }, { "wifi", "network" },
};

static const settings_tile_t g_tile_neutral = TILE_SYM(NXSYM_GEAR, 0xFF8E8E93);

static int pane_index(const char *id) {
    if (!id || !id[0]) return -1;
    for (int i = 0; i < PANE_COUNT; i++)
        if (g_panes[i].pane && g_panes[i].pane->id && sx_streq_ci(g_panes[i].pane->id, id)) return i;
    for (unsigned a = 0; a < sizeof(g_aliases) / sizeof(g_aliases[0]); a++)
        if (sx_streq_ci(g_aliases[a].alias, id)) return pane_index(g_aliases[a].id);
    return -1;
}

const settings_tile_t *settings_tile_for(const char *pane_id) {
    int i = pane_index(pane_id);
    return i >= 0 ? &g_panes[i].tile : &g_tile_neutral;
}

/* ============ View state ============ */

enum { FOCUS_SIDEBAR = 0, FOCUS_SEARCH, FOCUS_DETAIL };
enum { CAP_NONE = 0, CAP_SEARCH, CAP_LIST, CAP_BACK, CAP_FWD, CAP_PANE };

#define SIDEBAR_W   224.0f
#define TOOLBAR_H   46.0f
#define LIST_TOP    54.0f
#define HIST_MAX    32
#define SEARCH_MAX  40

static struct {
    uint32_t win_id;
    int      current;
    int      history[HIST_MAX];
    int      hist_len, hist_pos;
    char     search[SEARCH_MAX + 1];
    int      search_len;
    int      focus;
    float    list_scroll;
    int      sidebar_size;           /* 0 small, 1 medium, 2 large */
    int      pending;                /* pane index requested before launch, -1 none */
    /* pointer (local content coordinates) */
    int32_t  mx, my;
    int      hover_valid;
    int      capture;
    int      pressed_row;
    int32_t  press_y;
    float    list_scroll_start;
    int      list_dragging;
    /* modifiers */
    int      shift, ctrl, alt;
    /* geometry of the last frame */
    float    cw, ch;
} V = { .current = 0,   /* About This System */ .pending = -1, .sidebar_size = 1, .pressed_row = -1 };

static const float k_row_h[3]  = { 28.0f, 32.0f, 36.0f };
static const float k_tile_s[3] = { 20.0f, 22.0f, 25.0f };

int settings_sidebar_size(void) { return V.sidebar_size; }

void settings_set_sidebar_size(int size) {
    if (size < 0 || size > 2) return;
    V.sidebar_size = size;
}

static int window_is_open(void) {
    if (V.win_id == 0) return 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == V.win_id) return 1;
    }
    return 0;
}

int settings_open_app(const char *name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id && sx_streq_ci(w->title, name)) {
            settings_log2("open app (focus): ", name);
            return desktop_activate_window(w->id);
        }
    }
    settings_log2("open app (launch): ", name);
    return desktop_launch_app(name);
}

/* ============ Search & sidebar layout ============ */

static int pane_matches(int i) {
    if (V.search_len == 0) return 1;
    const settings_pane_t *p = g_panes[i].pane;
    if (!p) return 0;
    return sx_contains_ci(p->title, V.search) || sx_contains_ci(g_panes[i].keywords, V.search);
}

/* Row y offsets (relative to the list top, unscrolled); returns total height.
 * ys[i] < 0 for rows hidden by the search filter. */
static float list_layout(float *ys) {
    float rh = k_row_h[V.sidebar_size];
    float y = 0.0f;
    int last_group = -1, any = 0;
    for (int i = 0; i < PANE_COUNT; i++) {
        if (!pane_matches(i)) { ys[i] = -1.0f; continue; }
        if (g_panes[i].group != last_group) y += (any ? 6.0f : 0.0f) + NXFRAME_RAIL_GROUP_H;
        ys[i] = y;
        y += rh;
        last_group = g_panes[i].group;
        any = 1;
    }
    return y;
}

static nxc_rect_t list_rect(void) {
    return nxc_rect(0.0f, LIST_TOP, SIDEBAR_W, V.ch - LIST_TOP - 6.0f);
}

static void list_clamp(void) {
    float ys[PANE_COUNT];
    float total = list_layout(ys);
    float max = total - list_rect().h + 8.0f;
    if (max < 0.0f) max = 0.0f;
    if (V.list_scroll > max) V.list_scroll = max;
    if (V.list_scroll < 0.0f) V.list_scroll = 0.0f;
}

static void list_reveal(int idx) {
    float ys[PANE_COUNT];
    list_layout(ys);
    if (idx < 0 || idx >= PANE_COUNT || ys[idx] < 0.0f) return;
    float rh = k_row_h[V.sidebar_size];
    nxc_rect_t lr = list_rect();
    if (ys[idx] - V.list_scroll < 0.0f) V.list_scroll = ys[idx] - 4.0f;
    if (ys[idx] + rh - V.list_scroll > lr.h) V.list_scroll = ys[idx] + rh - lr.h + 4.0f;
    list_clamp();
}

static int list_row_at(int32_t lx, int32_t ly) {
    nxc_rect_t lr = list_rect();
    if (!nxglass_hit(lr, lx, ly)) return -1;
    float ys[PANE_COUNT];
    list_layout(ys);
    float rh = k_row_h[V.sidebar_size];
    float y = (float)ly - lr.y + V.list_scroll;
    for (int i = 0; i < PANE_COUNT; i++)
        if (ys[i] >= 0.0f && y >= ys[i] && y < ys[i] + rh) return i;
    return -1;
}

static int first_match(void) {
    for (int i = 0; i < PANE_COUNT; i++) if (pane_matches(i)) return i;
    return -1;
}

/* ============ Navigation ============ */

static void show_pane(int idx, int push) {
    if (idx < 0 || idx >= PANE_COUNT || !g_panes[idx].pane) return;
    if (idx == V.current && V.hist_len > 0) return;
    V.current = idx;
    if (push) {
        if (V.hist_len == 0) {
            V.history[0] = idx; V.hist_len = 1; V.hist_pos = 0;
        } else {
            V.hist_len = V.hist_pos + 1;                 /* drop forward entries */
            if (V.hist_len >= HIST_MAX) {               /* drop the oldest */
                for (int i = 1; i < HIST_MAX; i++) V.history[i - 1] = V.history[i];
                V.hist_len = HIST_MAX - 1;
            }
            V.history[V.hist_len++] = idx;
            V.hist_pos = V.hist_len - 1;
        }
    }
    if (g_panes[idx].pane->on_show) g_panes[idx].pane->on_show();
    list_reveal(idx);
    settings_log2("pane: ", g_panes[idx].pane->id);
}

static int can_back(void) { return V.hist_pos > 0; }
static int can_fwd(void)  { return V.hist_pos + 1 < V.hist_len; }

static void go_back(void) {
    if (!can_back()) return;
    V.hist_pos--;
    show_pane(V.history[V.hist_pos], 0);
}

static void go_fwd(void) {
    if (!can_fwd()) return;
    V.hist_pos++;
    show_pane(V.history[V.hist_pos], 0);
}

int settings_navigate(const char *pane_id) {
    int idx = pane_index(pane_id);
    if (idx < 0) {
        settings_log2("navigate: unknown pane ", pane_id ? pane_id : "(null)");
        return -1;
    }
    show_pane(idx, 1);
    return 0;
}

/* Control Center "pending panel" ids (control_center.c CC_PANEL_* and the
 * legacy Settings.app enum) mapped to pane ids. */
static const char *cc_panel_to_id(int panel) {
    switch (panel) {
        case 1:  return "network";
        case 2:  return "network";       /* Bluetooth lives with networking */
        case 3:  return "display";
        case 4:  return "sound";
        case 5:  return "storage";
        case 6:  return "users";
        case 7:  return "privacy";
        case 8:  return "privacy";
        case 9:  return "appearance";
        case 15: return "privacy";       /* CC_PANEL_SECURITY */
        case 17: return "developer";
        case 18: return "general";
        case 19: return "keyboard";
        case 20: return "mouse";
        case 27: return "mouse";
        case 28: return "datetime";
        default: return 0;
    }
}

static void poll_pending_panel(void) {
    int p = control_center_get_pending_panel();
    if (p == 0) return;
    const char *id = cc_panel_to_id(p);
    if (!id) { settings_log("control center requested an unknown panel"); return; }
    settings_log2("control center request: ", id);
    settings_navigate(id);
    if (V.win_id) desktop_activate_window(V.win_id);
}

int settings_view_show_pane(const char *pane_id) {
    int idx = pane_index(pane_id);
    if (idx < 0) {
        settings_log2("show_pane: unknown pane ", pane_id ? pane_id : "(null)");
        return -1;
    }
    if (window_is_open()) {
        show_pane(idx, 1);
        return desktop_activate_window(V.win_id);
    }
    V.pending = idx;
    if (desktop_launch_app("System Settings") != 0) {
        V.pending = -1;
        settings_log("show_pane: launch failed");
        return -1;
    }
    return 0;
}

/* ============ App view entry points ============ */

int settings_view_open(uint32_t win_id) {
    if (win_id == 0) return -1;
    V.win_id = win_id;
    if (sprefs_init() != 0) settings_log("open: preferences unavailable, changes apply for this session only");
    static const char *const sizes[] = { "small", "medium", "large" };
    V.sidebar_size = sprefs_get_choice("appearance", "sidebar_icon_size", sizes, 3, 1);

    int start = V.current;
    if (V.pending >= 0) {
        start = V.pending;
        V.pending = -1;
    } else {
        const char *cc = cc_panel_to_id(control_center_get_pending_panel());
        if (cc && pane_index(cc) >= 0) start = pane_index(cc);
    }
    if (start < 0 || start >= PANE_COUNT) start = pane_index("general");
    V.hist_len = 0;
    V.hist_pos = 0;
    V.current = -1;
    V.search[0] = '\0';
    V.search_len = 0;
    V.focus = FOCUS_SIDEBAR;
    V.list_scroll = 0.0f;
    V.capture = CAP_NONE;
    V.pressed_row = -1;
    V.list_dragging = 0;
    V.shift = V.ctrl = V.alt = 0;
    V.ch = 560.0f; V.cw = 860.0f;
    show_pane(start, 1);
    settings_log("opened");
    return 0;
}

static nxc_rect_t back_btn(void) { return nxc_rect(SIDEBAR_W + 14.0f, 11.0f, 28.0f, 24.0f); }
static nxc_rect_t fwd_btn(void)  { return nxc_rect(SIDEBAR_W + 44.0f, 11.0f, 28.0f, 24.0f); }
static nxc_rect_t search_rect(void) { return nxc_rect(12.0f, 12.0f, SIDEBAR_W - 24.0f, NXGLASS_FIELD_H); }
static nxc_rect_t pane_area_local(void) {
    return nxc_rect(SIDEBAR_W, TOOLBAR_H, V.cw - SIDEBAR_W, V.ch - TOOLBAR_H);
}

static nxc_rect_t at(nxc_rect_t content, nxc_rect_t local) {
    return nxc_rect(content.x + local.x, content.y + local.y, local.w, local.h);
}

static void render_sidebar(nxc_canvas_t *c, nxc_rect_t R) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_rail(c, nxc_rect(R.x, R.y, SIDEBAR_W, R.h));

    uint32_t fst = (V.focus == FOCUS_SEARCH) ? NXGLASS_FOCUSED : 0;
    if (V.hover_valid && nxglass_hit(search_rect(), V.mx, V.my)) fst |= NXGLASS_HOVER;
    nxglass_field(c, at(R, search_rect()), V.search, "Search", 1, fst);

    nxc_rect_t lr = at(R, list_rect());
    float ys[PANE_COUNT];
    float total = list_layout(ys);
    float rh = k_row_h[V.sidebar_size];
    float ts = k_tile_s[V.sidebar_size];
    int hover = (V.hover_valid && !V.list_dragging) ? list_row_at(V.mx, V.my) : -1;

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)lr.x, (int32_t)lr.y, (int32_t)lr.w, (int32_t)lr.h));
    int shown = 0, last_group = -1;
    for (int i = 0; i < PANE_COUNT; i++) {
        if (ys[i] < 0.0f) continue;
        shown++;
        float y = lr.y + ys[i] - V.list_scroll;
        int g = g_panes[i].group;
        if (g != last_group) {
            last_group = g;
            if (g >= 0 && g < GROUP_COUNT)
                nxframe_rail_group(c, lr.x + NXFRAME_RAIL_PAD + 12.0f, y - NXFRAME_RAIL_GROUP_H,
                                   lr.w - 2.0f * NXFRAME_RAIL_PAD, g_group_names[g]);
        }
        if (y + rh < lr.y || y > lr.y + lr.h) continue;
        nxc_rect_t row = nxc_rect(lr.x + NXFRAME_RAIL_PAD, y, lr.w - 2.0f * NXFRAME_RAIL_PAD, rh);
        int sel = (i == V.current);
        uint32_t st = sel ? NXFRAME_SELECTED : (i == hover ? NXFRAME_HOVER : 0);
        if (i == V.pressed_row && V.capture == CAP_LIST && !V.list_dragging && !sel) st |= NXFRAME_PRESSED;
        nxframe_rail_item(c, row, NXFRAME_SYM_SLOT, g_panes[i].pane->title, NULL, st);
        /* Monochrome glyph in the item's glyph slot (accent when selected) */
        settings_draw_glyph(c, &g_panes[i].tile, row.x + NXFRAME_GLYPH_CX, y + rh * 0.5f, ts,
                            sel ? th->accent : th->text_secondary);
    }
    if (shown == 0)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(lr.x, lr.y + 20.0f, lr.w, 24.0f), "No Results",
                          th->text_tertiary, NXC_ALIGN_CENTER);
    nxc_restore(c);

    /* Edge rule when the list is scrolled */
    if (V.list_scroll > 0.5f)
        nxframe_rule(c, R.x + 12.0f, lr.y - 1.0f, SIDEBAR_W - 24.0f);
    if (total - V.list_scroll > lr.h + 1.0f)
        nxframe_rule(c, R.x + 12.0f, lr.y + lr.h, SIDEBAR_W - 24.0f);
}

static void render_toolbar(nxc_canvas_t *c, nxc_rect_t R, const settings_pane_t *p) {
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t b = at(R, back_btn()), f = at(R, fwd_btn());
    uint32_t bs = can_back() ? 0 : NXGLASS_DISABLED, fs = can_fwd() ? 0 : NXGLASS_DISABLED;
    if (can_back() && V.hover_valid && nxglass_hit(back_btn(), V.mx, V.my)) bs |= NXGLASS_HOVER;
    if (can_fwd() && V.hover_valid && nxglass_hit(fwd_btn(), V.mx, V.my)) fs |= NXGLASS_HOVER;
    if (V.capture == CAP_BACK) bs |= NXGLASS_PRESSED;
    if (V.capture == CAP_FWD) fs |= NXGLASS_PRESSED;
    nxglass_button(c, b, NULL, NXGLASS_BUTTON_PLAIN, bs);
    nxglass_button(c, f, NULL, NXGLASS_BUTTON_PLAIN, fs);
    nxsym_draw(c, NXSYM_CHEVRON_LEFT, b.x + b.w * 0.5f, b.y + b.h * 0.5f, 12.0f,
               can_back() ? th->text_primary : th->text_tertiary, 0.0f);
    nxsym_draw(c, NXSYM_CHEVRON_RIGHT, f.x + f.w * 0.5f, f.y + f.h * 0.5f, 12.0f,
               can_fwd() ? th->text_primary : th->text_tertiary, 0.0f);

    /* Title: shown once the large in-pane title scrolls away, or always
     * for panes that do not draw one through sui. */
    int is_sui = p && p->on_show == sui_pane_reset;
    float sc = is_sui ? sui_scroll_offset() : 100.0f;
    if (p && sc > 36.0f) {
        float a = (sc - 36.0f) / 24.0f;
        if (a > 1.0f) a = 1.0f;
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(f.x + f.w + 12.0f, R.y, V.cw - SIDEBAR_W - 120.0f, TOOLBAR_H),
                          p->title, NXC_WITH_ALPHA(th->text_primary, (uint8_t)(255.0f * a)), NXC_ALIGN_LEFT);
        nxglass_separator(c, R.x + SIDEBAR_W, R.y + TOOLBAR_H - 1.0f, V.cw - SIDEBAR_W);
    }
}

void settings_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_canvas_t *c = nxui_canvas();
    if (!c) return;
    if (win->id != V.win_id) V.win_id = win->id;   /* window re-created without open (defensive) */
    poll_pending_panel();

    nxc_rect_t R = app_view_content_rect(win);
    V.cw = R.w; V.ch = R.h;
    if (R.w < SIDEBAR_W + 160.0f || R.h < 120.0f) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, R, "Window too small", nxui_theme()->text_tertiary, NXC_ALIGN_CENTER);
        return;
    }
    list_clamp();
    render_sidebar(c, R);

    const settings_pane_t *p = (V.current >= 0 && V.current < PANE_COUNT) ? g_panes[V.current].pane : 0;
    desktop_set_window_place(win->id, p ? p->title : NULL);
    nxc_rect_t pa = at(R, pane_area_local());
    if (p && p->render) {
        nxc_save(c);
        nxc_clip_rect(c, nxc_irect((int32_t)pa.x, (int32_t)pa.y, (int32_t)pa.w, (int32_t)pa.h));
        p->render(c, pa);
        nxc_restore(c);
    }
    render_toolbar(c, R, p);
}

/* ============ Pointer ============ */

void settings_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)ctx;
    if (!win || V.ch <= 0.0f) return;
    V.mx = lx; V.my = ly;
    V.hover_valid = 1;
    uint8_t press = (b & 1) && !(pb & 1);
    uint8_t release = !(b & 1) && (pb & 1);
    const settings_pane_t *p = (V.current >= 0 && V.current < PANE_COUNT) ? g_panes[V.current].pane : 0;
    nxc_rect_t pa = pane_area_local();

    if (press) {
        V.capture = CAP_NONE;
        if (nxglass_hit(search_rect(), lx, ly)) {
            V.capture = CAP_SEARCH;
            V.focus = FOCUS_SEARCH;
        } else if (nxglass_hit(list_rect(), lx, ly)) {
            V.capture = CAP_LIST;
            V.focus = FOCUS_SIDEBAR;
            V.pressed_row = list_row_at(lx, ly);
            V.press_y = ly;
            V.list_scroll_start = V.list_scroll;
            V.list_dragging = 0;
        } else if (nxglass_hit(back_btn(), lx, ly)) {
            V.capture = CAP_BACK;
        } else if (nxglass_hit(fwd_btn(), lx, ly)) {
            V.capture = CAP_FWD;
        } else if (nxglass_hit(pa, lx, ly)) {
            V.capture = CAP_PANE;
            V.focus = FOCUS_DETAIL;
        } else if (lx < (int32_t)SIDEBAR_W) {
            V.focus = FOCUS_SIDEBAR;
        }
    }

    switch (V.capture) {
        case CAP_LIST:
            if ((b & 1) && !press) {
                int32_t dy = ly - V.press_y;
                if (!V.list_dragging && (dy > 5 || dy < -5)) V.list_dragging = 1;
                if (V.list_dragging) {
                    V.list_scroll = V.list_scroll_start - (float)dy;
                    list_clamp();
                }
            }
            if (release) {
                if (!V.list_dragging && V.pressed_row >= 0 && list_row_at(lx, ly) == V.pressed_row)
                    show_pane(V.pressed_row, 1);
                V.pressed_row = -1;
                V.list_dragging = 0;
            }
            break;
        case CAP_BACK:
            if (release && nxglass_hit(back_btn(), lx, ly)) go_back();
            break;
        case CAP_FWD:
            if (release && nxglass_hit(fwd_btn(), lx, ly)) go_fwd();
            break;
        case CAP_PANE:
            if (p && p->mouse) p->mouse(lx - (int32_t)pa.x, ly - (int32_t)pa.y, b, pb);
            break;
        default:
            /* Hover only: let the pane track the pointer for hover states. */
            if (!(b & 1) && p && p->mouse && nxglass_hit(pa, lx, ly))
                p->mouse(lx - (int32_t)pa.x, ly - (int32_t)pa.y, b, pb);
            else if (!(b & 1))
                sui_pointer_left();
            break;
    }
    if (release) V.capture = CAP_NONE;
}

/* ============ Scroll wheel ============ */

void settings_view_scroll(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx) {
    (void)ctx;
    if (!win || V.ch <= 0.0f || notches == 0) return;
    if (V.capture == CAP_LIST && V.list_dragging) return;   /* drag in progress wins */
    if (nxglass_hit(list_rect(), lx, ly)) {
        V.list_scroll += (float)notches * k_row_h[V.sidebar_size];
        list_clamp();
        return;
    }
    const settings_pane_t *p = (V.current >= 0 && V.current < PANE_COUNT) ? g_panes[V.current].pane : 0;
    nxc_rect_t pa = pane_area_local();
    if (p && p->scroll && nxglass_hit(pa, lx, ly))
        p->scroll(lx - (int32_t)pa.x, ly - (int32_t)pa.y, notches);
}

/* ============ Keyboard ============ */

static char sc_to_ascii(uint8_t sc, int shift) {
    static const char lower[0x3A] = {
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', 0, 0,
        'q','w','e','r','t','y','u','i','o','p','[',']', 0, 0, 'a','s',
        'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
        'b','n','m',',','.','/', 0, 0, 0, ' '
    };
    static const char upper_num[] = "!@#$%^&*()_+";
    if (sc >= sizeof(lower)) return 0;
    char ch = lower[sc];
    if (!ch) return 0;
    if (shift) {
        if (ch >= 'a' && ch <= 'z') return (char)(ch - 32);
        if (sc >= 0x02 && sc <= 0x0D) return upper_num[sc - 0x02];
    }
    return ch;
}

static void search_changed(void) {
    V.list_scroll = 0.0f;
    list_clamp();
}

static void move_selection(int dir) {
    int i = V.current;
    for (int n = 0; n < PANE_COUNT; n++) {
        i += dir;
        if (i < 0 || i >= PANE_COUNT) return;
        if (pane_matches(i)) { show_pane(i, 1); return; }
    }
}

static void cycle_focus(int reverse) {
    static const int order[3] = { FOCUS_SIDEBAR, FOCUS_DETAIL, FOCUS_SEARCH };
    int k = 0;
    for (int i = 0; i < 3; i++) if (order[i] == V.focus) k = i;
    k = (k + (reverse ? 2 : 1)) % 3;
    V.focus = order[k];
}

void settings_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)win; (void)ctx;
    /* Modifiers (press and release) */
    if (sc == 0x2A || sc == 0x36) { V.shift = pressed ? 1 : 0; return; }
    if (sc == 0x1D) { V.ctrl = pressed ? 1 : 0; return; }
    if (sc == 0x38) { V.alt = pressed ? 1 : 0; return; }
    const settings_pane_t *p = (V.current >= 0 && V.current < PANE_COUNT) ? g_panes[V.current].pane : 0;
    char ch = sc_to_ascii(sc, V.shift);
    if (!pressed) {
        if (V.focus == FOCUS_DETAIL && p && p->key) p->key(sc, 0, ch);
        return;
    }

    /* Global shortcuts */
    if ((V.alt && sc == 0x4B) || (V.ctrl && sc == 0x1A)) { go_back(); return; }
    if ((V.alt && sc == 0x4D) || (V.ctrl && sc == 0x1B)) { go_fwd(); return; }
    if (V.ctrl && sc == 0x21) { V.focus = FOCUS_SEARCH; return; }          /* Ctrl+F */
    if (sc == 0x0F) { cycle_focus(V.shift); return; }                      /* Tab */
    if (V.ctrl || V.alt) return;

    switch (V.focus) {
        case FOCUS_SEARCH:
            if (sc == 0x01) {                                               /* Esc */
                if (V.search_len) { V.search_len = 0; V.search[0] = '\0'; search_changed(); }
                else V.focus = FOCUS_SIDEBAR;
            } else if (sc == 0x0E) {                                        /* Backspace */
                if (V.search_len > 0) { V.search[--V.search_len] = '\0'; search_changed(); }
            } else if (sc == 0x1C || sc == 0x50) {                          /* Enter, Down */
                int f = first_match();
                if (f >= 0) show_pane(f, 1);
                V.focus = FOCUS_SIDEBAR;
            } else if (ch && V.search_len < SEARCH_MAX) {
                V.search[V.search_len++] = ch;
                V.search[V.search_len] = '\0';
                search_changed();
            }
            break;
        case FOCUS_SIDEBAR:
            if (sc == 0x48) move_selection(-1);
            else if (sc == 0x50) move_selection(1);
            else if (sc == 0x4D || sc == 0x1C) V.focus = FOCUS_DETAIL;
            else if (sc == 0x01 && V.search_len) { V.search_len = 0; V.search[0] = '\0'; search_changed(); }
            else if (ch && ch != ' ' && V.search_len < SEARCH_MAX) {        /* type to search */
                V.focus = FOCUS_SEARCH;
                V.search[V.search_len++] = ch;
                V.search[V.search_len] = '\0';
                search_changed();
            }
            break;
        case FOCUS_DETAIL:
        default:
            if (p && p->key && p->key(sc, 1, ch)) return;
            if (sc == 0x4B || sc == 0x01) V.focus = FOCUS_SIDEBAR;          /* Left, Esc */
            break;
    }
}
