/*
 * NeolyxOS - App switcher (NX+Tab)
 *
 * A glass HUD with one icon per running app (most recently used first).
 * NX+Tab opens it on the previous app; each further Tab moves right
 * (Shift+Tab left) while NX stays held; releasing NX activates the
 * selection (restoring a minimized window and switching space or Stage
 * group as needed). Esc cancels. Clicking an icon activates it.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_SWITCHER_H
#define NX_SWITCHER_H

#include <stdint.h>
#include "nxcanvas.h"

#define SWITCHER_MAX_APPS 12

/* Open on the next (reverse=1: last) app. Opening when already open steps. */
void switcher_open(int reverse);
/* Move the selection one app right (reverse=1: left), wrapping. */
void switcher_step(int reverse);
/* Activate the selected app and close. */
void switcher_commit(void);
/* Close without activating. */
void switcher_cancel(void);
/* 1 while the HUD is shown (including its fade-out). */
int  switcher_active(void);
/* 1 while the HUD takes input (open and not fading out). */
int  switcher_open_now(void);
void switcher_tick(float dt);
void switcher_render(nxc_canvas_t *c);
/* Pointer input while open. Returns 1 when consumed. */
int  switcher_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons);
void switcher_on_window_destroyed(uint32_t window_id);

#endif /* NX_SWITCHER_H */
