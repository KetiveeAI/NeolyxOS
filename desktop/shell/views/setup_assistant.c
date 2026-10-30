/*
 * NeolyxOS - Setup Assistant (first-run registration and setup flow)
 *
 * Full-screen layer over a blurred wallpaper with one centered glass card
 * (NXGLASS_THICK). This file owns the flow: which page is current, the
 * spring page transitions, the step indicator, the Back/Continue footer,
 * keyboard focus and all input while active. Page content lives in
 * setup_pages.c; validation and persistence in setup_store.c.
 *
 * Activation: setup_assistant_init() activates the flow when the
 * setup-completed marker is missing. In the host preview it always starts
 * inactive (other areas' screenshots must not be covered); scripts use
 * "call setup_assistant_show".
 *
 * Keys: Tab / Shift+Tab move between fields and lists, arrows change the
 * focused list, Space flips the focused switch, Enter continues, Esc goes
 * back.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "setup_internal.h"

/* PS/2 set-1 scancodes */
#define SC_ESC        0x01
#define SC_BACKSPACE  0x0E
#define SC_TAB        0x0F
#define SC_ENTER      0x1C
#define SC_LSHIFT     0x2A
#define SC_RSHIFT     0x36
#define SC_SPACE      0x39
#define SC_UP         0x48
#define SC_LEFT       0x4B
#define SC_RIGHT      0x4D
#define SC_DOWN       0x50

#define SETUP_MAX_HITS 64

typedef struct {
    nxc_rect_t       r;
    setup_hit_kind_t kind;
    int              slot, item;
} setup_hit_t;

static struct {
    int           active;
    setup_page_t  page;
    setup_page_t  prev;
    int           dir;            /* +1 forward, -1 back */
    nxui_spring_t trans;          /* 0 -> 1 per page change */
    nxui_spring_t step_pos;       /* animated step indicator position */
    float         intro;          /* 0..1 whole-layer fade in */
    int           exiting;
    float         exit_t;         /* 1 -> 0 on exit */
    int           focus;          /* slot index on the current page, -1 none */
    int           kbd_nav;        /* show focus rings after keyboard navigation */
    int           shift;
    int32_t       mx, my;
    uint8_t       pressed_kind;   /* footer button held */
    setup_hit_t   hits[SETUP_MAX_HITS];
    int           nhits;
    int           collecting;     /* hits accepted this frame */
    nxc_rect_t    card;
} g_sa;

setup_model_t g_setup;

/* ============ Model defaults ============ */

static void model_reset(void) {
    /* Wipe every secret before the memory is reused. */
    setup_field_clear(&g_setup.password);
    setup_field_clear(&g_setup.verify);
    setup_field_clear(&g_setup.kid_password);
    setup_field_clear(&g_setup.net_password);
    setup_secure_wipe(&g_setup, sizeof(g_setup));

    g_setup.language = 0;
    g_setup.region = 0;
    g_setup.keyboard = 0;
    g_setup.net_count = 0;
    g_setup.net_sel = -1;
    g_setup.net_joined = -1;
    g_setup.password.secret = 1;
    g_setup.verify.secret = 1;
    g_setup.kid_password.secret = 1;
    g_setup.net_password.secret = 1;
    /* Privacy and analytics are all off by default (secure_wipe zeroed them). */
    g_setup.appearance = nxui_appearance() == NXUI_APPEARANCE_LIGHT ? SETUP_APPEAR_LIGHT : SETUP_APPEAR_DARK;
    g_setup.accent = 0;
    g_setup.result_prefs = g_setup.result_account = g_setup.result_marker = SETUP_ERR_NOT_FOUND;
}

/* ============ Shared helpers for pages ============ */

void setup_hit_add(nxc_rect_t r, setup_hit_kind_t kind, int slot, int item) {
    if (!g_sa.collecting || g_sa.nhits >= SETUP_MAX_HITS) return;
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    setup_hit_t *h = &g_sa.hits[g_sa.nhits++];
    h->r = r; h->kind = kind; h->slot = slot; h->item = item;
}

int setup_focus_slot(void) { return g_sa.focus; }

int setup_focus_ring_visible(int slot) { return g_sa.kbd_nav && g_sa.focus == slot; }

int setup_hover(nxc_rect_t r) {
    return g_sa.collecting && nxglass_hit(r, g_sa.mx, g_sa.my);
}

int setup_reduce_motion(void) { return g_setup.ax[SETUP_AX_REDUCE_MOTION] != 0; }

/* ============ Navigation ============ */

static int first_focus(setup_page_t p) {
    setup_slot_t slots[SETUP_MAX_SLOTS];
    int n = setup_page_slots(p, slots, SETUP_MAX_SLOTS);
    /* Prefer the first text field so typing works straight away. */
    for (int i = 0; i < n; i++)
        if (slots[i].kind == SETUP_SLOT_FIELD) return i;
    return n > 0 ? 0 : -1;
}

static void go_to(setup_page_t p, int dir) {
    if (p < 0 || p >= SP_COUNT || p == g_sa.page) return;
    g_sa.prev = g_sa.page;
    g_sa.page = p;
    g_sa.dir = dir;
    g_sa.trans.value = setup_reduce_motion() ? 1.0f : 0.0f;
    g_sa.trans.velocity = 0.0f;
    g_sa.nhits = 0;
    setup_page_enter(p);                 /* may change the slot set (network scan) */
    g_sa.focus = first_focus(p);
    static const char *const names[SP_COUNT] = {
        "welcome", "language", "region", "accessibility", "network", "account",
        "ketivee-id", "privacy", "appearance", "setting-up", "done" };
    setup_log2("[SETUP] page: ", names[p]);
}

static void go_next(void) {
    if (g_sa.exiting) return;
    if (g_sa.page == SP_SETTING_UP) return;           /* advances by itself */
    if (g_sa.page == SP_DONE) {
        g_sa.exiting = 1;
        setup_log("[SETUP] finished by user, closing assistant\n");
        return;
    }
    if (!setup_page_validate(g_sa.page)) {
        setup_log("[SETUP] continue blocked by validation\n");
        return;
    }
    go_to((setup_page_t)(g_sa.page + 1), +1);
}

static void go_back(void) {
    if (g_sa.exiting) return;
    if (g_sa.page <= SP_WELCOME || g_sa.page >= SP_SETTING_UP) return;
    go_to((setup_page_t)(g_sa.page - 1), -1);
}

static int back_enabled(void) {
    return g_sa.page > SP_WELCOME && g_sa.page < SP_SETTING_UP;
}

/* ============ Lifecycle ============ */

static void activate(void) {
    model_reset();
    g_sa.active = 1;
    g_sa.page = SP_WELCOME;
    g_sa.prev = SP_WELCOME;
    g_sa.dir = 1;
    g_sa.trans.value = 1.0f;
    g_sa.trans.velocity = 0.0f;
    g_sa.step_pos.value = 0.0f;
    g_sa.step_pos.velocity = 0.0f;
    g_sa.intro = 0.0f;
    g_sa.exiting = 0;
    g_sa.exit_t = 1.0f;
    g_sa.focus = -1;
    g_sa.shift = 0;
    g_sa.kbd_nav = 0;
    g_sa.nhits = 0;
    setup_page_enter(SP_WELCOME);
}

void setup_assistant_init(void) {
    g_sa.active = 0;
#ifdef NX_HOST_PREVIEW
    /* Preview: stay out of the way unless a script calls setup_assistant_show. */
    setup_log("[SETUP] preview build: assistant inactive until setup_assistant_show\n");
#else
    if (setup_store_marker_present()) {
        setup_log("[SETUP] setup-completed marker found, assistant inactive\n");
        return;
    }
    setup_log("[SETUP] no setup-completed marker, starting first-run setup\n");
    activate();
#endif
}

int setup_assistant_active(void) { return g_sa.active; }

void setup_assistant_show(void) {
    setup_log("[SETUP] assistant shown on request\n");
    activate();
}

static void deactivate(void) {
    /* Last chance: make sure no secret survives the assistant. */
    setup_field_clear(&g_setup.password);
    setup_field_clear(&g_setup.verify);
    setup_field_clear(&g_setup.kid_password);
    setup_field_clear(&g_setup.net_password);
    g_sa.active = 0;
    g_sa.nhits = 0;
    setup_log("[SETUP] assistant closed\n");
}

void setup_assistant_tick(float dt) {
    if (!g_sa.active) return;
    if (!(dt >= 0.0f)) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    int rm = setup_reduce_motion();

    if (rm) { g_sa.trans.value = 1.0f; g_sa.trans.velocity = 0.0f; }
    else nxui_spring_step(&g_sa.trans, 1.0f, 220.0f, 2.0f * 14.83f, dt);
    if (g_sa.trans.value > 0.999f) g_sa.trans.value = 1.0f;

    float step_target = 0.0f;
    if (g_sa.page >= SETUP_FIRST_STEP && g_sa.page <= SETUP_LAST_STEP)
        step_target = (float)(g_sa.page - SETUP_FIRST_STEP);
    else if (g_sa.page > SETUP_LAST_STEP)
        step_target = (float)(SETUP_STEP_COUNT - 1);
    if (rm) g_sa.step_pos.value = step_target;
    else nxui_spring_step(&g_sa.step_pos, step_target, 300.0f, 2.0f * 17.32f, dt);

    g_sa.intro = rm ? 1.0f : nxui_approach(g_sa.intro, 1.0f, 0.12f, dt);
    if (g_sa.exiting) {
        g_sa.exit_t = rm ? 0.0f : nxui_approach(g_sa.exit_t, 0.0f, 0.08f, dt);
        if (g_sa.exit_t < 0.02f) { deactivate(); return; }
    }

    if (setup_page_tick(g_sa.page, dt) && g_sa.trans.value >= 1.0f)
        go_to((setup_page_t)(g_sa.page + 1), +1);
}

/* ============ Rendering ============ */

static void draw_backdrop(nxc_canvas_t *c, float w, float h) {
    const nxc_surface_t *wb = nxui_wallpaper_blurred();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    if (wb && wb->width > 0 && wb->height > 0) {
        nxc_draw_surface(c, wb, nxc_irect(0, 0, wb->width, wb->height),
                         nxc_rect(0.0f, 0.0f, w, h), 255, NXC_FILTER_NEAREST);
    } else {
        nxc_fill_rect(c, nxc_rect(0.0f, 0.0f, w, h), light ? NXC_RGB(0xE8, 0xEA, 0xF0) : NXC_RGB(0x10, 0x12, 0x18));
    }
    nxc_fill_rect(c, nxc_rect(0.0f, 0.0f, w, h),
                  light ? NXC_ARGB(70, 0xF4, 0xF5, 0xF8) : NXC_ARGB(110, 0x06, 0x08, 0x0E));
}

static void draw_steps(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    const float dot = 7.0f, gap = 9.0f, pill = 22.0f;
    float total = (float)(SETUP_STEP_COUNT - 1) * (dot + gap) + pill;
    float x = card.x + (card.w - total) * 0.5f;
    float y = card.y + 26.0f;
    float pos = g_sa.step_pos.value;
    for (int i = 0; i < SETUP_STEP_COUNT; i++) {
        float d = (float)i - pos;
        if (d < 0.0f) d = -d;
        float k = d < 1.0f ? 1.0f - d : 0.0f;          /* 1 at the current step */
        float wdt = dot + (pill - dot) * k;
        int done = (float)i < pos - 0.5f;
        nxc_color_t col = k > 0.5f ? th->accent
                        : done ? NXC_WITH_ALPHA(th->accent, 140) : NXC_WITH_ALPHA(th->text_tertiary, 110);
        nxc_fill_rrect(c, nxc_rect(x, y, wdt, dot), dot * 0.5f, col);
        x += wdt + gap;
    }
}

static void draw_footer(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    float fy = card.y + card.h - 64.0f;
    if (g_sa.page == SP_SETTING_UP) return;   /* no controls while applying */
    nxglass_separator(c, card.x + 24.0f, fy, card.w - 48.0f);

    const char *cont = setup_page_continue_label(g_sa.page);
    const char *sec = setup_page_secondary_label(g_sa.page);
    float by = fy + 16.0f, bh = 32.0f;

    if (back_enabled()) {
        nxc_rect_t br = nxc_rect(card.x + 24.0f, by, 96.0f, bh);
        uint32_t st = (setup_hover(br) ? NXGLASS_HOVER : 0u) |
                      (g_sa.pressed_kind == SETUP_HIT_BACK ? NXGLASS_PRESSED : 0u);
        nxglass_button(c, br, "Back", NXGLASS_BUTTON_DEFAULT, st);
        setup_hit_add(br, SETUP_HIT_BACK, -1, 0);
    }

    float right = card.x + card.w - 24.0f;
    if (cont) {
        float cw = nxui_text_width(NXUI_TEXT_BODY_MEDIUM, cont) + 44.0f;
        if (cw < 120.0f) cw = 120.0f;
        nxc_rect_t cr = nxc_rect(right - cw, by, cw, bh);
        uint32_t st = (setup_hover(cr) ? NXGLASS_HOVER : 0u) |
                      (g_sa.pressed_kind == SETUP_HIT_CONTINUE ? NXGLASS_PRESSED : 0u);
        nxglass_button(c, cr, cont, NXGLASS_BUTTON_PRIMARY, st);
        setup_hit_add(cr, SETUP_HIT_CONTINUE, -1, 0);
        right = cr.x - 10.0f;
    }
    if (sec) {
        float sw = nxui_text_width(NXUI_TEXT_BODY_MEDIUM, sec) + 32.0f;
        nxc_rect_t sr = nxc_rect(right - sw, by, sw, bh);
        uint32_t st = (setup_hover(sr) ? NXGLASS_HOVER : 0u) |
                      (g_sa.pressed_kind == SETUP_HIT_SECONDARY ? NXGLASS_PRESSED : 0u);
        nxglass_button(c, sr, sec, NXGLASS_BUTTON_DEFAULT, st);
        setup_hit_add(sr, SETUP_HIT_SECONDARY, -1, 0);
    }

    if (g_sa.page >= SETUP_FIRST_STEP && g_sa.page <= SETUP_LAST_STEP) {
        char cap[24];
        int n = (int)(g_sa.page - SETUP_FIRST_STEP) + 1;
        /* "Step N of M" without printf */
        const char *pre = "Step ";
        size_t k = 0;
        while (*pre) cap[k++] = *pre++;
        if (n >= 10) cap[k++] = (char)('0' + n / 10);
        cap[k++] = (char)('0' + n % 10);
        const char *mid = " of ";
        while (*mid) cap[k++] = *mid++;
        cap[k++] = (char)('0' + SETUP_STEP_COUNT % 10);
        cap[k] = '\0';
        float cx = card.x + card.w * 0.5f;
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(cx - 80.0f, by, 160.0f, bh), cap,
                          th->text_tertiary, NXC_ALIGN_CENTER);
    }
}

static void draw_page_layer(nxc_canvas_t *c, setup_page_t p, nxc_rect_t card, float dx, float alpha) {
    if (alpha <= 0.01f) return;
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(alpha * 255.0f));
    nxc_translate(c, dx, 0.0f);
    setup_page_render(c, p, card);
    nxc_restore(c);
}

void setup_assistant_render(nxc_canvas_t *c) {
    if (!g_sa.active || !c) return;
    float sw = (float)nxui_width(), sh = (float)nxui_height();
    if (sw <= 0.0f || sh <= 0.0f) return;

    float layer = g_sa.intro * g_sa.exit_t;
    if (layer < 0.0f) layer = 0.0f;
    if (layer > 1.0f) layer = 1.0f;

    float cw = sw - 64.0f < 760.0f ? sw - 64.0f : 760.0f;
    float ch = sh - 64.0f < 580.0f ? sh - 64.0f : 580.0f;
    if (cw < 320.0f || ch < 360.0f) return;   /* screen too small to lay out */
    nxc_rect_t card = nxc_rect((float)(int)((sw - cw) * 0.5f), (float)(int)((sh - ch) * 0.5f), cw, ch);
    g_sa.card = card;

    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(layer * 255.0f));
    draw_backdrop(c, sw, sh);

    /* The card scales in slightly on entry and fades on exit. */
    float settle = g_sa.exiting ? 1.0f : layer;
    float lift = (1.0f - settle) * 18.0f;
    nxc_rect_t cr = nxc_rect(card.x, card.y + lift, card.w, card.h);
    int still = lift < 0.5f;
    if (still) cr = card;
    nxglass_panel(c, cr, 22.0f, NXGLASS_THICK, 4, !still);

    /* Hits are collected only for a settled, fully visible page. */
    g_sa.nhits = 0;
    float t = g_sa.trans.value;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    g_sa.collecting = still && t >= 1.0f && !g_sa.exiting;

    nxc_save(c);
    nxc_clip_rrect(c, cr, 22.0f);
    nxc_translate(c, 0.0f, cr.y - card.y);
    if (g_sa.page >= SETUP_FIRST_STEP && g_sa.page <= SP_APPEARANCE) draw_steps(c, card);
    if (t < 1.0f) {
        const float travel = 70.0f;
        float e = nxui_ease_out_cubic(t);
        draw_page_layer(c, g_sa.prev, card, -(float)g_sa.dir * travel * e, 1.0f - e);
        draw_page_layer(c, g_sa.page, card, (float)g_sa.dir * travel * (1.0f - e), e);
    } else {
        draw_page_layer(c, g_sa.page, card, 0.0f, 1.0f);
    }
    draw_footer(c, card);
    nxc_restore(c);

    g_sa.collecting = 0;
    nxc_restore(c);
}

/* ============ Input ============ */

static void slot_activate(int slot, int item) {
    setup_slot_t slots[SETUP_MAX_SLOTS];
    int n = setup_page_slots(g_sa.page, slots, SETUP_MAX_SLOTS);
    if (slot < 0 || slot >= n) return;
    g_sa.focus = slot;
    setup_slot_t *s = &slots[slot];
    switch (s->kind) {
        case SETUP_SLOT_LIST:
            if (s->sel && item >= 0 && item < s->count && *s->sel != item) {
                *s->sel = item;
                setup_page_changed(g_sa.page, slot);
            }
            break;
        case SETUP_SLOT_TOGGLE:
            if (s->flag) {
                *s->flag = (uint8_t)!*s->flag;
                setup_page_changed(g_sa.page, slot);
            }
            break;
        case SETUP_SLOT_FIELD:
        default:
            break;
    }
}

static int hit_at(int32_t x, int32_t y) {
    for (int i = g_sa.nhits - 1; i >= 0; i--)
        if (nxglass_hit(g_sa.hits[i].r, x, y)) return i;
    return -1;
}

int setup_assistant_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_sa.active) return 0;
    g_sa.mx = x;
    g_sa.my = y;
    int down = (buttons & 1) && !(prev_buttons & 1);
    int up = !(buttons & 1) && (prev_buttons & 1);
    if (g_sa.exiting || g_sa.trans.value < 1.0f) { g_sa.pressed_kind = 0; return 1; }

    int h = hit_at(x, y);
    if (down) {
        g_sa.pressed_kind = 0;
        if (h < 0) return 1;
        setup_hit_t hit = g_sa.hits[h];
        g_sa.kbd_nav = 0;
        if (hit.kind == SETUP_HIT_SLOT) {
            slot_activate(hit.slot, hit.item);
        } else {
            g_sa.pressed_kind = (uint8_t)hit.kind;   /* buttons act on release */
        }
        return 1;
    }
    if (up && g_sa.pressed_kind) {
        uint8_t k = g_sa.pressed_kind;
        g_sa.pressed_kind = 0;
        if (h < 0 || (uint8_t)g_sa.hits[h].kind != k) return 1;   /* released elsewhere */
        if (k == SETUP_HIT_BACK) go_back();
        else if (k == SETUP_HIT_CONTINUE) go_next();
        else if (k == SETUP_HIT_SECONDARY) {
            if (setup_page_secondary(g_sa.page)) go_to((setup_page_t)(g_sa.page + 1), +1);
        }
    }
    return 1;
}

int setup_assistant_handle_key(uint8_t scancode, uint8_t pressed, char ascii) {
    if (!g_sa.active) return 0;
    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) { g_sa.shift = pressed ? 1 : 0; return 1; }
    if (!pressed || g_sa.exiting) return 1;

    if (scancode == SC_ENTER) { if (g_sa.trans.value >= 1.0f) go_next(); return 1; }
    if (scancode == SC_ESC)   { if (g_sa.trans.value >= 1.0f) go_back(); return 1; }

    setup_slot_t slots[SETUP_MAX_SLOTS];
    int n = setup_page_slots(g_sa.page, slots, SETUP_MAX_SLOTS);
    if (scancode == SC_TAB) {
        g_sa.kbd_nav = 1;
        if (n > 0) {
            int f = g_sa.focus < 0 ? (g_sa.shift ? n - 1 : 0)
                                   : (g_sa.focus + (g_sa.shift ? n - 1 : 1)) % n;
            g_sa.focus = f;
        }
        return 1;
    }
    if (g_sa.focus < 0 || g_sa.focus >= n) return 1;
    setup_slot_t *s = &slots[g_sa.focus];

    switch (s->kind) {
        case SETUP_SLOT_FIELD:
            if (!s->field) break;
            if (scancode == SC_BACKSPACE) {
                if (setup_field_backspace(s->field) == SETUP_OK) setup_page_changed(g_sa.page, g_sa.focus);
            } else if (ascii) {
                if (setup_field_append(s->field, ascii) == SETUP_OK) setup_page_changed(g_sa.page, g_sa.focus);
            }
            break;
        case SETUP_SLOT_LIST: {
            if (!s->sel || s->count <= 0) break;
            g_sa.kbd_nav = 1;
            int d = 0;
            if (s->horizontal) d = scancode == SC_LEFT ? -1 : scancode == SC_RIGHT ? 1 : 0;
            else d = scancode == SC_UP ? -1 : scancode == SC_DOWN ? 1 : 0;
            if (d) {
                int v = *s->sel < 0 ? (d > 0 ? 0 : s->count - 1) : *s->sel + d;
                if (v < 0) v = 0;
                if (v >= s->count) v = s->count - 1;
                if (v != *s->sel) { *s->sel = v; setup_page_changed(g_sa.page, g_sa.focus); }
            }
            break;
        }
        case SETUP_SLOT_TOGGLE:
            if (scancode == SC_SPACE && s->flag) {
                *s->flag = (uint8_t)!*s->flag;
                setup_page_changed(g_sa.page, g_sa.focus);
            }
            break;
    }
    return 1;
}

#ifdef NX_HOST_PREVIEW
/* PREVIEW ONLY: lets scripts capture the flow in light appearance
 * ("call setup_assistant_preview_light"). Not built into the system. */
void setup_assistant_preview_light(void);
void setup_assistant_preview_light(void) {
    nxui_set_appearance(NXUI_APPEARANCE_LIGHT);
    if (g_sa.active) g_setup.appearance = SETUP_APPEAR_LIGHT;
}
#endif
