/*
 * NeolyxOS - NXRender Switch Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/switch.h"
#include <stdlib.h>

static void switch_render(nx_widget_t* self, nx_context_t* ctx);
static nx_event_result_t switch_handle_event(nx_widget_t* self, nx_event_t* event);
static void switch_destroy(nx_widget_t* self);

static const nx_widget_vtable_t switch_vtable = {
    .render = switch_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = switch_handle_event,
    .destroy = switch_destroy
};

nx_switch_t* nx_switch_create(void) {
    nx_switch_t* sw = calloc(1, sizeof(nx_switch_t));
    if (!sw) return NULL;
    
    nx_widget_init(&sw->base, &switch_vtable);
    sw->is_on = false;
    sw->on_color = nx_rgb(52, 199, 89);       /* Green */
    sw->off_color = nx_rgb(60, 60, 65);       /* Gray */
    sw->thumb_color = nx_rgb(255, 255, 255);  /* White */
    sw->width = 51;
    sw->height = 31;
    sw->animation_progress = 0.0f;
    
    return sw;
}

void nx_switch_destroy(nx_switch_t* sw) {
    if (!sw) return;
    nx_widget_destroy(&sw->base);
    free(sw);
}

static void switch_destroy(nx_widget_t* self) {
    (void)self;
}

void nx_switch_set_on(nx_switch_t* sw, bool is_on) {
    if (!sw || sw->is_on == is_on) return;
    sw->is_on = is_on;
    sw->animation_progress = is_on ? 1.0f : 0.0f;
    if (sw->on_toggled) {
        sw->on_toggled(sw, is_on, sw->callback_data);
    }
}

bool nx_switch_is_on(nx_switch_t* sw) {
    return sw ? sw->is_on : false;
}

void nx_switch_toggle(nx_switch_t* sw) {
    if (sw) nx_switch_set_on(sw, !sw->is_on);
}

void nx_switch_set_on_color(nx_switch_t* sw, nx_color_t color) {
    if (sw) sw->on_color = color;
}

void nx_switch_set_off_color(nx_switch_t* sw, nx_color_t color) {
    if (sw) sw->off_color = color;
}

void nx_switch_set_thumb_color(nx_switch_t* sw, nx_color_t color) {
    if (sw) sw->thumb_color = color;
}

void nx_switch_set_size(nx_switch_t* sw, uint32_t width, uint32_t height) {
    if (sw) {
        sw->width = width;
        sw->height = height;
    }
}

void nx_switch_set_on_toggled(nx_switch_t* sw, nx_switch_toggled_fn fn, void* data) {
    if (!sw) return;
    sw->on_toggled = fn;
    sw->callback_data = data;
}

static void switch_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_switch_t* sw = (nx_switch_t*)self;
    if (!self->visible) return;
    
    nx_rect_t bounds = self->bounds;
    uint32_t w = sw->width;
    uint32_t h = sw->height;
    int32_t y_offset = (bounds.height - h) / 2;
    
    /* Track */
    nx_rect_t track = { bounds.x, bounds.y + y_offset, w, h };
    nx_color_t track_color = sw->is_on ? sw->on_color : sw->off_color;
    nxgfx_fill_rounded_rect(ctx, track, track_color, h / 2);
    
    /* Thumb */
    uint32_t thumb_size = h - 4;
    float travel = w - thumb_size - 4;
    int32_t thumb_x = bounds.x + 2 + (int32_t)(sw->animation_progress * travel);
    nx_rect_t thumb = { thumb_x, bounds.y + y_offset + 2, thumb_size, thumb_size };
    nxgfx_fill_rounded_rect(ctx, thumb, sw->thumb_color, thumb_size / 2);
}

static nx_event_result_t switch_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_switch_t* sw = (nx_switch_t*)self;
    
    if (event->type == NX_EVENT_MOUSE_DOWN) {
        if (nx_rect_contains(self->bounds, event->pos)) {
            nx_switch_toggle(sw);
            return NX_EVENT_NEEDS_REDRAW;
        }
    }
    
    return NX_EVENT_IGNORED;
}
