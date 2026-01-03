/*
 * NeolyxOS Desktop Widgets Implementation
 * 
 * Production-ready widget library.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../include/widgets.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* Forward declare drawing functions from desktop_shell */
extern void desktop_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void desktop_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);

/* ============ Default Theme ============ */

widget_theme_t g_widget_theme = {
    .background     = 0xFF1E1E2E,
    .foreground     = 0xFF252535,
    .primary        = 0xFF6366F1,  /* Indigo */
    .primary_hover  = 0xFF818CF8,
    .primary_pressed = 0xFF4F46E5,
    .secondary      = 0xFF374151,
    .border         = 0xFF404055,
    .text           = 0xFFE2E8F0,
    .text_dim       = 0xFF94A3B8,
    .success        = 0xFF22C55E,
    .warning        = 0xFFEAB308,
    .error          = 0xFFEF4444,
};

/* ============ Static State ============ */

static uint32_t g_next_widget_id = 1;
static uint8_t g_widget_initialized = 0;

/* Memory pool for widgets (simple approach) */
#define MAX_WIDGETS 256
static widget_t g_widget_pool[MAX_WIDGETS];
static uint8_t g_widget_used[MAX_WIDGETS];

/* ============ Utility Functions ============ */

static int w_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void w_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Widget Pool Management ============ */

static widget_t *alloc_widget(void) {
    for (int i = 0; i < MAX_WIDGETS; i++) {
        if (!g_widget_used[i]) {
            g_widget_used[i] = 1;
            widget_t *w = &g_widget_pool[i];
            /* Zero initialize */
            uint8_t *p = (uint8_t *)w;
            for (uint32_t j = 0; j < sizeof(widget_t); j++) p[j] = 0;
            return w;
        }
    }
    return NULL;
}

static void free_widget(widget_t *w) {
    if (!w) return;
    int idx = (int)(w - g_widget_pool);
    if (idx >= 0 && idx < MAX_WIDGETS) {
        /* Free type-specific data */
        if (w->data) {
            kfree(w->data);
        }
        /* Free children array */
        if (w->children) {
            kfree(w->children);
        }
        g_widget_used[idx] = 0;
    }
}

/* ============ Button Implementation ============ */

static void button_render_impl(widget_t *widget, void *ctx) {
    (void)ctx;
    if (!widget || widget->type != WIDGET_BUTTON) return;
    if (!widget->visible) return;
    
    button_data_t *data = (button_data_t *)widget->data;
    if (!data) return;
    
    /* Determine colors based on state */
    uint32_t bg = data->bg_color;
    if (!widget->enabled) {
        bg = g_widget_theme.secondary;
    } else if (widget->state == WIDGET_STATE_PRESSED) {
        bg = g_widget_theme.primary_pressed;
    } else if (widget->state == WIDGET_STATE_HOVERED) {
        bg = g_widget_theme.primary_hover;
    }
    
    /* Draw button background */
    desktop_fill_rect(widget->x, widget->y, widget->width, widget->height, bg);
    
    /* Draw border */
    desktop_draw_rect(widget->x, widget->y, widget->width, widget->height, 
                     g_widget_theme.border);
    
    /* Calculate text position */
    int text_len = w_strlen(data->label);
    int text_width = text_len * 8;  /* 8px per char */
    int text_x = widget->x + (widget->width - text_width) / 2;
    int text_y = widget->y + (widget->height - 8) / 2;
    
    /* Draw label */
    uint32_t text_color = widget->enabled ? data->text_color : g_widget_theme.text_dim;
    desktop_draw_text(text_x, text_y, data->label, text_color);
}

static int button_event_impl(widget_t *widget, int event_type, void *event_data) {
    if (!widget || widget->type != WIDGET_BUTTON) return 0;
    if (!widget->enabled) return 0;
    
    widget_mouse_event_t *mouse = (widget_mouse_event_t *)event_data;
    
    switch (event_type) {
        case WIDGET_EVENT_MOUSE_MOVE: {
            /* Check if mouse is over button */
            int hover = (mouse->x >= widget->x && 
                        mouse->x < widget->x + (int32_t)widget->width &&
                        mouse->y >= widget->y && 
                        mouse->y < widget->y + (int32_t)widget->height);
            widget->state = hover ? WIDGET_STATE_HOVERED : WIDGET_STATE_NORMAL;
            return hover;
        }
        
        case WIDGET_EVENT_MOUSE_DOWN: {
            if (widget->state == WIDGET_STATE_HOVERED) {
                widget->state = WIDGET_STATE_PRESSED;
                return 1;
            }
            break;
        }
        
        case WIDGET_EVENT_MOUSE_UP: {
            if (widget->state == WIDGET_STATE_PRESSED) {
                widget->state = WIDGET_STATE_HOVERED;
                /* Trigger click callback */
                if (widget->on_click) {
                    widget->on_click(widget, widget->click_data);
                }
                return 1;
            }
            break;
        }
    }
    
    return 0;
}

/* ============ Label Implementation ============ */

static void label_render_impl(widget_t *widget, void *ctx) {
    (void)ctx;
    if (!widget || widget->type != WIDGET_LABEL) return;
    if (!widget->visible) return;
    
    label_data_t *data = (label_data_t *)widget->data;
    if (!data) return;
    
    desktop_draw_text(widget->x, widget->y, data->text, data->color);
}

static int label_event_impl(widget_t *widget, int event_type, void *event_data) {
    (void)widget; (void)event_type; (void)event_data;
    /* Labels don't handle events */
    return 0;
}

/* ============ Container Implementation ============ */

static void container_render_impl(widget_t *widget, void *ctx) {
    if (!widget || widget->type != WIDGET_CONTAINER) return;
    if (!widget->visible) return;
    
    /* Render all children */
    for (uint32_t i = 0; i < widget->child_count; i++) {
        widget_t *child = widget->children[i];
        if (child && child->render) {
            child->render(child, ctx);
        }
    }
}

static int container_event_impl(widget_t *widget, int event_type, void *event_data) {
    if (!widget || widget->type != WIDGET_CONTAINER) return 0;
    
    /* Forward event to children (reverse order for z-order) */
    for (int i = (int)widget->child_count - 1; i >= 0; i--) {
        widget_t *child = widget->children[i];
        if (child && child->handle_event) {
            if (child->handle_event(child, event_type, event_data)) {
                return 1;  /* Event consumed */
            }
        }
    }
    
    return 0;
}

/* ============ Public API ============ */

int widget_init(void) {
    if (g_widget_initialized) return 0;
    
    /* Clear widget pool */
    for (int i = 0; i < MAX_WIDGETS; i++) {
        g_widget_used[i] = 0;
    }
    
    g_widget_initialized = 1;
    serial_puts("[WIDGETS] Widget system initialized\n");
    return 0;
}

widget_t *widget_create(widget_type_t type, int32_t x, int32_t y,
                        uint32_t width, uint32_t height) {
    widget_t *w = alloc_widget();
    if (!w) {
        serial_puts("[WIDGETS] Error: Widget pool exhausted\n");
        return NULL;
    }
    
    w->id = g_next_widget_id++;
    w->type = type;
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    w->state = WIDGET_STATE_NORMAL;
    w->visible = 1;
    w->enabled = 1;
    
    return w;
}

void widget_destroy(widget_t *widget) {
    if (!widget) return;
    
    /* Destroy children first */
    if (widget->children) {
        for (uint32_t i = 0; i < widget->child_count; i++) {
            widget_destroy(widget->children[i]);
        }
    }
    
    free_widget(widget);
}

void widget_render(widget_t *widget, void *ctx) {
    if (!widget || !widget->visible) return;
    if (widget->render) {
        widget->render(widget, ctx);
    }
}

int widget_handle_event(widget_t *widget, int event_type, void *event_data) {
    if (!widget || !widget->enabled) return 0;
    if (widget->handle_event) {
        return widget->handle_event(widget, event_type, event_data);
    }
    return 0;
}

/* ============ Button API ============ */

widget_t *button_create(const char *label, int32_t x, int32_t y,
                        uint32_t width, uint32_t height) {
    widget_t *w = widget_create(WIDGET_BUTTON, x, y, width, height);
    if (!w) return NULL;
    
    /* Allocate button data */
    button_data_t *data = (button_data_t *)kmalloc(sizeof(button_data_t));
    if (!data) {
        free_widget(w);
        return NULL;
    }
    
    /* Initialize button data */
    w_strcpy(data->label, label, 64);
    data->bg_color = g_widget_theme.primary;
    data->text_color = g_widget_theme.text;
    data->border_radius = 4;
    data->text_align = ALIGN_CENTER;
    
    w->data = data;
    w->render = button_render_impl;
    w->handle_event = button_event_impl;
    
    return w;
}

void button_set_label(widget_t *button, const char *label) {
    if (!button || button->type != WIDGET_BUTTON) return;
    button_data_t *data = (button_data_t *)button->data;
    if (data) {
        w_strcpy(data->label, label, 64);
    }
}

void button_set_on_click(widget_t *button, widget_callback_t callback, void *data) {
    if (!button) return;
    button->on_click = callback;
    button->click_data = data;
}

void button_set_colors(widget_t *button, uint32_t bg, uint32_t text) {
    if (!button || button->type != WIDGET_BUTTON) return;
    button_data_t *data = (button_data_t *)button->data;
    if (data) {
        data->bg_color = bg;
        data->text_color = text;
    }
}

/* ============ Label API ============ */

widget_t *label_create(const char *text, int32_t x, int32_t y) {
    int text_len = w_strlen(text);
    widget_t *w = widget_create(WIDGET_LABEL, x, y, text_len * 8, 8);
    if (!w) return NULL;
    
    /* Allocate label data */
    label_data_t *data = (label_data_t *)kmalloc(sizeof(label_data_t));
    if (!data) {
        free_widget(w);
        return NULL;
    }
    
    /* Initialize label data */
    w_strcpy(data->text, text, 256);
    data->color = g_widget_theme.text;
    data->font_size = 0;  /* Default 8px */
    data->align = ALIGN_START;
    data->bold = 0;
    
    w->data = data;
    w->render = label_render_impl;
    w->handle_event = label_event_impl;
    
    return w;
}

void label_set_text(widget_t *label, const char *text) {
    if (!label || label->type != WIDGET_LABEL) return;
    label_data_t *data = (label_data_t *)label->data;
    if (data) {
        w_strcpy(data->text, text, 256);
        /* Update width */
        label->width = w_strlen(text) * 8;
    }
}

void label_set_color(widget_t *label, uint32_t color) {
    if (!label || label->type != WIDGET_LABEL) return;
    label_data_t *data = (label_data_t *)label->data;
    if (data) {
        data->color = color;
    }
}

/* ============ Container API ============ */

widget_t *container_create(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    widget_t *w = widget_create(WIDGET_CONTAINER, x, y, width, height);
    if (!w) return NULL;
    
    /* Allocate children array */
    w->child_capacity = 16;
    w->children = (widget_t **)kmalloc(sizeof(widget_t *) * w->child_capacity);
    if (!w->children) {
        free_widget(w);
        return NULL;
    }
    
    w->child_count = 0;
    w->render = container_render_impl;
    w->handle_event = container_event_impl;
    
    return w;
}

int container_add_child(widget_t *container, widget_t *child) {
    if (!container || container->type != WIDGET_CONTAINER || !child) {
        return -1;
    }
    
    /* Grow array if needed */
    if (container->child_count >= container->child_capacity) {
        uint32_t new_cap = container->child_capacity * 2;
        widget_t **new_arr = (widget_t **)kmalloc(sizeof(widget_t *) * new_cap);
        if (!new_arr) return -1;
        
        for (uint32_t i = 0; i < container->child_count; i++) {
            new_arr[i] = container->children[i];
        }
        kfree(container->children);
        container->children = new_arr;
        container->child_capacity = new_cap;
    }
    
    container->children[container->child_count++] = child;
    child->parent = container;
    
    return 0;
}

void container_remove_child(widget_t *container, widget_t *child) {
    if (!container || container->type != WIDGET_CONTAINER || !child) {
        return;
    }
    
    for (uint32_t i = 0; i < container->child_count; i++) {
        if (container->children[i] == child) {
            /* Shift remaining children */
            for (uint32_t j = i; j < container->child_count - 1; j++) {
                container->children[j] = container->children[j + 1];
            }
            container->child_count--;
            child->parent = NULL;
            return;
        }
    }
}

/* ============ Hit Testing ============ */

widget_t *widget_hit_test(widget_t *root, int32_t x, int32_t y) {
    if (!root || !root->visible) return NULL;
    
    /* Check if point is inside widget */
    if (x < root->x || x >= root->x + (int32_t)root->width ||
        y < root->y || y >= root->y + (int32_t)root->height) {
        return NULL;
    }
    
    /* For containers, check children (reverse order for z-order) */
    if (root->type == WIDGET_CONTAINER && root->children) {
        for (int i = (int)root->child_count - 1; i >= 0; i--) {
            widget_t *hit = widget_hit_test(root->children[i], x, y);
            if (hit) return hit;
        }
    }
    
    return root;
}
