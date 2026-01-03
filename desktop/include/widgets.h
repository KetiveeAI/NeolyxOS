/*
 * NeolyxOS Desktop Widgets
 * 
 * Production-ready widget library for desktop GUI.
 * Clean, minimal design inspired by SwiftUI/REOX.
 * 
 * Widgets:
 *   - Button: Clickable button with label
 *   - Label: Text display
 *   - TextField: Text input (planned)
 *   - Checkbox: Toggle (planned)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_WIDGETS_H
#define NEOLYX_WIDGETS_H

#include <stdint.h>

/* ============ Widget Types ============ */

typedef enum {
    WIDGET_NONE = 0,
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_TEXTFIELD,
    WIDGET_CHECKBOX,
    WIDGET_IMAGE,
    WIDGET_CONTAINER,
} widget_type_t;

/* ============ Widget State ============ */

typedef enum {
    WIDGET_STATE_NORMAL = 0,
    WIDGET_STATE_HOVERED,
    WIDGET_STATE_PRESSED,
    WIDGET_STATE_DISABLED,
    WIDGET_STATE_FOCUSED,
} widget_state_t;

/* ============ Alignment ============ */

typedef enum {
    ALIGN_START = 0,
    ALIGN_CENTER,
    ALIGN_END,
} alignment_t;

/* ============ Colors ============ */

/* Semantic color palette - easily themeable */
typedef struct {
    uint32_t background;
    uint32_t foreground;
    uint32_t primary;
    uint32_t primary_hover;
    uint32_t primary_pressed;
    uint32_t secondary;
    uint32_t border;
    uint32_t text;
    uint32_t text_dim;
    uint32_t success;
    uint32_t warning;
    uint32_t error;
} widget_theme_t;

/* Default dark theme */
extern widget_theme_t g_widget_theme;

/* ============ Base Widget ============ */

typedef struct widget widget_t;

typedef void (*widget_callback_t)(widget_t *widget, void *user_data);
typedef void (*widget_render_fn)(widget_t *widget, void *ctx);
typedef int (*widget_event_fn)(widget_t *widget, int event_type, void *event_data);

struct widget {
    /* Identity */
    uint32_t id;
    widget_type_t type;
    
    /* Bounds (relative to parent) */
    int32_t x, y;
    uint32_t width, height;
    
    /* State */
    widget_state_t state;
    uint8_t visible;
    uint8_t enabled;
    
    /* Callbacks */
    widget_callback_t on_click;
    void *click_data;
    
    /* Virtual methods */
    widget_render_fn render;
    widget_event_fn handle_event;
    
    /* Parent/children (for containers) */
    widget_t *parent;
    widget_t **children;
    uint32_t child_count;
    uint32_t child_capacity;
    
    /* Type-specific data */
    void *data;
};

/* ============ Button Widget ============ */

typedef struct {
    char label[64];
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t border_radius;
    alignment_t text_align;
} button_data_t;

/* ============ Label Widget ============ */

typedef struct {
    char text[256];
    uint32_t color;
    uint8_t font_size;  /* 0 = default (8px) */
    alignment_t align;
    uint8_t bold;
} label_data_t;

/* ============ Widget API ============ */

/**
 * widget_init - Initialize the widget system
 * 
 * Call once before using widgets.
 * 
 * Returns: 0 on success
 */
int widget_init(void);

/**
 * widget_create - Create a new widget
 * 
 * @type: Widget type
 * @x, y: Position
 * @width, height: Size
 * 
 * Returns: Widget pointer or NULL
 */
widget_t *widget_create(widget_type_t type, int32_t x, int32_t y, 
                        uint32_t width, uint32_t height);

/**
 * widget_destroy - Destroy a widget and its children
 */
void widget_destroy(widget_t *widget);

/**
 * widget_render - Render a widget
 * 
 * @widget: Widget to render
 * @ctx: Render context (framebuffer info)
 */
void widget_render(widget_t *widget, void *ctx);

/**
 * widget_handle_event - Send event to widget
 * 
 * @widget: Target widget
 * @event_type: Event type
 * @event_data: Event-specific data
 * 
 * Returns: 1 if handled, 0 if not
 */
int widget_handle_event(widget_t *widget, int event_type, void *event_data);

/* ============ Button API ============ */

/**
 * button_create - Create a button widget
 * 
 * @label: Button text
 * @x, y: Position
 * @width, height: Size
 * 
 * Returns: Widget pointer or NULL
 */
widget_t *button_create(const char *label, int32_t x, int32_t y,
                        uint32_t width, uint32_t height);

/**
 * button_set_label - Change button text
 */
void button_set_label(widget_t *button, const char *label);

/**
 * button_set_on_click - Set click callback
 */
void button_set_on_click(widget_t *button, widget_callback_t callback, void *data);

/**
 * button_set_colors - Set button colors
 */
void button_set_colors(widget_t *button, uint32_t bg, uint32_t text);

/* ============ Label API ============ */

/**
 * label_create - Create a label widget
 * 
 * @text: Label text
 * @x, y: Position
 * 
 * Returns: Widget pointer or NULL
 */
widget_t *label_create(const char *text, int32_t x, int32_t y);

/**
 * label_set_text - Change label text
 */
void label_set_text(widget_t *label, const char *text);

/**
 * label_set_color - Set text color
 */
void label_set_color(widget_t *label, uint32_t color);

/* ============ Container API ============ */

/**
 * container_create - Create a container widget
 * 
 * @x, y: Position
 * @width, height: Size
 */
widget_t *container_create(int32_t x, int32_t y, uint32_t width, uint32_t height);

/**
 * container_add_child - Add child widget
 */
int container_add_child(widget_t *container, widget_t *child);

/**
 * container_remove_child - Remove child widget
 */
void container_remove_child(widget_t *container, widget_t *child);

/* ============ Event Types ============ */

#define WIDGET_EVENT_MOUSE_MOVE     1
#define WIDGET_EVENT_MOUSE_DOWN     2
#define WIDGET_EVENT_MOUSE_UP       3
#define WIDGET_EVENT_KEY_DOWN       4
#define WIDGET_EVENT_KEY_UP         5
#define WIDGET_EVENT_FOCUS          6
#define WIDGET_EVENT_BLUR           7

typedef struct {
    int32_t x, y;        /* Position */
    int32_t dx, dy;      /* Delta */
    uint8_t buttons;     /* Button state */
} widget_mouse_event_t;

typedef struct {
    uint8_t scancode;
    uint8_t keycode;
    uint8_t modifiers;
} widget_key_event_t;

/* ============ Hit Testing ============ */

/**
 * widget_hit_test - Find widget at position
 * 
 * @root: Root widget to search
 * @x, y: Position to test
 * 
 * Returns: Widget at position or NULL
 */
widget_t *widget_hit_test(widget_t *root, int32_t x, int32_t y);

#endif /* NEOLYX_WIDGETS_H */
