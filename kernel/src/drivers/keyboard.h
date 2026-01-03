/*
 * NeolyxOS PS/2 Keyboard Driver
 * 
 * Production-ready keyboard driver with:
 * - PS/2 controller initialization
 * - Scancode to ASCII translation
 * - Key event buffering
 * - Blocking and non-blocking input
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* ============ Return Codes ============ */
#define KBD_SUCCESS         0
#define KBD_ERR_TIMEOUT    -1
#define KBD_ERR_BUFFER     -2
#define KBD_ERR_INVALID    -3

/* ============ Key Codes ============ */
#define KEY_NONE           0x00
#define KEY_ESCAPE         0x1B
#define KEY_ENTER          0x0D
#define KEY_BACKSPACE      0x08
#define KEY_TAB            0x09
#define KEY_SPACE          0x20

/* Arrow keys (special codes >= 0x80) */
#define KEY_UP             0x80
#define KEY_DOWN           0x81
#define KEY_LEFT           0x82
#define KEY_RIGHT          0x83
#define KEY_HOME           0x84
#define KEY_END            0x85
#define KEY_PGUP           0x86
#define KEY_PGDOWN         0x87
#define KEY_INSERT         0x88
#define KEY_DELETE         0x89

/* Function keys */
#define KEY_F1             0x90
#define KEY_F2             0x91
#define KEY_F3             0x92
#define KEY_F4             0x93
#define KEY_F5             0x94
#define KEY_F6             0x95
#define KEY_F7             0x96
#define KEY_F8             0x97
#define KEY_F9             0x98
#define KEY_F10            0x99
#define KEY_F11            0x9A
#define KEY_F12            0x9B

/* Modifier flags */
#define MOD_SHIFT          0x01
#define MOD_CTRL           0x02
#define MOD_ALT            0x04
#define MOD_CAPSLOCK       0x08

/* ============ Key Event ============ */
typedef struct {
    uint8_t keycode;     /* Translated key code */
    uint8_t scancode;    /* Raw scancode */
    uint8_t modifiers;   /* Active modifiers */
    uint8_t pressed;     /* 1 = pressed, 0 = released */
} kbd_event_t;

/* ============ API Functions ============ */

/**
 * kbd_init - Initialize keyboard driver
 * 
 * Initializes the PS/2 controller and keyboard, sets up
 * the scancode table, and prepares the key buffer.
 * 
 * Returns: KBD_SUCCESS on success, negative error code on failure
 */
int kbd_init(void);

/**
 * kbd_getchar - Get next character (blocking)
 * 
 * Waits for a key press and returns the ASCII character.
 * Special keys return codes >= 0x80.
 * 
 * Returns: Character code (0-255), or KEY_NONE on error
 */
uint8_t kbd_getchar(void);

/**
 * kbd_getchar_nonblock - Get next character (non-blocking)
 * 
 * Returns immediately with the next key if available.
 * 
 * Returns: Character code, or KEY_NONE if no key available
 */
uint8_t kbd_getchar_nonblock(void);

/**
 * kbd_get_event - Get full key event
 * 
 * Retrieves the next key event with scancode and modifiers.
 * 
 * @event: Pointer to event structure to fill
 * Returns: KBD_SUCCESS if event available, KBD_ERR_BUFFER if empty
 */
int kbd_get_event(kbd_event_t *event);

/**
 * kbd_has_key - Check if key is available
 * 
 * Returns: 1 if key available, 0 otherwise
 */
int kbd_has_key(void);

/**
 * kbd_get_modifiers - Get current modifier state
 * 
 * Returns: Current modifier flags (MOD_SHIFT, MOD_CTRL, etc.)
 */
uint8_t kbd_get_modifiers(void);

/**
 * kbd_set_leds - Set keyboard LEDs
 * 
 * @leds: LED mask (bit 0 = scroll, bit 1 = num, bit 2 = caps)
 * Returns: KBD_SUCCESS on success
 */
int kbd_set_leds(uint8_t leds);

#endif /* KEYBOARD_H */
