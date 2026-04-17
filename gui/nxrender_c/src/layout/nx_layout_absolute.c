/*
 * NeolyxOS - NXToolkit Layout Engine
 * Absolute / Floating Constraint resolution module
 * Copyright (c) 2025 KetiveeAI
 */

#include "layout/nx_layout_absolute.h"
#include "layout/nx_layout_math.h"
#include <stddef.h>

void nx_layout_absolute_calculate(nx_layout_node_t* node, nx_absolute_config_t config, float available_width, float available_height) {
    if (!node) return;
    
    node->type = NX_LAYOUT_TYPE_ABSOLUTE;
    
    /* Calculate dynamic scaling constraints based on defined floating points mapping exactly like absolute CSS bounds */
    bool has_left = !NX_IS_NAN(config.left);
    bool has_right = !NX_IS_NAN(config.right);
    bool has_top = !NX_IS_NAN(config.top);
    bool has_bottom = !NX_IS_NAN(config.bottom);
    
    /* X Axis resolution */
    if (has_left && has_right) {
        node->computed_x = config.left;
        node->computed_width = nx_layout_math_clamp(available_width - config.left - config.right, 0, available_width);
    } else if (has_left) {
        node->computed_x = config.left;
        node->computed_width = node->intrinsic_width;
    } else if (has_right) {
        node->computed_x = available_width - config.right - node->intrinsic_width;
        node->computed_width = node->intrinsic_width;
    } else {
        node->computed_x = 0; /* Default block layout */
        node->computed_width = node->intrinsic_width;
    }
    
    /* Y Axis resolution */
    if (has_top && has_bottom) {
        node->computed_y = config.top;
        node->computed_height = nx_layout_math_clamp(available_height - config.top - config.bottom, 0, available_height);
    } else if (has_top) {
        node->computed_y = config.top;
        node->computed_height = node->intrinsic_height;
    } else if (has_bottom) {
        node->computed_y = available_height - config.bottom - node->intrinsic_height;
        node->computed_height = node->intrinsic_height;
    } else {
        node->computed_y = 0;
        node->computed_height = node->intrinsic_height;
    }
    
    /* Enforce rigid constraints */
    node->computed_x = nx_layout_math_round_pixel(node->computed_x, 1.0f);
    node->computed_y = nx_layout_math_round_pixel(node->computed_y, 1.0f);
    node->computed_width = nx_layout_math_round_pixel(node->computed_width, 1.0f);
    node->computed_height = nx_layout_math_round_pixel(node->computed_height, 1.0f);
}
