/*
 * NeolyxOS - Dock and taskbar
 *
 * The shell's app launcher and window switcher at a screen edge, in one
 * of two layouts the user chooses in Settings:
 *
 *   DOCK_LAYOUT_DOCK     a floating glass shelf (bottom, left or right):
 *                        pinned apps, running apps, a separator, the
 *                        minimized windows and the Trash. Spring-driven
 *                        magnification, glass tooltips, launch bounce,
 *                        drag-to-reorder, right-click menu, autohide.
 *   DOCK_LAYOUT_TASKBAR  a full-width glass bar at the bottom: pinned
 *                        apps, then one pill per open window with its
 *                        title; the focused window's pill is highlighted.
 *
 * Running indicators come from the shell's window table
 * (desktop_window_slot), re-derived only when that table changes.
 * App icons are drawn through nxapp_icon_draw (nxicons.h); the dock never
 * draws its own icon artwork.
 *
 * Threading: single-threaded, called from the shell's frame loop only.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef DOCK_H
#define DOCK_H

#include <stdint.h>
#include "nxcanvas.h"
#include "nxappearance.h"
#include "nxevent.h"

/* ============ Constants ============ */

#define DOCK_MAX_ICONS          16      /* pinned apps */
#define DOCK_ICON_GAP           8
#define DOCK_MARGIN_BOTTOM      10      /* gap between shelf and screen edge */

#define DOCK_ICON_SIZE_MIN      32
#define DOCK_ICON_SIZE_MAX      96
#define DOCK_ICON_SIZE_DEFAULT  48
#define DOCK_MAG_SIZE_MIN       1.0f    /* magnified scale factor */
#define DOCK_MAG_SIZE_MAX       2.0f
#define DOCK_MAG_SIZE_DEFAULT   1.6f
#define DOCK_TASKBAR_HEIGHT     48

/* Return codes of the settings API */
#define DOCK_OK                 0
#define DOCK_ERR_INVALID       (-1)     /* argument out of range: nothing changed */
#define DOCK_ERR_STATE         (-2)     /* dock not initialized */
#define DOCK_ERR_PERSIST       (-3)     /* applied, but could not be saved */

/* Dock position (matches config string values "bottom", "left", "right") */
typedef enum {
    DOCK_POS_BOTTOM = 0,
    DOCK_POS_LEFT   = 1,
    DOCK_POS_RIGHT  = 2
} dock_position_t;

/* Layout (config string values "dock", "taskbar") */
typedef enum {
    DOCK_LAYOUT_DOCK    = 0,
    DOCK_LAYOUT_TASKBAR = 1
} dock_layout_t;

/* ============ Pinned app ============ */

typedef struct {
    uint32_t app_id;            /* App bundle ID hash */
    uint32_t icon_id;           /* legacy NXI icon id (kept for ABI; unused) */
    uint32_t color;             /* legacy fallback color (unused) */
    char label[32];             /* App name, as launched */
    int active;                 /* legacy, unused (derived from windows) */
    int running;                /* hint from dock_set_running (indicators use windows) */
    int minimized;              /* legacy, unused */
    int bouncing;               /* legacy, unused (see dock_start_bounce) */
    int bounce_frame;           /* legacy, unused */
} dock_icon_t;

/* ============ Dock state (read-only for callers) ============ */

typedef struct {
    uint32_t screen_width;
    uint32_t screen_height;

    /* Bounds of the visible shelf/bar this frame (screen coordinates) */
    int x, y;
    int width, height;

    /* Pinned apps, in dock order */
    dock_icon_t icons[DOCK_MAX_ICONS];
    int icon_count;

    int hover_idx;              /* hovered item in the current layout, -1 if none */
    int pressed_idx;            /* item under the pointer at press, -1 if none */

    int minimize_anim_idx;      /* legacy, unused */
    int minimize_progress;      /* legacy, unused */

    int visible;                /* 1 while any part of the dock is on screen */
    int autohide_timer;         /* legacy, unused */

    dock_position_t position;
    appearance_settings_t *settings;
} dock_state_t;

/* ============ Lifecycle and rendering ============ */

/* Initialize: load settings and pinned apps from nxconfig, subscribe to
 * NX_EVENT_DOCK_* events. Safe to call once; later calls are ignored. */
void dock_init(uint32_t screen_w, uint32_t screen_h);

/* Advance animations (call once per frame before dock_render). */
void dock_tick(void);

/* Draw the dock or taskbar, its tooltip and its context menu. */
void dock_render(nxc_canvas_t *c, int mouse_x, int mouse_y);

/* Pointer input. Left button: click / drag-to-reorder. Right button:
 * context menu. Returns 1 when the event was consumed (pointer over the
 * dock, a drag in progress, or the context menu open). */
int dock_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons);

/* Escape closes the context menu. Returns 1 when consumed. */
int dock_handle_key(uint8_t scancode, uint8_t pressed);

/* 1 while the context menu is open. */
int dock_menu_is_open(void);
void dock_menu_close(void);

/* ============ Pinned apps ============ */

/* Pin an app at the end. Returns its index, or -1 (full, bad label, or
 * already pinned: then the existing index is returned). */
int dock_add_icon(uint32_t app_id, const char *label, uint32_t color);
/* Unpin an app (its windows, if any, keep it in the running section). */
void dock_remove_icon(uint32_t app_id);
/* Kept for compatibility: indicators are derived from open windows. */
void dock_set_running(uint32_t app_id, int running);
/* Bounce the item for app_id (launch feedback). */
void dock_start_bounce(uint32_t app_id);

dock_state_t *dock_get_state(void);

/* ============ Settings API (for the Settings app) ============
 * Every setter validates its arguments: out-of-range values return
 * DOCK_ERR_INVALID and change nothing. Valid changes apply on the next
 * frame, are mirrored into nxappearance, saved through nxconfig ([dock]
 * section) and logged. DOCK_ERR_PERSIST means applied but not saved. */

/* on: 0 or 1. size: magnified icon scale, DOCK_MAG_SIZE_MIN..MAX (1.0-2.0). */
int dock_set_magnification(int on, float size);
/* px: DOCK_ICON_SIZE_MIN..DOCK_ICON_SIZE_MAX. The dock may draw icons
 * smaller when they would not fit on screen. */
int dock_set_icon_size(int px);
/* on: 0 or 1. Hidden dock reveals when the pointer touches its edge. */
int dock_set_autohide(int on);
int dock_set_position(dock_position_t pos);
int dock_set_layout(dock_layout_t layout);

/* Getters (valid before dock_init: return the defaults). */
int dock_get_magnification(int *on, float *size);   /* DOCK_OK or DOCK_ERR_INVALID (NULL) */
int dock_get_icon_size(void);
int dock_get_autohide(void);
dock_position_t dock_get_position(void);
dock_layout_t dock_get_layout(void);

/* Re-read the dock fields of nxappearance (icon size, magnification,
 * autohide), validating them. Invalid values are ignored and logged. */
void dock_update_settings(void);

/* ============ Shell integration ============ */

/* Where a minimizing window should fly to: the center of the dock's
 * minimized section (or the taskbar). Returns 0, or -1 if out is NULL. */
int dock_minimize_target(int *out_x, int *out_y);

/* Optional "Hide" for the context menu and taskbar pills: the shell
 * registers a function that minimizes one window (returns 0 on success).
 * Until one is registered, Hide is shown disabled. */
typedef int (*dock_hide_window_fn)(uint32_t window_id);
void dock_set_hide_handler(dock_hide_window_fn fn);

/* Event bus handler for NX_EVENT_DOCK_* events. */
void dock_event_handler(const nx_event_t *event, void *userdata);

#endif /* DOCK_H */
