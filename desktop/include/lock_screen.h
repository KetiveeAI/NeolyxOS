/*
 * NeolyxOS Lock Screen Header
 * 
 * Lock screen overlay with password entry and user authentication.
 * Connects to kernel user_auth module for real authentication.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_LOCK_SCREEN_H
#define NEOLYX_LOCK_SCREEN_H

#include <stdint.h>

/* ============ Lock Screen State ============ */

typedef enum {
    LOCK_STATE_HIDDEN = 0,    /* Lock screen not shown */
    LOCK_STATE_LOCKED,        /* Lock screen visible, waiting for password */
    LOCK_STATE_UNLOCKING,     /* Authentication in progress */
    LOCK_STATE_ERROR          /* Authentication failed */
} lock_state_t;

/* ============ Lock Screen API ============ */

/**
 * lock_screen_init - Initialize the lock screen
 * 
 * @fb_addr: Framebuffer address
 * @width: Screen width
 * @height: Screen height
 * @pitch: Framebuffer pitch in bytes
 * 
 * Returns: 0 on success, -1 on error
 */
int lock_screen_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);

/**
 * lock_screen_show - Show the lock screen
 * 
 * Displays the lock screen overlay and locks the session.
 */
void lock_screen_show(void);

/**
 * lock_screen_hide - Hide the lock screen
 * 
 * Called after successful authentication.
 */
void lock_screen_hide(void);

/**
 * lock_screen_render - Render the lock screen
 * 
 * Draws the lock screen UI if active.
 */
void lock_screen_render(void);

/**
 * lock_screen_handle_key - Handle keyboard input
 * 
 * @scancode: Key scancode
 * @pressed: 1 if key pressed, 0 if released
 */
void lock_screen_handle_key(uint8_t scancode, int pressed);

/**
 * lock_screen_handle_mouse - Handle mouse input
 * 
 * @dx: Mouse X delta
 * @dy: Mouse Y delta
 * @buttons: Button state bitmask
 */
void lock_screen_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);

/**
 * lock_screen_is_active - Check if lock screen is active
 * 
 * Returns: 1 if lock screen is visible, 0 otherwise
 */
int lock_screen_is_active(void);

/**
 * lock_screen_set_user - Set the user to display
 * 
 * @username: Username
 * @fullname: Full display name
 */
void lock_screen_set_user(const char *username, const char *fullname);

#endif /* NEOLYX_LOCK_SCREEN_H */
