/*
 * NeolyxOS - NXRender Compositor Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxrender/compositor.h"
#include <stdlib.h>
#include <string.h>

nx_surface_t* nx_surface_create(uint32_t width, uint32_t height) {
    nx_surface_t* s = (nx_surface_t*)malloc(sizeof(nx_surface_t));
    if (!s) return NULL;
    s->buffer = (uint32_t*)calloc(width * height, sizeof(uint32_t));
    if (!s->buffer) { free(s); return NULL; }
    s->width = width;
    s->height = height;
    s->dirty = (nx_rect_t){0, 0, width, height};
    s->needs_redraw = true;
    return s;
}

void nx_surface_destroy(nx_surface_t* surface) {
    if (surface) {
        free(surface->buffer);
        free(surface);
    }
}

void nx_surface_clear(nx_surface_t* surface, nx_color_t color) {
    if (!surface) return;
    uint32_t argb = ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16) |
                    ((uint32_t)color.g << 8) | color.b;
    for (uint32_t i = 0; i < surface->width * surface->height; i++) {
        surface->buffer[i] = argb;
    }
    surface->dirty = (nx_rect_t){0, 0, surface->width, surface->height};
}

void nx_surface_mark_dirty(nx_surface_t* surface, nx_rect_t rect) {
    if (!surface) return;
    if (!surface->needs_redraw) {
        surface->dirty = rect;
        surface->needs_redraw = true;
    } else {
        int32_t x0 = surface->dirty.x < rect.x ? surface->dirty.x : rect.x;
        int32_t y0 = surface->dirty.y < rect.y ? surface->dirty.y : rect.y;
        int32_t x1_old = surface->dirty.x + surface->dirty.width;
        int32_t x1_new = rect.x + rect.width;
        int32_t y1_old = surface->dirty.y + surface->dirty.height;
        int32_t y1_new = rect.y + rect.height;
        surface->dirty.x = x0;
        surface->dirty.y = y0;
        surface->dirty.width = (x1_old > x1_new ? x1_old : x1_new) - x0;
        surface->dirty.height = (y1_old > y1_new ? y1_old : y1_new) - y0;
    }
}

nx_layer_t* nx_layer_create(nx_surface_t* surface, int32_t z_order) {
    nx_layer_t* layer = (nx_layer_t*)malloc(sizeof(nx_layer_t));
    if (!layer) return NULL;
    layer->surface = surface;
    layer->position = (nx_point_t){0, 0};
    layer->z_order = z_order;
    layer->opacity = 1.0f;
    layer->visible = true;
    layer->next = NULL;
    return layer;
}

void nx_layer_destroy(nx_layer_t* layer) {
    if (layer) free(layer);
}

nx_compositor_t* nx_compositor_create(nx_context_t* ctx) {
    nx_compositor_t* comp = (nx_compositor_t*)malloc(sizeof(nx_compositor_t));
    if (!comp) return NULL;
    comp->ctx = ctx;
    comp->layers = NULL;
    comp->has_damage = false;
    return comp;
}

void nx_compositor_destroy(nx_compositor_t* comp) {
    if (comp) {
        nx_layer_t* l = comp->layers;
        while (l) {
            nx_layer_t* next = l->next;
            nx_layer_destroy(l);
            l = next;
        }
        free(comp);
    }
}

void nx_compositor_add_layer(nx_compositor_t* comp, nx_layer_t* layer) {
    if (!comp || !layer) return;
    if (!comp->layers || layer->z_order < comp->layers->z_order) {
        layer->next = comp->layers;
        comp->layers = layer;
        return;
    }
    nx_layer_t* curr = comp->layers;
    while (curr->next && curr->next->z_order <= layer->z_order) {
        curr = curr->next;
    }
    layer->next = curr->next;
    curr->next = layer;
}

void nx_compositor_remove_layer(nx_compositor_t* comp, nx_layer_t* layer) {
    if (!comp || !layer) return;
    if (comp->layers == layer) {
        comp->layers = layer->next;
        return;
    }
    nx_layer_t* curr = comp->layers;
    while (curr->next && curr->next != layer) curr = curr->next;
    if (curr->next == layer) curr->next = layer->next;
}

void nx_compositor_damage(nx_compositor_t* comp, nx_rect_t rect) {
    if (!comp) return;
    if (!comp->has_damage) {
        comp->damage = rect;
        comp->has_damage = true;
    } else {
        int32_t x0 = comp->damage.x < rect.x ? comp->damage.x : rect.x;
        int32_t y0 = comp->damage.y < rect.y ? comp->damage.y : rect.y;
        int32_t x1_old = comp->damage.x + comp->damage.width;
        int32_t x1_new = rect.x + rect.width;
        int32_t y1_old = comp->damage.y + comp->damage.height;
        int32_t y1_new = rect.y + rect.height;
        comp->damage.x = x0;
        comp->damage.y = y0;
        comp->damage.width = (x1_old > x1_new ? x1_old : x1_new) - x0;
        comp->damage.height = (y1_old > y1_new ? y1_old : y1_new) - y0;
    }
}

void nx_compositor_composite(nx_compositor_t* comp) {
    if (!comp || !comp->ctx) return;
    for (nx_layer_t* l = comp->layers; l; l = l->next) {
        if (!l->visible || !l->surface) continue;
        nx_surface_t* s = l->surface;
        for (uint32_t y = 0; y < s->height; y++) {
            for (uint32_t x = 0; x < s->width; x++) {
                uint32_t src = s->buffer[y * s->width + x];
                uint8_t a = (src >> 24) & 0xFF;
                if (a == 0) continue;
                a = (uint8_t)(a * l->opacity);
                nx_color_t c = { (src >> 16), (src >> 8), src, a };
                nxgfx_put_pixel(comp->ctx, l->position.x + x, l->position.y + y, c);
            }
        }
        s->needs_redraw = false;
    }
    comp->has_damage = false;
}
