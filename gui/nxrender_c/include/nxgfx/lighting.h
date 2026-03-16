/*
 * NeolyxOS - NXGFX 3D Lighting System
 * Physically-based lighting with reflection and environment light
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * LIGHTING MODEL:
 * ===============
 * Uses Blinn-Phong shading for realistic light behavior:
 * 
 * Final Color = Ambient + Diffuse + Specular
 * 
 * Where:
 *   Ambient  = ambient_strength * light_color * object_color
 *   Diffuse  = max(dot(normal, light_dir), 0) * light_color * object_color
 *   Specular = pow(max(dot(normal, half_vec), 0), shininess) * specular_strength * light_color
 *
 * REFLECTION:
 * ===========
 * R = 2 * dot(N, L) * N - L   (mirror reflection direction)
 *
 * For a single light in dark room:
 *   - Light falloff: 1.0 / (constant + linear*d + quadratic*d*d)
 *   - Shadows: ray-surface intersection test
 *   - Ambient occlusion: nearby geometry blocks indirect light
 */

#ifndef NXGFX_LIGHTING_H
#define NXGFX_LIGHTING_H

#include "nxgfx/nxgfx.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Vector Math (3D)
 * ============================================================================ */
typedef struct {
    float x, y, z;
} nx_vec3_t;

typedef struct {
    float x, y, z, w;
} nx_vec4_t;

/* 4x4 transformation matrix (column-major for GPU) */
typedef struct {
    float m[16];
} nx_mat4_t;

/* Vector operations */
nx_vec3_t nx_vec3(float x, float y, float z);
nx_vec3_t nx_vec3_add(nx_vec3_t a, nx_vec3_t b);
nx_vec3_t nx_vec3_sub(nx_vec3_t a, nx_vec3_t b);
nx_vec3_t nx_vec3_mul(nx_vec3_t v, float s);
nx_vec3_t nx_vec3_normalize(nx_vec3_t v);
float nx_vec3_dot(nx_vec3_t a, nx_vec3_t b);
float nx_vec3_length(nx_vec3_t v);
nx_vec3_t nx_vec3_reflect(nx_vec3_t incident, nx_vec3_t normal);
nx_vec3_t nx_vec3_cross(nx_vec3_t a, nx_vec3_t b);

/* Matrix operations */
nx_mat4_t nx_mat4_identity(void);
nx_mat4_t nx_mat4_translate(float x, float y, float z);
nx_mat4_t nx_mat4_scale(float x, float y, float z);
nx_mat4_t nx_mat4_rotate_x(float radians);
nx_mat4_t nx_mat4_rotate_y(float radians);
nx_mat4_t nx_mat4_rotate_z(float radians);
nx_mat4_t nx_mat4_perspective(float fov_y, float aspect, float near, float far);
nx_mat4_t nx_mat4_ortho(float left, float right, float bottom, float top, float near, float far);
nx_mat4_t nx_mat4_lookat(nx_vec3_t eye, nx_vec3_t center, nx_vec3_t up);
nx_mat4_t nx_mat4_multiply(nx_mat4_t a, nx_mat4_t b);
nx_vec4_t nx_mat4_transform_vec4(nx_mat4_t m, nx_vec4_t v);
nx_vec3_t nx_mat4_transform_point(nx_mat4_t m, nx_vec3_t p);
nx_vec3_t nx_mat4_transform_dir(nx_mat4_t m, nx_vec3_t d);

/* ============================================================================
 * Light Types
 * ============================================================================ */
typedef enum {
    NX_LIGHT_POINT,         /* Omni-directional, position-based */
    NX_LIGHT_DIRECTIONAL,   /* Infinite distance, direction-based (sun) */
    NX_LIGHT_SPOT,          /* Cone-shaped, position + direction */
    NX_LIGHT_AMBIENT        /* Uniform environment light */
} nx_light_type_t;

typedef struct {
    nx_light_type_t type;
    nx_vec3_t position;     /* For point/spot lights */
    nx_vec3_t direction;    /* For directional/spot lights */
    nx_vec3_t color;        /* RGB intensity (0-1 each, can exceed 1 for HDR) */
    float intensity;        /* Light strength multiplier */
    
    /* Attenuation (falloff) for point/spot lights */
    float constant;         /* Usually 1.0 */
    float linear;           /* Distance-based falloff */
    float quadratic;        /* Realistic inverse-square falloff */
    
    /* Spot light cone */
    float inner_cone;       /* Inner cone angle (radians) */
    float outer_cone;       /* Outer cone angle (radians) */
    
    bool cast_shadows;
    bool enabled;
} nx_light_t;

/* ============================================================================
 * Material Properties
 * ============================================================================ */
typedef struct {
    nx_vec3_t albedo;       /* Base color (diffuse) */
    float metallic;         /* 0 = dielectric, 1 = metal */
    float roughness;        /* 0 = mirror, 1 = rough */
    float ambient_occlusion; /* 0 = occluded, 1 = exposed */
    nx_vec3_t emissive;     /* Self-illumination */
    float opacity;          /* Transparency */
    float ior;              /* Index of refraction (glass ~1.5) */
} nx_material_t;

/* ============================================================================
 * Surface (for lighting calculation)
 * ============================================================================ */
typedef struct {
    nx_vec3_t position;     /* World position */
    nx_vec3_t normal;       /* Surface normal (normalized) */
    nx_vec3_t view_dir;     /* Direction to camera (normalized) */
    nx_material_t material;
} nx_lighting_surface_t;

/* ============================================================================
 * Lighting Scene
 * ============================================================================ */
#define NX_MAX_LIGHTS 16

typedef struct {
    nx_light_t lights[NX_MAX_LIGHTS];
    uint32_t light_count;
    nx_vec3_t ambient_color;    /* Global ambient */
    float ambient_intensity;
    nx_vec3_t camera_position;
} nx_lighting_scene_t;

/* ============================================================================
 * GPU Interface (for hardware acceleration)
 * ============================================================================ */
typedef struct {
    /* GPU buffer handles */
    uint32_t light_buffer_id;
    uint32_t material_buffer_id;
    uint32_t shadow_map_id;
    
    /* GPU state */
    bool gpu_available;
    void* gpu_context;      /* Platform-specific GPU context */
    
    /* Upload functions */
    void (*upload_lights)(void* ctx, const nx_light_t* lights, uint32_t count);
    void (*upload_material)(void* ctx, const nx_material_t* material);
    void (*render_shadows)(void* ctx, const nx_light_t* light);
} nx_gpu_interface_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/* Scene management */
nx_lighting_scene_t* nx_lighting_create(void);
void nx_lighting_destroy(nx_lighting_scene_t* scene);

/* Light management */
int nx_lighting_add_light(nx_lighting_scene_t* scene, nx_light_t light);
void nx_lighting_remove_light(nx_lighting_scene_t* scene, int index);
void nx_lighting_set_ambient(nx_lighting_scene_t* scene, nx_vec3_t color, float intensity);

/* Create light presets */
nx_light_t nx_light_point(nx_vec3_t pos, nx_vec3_t color, float intensity);
nx_light_t nx_light_directional(nx_vec3_t dir, nx_vec3_t color, float intensity);
nx_light_t nx_light_spot(nx_vec3_t pos, nx_vec3_t dir, float inner, float outer, nx_vec3_t color, float intensity);

/* Material presets */
nx_material_t nx_material_default(void);
nx_material_t nx_material_metal(nx_vec3_t color, float roughness);
nx_material_t nx_material_plastic(nx_vec3_t color, float roughness);
nx_material_t nx_material_glass(float ior);

/* ============================================================================
 * LIGHTING CALCULATION (CPU software rendering)
 * ============================================================================ */

/* Calculate final color for a surface point */
nx_vec3_t nx_lighting_calculate(
    const nx_lighting_scene_t* scene,
    const nx_lighting_surface_t* surface
);

/* Calculate light contribution from single light */
nx_vec3_t nx_light_contribution(
    const nx_light_t* light,
    const nx_lighting_surface_t* surface
);

/* Calculate reflection direction */
nx_vec3_t nx_calculate_reflection(nx_vec3_t incident, nx_vec3_t normal);

/* Calculate refraction direction (Snell's law) */
nx_vec3_t nx_calculate_refraction(nx_vec3_t incident, nx_vec3_t normal, float ior);

/* Calculate Fresnel effect (reflection vs refraction ratio) */
float nx_fresnel_schlick(float cos_theta, float ior);

/* Light attenuation at distance */
float nx_light_attenuation(const nx_light_t* light, float distance);

/* ============================================================================
 * GPU Communication
 * ============================================================================ */

/* Initialize GPU interface */
nx_gpu_interface_t* nx_gpu_init(void* platform_context);
void nx_gpu_destroy(nx_gpu_interface_t* gpu);

/* Sync lighting data to GPU */
void nx_gpu_upload_scene(nx_gpu_interface_t* gpu, const nx_lighting_scene_t* scene);

/* Render with GPU acceleration */
void nx_gpu_render_lit(nx_gpu_interface_t* gpu, const nx_lighting_scene_t* scene);

#ifdef __cplusplus
}
#endif
#endif
