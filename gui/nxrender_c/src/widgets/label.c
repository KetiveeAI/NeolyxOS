/*
 * NeolyxOS - NXRender Label Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include "widgets/label.h"
#include <stdlib.h>
#include <string.h>

static void label_render(nx_widget_t* self, nx_context_t* ctx);
static nx_size_t label_measure(nx_widget_t* self, nx_size_t available);
static void label_destroy(nx_widget_t* self);

static const nx_widget_vtable_t LABEL_VTABLE = {
    .render = label_render,
    .layout = NULL,
    .measure = label_measure,
    .handle_event = NULL,
    .destroy = label_destroy
};

nx_label_style_t nx_label_default_style(void) {
    return (nx_label_style_t){
        .text_color = NX_COLOR_WHITE,
        .font_size = 14,
        .align = NX_TEXT_ALIGN_LEFT
    };
}

nx_label_t* nx_label_create(const char* text) {
    nx_label_t* label = (nx_label_t*)malloc(sizeof(nx_label_t));
    if (!label) return NULL;
    nx_widget_init(&label->base, &LABEL_VTABLE);
    label->text = text ? strdup(text) : NULL;
    label->style = nx_label_default_style();
    return label;
}

void nx_label_set_text(nx_label_t* label, const char* text) {
    if (!label) return;
    free(label->text);
    label->text = text ? strdup(text) : NULL;
}

void nx_label_set_style(nx_label_t* label, nx_label_style_t style) {
    if (label) label->style = style;
}

static void label_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_label_t* label = (nx_label_t*)self;
    if (!label->text) return;
    nx_font_t* font = nxgfx_font_default(ctx, label->style.font_size);
    nx_size_t text_size = nxgfx_measure_text(ctx, label->text, font);
    nx_point_t pos = { .x = self->bounds.x, .y = self->bounds.y };
    switch (label->style.align) {
        case NX_TEXT_ALIGN_CENTER:
            pos.x += (self->bounds.width - text_size.width) / 2;
            break;
        case NX_TEXT_ALIGN_RIGHT:
            pos.x += self->bounds.width - text_size.width;
            break;
        default:
            break;
    }
    nxgfx_draw_text(ctx, label->text, pos, font, label->style.text_color);
    nxgfx_font_destroy(font);
}

static nx_size_t label_measure(nx_widget_t* self, nx_size_t available) {
    nx_label_t* label = (nx_label_t*)self;
    (void)available;
    if (!label->text) return (nx_size_t){0, 0};
    uint32_t scale = label->style.font_size / 16 + 1;
    return (nx_size_t){
        (uint32_t)strlen(label->text) * 8 * scale,
        16 * scale
    };
}

static void label_destroy(nx_widget_t* self) {
    nx_label_t* label = (nx_label_t*)self;
    free(label->text);
    free(label);
}
