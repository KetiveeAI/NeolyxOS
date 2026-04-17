/*
 * NeolyxOS - NXToolkit Layout Engine
 * Mathematical dimension constraints solver Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "layout/nx_layout_math.h"

/* CSS Margin collapse: 
 * If both margins are positive, use the higher.
 * If both are negative, use the lowest (most negative).
 * If mixed, add them together.
 */
float nx_layout_math_collapse_margins(float m1, float m2) {
    if (m1 >= 0.0f && m2 >= 0.0f) {
        return m1 > m2 ? m1 : m2;
    } else if (m1 < 0.0f && m2 < 0.0f) {
        return m1 < m2 ? m1 : m2;
    }
    return m1 + m2;
}

float nx_layout_math_clamp(float val, float min_val, float max_val) {
    if (!NX_IS_NAN(min_val) && val < min_val) return min_val;
    if (!NX_IS_NAN(max_val) && val > max_val) return max_val;
    return val;
}

float nx_layout_math_round_pixel(float val, float display_scale) {
    /* Precise logic mapping sub-pixel float logic (e.g. 5.5px) down to absolute 1.0f or 2.0f Retina coordinates */
    if (NX_IS_NAN(val)) return val;
    float scaled = val * display_scale;
    float frac = scaled - (long)scaled;
    
    /* Nearest-neighbor fraction boundary approximation */
    if (frac >= 0.5f) {
        scaled = (float)((long)scaled + 1);
    } else if (frac <= -0.5f) {
        scaled = (float)((long)scaled - 1);
    } else {
        scaled = (float)((long)scaled);
    }
    
    return scaled / display_scale;
}
