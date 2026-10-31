/*
 * NeolyxOS - Overview (the NeolyxOS take on Mission Control)
 *
 * Every window of the current space springs into a non-overlapping grid
 * (from cached window snapshots, never live full-size renders), with the
 * app icon and title under each. A strip of spaces runs along the top:
 * click a space to switch to it, "+" adds one, and dragging a window
 * thumbnail onto a space (or onto "+") moves the window there. Clicking a
 * window focuses it and exits; clicking empty space or Esc exits.
 *
 * Trigger: NX+Up, or overview_toggle().
 *
 * While inactive Overview costs nothing: no state is touched per frame.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_OVERVIEW_H
#define NX_OVERVIEW_H

#include <stdint.h>
#include "nxcanvas.h"

/* Enter Overview, or leave it (animated) when it is open. */
void overview_toggle(void);
/* 1 while Overview is visible, including its enter/exit animation. */
int  overview_active(void);
/* Advance the spring (dt seconds). Cheap no-op while inactive. */
void overview_tick(float dt);
/* Draw the dimmed desktop, spaces strip and window grid. Replaces the
 * normal window pass while overview_active(). */
void overview_render(nxc_canvas_t *c);
/* Pointer input while active. Returns 1 when consumed (always, while active). */
int  overview_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons);
/* Keyboard while active (Esc / Return exit). Returns 1 when consumed. */
int  overview_handle_key(uint8_t scancode, uint8_t pressed);
/* A window went away: drop it from the layout. */
void overview_on_window_destroyed(uint32_t window_id);

#endif /* NX_OVERVIEW_H */
