/*
 * NeolyxOS - NXRender ProgressBar Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/progressbar.h"
#include <stdlib.h>

static void progressbar_render(nx_widget_t* self, nx_context_t* ctx);

static const nx_widget_vtable_t progressbar_vtable = {
    .render = progressbar_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = NULL,
    .destroy = NULL
};

nx_progressbar_t* nx_progressbar_create(void) {
    nx_progressbar_t* pb = calloc(1, sizeof(nx_progressbar_t));
    if (!pb) return NULL;
    
    nx_widget_init(&pb->base, &progressbar_vtable);
    pb->progress = 0.0f;
    pb->style = NX_PROGRESS_LINEAR;
    pb->track_color = nx_rgb(45, 45, 50);
    pb->fill_color = nx_rgb(0, 122, 255);
    pb->height = 4;
    pb->corner_radius = 2;
    pb->indeterminate_offset = 0.0f;
    
    return pb;
}

void nx_progressbar_destroy(nx_progressbar_t* pb) {
    if (!pb) return;
    nx_widget_destroy(&pb->base);
    free(pb);
}

void nx_progressbar_set_progress(nx_progressbar_t* pb, float progress) {
    if (!pb) return;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    pb->progress = progress;
}

float nx_progressbar_get_progress(nx_progressbar_t* pb) {
    return pb ? pb->progress : 0.0f;
}

void nx_progressbar_set_style(nx_progressbar_t* pb, nx_progress_style_t style) {
    if (pb) pb->style = style;
}

void nx_progressbar_set_track_color(nx_progressbar_t* pb, nx_color_t color) {
    if (pb) pb->track_color = color;
}

void nx_progressbar_set_fill_color(nx_progressbar_t* pb, nx_color_t color) {
    if (pb) pb->fill_color = color;
}

void nx_progressbar_set_height(nx_progressbar_t* pb, uint32_t height) {
    if (pb) pb->height = height;
}

void nx_progressbar_set_corner_radius(nx_progressbar_t* pb, uint32_t radius) {
    if (pb) pb->corner_radius = radius;
}

void nx_progressbar_animate(nx_progressbar_t* pb, float dt) {
    if (!pb || pb->style != NX_PROGRESS_INDETERMINATE) return;
    pb->indeterminate_offset += dt * 2.0f;
    if (pb->indeterminate_offset > 1.0f) {
        pb->indeterminate_offset -= 1.0f;
    }
}

static void progressbar_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_progressbar_t* pb = (nx_progressbar_t*)self;
    if (!self->visible) return;
    
    nx_rect_t bounds = self->bounds;
    uint32_t h = pb->height;
    int32_t y_offset = (bounds.height - h) / 2;
    
    /* Track */
    nx_rect_t track = { bounds.x, bounds.y + y_offset, bounds.width, h };
    nxgfx_fill_rounded_rect(ctx, track, pb->track_color, pb->corner_radius);
    
    if (pb->style == NX_PROGRESS_LINEAR) {
        /* Fill based on progress */
        uint32_t fill_width = (uint32_t)(bounds.width * pb->progress);
        if (fill_width > 0) {
            nx_rect_t fill = { bounds.x, bounds.y + y_offset, fill_width, h };
            nxgfx_fill_rounded_rect(ctx, fill, pb->fill_color, pb->corner_radius);
        }
    } else {
        /* Indeterminate: sliding bar */
        uint32_t bar_width = bounds.width / 3;
        int32_t offset = (int32_t)((bounds.width + bar_width) * pb->indeterminate_offset);
        offset -= bar_width;
        
        nx_rect_t fill = { bounds.x + offset, bounds.y + y_offset, bar_width, h };
        nxgfx_fill_rounded_rect(ctx, fill, pb->fill_color, pb->corner_radius);
    }
}
