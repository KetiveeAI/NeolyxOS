/*
 * NeolyxOS - NXRender Input System Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "input/input.h"
#include <stdlib.h>
#include <string.h>

#define INPUT_QUEUE_SIZE 64

struct nx_input_manager {
    nx_input_state_t state;
    nx_input_event_t queue[INPUT_QUEUE_SIZE];
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
    /* Previous state for edge detection */
    bool prev_keys[256];
    bool prev_mouse[5];
    /* Focus */
    nx_widget_t* focused_widget;
    bool text_input_active;
    /* Gesture detection */
    uint32_t tap_timeout_ms;
    uint32_t long_press_ms;
    uint32_t swipe_threshold;
    /* Touch tracking */
    int32_t touch_start_x, touch_start_y;
    uint64_t touch_start_time;
};

nx_input_manager_t* nx_input_create(void) {
    nx_input_manager_t* input = calloc(1, sizeof(nx_input_manager_t));
    if (!input) return NULL;
    input->tap_timeout_ms = 200;
    input->long_press_ms = 500;
    input->swipe_threshold = 50;
    return input;
}

void nx_input_destroy(nx_input_manager_t* input) {
    free(input);
}

static void update_state(nx_input_manager_t* input, const nx_input_event_t* event) {
    switch (event->type) {
        case NX_INPUT_KEY_DOWN:
            input->prev_keys[event->data.key.keycode] = input->state.keys[event->data.key.keycode];
            input->state.keys[event->data.key.keycode] = true;
            input->state.modifiers = event->data.key.modifiers;
            break;
        case NX_INPUT_KEY_UP:
            input->prev_keys[event->data.key.keycode] = input->state.keys[event->data.key.keycode];
            input->state.keys[event->data.key.keycode] = false;
            input->state.modifiers = event->data.key.modifiers;
            break;
        case NX_INPUT_MOUSE_MOVE:
            input->state.mouse_x = event->data.mouse.x;
            input->state.mouse_y = event->data.mouse.y;
            break;
        case NX_INPUT_MOUSE_DOWN:
            input->prev_mouse[event->data.mouse.button] = input->state.mouse_buttons[event->data.mouse.button];
            input->state.mouse_buttons[event->data.mouse.button] = true;
            input->state.mouse_x = event->data.mouse.x;
            input->state.mouse_y = event->data.mouse.y;
            break;
        case NX_INPUT_MOUSE_UP:
            input->prev_mouse[event->data.mouse.button] = input->state.mouse_buttons[event->data.mouse.button];
            input->state.mouse_buttons[event->data.mouse.button] = false;
            break;
        case NX_INPUT_TOUCH_BEGIN:
        case NX_INPUT_TOUCH_MOVE:
        case NX_INPUT_TOUCH_END:
            input->state.touch_count = event->data.touch.count;
            memcpy(input->state.touches, event->data.touch.points, 
                   sizeof(nx_touch_point_t) * event->data.touch.count);
            break;
        default:
            break;
    }
}

void nx_input_push(nx_input_manager_t* input, const nx_input_event_t* event) {
    if (!input || input->queue_count >= INPUT_QUEUE_SIZE) return;
    update_state(input, event);
    input->queue[input->queue_tail] = *event;
    input->queue_tail = (input->queue_tail + 1) % INPUT_QUEUE_SIZE;
    input->queue_count++;
}

bool nx_input_poll(nx_input_manager_t* input, nx_input_event_t* event) {
    if (!input || input->queue_count == 0) return false;
    *event = input->queue[input->queue_head];
    input->queue_head = (input->queue_head + 1) % INPUT_QUEUE_SIZE;
    input->queue_count--;
    return true;
}

bool nx_input_wait(nx_input_manager_t* input, nx_input_event_t* event, uint32_t timeout_ms) {
    (void)timeout_ms;
    return nx_input_poll(input, event);
}

const nx_input_state_t* nx_input_state(nx_input_manager_t* input) {
    return input ? &input->state : NULL;
}

bool nx_input_key_down(nx_input_manager_t* input, nx_keycode_t key) {
    return input && key < 256 ? input->state.keys[key] : false;
}

bool nx_input_key_pressed(nx_input_manager_t* input, nx_keycode_t key) {
    if (!input || key >= 256) return false;
    return input->state.keys[key] && !input->prev_keys[key];
}

bool nx_input_mouse_down(nx_input_manager_t* input, nx_mouse_button_t button) {
    return input && button < 5 ? input->state.mouse_buttons[button] : false;
}

void nx_input_mouse_pos(nx_input_manager_t* input, int32_t* x, int32_t* y) {
    if (!input) return;
    if (x) *x = input->state.mouse_x;
    if (y) *y = input->state.mouse_y;
}

nx_key_mod_t nx_input_modifiers(nx_input_manager_t* input) {
    return input ? input->state.modifiers : NX_MOD_NONE;
}

void nx_input_set_focus(nx_input_manager_t* input, nx_widget_t* widget) {
    if (input) input->focused_widget = widget;
}

nx_widget_t* nx_input_get_focus(nx_input_manager_t* input) {
    return input ? input->focused_widget : NULL;
}

void nx_input_tab_next(nx_input_manager_t* input) {
    (void)input;
}

void nx_input_tab_prev(nx_input_manager_t* input) {
    (void)input;
}

void nx_input_start_text(nx_input_manager_t* input) {
    if (input) input->text_input_active = true;
}

void nx_input_stop_text(nx_input_manager_t* input) {
    if (input) input->text_input_active = false;
}

bool nx_input_is_text_active(nx_input_manager_t* input) {
    return input ? input->text_input_active : false;
}

void nx_input_set_tap_timeout(nx_input_manager_t* input, uint32_t ms) {
    if (input) input->tap_timeout_ms = ms;
}

void nx_input_set_long_press_duration(nx_input_manager_t* input, uint32_t ms) {
    if (input) input->long_press_ms = ms;
}

void nx_input_set_swipe_threshold(nx_input_manager_t* input, uint32_t pixels) {
    if (input) input->swipe_threshold = pixels;
}
