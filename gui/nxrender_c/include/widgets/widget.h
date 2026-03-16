/*
 * NeolyxOS - NXRender Widget Base
 * Base widget interface (C equivalent of Rust trait)
 * Copyright (c) 2025 KetiveeAI
 *
 * MEMORY OWNERSHIP RULES:
 * =======================
 * 1. Whoever creates a widget OWNS it and MUST destroy it
 * 2. Parent widgets own all child widgets added via nx_widget_add_child()
 * 3. nx_widget_destroy() MUST recursively destroy all children
 * 4. After nx_widget_add_child(), caller should NOT free the child
 * 5. nx_widget_remove_child() transfers ownership BACK to caller
 *
 * Example:
 *   nx_button_t* btn = nx_button_create("OK");  // Caller owns btn
 *   nx_widget_add_child(parent, (nx_widget_t*)btn);  // Parent now owns btn
 *   nx_widget_destroy(parent);  // Destroys parent AND btn
 */

#ifndef NXRENDER_WIDGET_H
#define NXRENDER_WIDGET_H

#include "nxgfx/nxgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nx_widget nx_widget_t;
typedef struct nx_event nx_event_t;

/* Widget ID */
typedef uint32_t nx_widget_id_t;

/* Event types */
typedef enum {
    NX_EVENT_NONE = 0,
    NX_EVENT_MOUSE_MOVE,
    NX_EVENT_MOUSE_DOWN,
    NX_EVENT_MOUSE_UP,
    NX_EVENT_MOUSE_ENTER,
    NX_EVENT_MOUSE_LEAVE,
    NX_EVENT_KEY_DOWN,
    NX_EVENT_KEY_UP,
    NX_EVENT_TEXT_INPUT,
    NX_EVENT_FOCUS,
    NX_EVENT_BLUR
} nx_event_type_t;

/* Mouse button */
#ifndef NX_MOUSE_BUTTON_DEFINED
#define NX_MOUSE_BUTTON_DEFINED
typedef enum {
    NX_MOUSE_LEFT = 0,
    NX_MOUSE_RIGHT,
    NX_MOUSE_MIDDLE
} nx_mouse_button_t;
#endif

/* Key modifiers */
typedef struct {
    bool shift;
    bool ctrl;
    bool alt;
    bool meta;
} nx_modifiers_t;

/* Event data */
struct nx_event {
    nx_event_type_t type;
    nx_point_t pos;
    nx_mouse_button_t button;
    uint32_t keycode;
    char text[32];
    nx_modifiers_t modifiers;
};

/* Event result */
typedef enum {
    NX_EVENT_IGNORED = 0,
    NX_EVENT_HANDLED,
    NX_EVENT_NEEDS_REDRAW
} nx_event_result_t;

/* Widget virtual table */
typedef struct {
    void (*render)(nx_widget_t* self, nx_context_t* ctx);
    void (*layout)(nx_widget_t* self, nx_rect_t bounds);
    nx_size_t (*measure)(nx_widget_t* self, nx_size_t available);
    nx_event_result_t (*handle_event)(nx_widget_t* self, nx_event_t* event);
    void (*destroy)(nx_widget_t* self);
} nx_widget_vtable_t;

/* Widget state */
typedef enum {
    NX_WIDGET_NORMAL = 0,
    NX_WIDGET_HOVERED = 1,
    NX_WIDGET_PRESSED = 2,
    NX_WIDGET_FOCUSED = 4,
    NX_WIDGET_DISABLED = 8
} nx_widget_state_t;

/* Base widget structure */
struct nx_widget {
    const nx_widget_vtable_t* vtable;
    nx_widget_id_t id;
    nx_rect_t bounds;
    nx_widget_state_t state;
    bool visible;
    bool enabled;
    nx_widget_t* parent;
    nx_widget_t** children;
    size_t child_count;
    size_t child_capacity;
    void* user_data;
};

/* Widget lifecycle */
void nx_widget_init(nx_widget_t* w, const nx_widget_vtable_t* vtable);
void nx_widget_destroy(nx_widget_t* w);

/* Child management */
void nx_widget_add_child(nx_widget_t* parent, nx_widget_t* child);
void nx_widget_remove_child(nx_widget_t* parent, nx_widget_t* child);

/* State management */
void nx_widget_set_state(nx_widget_t* w, nx_widget_state_t state);
void nx_widget_clear_state(nx_widget_t* w, nx_widget_state_t state);
bool nx_widget_has_state(nx_widget_t* w, nx_widget_state_t state);

/* Rendering */
void nx_widget_render(nx_widget_t* w, nx_context_t* ctx);
void nx_widget_render_children(nx_widget_t* w, nx_context_t* ctx);

/* Layout */
void nx_widget_layout(nx_widget_t* w, nx_rect_t bounds);
nx_size_t nx_widget_measure(nx_widget_t* w, nx_size_t available);

/* Events */
nx_event_result_t nx_widget_handle_event(nx_widget_t* w, nx_event_t* event);

/* ID generation */
nx_widget_id_t nx_widget_next_id(void);

#ifdef __cplusplus
}
#endif
#endif
