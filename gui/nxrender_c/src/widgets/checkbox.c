/*
 * NeolyxOS - NXRender Checkbox Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/checkbox.h"
#include <stdlib.h>
#include <string.h>

static void checkbox_render(nx_widget_t* self, nx_context_t* ctx);
static nx_event_result_t checkbox_handle_event(nx_widget_t* self, nx_event_t* event);
static void checkbox_destroy(nx_widget_t* self);

static const nx_widget_vtable_t checkbox_vtable = {
    .render = checkbox_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = checkbox_handle_event,
    .destroy = checkbox_destroy
};

nx_checkbox_t* nx_checkbox_create(const char* label) {
    nx_checkbox_t* cb = calloc(1, sizeof(nx_checkbox_t));
    if (!cb) return NULL;
    
    nx_widget_init(&cb->base, &checkbox_vtable);
    cb->label = label ? strdup(label) : NULL;
    cb->checked = false;
    cb->check_color = nx_rgb(0, 122, 255);
    cb->box_color = nx_rgb(60, 60, 65);
    cb->label_color = nx_rgb(240, 240, 245);
    cb->box_size = 18;
    
    return cb;
}

void nx_checkbox_destroy(nx_checkbox_t* cb) {
    if (!cb) return;
    free(cb->label);
    nx_widget_destroy(&cb->base);
    free(cb);
}

static void checkbox_destroy(nx_widget_t* self) {
    nx_checkbox_t* cb = (nx_checkbox_t*)self;
    free(cb->label);
}

void nx_checkbox_set_checked(nx_checkbox_t* cb, bool checked) {
    if (!cb || cb->checked == checked) return;
    cb->checked = checked;
    if (cb->on_changed) {
        cb->on_changed(cb, checked, cb->callback_data);
    }
}

bool nx_checkbox_is_checked(nx_checkbox_t* cb) {
    return cb ? cb->checked : false;
}

void nx_checkbox_set_label(nx_checkbox_t* cb, const char* label) {
    if (!cb) return;
    free(cb->label);
    cb->label = label ? strdup(label) : NULL;
}

const char* nx_checkbox_get_label(nx_checkbox_t* cb) {
    return cb ? cb->label : NULL;
}

void nx_checkbox_set_check_color(nx_checkbox_t* cb, nx_color_t color) {
    if (cb) cb->check_color = color;
}

void nx_checkbox_set_box_color(nx_checkbox_t* cb, nx_color_t color) {
    if (cb) cb->box_color = color;
}

void nx_checkbox_set_box_size(nx_checkbox_t* cb, uint32_t size) {
    if (cb) cb->box_size = size;
}

void nx_checkbox_set_on_changed(nx_checkbox_t* cb, nx_checkbox_changed_fn fn, void* data) {
    if (!cb) return;
    cb->on_changed = fn;
    cb->callback_data = data;
}

void nx_checkbox_toggle(nx_checkbox_t* cb) {
    if (cb) nx_checkbox_set_checked(cb, !cb->checked);
}

static void checkbox_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_checkbox_t* cb = (nx_checkbox_t*)self;
    if (!self->visible) return;
    
    nx_rect_t bounds = self->bounds;
    uint32_t size = cb->box_size;
    int32_t box_y = bounds.y + (bounds.height - size) / 2;
    
    /* Draw box */
    nx_rect_t box = { bounds.x, box_y, size, size };
    nxgfx_fill_rounded_rect(ctx, box, cb->box_color, 4);
    
    /* Draw check if checked */
    if (cb->checked) {
        nx_rect_t inner = { bounds.x + 3, box_y + 3, size - 6, size - 6 };
        nxgfx_fill_rounded_rect(ctx, inner, cb->check_color, 2);
    }
    
    /* Draw label */
    if (cb->label) {
        nx_point_t pos = { bounds.x + size + 8, box_y + size - 4 };
        nxgfx_draw_text(ctx, cb->label, pos, NULL, cb->label_color);
    }
}

static nx_event_result_t checkbox_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_checkbox_t* cb = (nx_checkbox_t*)self;
    
    if (event->type == NX_EVENT_MOUSE_DOWN) {
        if (nx_rect_contains(self->bounds, event->pos)) {
            nx_checkbox_toggle(cb);
            return NX_EVENT_NEEDS_REDRAW;
        }
    }
    
    return NX_EVENT_IGNORED;
}
