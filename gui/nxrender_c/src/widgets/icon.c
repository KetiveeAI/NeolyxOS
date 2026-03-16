/*
 * NeolyxOS - NXRender Icon Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/icon.h"
#include <stdlib.h>
#include <string.h>

static void icon_render(nx_widget_t* self, nx_context_t* ctx);
static void icon_destroy(nx_widget_t* self);

static const nx_widget_vtable_t icon_vtable = {
    .render = icon_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = NULL,
    .destroy = icon_destroy
};

nx_icon_t* nx_icon_create(void) {
    nx_icon_t* icon = calloc(1, sizeof(nx_icon_t));
    if (!icon) return NULL;
    
    nx_widget_init(&icon->base, &icon_vtable);
    icon->source = NX_ICON_NONE;
    icon->width = 24;
    icon->height = 24;
    icon->tint = nx_rgb(255, 255, 255);
    icon->use_tint = false;
    
    return icon;
}

nx_icon_t* nx_icon_create_builtin(nx_builtin_icon_t builtin) {
    nx_icon_t* icon = nx_icon_create();
    if (icon) {
        icon->source = NX_ICON_BUILTIN;
        icon->builtin = builtin;
    }
    return icon;
}

nx_icon_t* nx_icon_create_from_path(const char* path) {
    nx_icon_t* icon = nx_icon_create();
    if (icon && path) {
        icon->source = NX_ICON_PATH;
        icon->path = strdup(path);
    }
    return icon;
}

void nx_icon_destroy(nx_icon_t* icon) {
    if (!icon) return;
    free(icon->path);
    free(icon->data);
    nx_widget_destroy(&icon->base);
    free(icon);
}

static void icon_destroy(nx_widget_t* self) {
    nx_icon_t* icon = (nx_icon_t*)self;
    free(icon->path);
    free(icon->data);
    icon->path = NULL;
    icon->data = NULL;
}

void nx_icon_set_builtin(nx_icon_t* icon, nx_builtin_icon_t builtin) {
    if (!icon) return;
    free(icon->path);
    free(icon->data);
    icon->path = NULL;
    icon->data = NULL;
    icon->source = NX_ICON_BUILTIN;
    icon->builtin = builtin;
}

void nx_icon_set_path(nx_icon_t* icon, const char* path) {
    if (!icon) return;
    free(icon->path);
    free(icon->data);
    icon->data = NULL;
    icon->path = path ? strdup(path) : NULL;
    icon->source = path ? NX_ICON_PATH : NX_ICON_NONE;
}

void nx_icon_set_data(nx_icon_t* icon, const uint8_t* data, size_t size) {
    if (!icon) return;
    free(icon->path);
    free(icon->data);
    icon->path = NULL;
    
    if (data && size > 0) {
        icon->data = malloc(size);
        if (icon->data) {
            memcpy(icon->data, data, size);
            icon->data_size = size;
            icon->source = NX_ICON_DATA;
        }
    } else {
        icon->data = NULL;
        icon->source = NX_ICON_NONE;
    }
}

void nx_icon_set_size(nx_icon_t* icon, uint32_t width, uint32_t height) {
    if (icon) {
        icon->width = width;
        icon->height = height;
    }
}

uint32_t nx_icon_get_width(nx_icon_t* icon) {
    return icon ? icon->width : 0;
}

uint32_t nx_icon_get_height(nx_icon_t* icon) {
    return icon ? icon->height : 0;
}

void nx_icon_set_tint(nx_icon_t* icon, nx_color_t tint) {
    if (icon) {
        icon->tint = tint;
        icon->use_tint = true;
    }
}

void nx_icon_clear_tint(nx_icon_t* icon) {
    if (icon) icon->use_tint = false;
}

/* Built-in icon drawing (simple shapes) */
static void draw_builtin_icon(nx_context_t* ctx, nx_rect_t bounds, nx_builtin_icon_t icon, nx_color_t color) {
    int32_t cx = bounds.x + bounds.width / 2;
    int32_t cy = bounds.y + bounds.height / 2;
    int32_t size = (bounds.width < bounds.height ? bounds.width : bounds.height) / 2;
    
    switch (icon) {
        case NX_ICON_CLOSE: {
            nx_point_t p1 = { bounds.x + 4, bounds.y + 4 };
            nx_point_t p2 = { bounds.x + bounds.width - 4, bounds.y + bounds.height - 4 };
            nx_point_t p3 = { bounds.x + bounds.width - 4, bounds.y + 4 };
            nx_point_t p4 = { bounds.x + 4, bounds.y + bounds.height - 4 };
            nxgfx_draw_line(ctx, p1, p2, color, 2);
            nxgfx_draw_line(ctx, p3, p4, color, 2);
            break;
        }
        case NX_ICON_CHECK: {
            nx_point_t p1 = { bounds.x + 4, cy };
            nx_point_t p2 = { cx - 2, bounds.y + bounds.height - 6 };
            nx_point_t p3 = { bounds.x + bounds.width - 4, bounds.y + 6 };
            nxgfx_draw_line(ctx, p1, p2, color, 2);
            nxgfx_draw_line(ctx, p2, p3, color, 2);
            break;
        }
        case NX_ICON_PLUS: {
            nx_point_t h1 = { bounds.x + 4, cy };
            nx_point_t h2 = { bounds.x + bounds.width - 4, cy };
            nx_point_t v1 = { cx, bounds.y + 4 };
            nx_point_t v2 = { cx, bounds.y + bounds.height - 4 };
            nxgfx_draw_line(ctx, h1, h2, color, 2);
            nxgfx_draw_line(ctx, v1, v2, color, 2);
            break;
        }
        case NX_ICON_MINUS: {
            nx_point_t h1 = { bounds.x + 4, cy };
            nx_point_t h2 = { bounds.x + bounds.width - 4, cy };
            nxgfx_draw_line(ctx, h1, h2, color, 2);
            break;
        }
        default: {
            /* Placeholder: draw circle */
            nxgfx_fill_circle(ctx, (nx_point_t){cx, cy}, size, color);
            break;
        }
    }
}

static void icon_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_icon_t* icon = (nx_icon_t*)self;
    if (!self->visible) return;
    
    nx_rect_t bounds = self->bounds;
    nx_color_t color = icon->use_tint ? icon->tint : nx_rgb(200, 200, 200);
    
    if (icon->source == NX_ICON_BUILTIN) {
        draw_builtin_icon(ctx, bounds, icon->builtin, color);
    } else {
        /* For path/data icons: placeholder circle */
        int32_t cx = bounds.x + bounds.width / 2;
        int32_t cy = bounds.y + bounds.height / 2;
        int32_t r = (bounds.width < bounds.height ? bounds.width : bounds.height) / 2;
        nxgfx_fill_circle(ctx, (nx_point_t){cx, cy}, r, color);
    }
}
