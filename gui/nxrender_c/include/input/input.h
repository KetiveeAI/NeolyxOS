/*
 * NeolyxOS - NXRender Input System
 * Keyboard, mouse, touch, and gesture handling
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_INPUT_H
#define NXRENDER_INPUT_H

#include "nxgfx/nxgfx.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Key Codes (USB HID compatible)
 * ============================================================================ */
typedef enum {
    NX_KEY_UNKNOWN = 0,
    /* Letters */
    NX_KEY_A = 4, NX_KEY_B, NX_KEY_C, NX_KEY_D, NX_KEY_E, NX_KEY_F, NX_KEY_G,
    NX_KEY_H, NX_KEY_I, NX_KEY_J, NX_KEY_K, NX_KEY_L, NX_KEY_M, NX_KEY_N,
    NX_KEY_O, NX_KEY_P, NX_KEY_Q, NX_KEY_R, NX_KEY_S, NX_KEY_T, NX_KEY_U,
    NX_KEY_V, NX_KEY_W, NX_KEY_X, NX_KEY_Y, NX_KEY_Z,
    /* Numbers */
    NX_KEY_1 = 30, NX_KEY_2, NX_KEY_3, NX_KEY_4, NX_KEY_5,
    NX_KEY_6, NX_KEY_7, NX_KEY_8, NX_KEY_9, NX_KEY_0,
    /* Special */
    NX_KEY_ENTER = 40, NX_KEY_ESCAPE, NX_KEY_BACKSPACE, NX_KEY_TAB,
    NX_KEY_SPACE, NX_KEY_MINUS, NX_KEY_EQUALS, NX_KEY_LBRACKET,
    NX_KEY_RBRACKET, NX_KEY_BACKSLASH, NX_KEY_SEMICOLON = 51,
    NX_KEY_APOSTROPHE, NX_KEY_GRAVE, NX_KEY_COMMA, NX_KEY_PERIOD,
    NX_KEY_SLASH,
    /* Function keys */
    NX_KEY_F1 = 58, NX_KEY_F2, NX_KEY_F3, NX_KEY_F4, NX_KEY_F5, NX_KEY_F6,
    NX_KEY_F7, NX_KEY_F8, NX_KEY_F9, NX_KEY_F10, NX_KEY_F11, NX_KEY_F12,
    /* Navigation */
    NX_KEY_INSERT = 73, NX_KEY_HOME, NX_KEY_PAGEUP, NX_KEY_DELETE,
    NX_KEY_END, NX_KEY_PAGEDOWN, NX_KEY_RIGHT, NX_KEY_LEFT,
    NX_KEY_DOWN, NX_KEY_UP,
    /* Modifiers */
    NX_KEY_LCTRL = 224, NX_KEY_LSHIFT, NX_KEY_LALT, NX_KEY_LMETA,
    NX_KEY_RCTRL, NX_KEY_RSHIFT, NX_KEY_RALT, NX_KEY_RMETA
} nx_keycode_t;

/* Key modifiers bitfield */
typedef enum {
    NX_MOD_NONE  = 0,
    NX_MOD_SHIFT = (1 << 0),
    NX_MOD_CTRL  = (1 << 1),
    NX_MOD_ALT   = (1 << 2),
    NX_MOD_META  = (1 << 3),
    NX_MOD_CAPS  = (1 << 4)
} nx_key_mod_t;

/* ============================================================================
 * Mouse Buttons (avoid redefinition if already in widget.h)
 * ============================================================================ */
#ifndef NX_MOUSE_BUTTON_DEFINED
#define NX_MOUSE_BUTTON_DEFINED
typedef enum {
    NX_MOUSE_BUTTON_LEFT = 0,
    NX_MOUSE_BUTTON_RIGHT,
    NX_MOUSE_BUTTON_MIDDLE,
    NX_MOUSE_BUTTON_X1,
    NX_MOUSE_BUTTON_X2
} nx_mouse_button_t;
#endif

/* ============================================================================
 * Input Events
 * ============================================================================ */
typedef enum {
    NX_INPUT_NONE = 0,
    /* Keyboard */
    NX_INPUT_KEY_DOWN,
    NX_INPUT_KEY_UP,
    NX_INPUT_TEXT,
    /* Mouse */
    NX_INPUT_MOUSE_MOVE,
    NX_INPUT_MOUSE_DOWN,
    NX_INPUT_MOUSE_UP,
    NX_INPUT_MOUSE_SCROLL,
    NX_INPUT_MOUSE_ENTER,
    NX_INPUT_MOUSE_LEAVE,
    /* Touch */
    NX_INPUT_TOUCH_BEGIN,
    NX_INPUT_TOUCH_MOVE,
    NX_INPUT_TOUCH_END,
    NX_INPUT_TOUCH_CANCEL,
    /* Gestures */
    NX_INPUT_GESTURE_TAP,
    NX_INPUT_GESTURE_DOUBLE_TAP,
    NX_INPUT_GESTURE_LONG_PRESS,
    NX_INPUT_GESTURE_SWIPE,
    NX_INPUT_GESTURE_PINCH,
    NX_INPUT_GESTURE_ROTATE
} nx_input_type_t;

/* Gesture direction for swipes */
typedef enum {
    NX_SWIPE_LEFT,
    NX_SWIPE_RIGHT,
    NX_SWIPE_UP,
    NX_SWIPE_DOWN
} nx_swipe_dir_t;

/* Touch point */
typedef struct {
    int32_t id;
    int32_t x, y;
    float pressure;
} nx_touch_point_t;

/* Complete input event */
typedef struct {
    nx_input_type_t type;
    uint64_t timestamp;
    union {
        /* Keyboard */
        struct {
            nx_keycode_t keycode;
            nx_key_mod_t modifiers;
            bool repeat;
        } key;
        /* Text input */
        struct {
            char text[32];
        } text;
        /* Mouse */
        struct {
            int32_t x, y;
            int32_t dx, dy;
            nx_mouse_button_t button;
            nx_key_mod_t modifiers;
        } mouse;
        /* Scroll */
        struct {
            int32_t x, y;
            float delta_x, delta_y;
            bool precise;
        } scroll;
        /* Touch */
        struct {
            nx_touch_point_t points[10];
            uint32_t count;
        } touch;
        /* Gestures */
        struct {
            nx_swipe_dir_t direction;
            float velocity;
        } swipe;
        struct {
            float scale;
            float delta;
        } pinch;
        struct {
            float angle;
            float delta;
        } rotate;
    } data;
} nx_input_event_t;

/* ============================================================================
 * Input State
 * ============================================================================ */
typedef struct {
    /* Keyboard */
    bool keys[256];
    nx_key_mod_t modifiers;
    /* Mouse */
    int32_t mouse_x, mouse_y;
    bool mouse_buttons[5];
    /* Touch */
    nx_touch_point_t touches[10];
    uint32_t touch_count;
} nx_input_state_t;

/* ============================================================================
 * Input Manager
 * ============================================================================ */
typedef struct nx_input_manager nx_input_manager_t;

nx_input_manager_t* nx_input_create(void);
void nx_input_destroy(nx_input_manager_t* input);

/* Polling */
bool nx_input_poll(nx_input_manager_t* input, nx_input_event_t* event);
bool nx_input_wait(nx_input_manager_t* input, nx_input_event_t* event, uint32_t timeout_ms);
void nx_input_push(nx_input_manager_t* input, const nx_input_event_t* event);

/* State queries */
const nx_input_state_t* nx_input_state(nx_input_manager_t* input);
bool nx_input_key_down(nx_input_manager_t* input, nx_keycode_t key);
bool nx_input_key_pressed(nx_input_manager_t* input, nx_keycode_t key);
bool nx_input_mouse_down(nx_input_manager_t* input, nx_mouse_button_t button);
void nx_input_mouse_pos(nx_input_manager_t* input, int32_t* x, int32_t* y);
nx_key_mod_t nx_input_modifiers(nx_input_manager_t* input);

/* Focus */
typedef struct nx_widget nx_widget_t;
void nx_input_set_focus(nx_input_manager_t* input, nx_widget_t* widget);
nx_widget_t* nx_input_get_focus(nx_input_manager_t* input);
void nx_input_tab_next(nx_input_manager_t* input);
void nx_input_tab_prev(nx_input_manager_t* input);

/* Text input */
void nx_input_start_text(nx_input_manager_t* input);
void nx_input_stop_text(nx_input_manager_t* input);
bool nx_input_is_text_active(nx_input_manager_t* input);

/* Gesture configuration */
void nx_input_set_tap_timeout(nx_input_manager_t* input, uint32_t ms);
void nx_input_set_long_press_duration(nx_input_manager_t* input, uint32_t ms);
void nx_input_set_swipe_threshold(nx_input_manager_t* input, uint32_t pixels);

#ifdef __cplusplus
}
#endif
#endif
