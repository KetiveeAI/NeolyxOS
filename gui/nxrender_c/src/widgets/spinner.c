/*
 * NeolyxOS - NXRender Spinner Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/spinner.h"
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void spinner_render(nx_widget_t* self, nx_context_t* ctx);

static const nx_widget_vtable_t spinner_vtable = {
    .render = spinner_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = NULL,
    .destroy = NULL
};

nx_spinner_t* nx_spinner_create(void) {
    nx_spinner_t* sp = calloc(1, sizeof(nx_spinner_t));
    if (!sp) return NULL;
    
    nx_widget_init(&sp->base, &spinner_vtable);
    sp->style = NX_SPINNER_CIRCULAR;
    sp->animating = false;
    sp->rotation = 0.0f;
    sp->speed = 1.0f;
    sp->color = nx_rgb(0, 122, 255);
    sp->track_color = nx_rgb(45, 45, 50);
    sp->size = 32;
    sp->thickness = 3;
    
    return sp;
}

void nx_spinner_destroy(nx_spinner_t* sp) {
    if (!sp) return;
    nx_widget_destroy(&sp->base);
    free(sp);
}

void nx_spinner_start(nx_spinner_t* sp) {
    if (sp) sp->animating = true;
}

void nx_spinner_stop(nx_spinner_t* sp) {
    if (sp) sp->animating = false;
}

bool nx_spinner_is_animating(nx_spinner_t* sp) {
    return sp ? sp->animating : false;
}

void nx_spinner_animate(nx_spinner_t* sp, float dt) {
    if (!sp || !sp->animating) return;
    sp->rotation += dt * sp->speed * 360.0f;
    if (sp->rotation >= 360.0f) sp->rotation -= 360.0f;
}

void nx_spinner_set_style(nx_spinner_t* sp, nx_spinner_style_t style) {
    if (sp) sp->style = style;
}

void nx_spinner_set_color(nx_spinner_t* sp, nx_color_t color) {
    if (sp) sp->color = color;
}

void nx_spinner_set_track_color(nx_spinner_t* sp, nx_color_t color) {
    if (sp) sp->track_color = color;
}

void nx_spinner_set_size(nx_spinner_t* sp, uint32_t size) {
    if (sp) sp->size = size;
}

void nx_spinner_set_thickness(nx_spinner_t* sp, uint32_t thickness) {
    if (sp) sp->thickness = thickness;
}

void nx_spinner_set_speed(nx_spinner_t* sp, float speed) {
    if (sp) sp->speed = speed;
}

static void spinner_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_spinner_t* sp = (nx_spinner_t*)self;
    if (!self->visible) return;
    
    nx_rect_t bounds = self->bounds;
    int32_t cx = bounds.x + bounds.width / 2;
    int32_t cy = bounds.y + bounds.height / 2;
    uint32_t r = sp->size / 2;
    
    if (sp->style == NX_SPINNER_CIRCULAR) {
        /* Track circle */
        nxgfx_draw_arc(ctx, (nx_point_t){cx, cy}, r, 0, 2 * M_PI, sp->track_color, sp->thickness);
        
        /* Spinning arc (90 degrees) */
        float start = sp->rotation * M_PI / 180.0f;
        float end = start + M_PI / 2;
        nxgfx_draw_thick_arc(ctx, (nx_point_t){cx, cy}, r, start, end, sp->color, sp->thickness);
        
    } else if (sp->style == NX_SPINNER_DOTS) {
        /* 8 dots around circle */
        int num_dots = 8;
        for (int i = 0; i < num_dots; i++) {
            float angle = (i * 360.0f / num_dots + sp->rotation) * M_PI / 180.0f;
            int32_t dx = cx + (int32_t)(r * cosf(angle));
            int32_t dy = cy + (int32_t)(r * sinf(angle));
            
            /* Fade dots based on position */
            uint8_t alpha = 255 - (i * 255 / num_dots);
            nx_color_t dot_color = { sp->color.r, sp->color.g, sp->color.b, alpha };
            nxgfx_fill_circle(ctx, (nx_point_t){dx, dy}, sp->thickness + 1, dot_color);
        }
        
    } else if (sp->style == NX_SPINNER_BAR) {
        /* 3 vertical bars */
        int bar_count = 3;
        int bar_width = sp->size / 5;
        int bar_gap = bar_width / 2;
        int total_width = bar_count * bar_width + (bar_count - 1) * bar_gap;
        int start_x = cx - total_width / 2;
        
        for (int i = 0; i < bar_count; i++) {
            /* Animate height based on rotation */
            float phase = sp->rotation + i * 120.0f;
            float scale = 0.5f + 0.5f * sinf(phase * M_PI / 180.0f);
            int bar_height = (int)(sp->size * (0.3f + 0.7f * scale));
            int bar_y = cy - bar_height / 2;
            
            nx_rect_t bar = { start_x + i * (bar_width + bar_gap), bar_y, bar_width, bar_height };
            nxgfx_fill_rounded_rect(ctx, bar, sp->color, bar_width / 2);
        }
    }
}
