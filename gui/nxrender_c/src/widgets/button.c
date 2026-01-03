/*
 * NeolyxOS - NXRender Button Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include "widgets/button.h"
#include <stdlib.h>
#include <string.h>

static void button_render(nx_widget_t* self, nx_context_t* ctx);
static nx_size_t button_measure(nx_widget_t* self, nx_size_t available);
static nx_event_result_t button_handle_event(nx_widget_t* self, nx_event_t* event);
static void button_destroy(nx_widget_t* self);

static const nx_widget_vtable_t BUTTON_VTABLE = {
    .render = button_render,
    .layout = NULL,
    .measure = button_measure,
    .handle_event = button_handle_event,
    .destroy = button_destroy
};

nx_button_style_t nx_button_default_style(void) {
    return (nx_button_style_t){
        .background = nx_rgba(60, 130, 220, 255),
        .background_hover = nx_rgba(80, 150, 240, 255),
        .background_pressed = nx_rgba(40, 100, 180, 255),
        .text_color = NX_COLOR_WHITE,
        .corner_radius = 6,
        .padding = 12,
        .font_size = 14
    };
}

nx_button_t* nx_button_create(const char* label) {
    nx_button_t* btn = (nx_button_t*)malloc(sizeof(nx_button_t));
    if (!btn) return NULL;
    nx_widget_init(&btn->base, &BUTTON_VTABLE);
    btn->label = label ? strdup(label) : NULL;
    btn->style = nx_button_default_style();
    btn->on_click = NULL;
    btn->callback_data = NULL;
    return btn;
}

void nx_button_set_on_click(nx_button_t* btn, nx_button_callback_t cb, void* data) {
    if (!btn) return;
    btn->on_click = cb;
    btn->callback_data = data;
}

void nx_button_set_style(nx_button_t* btn, nx_button_style_t style) {
    if (btn) btn->style = style;
}

void nx_button_set_label(nx_button_t* btn, const char* label) {
    if (!btn) return;
    free(btn->label);
    btn->label = label ? strdup(label) : NULL;
}

static void button_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_button_t* btn = (nx_button_t*)self;
    nx_color_t bg = btn->style.background;
    if (nx_widget_has_state(self, NX_WIDGET_PRESSED)) {
        bg = btn->style.background_pressed;
    } else if (nx_widget_has_state(self, NX_WIDGET_HOVERED)) {
        bg = btn->style.background_hover;
    }
    nxgfx_fill_rounded_rect(ctx, self->bounds, bg, btn->style.corner_radius);
    if (btn->label) {
        nx_font_t* font = nxgfx_font_default(ctx, btn->style.font_size);
        nx_size_t text_size = nxgfx_measure_text(ctx, btn->label, font);
        nx_point_t text_pos = {
            .x = self->bounds.x + (self->bounds.width - text_size.width) / 2,
            .y = self->bounds.y + (self->bounds.height - text_size.height) / 2
        };
        nxgfx_draw_text(ctx, btn->label, text_pos, font, btn->style.text_color);
        nxgfx_font_destroy(font);
    }
}

static nx_size_t button_measure(nx_widget_t* self, nx_size_t available) {
    nx_button_t* btn = (nx_button_t*)self;
    (void)available;
    uint32_t w = btn->style.padding * 2;
    uint32_t h = btn->style.font_size + btn->style.padding * 2;
    if (btn->label) {
        w += (uint32_t)strlen(btn->label) * 8;
    }
    return (nx_size_t){w, h};
}

static nx_event_result_t button_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_button_t* btn = (nx_button_t*)self;
    if (!nx_rect_contains(self->bounds, event->pos)) {
        if (nx_widget_has_state(self, NX_WIDGET_HOVERED)) {
            nx_widget_clear_state(self, NX_WIDGET_HOVERED);
            return NX_EVENT_NEEDS_REDRAW;
        }
        return NX_EVENT_IGNORED;
    }
    switch (event->type) {
        case NX_EVENT_MOUSE_MOVE:
            if (!nx_widget_has_state(self, NX_WIDGET_HOVERED)) {
                nx_widget_set_state(self, NX_WIDGET_HOVERED);
                return NX_EVENT_NEEDS_REDRAW;
            }
            return NX_EVENT_HANDLED;
        case NX_EVENT_MOUSE_DOWN:
            nx_widget_set_state(self, NX_WIDGET_PRESSED);
            return NX_EVENT_NEEDS_REDRAW;
        case NX_EVENT_MOUSE_UP:
            if (nx_widget_has_state(self, NX_WIDGET_PRESSED)) {
                nx_widget_clear_state(self, NX_WIDGET_PRESSED);
                if (btn->on_click) btn->on_click(btn->callback_data);
                return NX_EVENT_NEEDS_REDRAW;
            }
            return NX_EVENT_HANDLED;
        default:
            return NX_EVENT_IGNORED;
    }
}

static void button_destroy(nx_widget_t* self) {
    nx_button_t* btn = (nx_button_t*)self;
    free(btn->label);
    free(btn);
}
