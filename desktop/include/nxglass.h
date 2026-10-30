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

/* ======================================================================
 * Additions (Glass area). Everything below is additive; see
 * docs/desktop/glass.md for the design tokens and usage guide.
 * ====================================================================== */

/* ============ Design tokens ============ */

/* Vibrancy-friendly colors and geometry for the current appearance.
 * Text colors are tuned to stay legible over every glass level. */
typedef struct {
    /* Text on glass */
    nxc_color_t label;             /* primary text */
    nxc_color_t label_secondary;   /* supporting text */
    nxc_color_t label_tertiary;    /* placeholders, hints */
    nxc_color_t label_quaternary;  /* disabled */
    /* Control fills (translucent, sit on any glass level) */
    nxc_color_t fill;
    nxc_color_t fill_hover;
    nxc_color_t fill_pressed;
    nxc_color_t fill_inset;        /* text fields, wells */
    nxc_color_t raised;            /* selected segment, knobs on glass */
    /* Light */
    nxc_color_t rim_top;           /* 1px rim at the top-left (brightest) */
    nxc_color_t rim_bottom;        /* 1px rim at the bottom-right */
    nxc_color_t edge;              /* outer definition hairline */
    nxc_color_t separator;
    /* Accent */
    nxc_color_t accent;
    nxc_color_t accent_hover;
    nxc_color_t accent_pressed;
    nxc_color_t accent_soft;       /* accent at low alpha (selection wash) */
    nxc_color_t focus_ring;
    nxc_color_t on_accent;         /* text/icons on accent fills */
    /* Status */
    nxc_color_t success, warning, destructive, info;
    /* Geometry */
    float radius_small;            /* 6: checkboxes, keycaps */
    float radius_control;          /* 8: buttons, fields */
    float radius_card;             /* 12: cards, tiles */
    float radius_panel;            /* 14: popovers, panels */
    float radius_sheet;            /* 18: sheets, dialogs, HUDs */
    float spacing;                 /* 8: base grid unit */
} nxglass_tokens_t;

/* Tokens for the current appearance (never NULL; rebuilt on change). */
const nxglass_tokens_t *nxglass_tokens(void);

/* ============ More surfaces ============ */

/* Accent-tinted glass: the level's material with `tint` (alpha ignored)
 * blended into its tint color. Cached like nxglass_panel when live = 0. */
nxc_status_t nxglass_panel_tinted(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                  nxglass_level_t level, int32_t elevation, int live,
                                  nxc_color_t tint);

/* The glass light treatment alone (rim gradient, sheen, inner glow) for
 * surfaces drawn another way, e.g. window bodies. */
nxc_status_t nxglass_rim(nxc_canvas_t *c, nxc_rect_t r, float radius, nxglass_level_t level);

/* Dimming scrim behind modal sheets/dialogs; amount 0..1 (animate it). */
nxc_status_t nxglass_scrim(nxc_canvas_t *c, nxc_rect_t r, float amount);

typedef enum {
    NXGLASS_ARROW_NONE = 0,
    NXGLASS_ARROW_TOP,       /* arrow on the top edge pointing up */
    NXGLASS_ARROW_BOTTOM,
    NXGLASS_ARROW_LEFT,
    NXGLASS_ARROW_RIGHT
} nxglass_arrow_t;

#define NXGLASS_ARROW_SIZE 9.0f

/* Popover: REGULAR glass with an arrow on one edge. arrow_pos is the
 * arrow's center along that edge in absolute canvas units (clamped to
 * stay clear of the corners). r excludes the arrow. */
nxc_status_t nxglass_popover(nxc_canvas_t *c, nxc_rect_t r, nxglass_arrow_t arrow,
                             float arrow_pos, int live);

/* Sheet attached to the top of a window content rect: square top
 * corners, rounded bottom, THICK glass, deep shadow. */
nxc_status_t nxglass_sheet(nxc_canvas_t *c, nxc_rect_t r, int live);

/* Alert dialog: icon (NXSYM_COUNT = none), title, message (may be NULL)
 * and up to 3 buttons laid out right-to-left; default_button gets the
 * primary style, hovered/pressed (index or -1) get their states. */
#define NXGLASS_DIALOG_MAX_BUTTONS 3
nxc_status_t nxglass_dialog(nxc_canvas_t *c, nxc_rect_t r, int icon, const char *title,
                            const char *message, const char *const *buttons, int count,
                            int default_button, int hovered, int pressed);
/* Which dialog button (index into buttons) is under (x, y), or -1. */
int nxglass_dialog_button_at(nxc_rect_t r, const char *const *buttons, int count,
                             int32_t x, int32_t y);
/* Recommended dialog size for the content (width fixed at 320). */
nxc_rect_t nxglass_dialog_rect(float center_x, float center_y, const char *message);

/* Tooltip anchored below (anchor_x, anchor_y), clamped on screen. */
nxc_status_t nxglass_tooltip(nxc_canvas_t *c, float anchor_x, float anchor_y, const char *text);

/* ============ More controls ============ */

#define NXGLASS_CHECK_SIZE   16.0f
#define NXGLASS_STEPPER_W    64.0f
#define NXGLASS_STEPPER_H    24.0f
#define NXGLASS_ICON_BUTTON  28.0f
#define NXGLASS_SCROLLBAR_W  10.0f

/* Toggle hit rect for a toggle drawn at (x, y). */
nxc_rect_t nxglass_toggle_rect(float x, float y);

/* Checkbox (+ optional label to the right). checked_t 0..1 animates the
 * fill and check stroke; pass 0 or 1 if not animating. Returns the hit
 * rect (box + label) in *hit when non-NULL. */
nxc_status_t nxglass_checkbox(nxc_canvas_t *c, float x, float y, const char *label,
                              float checked_t, uint32_t state, nxc_rect_t *hit);
/* Radio button, same conventions. */
nxc_status_t nxglass_radio(nxc_canvas_t *c, float x, float y, const char *label,
                           float selected_t, uint32_t state, nxc_rect_t *hit);

/* Stepper [ - | + ] at (x, y). hovered/pressed: -1 none, 0 minus, 1 plus.
 * Disable a side with NXGLASS_DISABLED in minus_state / plus_state. */
nxc_status_t nxglass_stepper(nxc_canvas_t *c, float x, float y, int hovered, int pressed,
                             uint32_t minus_state, uint32_t plus_state);
/* -1 outside, 0 minus half, 1 plus half. */
int nxglass_stepper_part_at(float x, float y, int32_t px, int32_t py);

/* Small caps section header above a group of rows. */
nxc_status_t nxglass_section_header(nxc_canvas_t *c, float x, float y, float w, const char *title);

/* Round glass button with a symbol (NXSYM_*), size from r (square or pill).
 * NXGLASS_SELECTED draws it accent-filled. */
nxc_status_t nxglass_icon_button(nxc_canvas_t *c, nxc_rect_t r, int sym, uint32_t state);

/* Overlay scrollbar in track rect r (vertical when r.h > r.w).
 * content: total content length, view: visible length, offset: scroll
 * position (0..content-view). Hidden when content <= view. */
nxc_status_t nxglass_scrollbar(nxc_canvas_t *c, nxc_rect_t r, float content, float view,
                               float offset, uint32_t state);
/* Thumb rect for the same arguments (zero rect when hidden). */
nxc_rect_t nxglass_scrollbar_thumb(nxc_rect_t r, float content, float view, float offset);
/* Scroll offset that puts the thumb center at pointer (px, py). */
float nxglass_scrollbar_offset_at(nxc_rect_t r, float content, float view, int32_t px, int32_t py);

/* Large module slider (Control Center style): thick track, symbol inside
 * at the left. sym_value is passed to nxsym_draw (volume waves etc.). */
nxc_status_t nxglass_slider_large(nxc_canvas_t *c, nxc_rect_t r, float value, int sym,
                                  float sym_value, uint32_t state);

/* Module tile (Control Center style): round symbol well, title, optional
 * subtitle, optional trailing chevron. NXGLASS_SELECTED = on (well filled
 * with on_color). */
nxc_status_t nxglass_tile(nxc_canvas_t *c, nxc_rect_t r, int sym, const char *title,
                          const char *subtitle, nxc_color_t on_color, int chevron,
                          uint32_t state);

/* HUD (volume / brightness / keyboard OSD): ULTRATHIN glass, symbol,
 * text and an optional level bar (level < 0 = none). opacity 0..255. */
nxc_status_t nxglass_hud(nxc_canvas_t *c, nxc_rect_t r, int sym, float sym_value,
                         const char *title, const char *subtitle, float level,
                         nxc_color_t sym_color, uint8_t opacity);

/* ============ Window chrome ============ */

/* Traffic-light button centers relative to the title bar's left edge
 * (matching the shell's 12 px hit boxes). */
#define NXGLASS_TRAFFIC_X0     14.0f
#define NXGLASS_TRAFFIC_STEP   16.0f
#define NXGLASS_TRAFFIC_R       6.0f

/* Glass title bar in r (the top strip of a window frame, rounded top
 * corners of `radius`). hovered_controls shows the glyphs in the lights. */
nxc_status_t nxglass_titlebar(nxc_canvas_t *c, nxc_rect_t r, float radius, const char *title,
                              int focused, int hovered_controls);
/* 0 close, 1 minimize, 2 zoom, -1 none. */
int nxglass_titlebar_button_at(nxc_rect_t r, int32_t x, int32_t y);

/* ============ Preview gallery ============ */

/* Show/hide a full-screen gallery of every level and control in the
 * current appearance (design review; not shown to users by default).
 * Drawn by nxglass_gallery_render(), which keyboard_osd_help_render()
 * calls as the last overlay of the frame. */
void nxglass_gallery_toggle(void);
int  nxglass_gallery_visible(void);
void nxglass_gallery_render(nxc_canvas_t *c);
/* Switch appearance (for screenshots via the preview `call` command). */
void nxglass_gallery_light(void);
void nxglass_gallery_dark(void);

#endif /* NXGLASS_H */
