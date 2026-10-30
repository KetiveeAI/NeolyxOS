/*
 * NeolyxOS - NXGlass: the shared glassmorphism layer
 *
 * One visual language for every shell surface and shell-hosted app:
 * frosted glass levels, glass cards, sidebars and the standard controls
 * (buttons, toggles, sliders, segmented controls, list rows, fields,
 * progress). Built on NXUI (materials, theme, type) and NXCanvas.
 *
 * Rules for callers:
 *   - Draw through these helpers instead of hand-rolling fills, so a
 *     change to the glass look lands everywhere at once.
 *   - All rects are absolute canvas coordinates.
 *   - Controls are immediate-mode: pass the current state flags, keep
 *     your own model; use nxglass_hit() for hit testing.
 *   - The API is additive: existing signatures do not change.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXGLASS_H
#define NXGLASS_H

#include <stdint.h>
#include "nxcanvas.h"
#include "nxui.h"

/* ============ Glass levels ============ */

typedef enum {
    NXGLASS_ULTRATHIN = 0,   /* most transparent: HUDs over content */
    NXGLASS_THIN,            /* menus, popovers, dock */
    NXGLASS_REGULAR,         /* sheets, panels, Stage strip */
    NXGLASS_THICK,           /* window sidebars, setup assistant cards */
    NXGLASS_CHROME,          /* menu bar, title bars */
    NXGLASS_LEVEL_COUNT
} nxglass_level_t;

/* The material for a level in the current appearance (never NULL). */
const nxc_material_t *nxglass_material(nxglass_level_t level);

/* Frosted panel with soft shadow (elevation 0..4, see nxui_elevation).
 * Cached when static (uses nxui_panel); pass live=1 for panels that
 * move or resize every frame. */
nxc_status_t nxglass_panel(nxc_canvas_t *c, nxc_rect_t r, float radius,
                           nxglass_level_t level, int32_t elevation, int live);

/* A grouped card inside a window or panel: subtle fill + hairline. */
nxc_status_t nxglass_card(nxc_canvas_t *c, nxc_rect_t r, float radius);

/* Window sidebar background (left column of Settings, Font Manager...). */
nxc_status_t nxglass_sidebar(nxc_canvas_t *c, nxc_rect_t r);

/* Horizontal hairline separator. */
void nxglass_separator(nxc_canvas_t *c, float x, float y, float w);

/* ============ Controls ============ */

#define NXGLASS_HOVER     0x01u
#define NXGLASS_PRESSED   0x02u
#define NXGLASS_FOCUSED   0x04u
#define NXGLASS_DISABLED  0x08u
#define NXGLASS_SELECTED  0x10u

typedef enum {
    NXGLASS_BUTTON_DEFAULT = 0,  /* glass fill */
    NXGLASS_BUTTON_PRIMARY,      /* accent fill */
    NXGLASS_BUTTON_DESTRUCTIVE,  /* red fill */
    NXGLASS_BUTTON_PLAIN         /* text only, fill on hover */
} nxglass_button_kind_t;

int nxglass_hit(nxc_rect_t r, int32_t x, int32_t y);

/* Standard control heights */
#define NXGLASS_BUTTON_H    28.0f
#define NXGLASS_ROW_H       36.0f
#define NXGLASS_FIELD_H     30.0f
#define NXGLASS_TOGGLE_W    38.0f
#define NXGLASS_TOGGLE_H    22.0f

nxc_status_t nxglass_button(nxc_canvas_t *c, nxc_rect_t r, const char *label,
                            nxglass_button_kind_t kind, uint32_t state);
/* on_t: 0..1 animated position (pass 0 or 1 if not animating). */
nxc_status_t nxglass_toggle(nxc_canvas_t *c, float x, float y, float on_t, uint32_t state);
/* value 0..1 */
nxc_status_t nxglass_slider(nxc_canvas_t *c, nxc_rect_t r, float value, uint32_t state);
/* Map a pointer x to a slider value 0..1 for the same rect. */
float nxglass_slider_value_at(nxc_rect_t r, int32_t x);
nxc_status_t nxglass_segmented(nxc_canvas_t *c, nxc_rect_t r, const char *const *labels,
                               int count, int selected, int hovered);
/* Which segment x falls in, or -1. */
int nxglass_segment_at(nxc_rect_t r, int count, int32_t x, int32_t y);
/* List/sidebar row: optional symbol icon (NXSYM_COUNT = none), title,
 * optional trailing detail text (NULL = none). */
nxc_status_t nxglass_row(nxc_canvas_t *c, nxc_rect_t r, int icon, const char *title,
                         const char *detail, uint32_t state);
/* Text field / search field. placeholder shown when text is empty. */
nxc_status_t nxglass_field(nxc_canvas_t *c, nxc_rect_t r, const char *text,
                           const char *placeholder, int search, uint32_t state);
/* Determinate progress 0..1, or indeterminate when value < 0 (uses nxui_time). */
nxc_status_t nxglass_progress(nxc_canvas_t *c, nxc_rect_t r, float value);
/* Small rounded status badge ("Connected", "Running"...). */
nxc_status_t nxglass_badge(nxc_canvas_t *c, float x, float y, const char *label, nxc_color_t color);

#endif /* NXGLASS_H */
