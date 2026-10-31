/*
 * NeolyxOS - Stage: a leaner Stage Manager
 *
 * The active window group sits centered on the stage; every other window
 * of the current space becomes a small tilted thumbnail in a left-side
 * strip, grouped by app (up to STAGE_MAX_GROUPS shown). Click a thumbnail
 * to swap it onto the stage with a spring; drag a thumbnail onto the
 * stage to add its windows to the current group. Toggle: NX+S or
 * stage_toggle().
 *
 * Cost model:
 *   - Thumbnails come from the window manager's snapshot cache
 *     (wm_snapshot_*), refreshed at most once per STAGE_REFRESH_MS per
 *     window and one window per frame, never by rendering windows at full
 *     size every frame.
 *   - The strip (glass panel + tilted thumbnails + icons) is composed into
 *     one cached layer and rebuilt only when a thumbnail or the grouping
 *     changes; a frame costs one panel blit and one layer blit.
 *   - With Stage off nothing runs: every entry point returns at once and
 *     no snapshot leases are taken.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_STAGE_H
#define NX_STAGE_H

#include <stdint.h>
#include "nxcanvas.h"

#define STAGE_MAX_GROUPS   6      /* thumbnails shown in the strip */
#define STAGE_GROUP_MAX    8      /* windows per group */
#define STAGE_STRIP_W      196    /* strip width (px) */
#define STAGE_REFRESH_MS   1500   /* thumbnail refresh period */

/* Turn Stage on or off. */
void stage_toggle(void);
int  stage_active(void);
/* Per-frame animation (no-op while off). */
void stage_tick(float dt);
/* The strip, drawn below the windows. */
void stage_render(nxc_canvas_t *c);
/* Windows flying between strip and stage and a dragged thumbnail, drawn
 * above the windows. */
void stage_render_overlay(nxc_canvas_t *c);
/* Pointer input for the strip and thumbnail drags. Returns 1 if consumed. */
int  stage_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons);

/* Window lifecycle hooks from the shell (all no-ops while off). */
void stage_on_window_created(uint32_t window_id);
void stage_on_window_destroyed(uint32_t window_id);
/* Activated from the dock, switcher or Overview: bring its group on stage. */
void stage_on_window_activated(uint32_t window_id);

/* Where a parked window's thumbnail is drawn. Returns 1 and fills *r. */
int  stage_window_thumb_rect(uint32_t window_id, nxc_rect_t *r);

#endif /* NX_STAGE_H */
