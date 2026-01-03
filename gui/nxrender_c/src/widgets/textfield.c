/*
 * NeolyxOS - NXRender TextField Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include "widgets/textfield.h"
#include <stdlib.h>
#include <string.h>

static void textfield_render(nx_widget_t* self, nx_context_t* ctx);
static nx_size_t textfield_measure(nx_widget_t* self, nx_size_t available);
static nx_event_result_t textfield_handle_event(nx_widget_t* self, nx_event_t* event);
static void textfield_destroy(nx_widget_t* self);

static const nx_widget_vtable_t TEXTFIELD_VTABLE = {
    .render = textfield_render,
    .layout = NULL,
    .measure = textfield_measure,
    .handle_event = textfield_handle_event,
    .destroy = textfield_destroy
};

nx_textfield_style_t nx_textfield_default_style(void) {
    return (nx_textfield_style_t){
        .background = nx_rgba(45, 45, 50, 255),
        .background_focused = nx_rgba(55, 55, 60, 255),
        .text_color = NX_COLOR_WHITE,
        .placeholder_color = nx_rgba(128, 128, 128, 255),
        .cursor_color = nx_rgba(100, 180, 255, 255),
        .border_color = nx_rgba(70, 70, 75, 255),
        .border_focused = nx_rgba(100, 150, 255, 255),
        .corner_radius = 4,
        .padding = 8,
        .font_size = 14,
        .border_width = 1
    };
}

nx_textfield_t* nx_textfield_create(const char* placeholder) {
    nx_textfield_t* tf = calloc(1, sizeof(nx_textfield_t));
    if (!tf) return NULL;
    nx_widget_init(&tf->base, &TEXTFIELD_VTABLE);
    tf->text_capacity = 256;
    tf->text = calloc(tf->text_capacity, 1);
    tf->placeholder = placeholder ? strdup(placeholder) : NULL;
    tf->style = nx_textfield_default_style();
    return tf;
}

void nx_textfield_set_text(nx_textfield_t* tf, const char* text) {
    if (!tf) return;
    size_t len = text ? strlen(text) : 0;
    if (len >= tf->text_capacity) {
        tf->text_capacity = len + 64;
        tf->text = realloc(tf->text, tf->text_capacity);
    }
    if (text) strcpy(tf->text, text); else tf->text[0] = '\0';
    tf->cursor_pos = len;
}

const char* nx_textfield_get_text(nx_textfield_t* tf) {
    return tf ? tf->text : "";
}

void nx_textfield_set_placeholder(nx_textfield_t* tf, const char* placeholder) {
    if (!tf) return;
    free(tf->placeholder);
    tf->placeholder = placeholder ? strdup(placeholder) : NULL;
}

void nx_textfield_set_on_change(nx_textfield_t* tf, nx_textfield_callback_t cb, void* data) {
    if (tf) { tf->on_change = cb; tf->callback_data = data; }
}

void nx_textfield_set_on_submit(nx_textfield_t* tf, nx_textfield_callback_t cb, void* data) {
    if (tf) { tf->on_submit = cb; tf->callback_data = data; }
}

void nx_textfield_set_password_mode(nx_textfield_t* tf, bool enabled) {
    if (tf) tf->password_mode = enabled;
}

void nx_textfield_set_style(nx_textfield_t* tf, nx_textfield_style_t style) {
    if (tf) tf->style = style;
}

static void textfield_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_textfield_t* tf = (nx_textfield_t*)self;
    nx_textfield_style_t* s = &tf->style;
    bool focused = nx_widget_has_state(self, NX_WIDGET_FOCUSED);
    nx_color_t bg = focused ? s->background_focused : s->background;
    nx_color_t border = focused ? s->border_focused : s->border_color;
    nxgfx_fill_rounded_rect(ctx, self->bounds, bg, s->corner_radius);
    nxgfx_draw_rect(ctx, self->bounds, border, s->border_width);
    nx_font_t* font = nxgfx_font_default(ctx, s->font_size);
    nx_point_t text_pos = { self->bounds.x + s->padding, self->bounds.y + s->padding };
    const char* display = tf->text[0] ? tf->text : tf->placeholder;
    nx_color_t text_color = tf->text[0] ? s->text_color : s->placeholder_color;
    if (tf->password_mode && tf->text[0]) {
        size_t len = strlen(tf->text);
        char* masked = malloc(len + 1);
        memset(masked, '*', len);
        masked[len] = '\0';
        nxgfx_draw_text(ctx, masked, text_pos, font, text_color);
        free(masked);
    } else if (display) {
        nxgfx_draw_text(ctx, display, text_pos, font, text_color);
    }
    if (focused && tf->text) {
        uint32_t scale = s->font_size / 16 + 1;
        int32_t cursor_x = text_pos.x + (int32_t)(tf->cursor_pos * 8 * scale);
        nx_point_t c1 = { cursor_x, self->bounds.y + s->padding };
        nx_point_t c2 = { cursor_x, self->bounds.y + self->bounds.height - s->padding };
        nxgfx_draw_line(ctx, c1, c2, s->cursor_color, 2);
    }
    nxgfx_font_destroy(font);
}

static nx_size_t textfield_measure(nx_widget_t* self, nx_size_t available) {
    nx_textfield_t* tf = (nx_textfield_t*)self;
    (void)available;
    uint32_t h = tf->style.font_size + tf->style.padding * 2;
    return (nx_size_t){ 200, h };
}

static nx_event_result_t textfield_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_textfield_t* tf = (nx_textfield_t*)self;
    switch (event->type) {
        case NX_EVENT_MOUSE_DOWN:
            if (nx_rect_contains(self->bounds, event->pos)) {
                nx_widget_set_state(self, NX_WIDGET_FOCUSED);
                return NX_EVENT_NEEDS_REDRAW;
            } else if (nx_widget_has_state(self, NX_WIDGET_FOCUSED)) {
                nx_widget_clear_state(self, NX_WIDGET_FOCUSED);
                return NX_EVENT_NEEDS_REDRAW;
            }
            break;
        case NX_EVENT_TEXT_INPUT:
            if (nx_widget_has_state(self, NX_WIDGET_FOCUSED)) {
                size_t len = strlen(tf->text);
                size_t add_len = strlen(event->text);
                if (len + add_len >= tf->text_capacity) {
                    tf->text_capacity = len + add_len + 64;
                    tf->text = realloc(tf->text, tf->text_capacity);
                }
                memmove(tf->text + tf->cursor_pos + add_len, tf->text + tf->cursor_pos, len - tf->cursor_pos + 1);
                memcpy(tf->text + tf->cursor_pos, event->text, add_len);
                tf->cursor_pos += add_len;
                if (tf->on_change) tf->on_change(tf->text, tf->callback_data);
                return NX_EVENT_NEEDS_REDRAW;
            }
            break;
        case NX_EVENT_KEY_DOWN:
            if (!nx_widget_has_state(self, NX_WIDGET_FOCUSED)) break;
            if (event->keycode == 0x0E && tf->cursor_pos > 0) {
                memmove(tf->text + tf->cursor_pos - 1, tf->text + tf->cursor_pos, strlen(tf->text) - tf->cursor_pos + 1);
                tf->cursor_pos--;
                if (tf->on_change) tf->on_change(tf->text, tf->callback_data);
                return NX_EVENT_NEEDS_REDRAW;
            } else if (event->keycode == 0x1C && tf->on_submit) {
                tf->on_submit(tf->text, tf->callback_data);
                return NX_EVENT_HANDLED;
            } else if (event->keycode == 0x4B && tf->cursor_pos > 0) {
                tf->cursor_pos--;
                return NX_EVENT_NEEDS_REDRAW;
            } else if (event->keycode == 0x4D && tf->cursor_pos < strlen(tf->text)) {
                tf->cursor_pos++;
                return NX_EVENT_NEEDS_REDRAW;
            }
            break;
        default: break;
    }
    return NX_EVENT_IGNORED;
}

static void textfield_destroy(nx_widget_t* self) {
    nx_textfield_t* tf = (nx_textfield_t*)self;
    free(tf->text);
    free(tf->placeholder);
    free(tf);
}
