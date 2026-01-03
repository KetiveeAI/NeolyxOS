/*
 * NeolyxOS Keyboard Driver
 * 
 * PS/2 keyboard driver with IRQ1 handling.
 * Converts scancodes to ASCII with shift/caps support.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_KEYBOARD_H
#define NEOLYX_KEYBOARD_H

#include <stdint.h>

/* ============ Key Codes ============ */

#define KEY_ESCAPE      0x01
#define KEY_BACKSPACE   0x0E
#define KEY_TAB         0x0F
#define KEY_ENTER       0x1C
#define KEY_LCTRL       0x1D
#define KEY_LSHIFT      0x2A
#define KEY_RSHIFT      0x36
#define KEY_LALT        0x38
#define KEY_CAPSLOCK    0x3A
#define KEY_F1          0x3B
#define KEY_F2          0x3C
#define KEY_F3          0x3D
#define KEY_F4          0x3E
#define KEY_F5          0x3F
#define KEY_F6          0x40
#define KEY_F7          0x41
#define KEY_F8          0x42
#define KEY_F9          0x43
#define KEY_F10         0x44
#define KEY_NUMLOCK     0x45
#define KEY_SCROLLLOCK  0x46
#define KEY_HOME        0x47
#define KEY_UP          0x48
#define KEY_PAGEUP      0x49
#define KEY_LEFT        0x4B
#define KEY_RIGHT       0x4D
#define KEY_END         0x4F
#define KEY_DOWN        0x50
#define KEY_PAGEDOWN    0x51
#define KEY_INSERT      0x52
#define KEY_DELETE      0x53
#define KEY_F11         0x57
#define KEY_F12         0x58

/* ============ Numpad Keys ============ */

#define KEY_KP_7        0x47    /* Numpad 7 / Home */
#define KEY_KP_8        0x48    /* Numpad 8 / Up */
#define KEY_KP_9        0x49    /* Numpad 9 / PgUp */
#define KEY_KP_MINUS    0x4A    /* Numpad - */
#define KEY_KP_4        0x4B    /* Numpad 4 / Left */
#define KEY_KP_5        0x4C    /* Numpad 5 */
#define KEY_KP_6        0x4D    /* Numpad 6 / Right */
#define KEY_KP_PLUS     0x4E    /* Numpad + */
#define KEY_KP_1        0x4F    /* Numpad 1 / End */
#define KEY_KP_2        0x50    /* Numpad 2 / Down */
#define KEY_KP_3        0x51    /* Numpad 3 / PgDn */
#define KEY_KP_0        0x52    /* Numpad 0 / Ins */
#define KEY_KP_DECIMAL  0x53    /* Numpad . / Del */
#define KEY_KP_MULTIPLY 0x37    /* Numpad * */
#define KEY_KP_ENTER    0x9C    /* Numpad Enter (extended E0 1C) */
#define KEY_KP_DIVIDE   0xB5    /* Numpad / (extended E0 35) */

/* ============ NeolyxOS Special Keys ============ */

/* ALLOW key (physical ENTER, semantic: confirm/accept)
 * Physical key: ENTER
 * Neolyx symbol: KEY_ALLOW
 * UI label: ALLOW
 * 
 * Used for:
 *   - Dialogs: Allow/Deny
 *   - Installers: Allow
 *   - Permissions: Allow
 *   - Commands: Confirm
 */
#define KEY_ALLOW       KEY_ENTER   /* Legacy compatibility */
#define KEY_ALLOW_CODE  0x1C        /* Same as ENTER scancode */

/* MODE key (Caps Lock position, system modifier)
 * Changes keyboard layer for system shortcuts
 */
#define KEY_MODE        0x3A        /* Physical Caps Lock */
#define KEY_MODE_CODE   KEY_CAPSLOCK

/* META key (Super/Windows key, NeolyxOS launcher) */
#define KEY_META        0x5B        /* Extended: E0 5B */
#define KEY_RMETA       0x5C        /* Extended: E0 5C */

/* MOD key (Command-like modifier for shortcuts) */
#define KEY_LMOD        0x5B        /* Left meta acts as MOD */
#define KEY_RMOD        0x5C        /* Right meta acts as MOD */

/* ============ Media Keys (Shift+Fn) ============ */

#define KEY_MEDIA_PLAY      0x60    /* Shift+S1: Play/Pause */
#define KEY_MEDIA_STOP      0x61    /* Shift+S2: Stop */
#define KEY_MEDIA_PREV      0x62    /* Shift+S3: Previous track */
#define KEY_MEDIA_NEXT      0x63    /* Shift+S4: Next track */
#define KEY_MEDIA_MUTE      0x64    /* Shift+S5: Mute */
#define KEY_MEDIA_VOL_DOWN  0x65    /* Shift+S6: Volume down */
#define KEY_MEDIA_VOL_UP    0x66    /* Shift+S7: Volume up */
#define KEY_MEDIA_BRIGHTNESS_DOWN 0x67  /* Shift+S8: Brightness- */
#define KEY_MEDIA_BRIGHTNESS_UP   0x68  /* Shift+S9: Brightness+ */
#define KEY_MEDIA_DISPLAY   0x69    /* Shift+S10: Display switch */
#define KEY_MEDIA_WIFI      0x6A    /* Shift+S11: WiFi toggle */
#define KEY_MEDIA_AIRPLANE  0x6B    /* Shift+S12: Airplane mode */

/* ============ Special Function Keys (S1-S12) ============ */

#define KEY_S1          KEY_F1      /* Special row, also F1 */
#define KEY_S2          KEY_F2
#define KEY_S3          KEY_F3
#define KEY_S4          KEY_F4
#define KEY_S5          KEY_F5
#define KEY_S6          KEY_F6
#define KEY_S7          KEY_F7
#define KEY_S8          KEY_F8
#define KEY_S9          KEY_F9
#define KEY_S10         KEY_F10
#define KEY_S11         KEY_F11
#define KEY_S12         KEY_F12

/* ============ Key Flags ============ */

/* Special key flags */
#define KEY_FLAG_SHIFT      0x0100
#define KEY_FLAG_CTRL       0x0200
#define KEY_FLAG_ALT        0x0400
#define KEY_FLAG_CAPSLOCK   0x0800
#define KEY_FLAG_RELEASED   0x8000

/* NeolyxOS modifier flags */
#define KEY_FLAG_MODE       0x1000  /* MODE key held */
#define KEY_FLAG_META       0x2000  /* META key held */
#define KEY_FLAG_MOD        0x4000  /* MOD key held (for shortcuts) */

/* ============ Keyboard API ============ */

/**
 * Initialize the keyboard driver.
 */
void keyboard_init(void);

/**
 * Keyboard IRQ handler (called from IRQ1).
 */
void keyboard_irq(void);

/**
 * Check if a key is available in the buffer.
 * @return 1 if key available, 0 otherwise
 */
int keyboard_has_key(void);

/**
 * Get next key from buffer (blocking).
 * @return ASCII code or special key code with flags
 */
uint16_t keyboard_get_key(void);

/**
 * Get next key from buffer (non-blocking).
 * @return ASCII code, or 0 if no key available
 */
uint16_t keyboard_get_key_nb(void);

/**
 * Get current modifier state.
 * @return Combination of KEY_FLAG_* values
 */
uint16_t keyboard_get_modifiers(void);

/**
 * Get raw scancode (for debugging).
 * @return Last scancode received
 */
uint8_t keyboard_get_scancode(void);

/* ============ NeolyxOS Keyboard Helpers ============ */

/**
 * Check if ALLOW (Enter) key was pressed
 * Semantic: user confirms/accepts action
 */
static inline int keyboard_is_allow(uint16_t key) {
    return (key & 0xFF) == KEY_ALLOW_CODE;
}

/**
 * Check if MODE modifier is active
 * MODE changes keyboard to system shortcut layer
 */
static inline int keyboard_mode_active(uint16_t modifiers) {
    return (modifiers & KEY_FLAG_MODE) != 0;
}

/**
 * Check if MOD (Command-like) modifier is active
 */
static inline int keyboard_mod_active(uint16_t modifiers) {
    return (modifiers & KEY_FLAG_MOD) != 0;
}

/**
 * Convert Shift+Fn to media key
 * @param scancode Function key scancode (F1-F12)
 * @param shift 1 if shift held
 * @return Media key code, or 0 if not a media shortcut
 */
static inline uint8_t keyboard_fn_to_media(uint8_t scancode, int shift) {
    if (!shift) return 0;
    switch (scancode) {
        case KEY_F1:  return KEY_MEDIA_PLAY;
        case KEY_F2:  return KEY_MEDIA_STOP;
        case KEY_F3:  return KEY_MEDIA_PREV;
        case KEY_F4:  return KEY_MEDIA_NEXT;
        case KEY_F5:  return KEY_MEDIA_MUTE;
        case KEY_F6:  return KEY_MEDIA_VOL_DOWN;
        case KEY_F7:  return KEY_MEDIA_VOL_UP;
        case KEY_F8:  return KEY_MEDIA_BRIGHTNESS_DOWN;
        case KEY_F9:  return KEY_MEDIA_BRIGHTNESS_UP;
        case KEY_F10: return KEY_MEDIA_DISPLAY;
        case KEY_F11: return KEY_MEDIA_WIFI;
        case KEY_F12: return KEY_MEDIA_AIRPLANE;
        default: return 0;
    }
}

/**
 * Get human-readable key name
 */
const char* keyboard_key_name(uint8_t scancode);

#endif /* NEOLYX_KEYBOARD_H */
