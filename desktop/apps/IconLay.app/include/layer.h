/*
 * Layer System for IconLay
 * 
 * Layer manipulation, blending, and rendering.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef LAYER_H
#define LAYER_H

#include "nxi_format.h"
#include <stdint.h>
#include <stdbool.h>

/* Layer bounding box */
typedef struct {
    float x, y;          /* Top-left (normalized 0-1) */
    float width, height; /* Size (normalized 0-1) */
} layer_bounds_t;

/* Hit test a layer at point (normalized coords) */
bool layer_hit_test(const nxi_layer_t* layer, float x, float y);

/* Get layer bounding box */
layer_bounds_t layer_get_bounds(const nxi_layer_t* layer);

/* Set layer position (center point, normalized 0-1) */
void layer_set_position(nxi_layer_t* layer, float x, float y);

/* Set layer scale */
void layer_set_scale(nxi_layer_t* layer, float scale);

/* Set layer fill color (RGBA) */
void layer_set_color(nxi_layer_t* layer, uint32_t color);

/* Set layer opacity (0-255) */
void layer_set_opacity(nxi_layer_t* layer, uint8_t opacity);

/* Toggle layer visibility */
void layer_set_visible(nxi_layer_t* layer, bool visible);

/* Duplicate layer */
nxi_layer_t* layer_duplicate(const nxi_layer_t* layer);

/* Render layer to pixel buffer 
 * buf: RGBA pixel buffer (size x size x 4 bytes)
 * size: target render size
 * Returns 0 on success
 */
int layer_render(const nxi_layer_t* layer, uint8_t* buf, uint32_t size);

/* Render all layers (bottom to top) with blending */
int layers_render_all(nxi_layer_t** layers, uint32_t count, 
                      uint8_t* buf, uint32_t size);

/* Apply effect to layer render */
void layer_apply_effects(uint8_t* buf, uint32_t size, 
                         const nxi_layer_t* layer);

#endif /* LAYER_H */
