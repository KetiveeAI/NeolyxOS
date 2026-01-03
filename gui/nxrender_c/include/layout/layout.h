/*
 * NeolyxOS - NXRender Layout Engine
 * Flexbox and stack layout implementations
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_LAYOUT_H
#define NXRENDER_LAYOUT_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flex direction */
typedef enum {
    NX_FLEX_ROW,
    NX_FLEX_ROW_REVERSE,
    NX_FLEX_COLUMN,
    NX_FLEX_COLUMN_REVERSE
} nx_flex_direction_t;

/* Justify content (main axis) */
typedef enum {
    NX_JUSTIFY_START,
    NX_JUSTIFY_END,
    NX_JUSTIFY_CENTER,
    NX_JUSTIFY_SPACE_BETWEEN,
    NX_JUSTIFY_SPACE_AROUND,
    NX_JUSTIFY_SPACE_EVENLY
} nx_justify_t;

/* Align items (cross axis) */
typedef enum {
    NX_ALIGN_START,
    NX_ALIGN_END,
    NX_ALIGN_CENTER,
    NX_ALIGN_STRETCH
} nx_align_t;

/* Layout constraints */
typedef struct {
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
} nx_constraints_t;

/* Flexbox layout */
typedef struct {
    nx_flex_direction_t direction;
    nx_justify_t justify;
    nx_align_t align;
    uint32_t gap;
    bool wrap;
} nx_flex_layout_t;

/* Stack layout (z-axis stacking) */
typedef struct {
    nx_align_t horizontal;
    nx_align_t vertical;
} nx_stack_layout_t;

/* Default layouts */
nx_flex_layout_t nx_flex_row(void);
nx_flex_layout_t nx_flex_column(void);
nx_stack_layout_t nx_stack_center(void);

/* Apply flexbox layout to children */
void nx_layout_flex(nx_widget_t* container, nx_flex_layout_t layout, nx_rect_t bounds);

/* Apply stack layout to children */
void nx_layout_stack(nx_widget_t* container, nx_stack_layout_t layout, nx_rect_t bounds);

/* Constraint helpers */
nx_constraints_t nx_constraints_tight(uint32_t w, uint32_t h);
nx_constraints_t nx_constraints_loose(uint32_t max_w, uint32_t max_h);
nx_constraints_t nx_constraints_expand(void);

#ifdef __cplusplus
}
#endif
#endif
