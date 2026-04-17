/*
 * NeolyxOS - NXToolkit Layout Engine
 * Mathematical dimension constraints solver
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_LAYOUT_MATH_H
#define NX_LAYOUT_MATH_H

#include "layout/nx_layout_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Utility macros */
#define NX_FLOAT_NAN     0.0f/0.0f
#define NX_IS_NAN(v)     ((v) != (v))
#define NX_FLOAT_MAX     3.402823466e+38F

/* Collapse margins mathematically across boundary layers */
float nx_layout_math_collapse_margins(float m1, float m2);

/* Dimension constraint solving logic */
float nx_layout_math_clamp(float val, float min_val, float max_val);

/* Apply precise screen sub-pixel rounding */
float nx_layout_math_round_pixel(float val, float display_scale);

#ifdef __cplusplus
}
#endif
#endif
