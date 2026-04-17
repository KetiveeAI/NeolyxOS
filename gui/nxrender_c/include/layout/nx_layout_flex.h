/*
 * NeolyxOS - NXToolkit Layout Engine
 * Node-based CSS Flexbox Resolver
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_LAYOUT_FLEX_H
#define NX_LAYOUT_FLEX_H

#include "layout/nx_layout_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Re-using layout enums from our previous layout.h structure */
#include "layout/layout.h"

typedef struct {
    nx_flex_direction_t direction;
    nx_justify_t justify;
    nx_align_t align_items;
    nx_align_t align_content; /* Used when flex-wrap creates multiple lines */
    float gap;
    bool wrap;
} nx_flex_config_t;

/* Attaches the Flexbox Configuration parameter memory to the shadow node */
void nx_layout_flex_set_config(nx_layout_node_t* node, nx_flex_config_t config);

/* Complex CSS-compliant flex algorithm measuring all child constraints/wrapping */
void nx_layout_flex_calculate(nx_layout_node_t* root, float available_width, float available_height);

#ifdef __cplusplus
}
#endif
#endif
