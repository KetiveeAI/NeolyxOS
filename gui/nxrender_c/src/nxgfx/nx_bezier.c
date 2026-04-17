/*
 * NeolyxOS - NXGFX Bezier Engine
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgfx_path.h"

/* Helper: absolute value for float */
static inline float nx_fabs(float a) {
    return (a < 0.0f) ? -a : a;
}

/* Math routines are now provided by nxgfx_path.h / nx_math.c */
static void subdivide_cubic(nx_vec2_t p0, nx_vec2_t p1, nx_vec2_t p2, nx_vec2_t p3,
                            float tol_squared, nx_vec2_t* out_points, uint32_t* count, uint32_t capacity) {
    if (*count >= capacity) return;
    
    /* Calculate flatness: max distance of p1/p2 from line p0-p3 */
    float dx = p3.x - p0.x;
    float dy = p3.y - p0.y;
    
    /* Line equation coefficients a*x + b*y + c = 0 */
    float len_sq = dx * dx + dy * dy;
    float dist1_sq = 0.0f, dist2_sq = 0.0f;
    
    if (len_sq > 0.0001f) {
        /* Cross product relative to line segment */
        float cross1 = dy * (p1.x - p0.x) - dx * (p1.y - p0.y);
        float cross2 = dy * (p2.x - p0.x) - dx * (p2.y - p0.y);
        dist1_sq = (cross1 * cross1) / len_sq;
        dist2_sq = (cross2 * cross2) / len_sq;
    } else {
        /* p0 roughly equals p3 */
        dist1_sq = nx_dist_sq(p0, p1);
        dist2_sq = nx_dist_sq(p0, p2);
    }
    
    /* Check if flat enough */
    if (dist1_sq <= tol_squared && dist2_sq <= tol_squared) {
        /* Curve is flat, emit endpoint p3 */
        if (*count < capacity) {
            out_points[*count] = p3;
            (*count)++;
        }
        return;
    }
    
    /* Subdivide using De Casteljau */
    nx_vec2_t p01 = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    nx_vec2_t p12 = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    nx_vec2_t p23 = { (p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f };
    
    nx_vec2_t p012 = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };
    nx_vec2_t p123 = { (p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f };
    
    nx_vec2_t p0123 = { (p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f };
    
    /* Left half */
    subdivide_cubic(p0, p01, p012, p0123, tol_squared, out_points, count, capacity);
    
    /* Right half */
    subdivide_cubic(p0123, p123, p23, p3, tol_squared, out_points, count, capacity);
}

void nx_bezier_flatten_cubic(nx_vec2_t p0, nx_vec2_t p1, nx_vec2_t p2, nx_vec2_t p3,
                             float tolerance, nx_vec2_t* out_points, uint32_t* count, uint32_t capacity) {
    if (!out_points || !count || *count >= capacity) return;
    
    float tol_sq = tolerance * tolerance;
    /* Always add starting point if empty */
    if (*count == 0) {
        out_points[*count] = p0;
        (*count)++;
    }
    
    subdivide_cubic(p0, p1, p2, p3, tol_sq, out_points, count, capacity);
}

/* Helper: Recursive De Casteljau subdivision for quadratic curves */
static void subdivide_quad(nx_vec2_t p0, nx_vec2_t p1, nx_vec2_t p2, 
                           float tol_squared, nx_vec2_t* out_points, uint32_t* count, uint32_t capacity) {
    if (*count >= capacity) return;
    
    float dx = p2.x - p0.x;
    float dy = p2.y - p0.y;
    float len_sq = dx * dx + dy * dy;
    float dist_sq = 0.0f;
    
    if (len_sq > 0.0001f) {
        float cross = dy * (p1.x - p0.x) - dx * (p1.y - p0.y);
        dist_sq = (cross * cross) / len_sq;
    } else {
        dist_sq = nx_dist_sq(p0, p1);
    }
    
    if (dist_sq <= tol_squared) {
        if (*count < capacity) {
            out_points[*count] = p2;
            (*count)++;
        }
        return;
    }
    
    nx_vec2_t p01 = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    nx_vec2_t p12 = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    nx_vec2_t p012 = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };
    
    subdivide_quad(p0, p01, p012, tol_squared, out_points, count, capacity);
    subdivide_quad(p012, p12, p2, tol_squared, out_points, count, capacity);
}

void nx_bezier_flatten_quad(nx_vec2_t p0, nx_vec2_t p1, nx_vec2_t p2, 
                            float tolerance, nx_vec2_t* out_points, uint32_t* count, uint32_t capacity) {
    if (!out_points || !count || *count >= capacity) return;
    
    float tol_sq = tolerance * tolerance;
    if (*count == 0) {
        out_points[*count] = p0;
        (*count)++;
    }
    
    subdivide_quad(p0, p1, p2, tol_sq, out_points, count, capacity);
}
