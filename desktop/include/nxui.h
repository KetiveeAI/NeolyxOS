/*
 * NeolyxOS - NXUI: desktop shell visual layer
 *
 * The design system every shell component draws with: one frame canvas
 * backed by a double-buffered surface, the system typefaces, a semantic
 * color/material/elevation theme for dark and light appearance, the
 * wallpaper (and its pre-blurred copy used by frosted materials), a
 * userspace cursor and time/easing helpers. Built on NXCanvas
 * (nxcanvas.h) and the vector icon set (nxicons.h).
 *
 * Frame lifecycle (desktop_shell.c owns it):
 *   nxui_frame_begin()            reset canvas state, draw wallpaper
 *   ... components draw into nxui_canvas() in z-order ...
 *   nxui_frame_end(mx, my)        draw cursor, flip backbuffer to screen
 *
 * Component rule: never write framebuffer pixels directly. Draw through
 * the canvas returned by nxui_canvas(); restore any state you push
 * (nxc_save/nxc_restore must balance within a component's render call).
 *
 * Window content callbacks run with the canvas translated so (0, 0) is
 * the top-left of the window's content area and clipped (with the
 * window's rounded bottom corners) to that area.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXUI_H
#define NXUI_H

#include <stdint.h>
#include "nxcanvas.h"
#include "nxicons.h"

/* ============ Lifecycle ============ */

/* Map the framebuffer, allocate the backbuffer, initialize NXCanvas,
 * load the embedded system fonts, build the wallpaper and its blurred
 * copy, and initialize the icon cache. Logs every step. */
nxc_status_t nxui_init(void);

/* Start a frame: resets canvas state and draws the wallpaper. */
void nxui_frame_begin(void);
/* Finish a frame: draws the cursor at (cursor_x, cursor_y) and presents
 * the backbuffer through the kernel flip syscall. */
nxc_status_t nxui_frame_end(int32_t cursor_x, int32_t cursor_y);

/* The frame canvas (valid after nxui_init). */
nxc_canvas_t *nxui_canvas(void);
int32_t nxui_width(void);
int32_t nxui_height(void);

/* Seconds since nxui_init (monotonic, millisecond resolution). */
float nxui_time(void);
/* Seconds elapsed since the previous nxui_frame_begin (clamped 0..0.1). */
float nxui_frame_dt(void);

/* ============ Appearance & theme ============ */

typedef enum {
    NXUI_APPEARANCE_DARK  = 0,
    NXUI_APPEARANCE_LIGHT = 1
} nxui_appearance_t;

typedef struct {
    nxui_appearance_t appearance;

    /* Text */
    nxc_color_t text_primary;
    nxc_color_t text_secondary;
    nxc_color_t text_tertiary;
    nxc_color_t text_on_accent;

    /* Accents and status */
    nxc_color_t accent;
    nxc_color_t accent_pressed;
    nxc_color_t destructive;
    nxc_color_t success;
    nxc_color_t warning;

    /* Lines and controls */
    nxc_color_t separator;          /* hairlines between groups */
    nxc_color_t highlight;          /* 1px top highlight on raised glass */
    nxc_color_t control_fill;       /* buttons/tiles at rest */
    nxc_color_t control_fill_hover;
    nxc_color_t control_fill_pressed;
    nxc_color_t control_fill_on;    /* toggles that are on (accent based) */
    nxc_color_t selection;          /* selected row/item background */

    /* Materials (frosted glass) */
    nxc_material_t mat_menubar;
    nxc_material_t mat_dock;
    nxc_material_t mat_popover;     /* control center, menus, notifications */
    nxc_material_t mat_hud;         /* volume/brightness OSD */
    nxc_material_t mat_widget;      /* desktop widgets (clock) */
    nxc_material_t mat_sidebar;     /* window sidebars */

    /* Windows */
    nxc_color_t window_bg;
    nxc_color_t window_titlebar;
    nxc_color_t window_titlebar_inactive;
    nxc_color_t window_border;      /* 1px outline, low alpha */
    nxc_color_t traffic_close;
    nxc_color_t traffic_minimize;
    nxc_color_t traffic_zoom;
    nxc_color_t traffic_inactive;

    /* Shadow color (alpha is scaled per elevation) */
    nxc_color_t shadow;

    /* Geometry */
    float radius_window;            /* 12 */
    float radius_popover;           /* 14 */
    float radius_dock;              /* 22 */
    float radius_widget;            /* 20 */
    float radius_control;           /* 8 */
    float menubar_height;           /* 28 */
} nxui_theme_t;

const nxui_theme_t *nxui_theme(void);
nxui_appearance_t nxui_appearance(void);
void nxui_set_appearance(nxui_appearance_t appearance);

/* ============ Typography ============ */

typedef enum {
    NXUI_FONT_REGULAR = 0,   /* NXSans Text  */
    NXUI_FONT_MEDIUM  = 1,   /* NXSans UI    */
    NXUI_FONT_BOLD    = 2,   /* NXSans Bold  */
    NXUI_FONT_DISPLAY = 3,   /* NXSans Display (large sizes) */
    NXUI_FONT_MONO    = 4,   /* NXSans Mono  */
    NXUI_FONT_COUNT
} nxui_font_id_t;

/* Text styles (font, size px, tracking px) - the type scale. */
typedef enum {
    NXUI_TEXT_CAPTION = 0,   /* Regular 11 */
    NXUI_TEXT_FOOTNOTE,      /* Regular 12 */
    NXUI_TEXT_BODY,          /* Regular 13 */
    NXUI_TEXT_BODY_MEDIUM,   /* Medium  13 */
    NXUI_TEXT_MENU,          /* Medium  13.5 (menu bar items) */
    NXUI_TEXT_MENU_BOLD,     /* Bold    13.5 (menu bar app name) */
    NXUI_TEXT_HEADLINE,      /* Bold    13 */
    NXUI_TEXT_TITLE3,        /* Medium  15 */
    NXUI_TEXT_TITLE2,        /* Bold    17 */
    NXUI_TEXT_TITLE1,        /* Bold    22 */
    NXUI_TEXT_LARGE_TITLE,   /* Bold    28 */
    NXUI_TEXT_CLOCK,         /* Display 56, tight tracking (widgets, lock screen) */
    NXUI_TEXT_MONO,          /* Mono    12.5 */
    NXUI_TEXT_COUNT
} nxui_text_style_t;

typedef struct {
    nxui_font_id_t font;
    float          size;
    float          tracking;
} nxui_type_t;

const nxc_font_t *nxui_font(nxui_font_id_t id);
nxui_type_t nxui_type(nxui_text_style_t style);

/* Draw text in a style at a baseline. opts may be NULL. */
nxc_status_t nxui_text(nxc_canvas_t *c, nxui_text_style_t style, float x, float baseline_y,
                       const char *utf8, nxc_color_t color, const nxc_text_opts_t *opts);
float nxui_text_width(nxui_text_style_t style, const char *utf8);
/* Ascent/descent in px for the style (positive numbers). */
float nxui_text_ascent(nxui_text_style_t style);
float nxui_text_descent(nxui_text_style_t style);
/* Draw text vertically centered (on cap height) in rect, aligned per align,
 * truncated with an ellipsis to the rect width. */
nxc_status_t nxui_text_in_rect(nxc_canvas_t *c, nxui_text_style_t style, nxc_rect_t r,
                               const char *utf8, nxc_color_t color, nxc_align_t align);

/* ============ Surfaces, materials, elevation ============ */

/* Draw a frosted material panel (backdrop blur of the wallpaper + tint +
 * grain + highlight) in a rounded rect. */
nxc_status_t nxui_material(nxc_canvas_t *c, nxc_rect_t r, float radius,
                           const nxc_material_t *m);

/* Standard soft shadows: 0 = none, 1 = control, 2 = popover/dock,
 * 3 = inactive window, 4 = active window. Draw before the surface. */
nxc_status_t nxui_elevation(nxc_canvas_t *c, nxc_rect_t r, float radius, int32_t level);

/* The wallpaper and its pre-blurred copy (same size as the screen). */
const nxc_surface_t *nxui_wallpaper(void);
const nxc_surface_t *nxui_wallpaper_blurred(void);

/* ============ Motion ============ */

/* Critically damped spring toward target; call once per frame with dt.
 * stiffness ~ 300, damping ~ 2*sqrt(stiffness) for no overshoot. */
typedef struct { float value, velocity; } nxui_spring_t;
void nxui_spring_step(nxui_spring_t *s, float target, float stiffness, float damping, float dt);
/* Move value toward target with exponential smoothing (time constant tau s). */
float nxui_approach(float value, float target, float tau, float dt);
float nxui_ease_out_cubic(float t);
float nxui_ease_in_out_cubic(float t);

#endif /* NXUI_H */
