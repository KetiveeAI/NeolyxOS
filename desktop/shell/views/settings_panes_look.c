/*
 * NeolyxOS - System Settings: look & feel panes
 *
 * Appearance, Display (Night Mode), Wallpaper, Dock & Taskbar, Fonts.
 *
 * Live system state is the source of truth for what the system applies
 * today (NXUI appearance, Night Mode strength, dock settings); every
 * change is applied, saved through settings_prefs.c and audited.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "dock.h"
#include "desktop_shell.h"
#include "control_center.h"
#include "nxappearance.h"

/* ============ Appearance ============ */

#define ACCENT_COUNT 9
static const nxc_color_t k_accent[ACCENT_COUNT] = {
    0xFFE8660A, 0xFF0A84FF, 0xFFAF52DE, 0xFFFF375F, 0xFFFF453A,
    0xFFFF9F0A, 0xFFFFD60A, 0xFF30D158, 0xFF8E8E93
};
static const char *const k_accent_names[ACCENT_COUNT] = {
    "Saffron", "Blue", "Purple", "Pink", "Red", "Orange", "Yellow", "Green", "Graphite"
};
static const char *const k_accent_ids[ACCENT_COUNT] = {
    "saffron", "blue", "purple", "pink", "red", "orange", "yellow", "green", "graphite"
};

nxc_color_t settings_accent_color(void) {
    int i = sprefs_get_choice("appearance", "accent", k_accent_ids, ACCENT_COUNT, 0);
    return k_accent[i];
}

/* A miniature desktop used as the Light/Dark choice (drawn locally:
 * nxglass has no thumbnail picker control). */
static void draw_mode_thumb(nxc_canvas_t *c, nxc_rect_t r, int dark, int selected, uint32_t st) {
    const nxui_theme_t *th = nxui_theme();
    nxc_color_t top = dark ? NXC_RGB(0x1B, 0x22, 0x3A) : NXC_RGB(0xC9, 0xD6, 0xEE);
    nxc_color_t bot = dark ? NXC_RGB(0x3A, 0x1F, 0x4A) : NXC_RGB(0xF4, 0xD9, 0xC4);
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x + r.w, r.y + r.h, top, bot);
    nxc_fill_rrect_paint(c, r, 8.0f, &p);
    /* menu bar */
    nxc_save(c);
    nxc_clip_rrect(c, r, 8.0f);
    nxc_fill_rect(c, nxc_rect(r.x, r.y, r.w, 8.0f), dark ? NXC_ARGB(170, 0x10, 0x14, 0x20) : NXC_ARGB(170, 0xFF, 0xFF, 0xFF));
    nxc_restore(c);
    /* window */
    nxc_rect_t w = nxc_rect(r.x + r.w * 0.18f, r.y + r.h * 0.28f, r.w * 0.64f, r.h * 0.52f);
    nxc_fill_rrect(c, w, 4.0f, dark ? NXC_RGB(0x24, 0x28, 0x33) : NXC_RGB(0xFB, 0xFB, 0xFD));
    nxc_fill_rrect(c, nxc_rect(w.x, w.y, w.w * 0.32f, w.h), 4.0f, dark ? NXC_RGB(0x2E, 0x33, 0x40) : NXC_RGB(0xEC, 0xEE, 0xF2));
    nxc_fill_rrect(c, nxc_rect(w.x + w.w * 0.42f, w.y + 6.0f, w.w * 0.45f, 4.0f), 2.0f, th->accent);
    nxc_fill_rrect(c, nxc_rect(w.x + w.w * 0.42f, w.y + 14.0f, w.w * 0.3f, 3.0f), 1.5f,
                   dark ? NXC_ARGB(120, 0xFF, 0xFF, 0xFF) : NXC_ARGB(90, 0, 0, 0));
    nxc_stroke_rrect(c, r, 8.0f, 1.0f, th->separator);
    if (selected)
        nxc_stroke_rrect(c, nxc_rect(r.x - 4.0f, r.y - 4.0f, r.w + 8.0f, r.h + 8.0f), 11.0f, 2.5f, th->accent);
    else if (st & NXGLASS_HOVER)
        nxc_stroke_rrect(c, nxc_rect(r.x - 4.0f, r.y - 4.0f, r.w + 8.0f, r.h + 8.0f), 11.0f, 2.0f, th->control_fill_hover);
}

static void apply_appearance(int dark) {
    nxui_set_appearance(dark ? NXUI_APPEARANCE_DARK : NXUI_APPEARANCE_LIGHT);
    appearance_settings_t *a = nxappearance_get();
    if (a) a->theme = dark ? THEME_DARK : THEME_LIGHT;
    control_center_state_t *cc = control_center_get_state();
    if (cc) cc->dark_mode.enabled = dark;            /* keep the Control Center tile in step */
    static const char *const modes[] = { "light", "dark" };
    sprefs_set_choice("appearance", "mode", modes, 2, dark ? 1 : 0);
    settings_log(dark ? "appearance: dark" : "appearance: light");
}

static void build_appearance(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "appearance", "Appearance", "Choose how NeolyxOS looks: light or dark, accent color and sidebar size.");
    int dark = nxui_appearance() == NXUI_APPEARANCE_DARK;

    sui_card_begin(ui);
    nxc_rect_t blk = sui_block(ui, 132.0f);
    float tw = 128.0f, tg = 28.0f;
    float label_w = 120.0f;
    float tx0 = blk.x + blk.w - 2.0f * tw - tg;
    if (tx0 < blk.x + label_w) tx0 = blk.x + label_w;
    for (int i = 0; i < 2; i++) {
        int is_dark = (i == 1);
        nxc_rect_t r = nxc_rect(tx0 + (float)i * (tw + tg), blk.y + 18.0f, tw, 80.0f);
        uint32_t st = 0;
        if (sui_hotspot(ui, nxc_rect(r.x - 4.0f, r.y - 4.0f, r.w + 8.0f, r.h + 30.0f), &st) && dark != is_dark) {
            apply_appearance(is_dark);
            dark = is_dark;
        }
        if (ui->pass == SUI_DRAW) {
            draw_mode_thumb(ui->c, r, is_dark, dark == is_dark, st);
            nxui_text_in_rect(ui->c, dark == is_dark ? NXUI_TEXT_BODY_MEDIUM : NXUI_TEXT_BODY,
                              nxc_rect(r.x, r.y + r.h + 8.0f, r.w, 18.0f), is_dark ? "Dark" : "Light",
                              dark == is_dark ? th->text_primary : th->text_secondary, NXC_ALIGN_CENTER);
        }
    }
    if (ui->pass == SUI_DRAW)
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(blk.x, blk.y, label_w, blk.h), "Appearance",
                          th->text_primary, NXC_ALIGN_LEFT);

    int accent = sprefs_get_choice("appearance", "accent", k_accent_ids, ACCENT_COUNT, 0);
    if (sui_swatches(ui, "Accent color", k_accent, k_accent_names, ACCENT_COUNT, &accent))
        sprefs_set_choice("appearance", "accent", k_accent_ids, ACCENT_COUNT, accent);
    sui_card_end(ui);
    sui_note(ui, "The accent color is saved. The desktop keeps its Saffron accent until NXUI can apply another one.");

    static const char *const sizes[] = { "small", "medium", "large" };
    static const char *const size_labels[] = { "Small", "Medium", "Large" };
    int sz = settings_sidebar_size();
    sui_card_begin(ui);
    if (sui_segmented(ui, "Sidebar icon size", size_labels, 3, &sz)) {
        settings_set_sidebar_size(sz);
        sprefs_set_choice("appearance", "sidebar_icon_size", sizes, 3, sz);
    }
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_appearance, "appearance", "Appearance", build_appearance);

/* ============ Display ============ */

static void build_display(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "display", "Display", "Resolution of the connected display and Night Mode warmth.");
    uint32_t w = 0, h = 0, bpp = 0;
    int ok = settings_sys_display(&w, &h, &bpp) == 0;
    char res[40], n[16], depth[24];
    if (ok) {
        sx_itoa(w, res, (int)sizeof(res));
        sx_strcat(res, " x ", (int)sizeof(res));
        sx_strcat(res, sx_itoa(h, n, (int)sizeof(n)), (int)sizeof(res));
        sx_itoa(bpp, depth, (int)sizeof(depth));
        sx_strcat(depth, "-bit", (int)sizeof(depth));
    } else {
        sx_strcpy(res, "Unavailable", (int)sizeof(res));
        sx_strcpy(depth, "Unavailable", (int)sizeof(depth));
    }

    sui_card_begin(ui);
    nxc_rect_t blk = sui_block(ui, 160.0f);
    if (ui->pass == SUI_DRAW) {
        /* Monitor with the live wallpaper */
        float mw = 168.0f, mh = ok && w ? mw * (float)h / (float)w : 105.0f;
        if (mh > 108.0f) mh = 108.0f;
        float mx = blk.x + (blk.w - mw) * 0.5f, my = blk.y + 14.0f;
        nxc_rect_t scr = nxc_rect(mx, my, mw, mh);
        nxc_fill_rrect(ui->c, nxc_rect(mx - 4.0f, my - 4.0f, mw + 8.0f, mh + 8.0f), 7.0f, NXC_RGB(0x1A, 0x1C, 0x22));
        const nxc_surface_t *wp = nxui_wallpaper();
        if (wp) nxc_draw_surface_rrect(ui->c, wp, nxc_irect(0, 0, wp->width, wp->height), scr, 3.0f, 255, NXC_FILTER_SMOOTH);
        float night = desktop_get_night_mode();
        if (night > 0.001f) nxc_fill_rrect(ui->c, scr, 3.0f, NXC_ARGB((uint8_t)(night * 70.0f), 0xFF, 0x8A, 0x20));
        nxc_fill_rect(ui->c, nxc_rect(mx + mw * 0.5f - 10.0f, my + mh + 4.0f, 20.0f, 8.0f), NXC_RGB(0x5A, 0x5E, 0x68));
        nxc_fill_rrect(ui->c, nxc_rect(mx + mw * 0.5f - 30.0f, my + mh + 12.0f, 60.0f, 4.0f), 2.0f, NXC_RGB(0x5A, 0x5E, 0x68));
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(blk.x, my + mh + 24.0f, blk.w, 16.0f),
                          "Display 1", th->text_primary, NXC_ALIGN_CENTER);
    }
    sui_info(ui, "Resolution", res);
    sui_info(ui, "Color depth", depth);
    sui_info(ui, "Scaling", "Default (1x)");
    sui_card_end(ui);

    /* Night Mode: live strength from the shell; the warmth used when it is
     * turned on is remembered in prefs (and for the session while off). */
    static float warm_session = -1.0f;
    if (warm_session < 0.0f)
        warm_session = (float)sprefs_get_int("display", "night_warmth", 50, 10, 100) / 100.0f;
    float strength = desktop_get_night_mode();
    int on = strength > 0.001f;
    float warmth = on ? strength : warm_session;
    sui_section(ui, "Night Mode");
    sui_card_begin(ui);
    if (sui_toggle(ui, "Night Mode", "Warms the colors of the display to reduce blue light", &on)) {
        desktop_set_night_mode(on ? warm_session : 0.0f);
        sprefs_set_bool("display", "night_mode", on);
        settings_log(on ? "display: night mode on" : "display: night mode off");
    }
    int r = sui_slider(ui, "Warmth", &warmth, "Less", "More");
    if (r) {
        if (warmth < 0.1f) warmth = 0.1f;
        warm_session = warmth;
        if (on) desktop_set_night_mode(warmth);
        if (r == 2) {
            int pct = (int)(warmth * 100.0f + 0.5f);
            if (pct < 10) pct = 10;
            if (pct > 100) pct = 100;
            sprefs_set_int("display", "night_warmth", pct, 10, 100);
        }
    }
    sui_card_end(ui);
    sui_note(ui, "Brightness follows the display's own controls: the kernel has no backlight interface for desktop displays yet.");
}

SUI_DEFINE_PANE(settings_pane_display, "display", "Display", build_display);

/* ============ Wallpaper ============ */

static void build_wallpaper(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "wallpaper", "Wallpaper", "The picture behind your windows and the source of the frosted glass tint.");
    sui_card_begin(ui);
    float pw = ui->w - 28.0f;
    if (pw > 420.0f) pw = 420.0f;
    float ph = pw * (float)(nxui_height() > 0 ? nxui_height() : 800) / (float)(nxui_width() > 0 ? nxui_width() : 1280);
    nxc_rect_t blk = sui_block(ui, ph + 74.0f);
    if (ui->pass == SUI_DRAW) {
        nxc_rect_t pr = nxc_rect(blk.x + (blk.w - pw) * 0.5f, blk.y + 16.0f, pw, ph);
        const nxc_surface_t *wp = nxui_wallpaper();
        if (wp) {
            nxui_elevation(ui->c, pr, 8.0f, 1);
            nxc_draw_surface_rrect(ui->c, wp, nxc_irect(0, 0, wp->width, wp->height), pr, 8.0f, 255, NXC_FILTER_SMOOTH);
        } else {
            nxc_fill_rrect(ui->c, pr, 8.0f, th->control_fill);
            nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, pr, "No wallpaper loaded", th->text_tertiary, NXC_ALIGN_CENTER);
        }
        nxc_stroke_rrect(ui->c, pr, 8.0f, 1.0f, th->separator);
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(blk.x, pr.y + ph + 10.0f, blk.w, 18.0f),
                          "Vajra Aurora", th->text_primary, NXC_ALIGN_CENTER);
        nxui_text_in_rect(ui->c, NXUI_TEXT_FOOTNOTE, nxc_rect(blk.x, pr.y + ph + 30.0f, blk.w, 16.0f),
                          "Generated for this display at startup", th->text_secondary, NXC_ALIGN_CENTER);
    }
    char res[32], n[16];
    sx_itoa(nxui_width(), res, (int)sizeof(res));
    sx_strcat(res, " x ", (int)sizeof(res));
    sx_strcat(res, sx_itoa(nxui_height(), n, (int)sizeof(n)), (int)sizeof(res));
    sui_info(ui, "Size", res);
    sui_status(ui, "Frosted glass", nxui_wallpaper_blurred() ? "Active" : "Off",
               nxui_wallpaper_blurred() ? th->success : th->text_tertiary);
    sui_card_end(ui);
    sui_note(ui, "Choosing another picture needs a wallpaper selection interface in the desktop shell, which is not available yet.");
}

SUI_DEFINE_PANE(settings_pane_wallpaper, "wallpaper", "Wallpaper", build_wallpaper);

/* ============ Dock & Taskbar ============ */

static void build_dock(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "dock", "Dock & Taskbar", "Choose a floating Dock or a full-width taskbar, and how it behaves.");
    settings_dock_info_t d;
    if (settings_dock_read(&d) != 0 || !d.available) {
        sui_card_begin(ui);
        sui_status(ui, "Dock", "Not running", th->warning);
        sui_card_end(ui);
        return;
    }

    /* The dock API validates, applies, saves ([dock] section) and logs. */
    sui_card_begin(ui);
    static const char *const layouts[] = { "Dock", "Taskbar" };
    int layout = d.layout;
    if (sui_segmented(ui, "Style", layouts, 2, &layout)) settings_dock_set_layout(layout);
    if (layout == 0) {
        static const char *const pos_names[] = { "Left", "Bottom", "Right" };
        static const int pos_map[] = { 1, 0, 2 };   /* segment -> dock_position_t */
        int seg = d.position == 1 ? 0 : (d.position == 2 ? 2 : 1);
        if (sui_segmented(ui, "Position on screen", pos_names, 3, &seg)) settings_dock_set_position(pos_map[seg]);
    }
    sui_card_end(ui);

    sui_card_begin(ui);
    float size = (float)(d.icon_size - DOCK_ICON_SIZE_MIN) / (float)(DOCK_ICON_SIZE_MAX - DOCK_ICON_SIZE_MIN);
    if (sui_slider(ui, "Size", &size, "Small", "Large"))
        settings_dock_set_icon_size(DOCK_ICON_SIZE_MIN + (int)(size * (float)(DOCK_ICON_SIZE_MAX - DOCK_ICON_SIZE_MIN) + 0.5f));
    int mag = d.magnification;
    if (sui_toggle(ui, "Magnification", "Enlarge icons under the pointer", &mag))
        settings_dock_set_magnification(mag, d.magnification_amount);
    if (mag) {
        float amt = d.magnification_amount - 1.0f;
        if (sui_slider(ui, "Magnification size", &amt, "Small", "Large"))
            settings_dock_set_magnification(1, 1.0f + amt);
    }
    int ah = d.autohide;
    if (sui_toggle(ui, "Automatically hide and show", "Reveal by moving the pointer to the screen edge", &ah))
        settings_dock_set_autohide(ah);
    char n[16];
    sui_info(ui, "Apps in the Dock", sx_itoa(d.app_count, n, (int)sizeof(n)));
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_dock, "dock", "Dock & Taskbar", build_dock);

/* ============ Fonts ============ */

static void build_fonts(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "fonts", "Fonts", "The system typefaces used across the desktop and apps.");
    static const struct { const char *name; nxui_text_style_t style; nxui_font_id_t font; } faces[] = {
        { "NXSans Text",    NXUI_TEXT_BODY,        NXUI_FONT_REGULAR },
        { "NXSans UI",      NXUI_TEXT_BODY_MEDIUM, NXUI_FONT_MEDIUM },
        { "NXSans Bold",    NXUI_TEXT_HEADLINE,    NXUI_FONT_BOLD },
        { "NXSans Display", NXUI_TEXT_TITLE3,      NXUI_FONT_DISPLAY },
        { "NXSans Mono",    NXUI_TEXT_MONO,        NXUI_FONT_MONO },
    };
    int loaded = 0;
    sui_section(ui, "System Typefaces");
    sui_card_begin(ui);
    for (unsigned i = 0; i < sizeof(faces) / sizeof(faces[0]); i++) {
        int ok = nxui_font(faces[i].font) != 0;
        loaded += ok;
        nxc_rect_t b = sui_block(ui, 40.0f);
        if (ui->pass == SUI_DRAW) {
            nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(b.x, b.y, 150.0f, b.h), faces[i].name,
                              th->text_primary, NXC_ALIGN_LEFT);
            nxui_text_in_rect(ui->c, ok ? faces[i].style : NXUI_TEXT_FOOTNOTE,
                              nxc_rect(b.x + 160.0f, b.y, b.w - 160.0f, b.h),
                              ok ? "The quick brown fox jumps" : "Not loaded",
                              ok ? th->text_secondary : th->warning, NXC_ALIGN_RIGHT);
        }
    }
    sui_card_end(ui);

    char sum[48], n[8];
    sx_join3(sum, (int)sizeof(sum), sx_itoa(loaded, n, (int)sizeof(n)), " of 5 system faces loaded", NULL);
    sui_card_begin(ui);
    sui_info(ui, "Interface font", "NXSans UI, 13 pt");
    sui_info(ui, "Status", sum);
    if (sui_button_row(ui, "Font Manager", "Preview, install and manage fonts", "Open Font Manager",
                       NXGLASS_BUTTON_PRIMARY, 1))
        settings_open_app("Font Manager");
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_fonts, "fonts", "Fonts", build_fonts);
