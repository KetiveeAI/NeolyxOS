/*
 * NeolyxOS - NXRender Slider Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/slider.h"
#include <stdlib.h>
#include <string.h>

static void slider_render(nx_widget_t* self, nx_context_t* ctx);
static void slider_layout(nx_widget_t* self, nx_rect_t bounds);
static nx_size_t slider_measure(nx_widget_t* self, nx_size_t available);
static nx_event_result_t slider_handle_event(nx_widget_t* self, nx_event_t* event);
static void slider_destroy(nx_widget_t* self);

static const nx_widget_vtable_t SLIDER_VTABLE = {
    .render = slider_render,
    .layout = slider_layout,
    .measure = slider_measure,
    .handle_event = slider_handle_event,
    .destroy = slider_destroy
};

nx_slider_t* nx_slider_create(float min, float max, float initial) {
    nx_slider_t* s = calloc(1, sizeof(nx_slider_t));
    if (!s) return NULL;
    nx_widget_init(&s->base, &SLIDER_VTABLE);
    s->min_value = min;
    s->max_value = max;
    s->value = initial < min ? min : (initial > max ? max : initial);
    s->step = 0;
    s->direction = NX_SLIDER_HORIZONTAL;
    s->track_color = nx_rgba(80, 80, 90, 255);
    s->fill_color = nx_rgba(0, 122, 255, 255);
    s->thumb_color = nx_rgba(255, 255, 255, 255);
    s->track_height = 4;
    s->thumb_radius = 8;
    s->dragging = false;
    return s;
}

void nx_slider_set_value(nx_slider_t* slider, float value) {
    if (!slider) return;
    if (value < slider->min_value) value = slider->min_value;
    if (value > slider->max_value) value = slider->max_value;
    if (slider->step > 0) {
        value = slider->min_value + 
            ((int)((value - slider->min_value) / slider->step + 0.5f)) * slider->step;
    }
    slider->value = value;
}

float nx_slider_get_value(nx_slider_t* slider) {
    return slider ? slider->value : 0;
}

void nx_slider_set_step(nx_slider_t* slider, float step) {
    if (slider) slider->step = step > 0 ? step : 0;
}

void nx_slider_set_direction(nx_slider_t* slider, nx_slider_direction_t dir) {
    if (slider) slider->direction = dir;
}

void nx_slider_set_on_change(nx_slider_t* slider, void (*callback)(float, void*), void* data) {
    if (!slider) return;
    slider->on_change = callback;
    slider->callback_data = data;
}

static void slider_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_slider_t* s = (nx_slider_t*)self;
    nx_rect_t b = self->bounds;
    
    float ratio = (s->value - s->min_value) / (s->max_value - s->min_value);
    
    if (s->direction == NX_SLIDER_HORIZONTAL) {
        int32_t track_y = b.y + (b.height - s->track_height) / 2;
        nxgfx_fill_rounded_rect(ctx, (nx_rect_t){b.x, track_y, b.width, s->track_height}, 
                                 s->track_color, s->track_height / 2);
        uint32_t fill_w = (uint32_t)(b.width * ratio);
        if (fill_w > 0) {
            nxgfx_fill_rounded_rect(ctx, (nx_rect_t){b.x, track_y, fill_w, s->track_height}, 
                                     s->fill_color, s->track_height / 2);
        }
        int32_t thumb_x = b.x + (int32_t)(b.width * ratio) - s->thumb_radius;
        int32_t thumb_y = b.y + (b.height / 2) - s->thumb_radius;
        nxgfx_fill_circle(ctx, (nx_point_t){thumb_x + s->thumb_radius, thumb_y + s->thumb_radius}, 
                          s->thumb_radius, s->thumb_color);
    } else {
        int32_t track_x = b.x + (b.width - s->track_height) / 2;
        nxgfx_fill_rounded_rect(ctx, (nx_rect_t){track_x, b.y, s->track_height, b.height}, 
                                 s->track_color, s->track_height / 2);
        uint32_t fill_h = (uint32_t)(b.height * ratio);
        int32_t fill_y = b.y + b.height - fill_h;
        if (fill_h > 0) {
            nxgfx_fill_rounded_rect(ctx, (nx_rect_t){track_x, fill_y, s->track_height, fill_h}, 
                                     s->fill_color, s->track_height / 2);
        }
        int32_t thumb_y = b.y + b.height - (int32_t)(b.height * ratio) - s->thumb_radius;
        int32_t thumb_x = b.x + (b.width / 2) - s->thumb_radius;
        nxgfx_fill_circle(ctx, (nx_point_t){thumb_x + s->thumb_radius, thumb_y + s->thumb_radius}, 
                          s->thumb_radius, s->thumb_color);
    }
}

static void slider_layout(nx_widget_t* self, nx_rect_t bounds) {
    self->bounds = bounds;
}

static nx_size_t slider_measure(nx_widget_t* self, nx_size_t available) {
    nx_slider_t* s = (nx_slider_t*)self;
    if (s->direction == NX_SLIDER_HORIZONTAL) {
        return (nx_size_t){available.width, s->thumb_radius * 2 + 4};
    }
    return (nx_size_t){s->thumb_radius * 2 + 4, available.height};
}

static nx_event_result_t slider_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_slider_t* s = (nx_slider_t*)self;
    nx_rect_t b = self->bounds;
    
    switch (event->type) {
        case NX_EVENT_MOUSE_DOWN:
            if (event->pos.x >= b.x && event->pos.x < b.x + (int32_t)b.width &&
                event->pos.y >= b.y && event->pos.y < b.y + (int32_t)b.height) {
                s->dragging = true;
                float ratio;
                if (s->direction == NX_SLIDER_HORIZONTAL) {
                    ratio = (float)(event->pos.x - b.x) / b.width;
                } else {
                    ratio = 1.0f - (float)(event->pos.y - b.y) / b.height;
                }
                float old = s->value;
                nx_slider_set_value(s, s->min_value + ratio * (s->max_value - s->min_value));
                if (s->value != old && s->on_change) {
                    s->on_change(s->value, s->callback_data);
                }
                return NX_EVENT_NEEDS_REDRAW;
            }
            break;
        case NX_EVENT_MOUSE_MOVE:
            if (s->dragging) {
                float ratio;
                if (s->direction == NX_SLIDER_HORIZONTAL) {
                    ratio = (float)(event->pos.x - b.x) / b.width;
                } else {
                    ratio = 1.0f - (float)(event->pos.y - b.y) / b.height;
                }
                if (ratio < 0) ratio = 0;
                if (ratio > 1) ratio = 1;
                float old = s->value;
                nx_slider_set_value(s, s->min_value + ratio * (s->max_value - s->min_value));
                if (s->value != old && s->on_change) {
                    s->on_change(s->value, s->callback_data);
                }
                return NX_EVENT_NEEDS_REDRAW;
            }
            break;
        case NX_EVENT_MOUSE_UP:
            if (s->dragging) {
                s->dragging = false;
                return NX_EVENT_HANDLED;
            }
            break;
        default:
            break;
    }
    return NX_EVENT_IGNORED;
}

static void slider_destroy(nx_widget_t* self) {
    free(self);
}
