/*
 * NeolyxOS - NXToolkit Layout Engine
 * Absolute fixed constraint nodes
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_LAYOUT_ABSOLUTE_H
#define NX_LAYOUT_ABSOLUTE_H

#include "layout/nx_layout_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float top;
    float right;
    float bottom;
    float left;
    int32_t z_index;
} nx_absolute_config_t;

/* Absolute calculation positions a floating node dynamically independent to the root bounds */
void nx_layout_absolute_calculate(nx_layout_node_t* node, nx_absolute_config_t config, float available_width, float available_height);

#ifdef __cplusplus
}
#endif
#endif
