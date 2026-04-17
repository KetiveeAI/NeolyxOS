/*
 * NeolyxOS - NXToolkit Layout Engine
 * Advanced Node-based Grid Engine
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_LAYOUT_GRID_H
#define NX_LAYOUT_GRID_H

#include "layout/nx_layout_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t cols;
    uint32_t rows;
    float col_gap;
    float row_gap;
} nx_grid_config_t;

/* Sets grid engine parameters directly on the layout node */
void nx_layout_grid_set_config(nx_layout_node_t* node, nx_grid_config_t config);

/* Resolves explicit spatial grid constraints mimicking CSS-Grid */
void nx_layout_grid_calculate(nx_layout_node_t* node, float available_width, float available_height);

#ifdef __cplusplus
}
#endif
#endif
