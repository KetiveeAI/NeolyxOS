/*
 * NeolyxOS - NXGlass: the shared glassmorphism layer (see nxglass.h)
 *
 * First working implementation on top of the NXUI theme materials. The
 * Glass area owns this file; callers depend only on nxglass.h.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxglass.h"
#include "nxicons.h"

/* ============ Glass levels ============ */

const nxc_material_t *nxglass_material(nxglass_level_t level) {
    const nxui_theme_t *th = nxui_theme();
    switch (level) {
        case NXGLASS_ULTRATHIN: return &th->mat_hud;
        case NXGLASS_THIN:      return &th->mat_dock;
        case NXGLASS_REGULAR:   return &th->mat_popover;
        case NXGLASS_THICK:     return &th->mat_sidebar;
        case NXGLASS_CHROME:    return &th->mat_menubar;
        default:                return &th->mat_popover;
    }
}

nxc_status_t nxglass_panel(nxc_canvas_t *c, nxc_rect_t r, float radius,
                           nxglass_level_t level, int32_t elevation, int live) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (r.w <= 0.0f || r.h <= 0.0f) return NXC_OK;
    const nxc_material_t *m = nxglass_material(level);
    if (!live) return nxui_panel(c, r, radius, m, elevation);
    if (elevation > 0) nxui_elevation(c, r, radius, elevation);
    return nxui_material(c, r, radius, m);
}

nxc_status_t nxglass_card(nxc_canvas_t *c, nxc_rect_t r, float radius) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rrect(c, r, radius, th->control_fill);
    nxc_stroke_rrect(c, r, radius, 1.0f, th->separator);
    return NXC_OK;
}

nxc_status_t nxglass_sidebar(nxc_canvas_t *c, nxc_rect_t r) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, r, NXC_WITH_ALPHA(th->mat_sidebar.tint, 140));
    nxc_fill_rect(c, nxc_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), th->separator);
    return NXC_OK;
}

void nxglass_separator(nxc_canvas_t *c, float x, float y, float w) {
    if (!c || w <= 0.0f) return;
    nxc_fill_rect(c, nxc_rect(x, y, w, 1.0f), nxui_theme()->separator);
}

/* ============ Controls ============ */

int nxglass_hit(nxc_rect_t r, int32_t x, int32_t y) {
    return (float)x >= r.x && (float)x < r.x + r.w && (float)y >= r.y && (float)y < r.y + r.h;
}

nxc_status_t nxglass_button(nxc_canvas_t *c, nxc_rect_t r, const char *label,
                            nxglass_button_kind_t kind, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    nxc_color_t fill, text = th->text_primary;
    switch (kind) {
        case NXGLASS_BUTTON_PRIMARY:
            fill = (state & NXGLASS_PRESSED) ? th->accent_pressed : th->accent;
            text = th->text_on_accent;
            break;
        case NXGLASS_BUTTON_DESTRUCTIVE:
            fill = th->destructive;
            text = th->text_on_accent;
            break;
        case NXGLASS_BUTTON_PLAIN:
            fill = (state & (NXGLASS_HOVER | NXGLASS_PRESSED)) ? th->control_fill_hover : NXC_ARGB(0, 0, 0, 0);
            text = th->accent;
            break;
        default:
            fill = (state & NXGLASS_PRESSED) ? th->control_fill_pressed
                 : (state & NXGLASS_HOVER) ? th->control_fill_hover : th->control_fill;
            break;
    }
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 110);
    nxc_fill_rrect(c, r, th->radius_control, fill);
    if (kind == NXGLASS_BUTTON_DEFAULT)
        nxc_fill_rect(c, nxc_rect(r.x + th->radius_control, r.y, r.w - 2.0f * th->radius_control, 1.0f), th->highlight);
    if (state & NXGLASS_FOCUSED)
        nxc_stroke_rrect(c, nxc_rect(r.x - 2.0f, r.y - 2.0f, r.w + 4.0f, r.h + 4.0f),
                         th->radius_control + 2.0f, 2.0f, NXC_WITH_ALPHA(th->accent, 150));
    if (label) nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, r, label, text, NXC_ALIGN_CENTER);
    nxc_restore(c);
    return NXC_OK;
}

nxc_status_t nxglass_toggle(nxc_canvas_t *c, float x, float y, float on_t, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    if (on_t < 0.0f) on_t = 0.0f;
    if (on_t > 1.0f) on_t = 1.0f;
    nxc_rect_t r = nxc_rect(x, y, NXGLASS_TOGGLE_W, NXGLASS_TOGGLE_H);
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 110);
    nxc_fill_rrect(c, r, r.h * 0.5f, th->control_fill);
    if (on_t > 0.0f) nxc_fill_rrect(c, r, r.h * 0.5f, NXC_WITH_ALPHA(th->control_fill_on, (uint8_t)(255.0f * on_t)));
    float knob_r = r.h * 0.5f - 2.0f;
    float kx = r.x + 2.0f + knob_r + on_t * (r.w - 4.0f - 2.0f * knob_r);
    nxc_fill_circle(c, kx, r.y + r.h * 0.5f, knob_r, NXC_ARGB(255, 0xFF, 0xFF, 0xFF));
    nxc_restore(c);
    return NXC_OK;
}

nxc_status_t nxglass_slider(nxc_canvas_t *c, nxc_rect_t r, float value, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    float cy = r.y + r.h * 0.5f;
    nxc_rect_t track = nxc_rect(r.x, cy - 2.0f, r.w, 4.0f);
    nxc_fill_rrect(c, track, 2.0f, th->control_fill);
    nxc_fill_rrect(c, nxc_rect(r.x, cy - 2.0f, r.w * value, 4.0f), 2.0f, th->accent);
    float kx = r.x + r.w * value;
    nxc_fill_circle(c, kx, cy, (state & NXGLASS_PRESSED) ? 9.0f : 8.0f, NXC_ARGB(255, 0xFF, 0xFF, 0xFF));
    nxc_stroke_circle(c, kx, cy, 8.0f, 0.75f, NXC_ARGB(50, 0, 0, 0));
    return NXC_OK;
}

float nxglass_slider_value_at(nxc_rect_t r, int32_t x) {
    if (r.w <= 0.0f) return 0.0f;
    float v = ((float)x - r.x) / r.w;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

nxc_status_t nxglass_segmented(nxc_canvas_t *c, nxc_rect_t r, const char *const *labels,
                               int count, int selected, int hovered) {
    if (!c || !labels || count <= 0) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rrect(c, r, th->radius_control, th->control_fill);
    float sw = r.w / (float)count;
    for (int i = 0; i < count; i++) {
        nxc_rect_t s = nxc_rect(r.x + sw * (float)i, r.y, sw, r.h);
        if (i == selected) {
            nxc_rect_t k = nxc_rect(s.x + 2.0f, s.y + 2.0f, s.w - 4.0f, s.h - 4.0f);
            nxc_fill_rrect(c, k, th->radius_control - 2.0f, th->control_fill_hover);
            nxc_fill_rect(c, nxc_rect(k.x + 4.0f, k.y, k.w - 8.0f, 1.0f), th->highlight);
        } else if (i == hovered) {
            nxc_fill_rrect(c, nxc_rect(s.x + 2.0f, s.y + 2.0f, s.w - 4.0f, s.h - 4.0f),
                           th->radius_control - 2.0f, NXC_WITH_ALPHA(th->control_fill_hover, 90));
        }
        if (labels[i])
            nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, s, labels[i],
                              i == selected ? th->text_primary : th->text_secondary, NXC_ALIGN_CENTER);
    }
    return NXC_OK;
}

int nxglass_segment_at(nxc_rect_t r, int count, int32_t x, int32_t y) {
    if (count <= 0 || !nxglass_hit(r, x, y)) return -1;
    int i = (int)(((float)x - r.x) / (r.w / (float)count));
    return i < 0 ? 0 : (i >= count ? count - 1 : i);
}

nxc_status_t nxglass_row(nxc_canvas_t *c, nxc_rect_t r, int icon, const char *title,
                         const char *detail, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    if (state & NXGLASS_SELECTED) nxc_fill_rrect(c, r, th->radius_control, th->selection);
    else if (state & NXGLASS_HOVER) nxc_fill_rrect(c, r, th->radius_control, th->control_fill_hover);
    float tx = r.x + 10.0f;
    if (icon >= 0 && icon < (int)NXSYM_COUNT) {
        nxsym_draw(c, (nxsym_t)icon, r.x + 20.0f, r.y + r.h * 0.5f, 16.0f,
                   (state & NXGLASS_SELECTED) ? th->accent : th->text_secondary, 0.0f);
        tx = r.x + 36.0f;
    }
    float dw = detail ? nxui_text_width(NXUI_TEXT_BODY, detail) + 12.0f : 0.0f;
    if (title)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(tx, r.y, r.x + r.w - tx - dw, r.h), title,
                          (state & NXGLASS_DISABLED) ? th->text_tertiary : th->text_primary, NXC_ALIGN_LEFT);
    if (detail)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(r.x + r.w - dw, r.y, dw - 10.0f, r.h), detail,
                          th->text_secondary, NXC_ALIGN_RIGHT);
    return NXC_OK;
}

nxc_status_t nxglass_field(nxc_canvas_t *c, nxc_rect_t r, const char *text,
                           const char *placeholder, int search, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rrect(c, r, th->radius_control, th->control_fill);
    if (state & NXGLASS_FOCUSED)
        nxc_stroke_rrect(c, r, th->radius_control, 1.5f, NXC_WITH_ALPHA(th->accent, 200));
    float tx = r.x + 10.0f;
    if (search) {
        nxsym_draw(c, NXSYM_SEARCH, r.x + 16.0f, r.y + r.h * 0.5f, 13.0f, th->text_tertiary, 0.0f);
        tx = r.x + 28.0f;
    }
    int empty = !text || !text[0];
    nxc_rect_t tr = nxc_rect(tx, r.y, r.x + r.w - tx - 8.0f, r.h);
    nxui_text_in_rect(c, NXUI_TEXT_BODY, tr, empty ? (placeholder ? placeholder : "") : text,
                      empty ? th->text_tertiary : th->text_primary, NXC_ALIGN_LEFT);
    if ((state & NXGLASS_FOCUSED) && ((int)(nxui_time() * 2.0f) & 1) == 0) {
        float cx = tx + (empty ? 0.0f : nxui_text_width(NXUI_TEXT_BODY, text)) + 1.0f;
        if (cx > r.x + r.w - 8.0f) cx = r.x + r.w - 8.0f;
        nxc_fill_rect(c, nxc_rect(cx, r.y + 7.0f, 1.5f, r.h - 14.0f), th->accent);
    }
    return NXC_OK;
}

nxc_status_t nxglass_progress(nxc_canvas_t *c, nxc_rect_t r, float value) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxui_theme_t *th = nxui_theme();
    float rad = r.h * 0.5f;
    nxc_fill_rrect(c, r, rad, th->control_fill);
    if (value < 0.0f) {
        float t = nxui_time();
        float seg = r.w * 0.3f;
        float p = t - (float)(int)t;
        float x = r.x - seg + (r.w + seg) * p;
        nxc_save(c);
        nxc_clip_rrect(c, r, rad);
        nxc_fill_rrect(c, nxc_rect(x, r.y, seg, r.h), rad, th->accent);
        nxc_restore(c);
    } else {
        if (value > 1.0f) value = 1.0f;
        if (value > 0.0f) nxc_fill_rrect(c, nxc_rect(r.x, r.y, r.w * value, r.h), rad, th->accent);
    }
    return NXC_OK;
}

nxc_status_t nxglass_badge(nxc_canvas_t *c, float x, float y, const char *label, nxc_color_t color) {
    if (!c || !label) return NXC_ERR_INVALID_ARG;
    float w = nxui_text_width(NXUI_TEXT_CAPTION, label) + 14.0f;
    nxc_rect_t r = nxc_rect(x, y, w, 18.0f);
    nxc_fill_rrect(c, r, 9.0f, NXC_WITH_ALPHA(color, 50));
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, r, label, color, NXC_ALIGN_CENTER);
    return NXC_OK;
}
