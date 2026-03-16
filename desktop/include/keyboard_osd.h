/*
 * NeolyxOS Keyboard OSD Header
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef KEYBOARD_OSD_H
#define KEYBOARD_OSD_H

#include <stdint.h>

/* Modifier key flags */
#define MOD_CAPS_LOCK   0x01
#define MOD_NUM_LOCK    0x02
#define MOD_SCROLL_LOCK 0x04
#define MOD_CTRL        0x08
#define MOD_ALT         0x10
#define MOD_SHIFT       0x20
#define MOD_SUPER       0x40

/* OSD display type */
typedef enum {
    KBD_OSD_NONE = 0,
    KBD_OSD_CAPS_LOCK,
    KBD_OSD_NUM_LOCK,
    KBD_OSD_SHORTCUT,
    KBD_OSD_INPUT_MODE,
    KBD_OSD_HELP_OVERLAY   /* Full-screen shortcut help */
} kbd_osd_type_t;

/* OSD state */
typedef struct {
    int visible;
    kbd_osd_type_t type;
    char text[64];
    uint64_t show_time;
    uint8_t opacity;
    int32_t x, y;
    uint32_t width, height;
} keyboard_osd_state_t;

/* Initialize keyboard OSD */
void keyboard_osd_init(uint32_t screen_w, uint32_t screen_h);

/* Show Caps Lock indicator */
void keyboard_osd_caps_lock(int on);

/* Show Num Lock indicator */
void keyboard_osd_num_lock(int on);

/* Show keyboard shortcut hint */
void keyboard_osd_shortcut(const char *keys, const char *action);

/* Hide keyboard OSD */
void keyboard_osd_hide(void);

/* Check if visible */
int keyboard_osd_is_visible(void);

/* Update state (call each frame) */
void keyboard_osd_tick(void);

/* Render keyboard OSD */
void keyboard_osd_render(uint32_t *fb, uint32_t pitch);

/* Help overlay functions (full-screen shortcut help) */
void keyboard_osd_help_show(void);
void keyboard_osd_help_hide(void);
int keyboard_osd_help_visible(void);
void keyboard_osd_help_render(uint32_t *fb, uint32_t pitch, uint32_t sw, uint32_t sh);

#endif /* KEYBOARD_OSD_H */
