/*
 * NX3D - NeolyxOS Basic 3D Rendering
 * 
 * Software 3D pipeline with vector math, transformation, and rasterization.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_NX3D_H
#define NEOLYX_NX3D_H

#include <stdint.h>

/* ============ Vector Types ============ */

typedef struct {
    float x, y, z;
} nx3d_vec3_t;

typedef struct {
    float x, y, z, w;
} nx3d_vec4_t;

/* ============ Matrix (Column-Major 4x4) ============ */

typedef struct {
    float m[16];  /* Column-major order */
} nx3d_mat4_t;

/* ============ Vertex ============ */

typedef struct {
    nx3d_vec3_t position;
    nx3d_vec3_t normal;
    float u, v;           /* Texture coords */
    uint32_t color;       /* ARGB */
} nx3d_vertex_t;

/* ============ Triangle ============ */

typedef struct {
    uint32_t v0, v1, v2;  /* Vertex indices */
} nx3d_triangle_t;

/* ============ Mesh ============ */

typedef struct {
    nx3d_vertex_t *vertices;
    uint32_t vertex_count;
    nx3d_triangle_t *triangles;
    uint32_t triangle_count;
} nx3d_mesh_t;

/* ============ Render Context ============ */

typedef struct {
    uint32_t *color_buffer;
    float *depth_buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    
    nx3d_mat4_t model;
    nx3d_mat4_t view;
    nx3d_mat4_t projection;
    nx3d_mat4_t mvp;  /* Pre-multiplied */
} nx3d_context_t;

/* ============ Initialization ============ */

int nx3d_init(nx3d_context_t *ctx, uint32_t *color_buf, float *depth_buf,
              uint32_t width, uint32_t height, uint32_t pitch);

void nx3d_shutdown(nx3d_context_t *ctx);

/* ============ Clear ============ */

void nx3d_clear(nx3d_context_t *ctx, uint32_t color, float depth);
void nx3d_clear_depth(nx3d_context_t *ctx, float depth);

/* ============ Matrix Operations ============ */

void nx3d_mat4_identity(nx3d_mat4_t *m);
void nx3d_mat4_multiply(nx3d_mat4_t *out, const nx3d_mat4_t *a, const nx3d_mat4_t *b);

/* Projection */
void nx3d_perspective(nx3d_mat4_t *m, float fov, float aspect, float near, float far);

/* View */
void nx3d_lookat(nx3d_mat4_t *m, nx3d_vec3_t eye, nx3d_vec3_t center, nx3d_vec3_t up);

/* Transform */
void nx3d_translate(nx3d_mat4_t *m, float x, float y, float z);
void nx3d_rotate_y(nx3d_mat4_t *m, float angle);
void nx3d_scale(nx3d_mat4_t *m, float sx, float sy, float sz);

/* ============ Vector Operations ============ */

nx3d_vec3_t nx3d_vec3_add(nx3d_vec3_t a, nx3d_vec3_t b);
nx3d_vec3_t nx3d_vec3_sub(nx3d_vec3_t a, nx3d_vec3_t b);
nx3d_vec3_t nx3d_vec3_mul(nx3d_vec3_t v, float s);
float nx3d_vec3_dot(nx3d_vec3_t a, nx3d_vec3_t b);
nx3d_vec3_t nx3d_vec3_cross(nx3d_vec3_t a, nx3d_vec3_t b);
nx3d_vec3_t nx3d_vec3_normalize(nx3d_vec3_t v);

/* ============ Transform ============ */

nx3d_vec4_t nx3d_transform(const nx3d_mat4_t *m, nx3d_vec4_t v);
void nx3d_update_mvp(nx3d_context_t *ctx);

/* ============ Rendering ============ */

void nx3d_draw_triangle(nx3d_context_t *ctx, 
                        const nx3d_vertex_t *v0,
                        const nx3d_vertex_t *v1,
                        const nx3d_vertex_t *v2);

void nx3d_draw_mesh(nx3d_context_t *ctx, const nx3d_mesh_t *mesh);

/* ============ Primitive Meshes ============ */

nx3d_mesh_t* nx3d_create_cube(float size);
void nx3d_destroy_mesh(nx3d_mesh_t *mesh);

#endif /* NEOLYX_NX3D_H */
