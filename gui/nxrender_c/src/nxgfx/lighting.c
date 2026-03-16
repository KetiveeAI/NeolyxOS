/*
 * NeolyxOS - NXGFX 3D Lighting Implementation
 * Physically-based Blinn-Phong lighting with realistic behavior
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * PHYSICS MODEL:
 * ==============
 * This implements how light ACTUALLY behaves in real life:
 *
 * 1. INVERSE SQUARE LAW
 *    Light intensity falls off with distance squared.
 *    In a dark room with one light source, objects far from
 *    the light receive much less illumination.
 *
 * 2. DIFFUSE REFLECTION (Lambert's Law)
 *    Rough surfaces scatter light equally in all directions.
 *    The brightness depends on the angle between surface and light.
 *
 * 3. SPECULAR REFLECTION (Mirror-like)
 *    Shiny surfaces reflect light in a specific direction.
 *    Creates highlights that move as you change viewpoint.
 *
 * 4. AMBIENT LIGHT
 *    In real rooms, light bounces off walls creating indirect
 *    illumination. We approximate this with ambient term.
 *
 * 5. FRESNEL EFFECT
 *    At grazing angles, surfaces become more reflective.
 *    This is why water looks like a mirror at sunset.
 */

#include "nxgfx/lighting.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Vector Math Implementation
 * ============================================================================ */

nx_vec3_t nx_vec3(float x, float y, float z) {
    return (nx_vec3_t){x, y, z};
}

nx_vec3_t nx_vec3_add(nx_vec3_t a, nx_vec3_t b) {
    return (nx_vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

nx_vec3_t nx_vec3_sub(nx_vec3_t a, nx_vec3_t b) {
    return (nx_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

nx_vec3_t nx_vec3_mul(nx_vec3_t v, float s) {
    return (nx_vec3_t){v.x * s, v.y * s, v.z * s};
}

float nx_vec3_dot(nx_vec3_t a, nx_vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float nx_vec3_length(nx_vec3_t v) {
    return sqrtf(nx_vec3_dot(v, v));
}

nx_vec3_t nx_vec3_normalize(nx_vec3_t v) {
    float len = nx_vec3_length(v);
    if (len < 0.0001f) return (nx_vec3_t){0, 0, 0};
    return nx_vec3_mul(v, 1.0f / len);
}

nx_vec3_t nx_vec3_cross(nx_vec3_t a, nx_vec3_t b) {
    return (nx_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

/*
 * REFLECTION CALCULATION
 * ======================
 * When light hits a surface:
 * - Incident ray I hits surface at point P with normal N
 * - Reflected ray R = I - 2 * dot(I, N) * N
 *
 * This is exactly how mirrors work in real life.
 */
nx_vec3_t nx_vec3_reflect(nx_vec3_t incident, nx_vec3_t normal) {
    float d = nx_vec3_dot(incident, normal);
    return nx_vec3_sub(incident, nx_vec3_mul(normal, 2.0f * d));
}

/* ============================================================================
 * Light Creation
 * ============================================================================ */

nx_light_t nx_light_point(nx_vec3_t pos, nx_vec3_t color, float intensity) {
    return (nx_light_t){
        .type = NX_LIGHT_POINT,
        .position = pos,
        .direction = {0, -1, 0},
        .color = color,
        .intensity = intensity,
        .constant = 1.0f,
        .linear = 0.09f,       /* Realistic falloff values */
        .quadratic = 0.032f,
        .inner_cone = 0,
        .outer_cone = 0,
        .cast_shadows = true,
        .enabled = true
    };
}

nx_light_t nx_light_directional(nx_vec3_t dir, nx_vec3_t color, float intensity) {
    return (nx_light_t){
        .type = NX_LIGHT_DIRECTIONAL,
        .position = {0, 0, 0},
        .direction = nx_vec3_normalize(dir),
        .color = color,
        .intensity = intensity,
        .constant = 1.0f,
        .linear = 0,
        .quadratic = 0,
        .inner_cone = 0,
        .outer_cone = 0,
        .cast_shadows = true,
        .enabled = true
    };
}

nx_light_t nx_light_spot(nx_vec3_t pos, nx_vec3_t dir, float inner, float outer, nx_vec3_t color, float intensity) {
    return (nx_light_t){
        .type = NX_LIGHT_SPOT,
        .position = pos,
        .direction = nx_vec3_normalize(dir),
        .color = color,
        .intensity = intensity,
        .constant = 1.0f,
        .linear = 0.09f,
        .quadratic = 0.032f,
        .inner_cone = inner,
        .outer_cone = outer,
        .cast_shadows = true,
        .enabled = true
    };
}

/* ============================================================================
 * Material Creation
 * ============================================================================ */

nx_material_t nx_material_default(void) {
    return (nx_material_t){
        .albedo = {0.8f, 0.8f, 0.8f},
        .metallic = 0.0f,
        .roughness = 0.5f,
        .ambient_occlusion = 1.0f,
        .emissive = {0, 0, 0},
        .opacity = 1.0f,
        .ior = 1.5f
    };
}

nx_material_t nx_material_metal(nx_vec3_t color, float roughness) {
    return (nx_material_t){
        .albedo = color,
        .metallic = 1.0f,
        .roughness = roughness,
        .ambient_occlusion = 1.0f,
        .emissive = {0, 0, 0},
        .opacity = 1.0f,
        .ior = 2.5f  /* Metals have high IOR */
    };
}

nx_material_t nx_material_plastic(nx_vec3_t color, float roughness) {
    return (nx_material_t){
        .albedo = color,
        .metallic = 0.0f,
        .roughness = roughness,
        .ambient_occlusion = 1.0f,
        .emissive = {0, 0, 0},
        .opacity = 1.0f,
        .ior = 1.5f
    };
}

nx_material_t nx_material_glass(float ior) {
    return (nx_material_t){
        .albedo = {1, 1, 1},
        .metallic = 0.0f,
        .roughness = 0.0f,
        .ambient_occlusion = 1.0f,
        .emissive = {0, 0, 0},
        .opacity = 0.1f,
        .ior = ior  /* Glass typically 1.45-1.55 */
    };
}

/* ============================================================================
 * Scene Management
 * ============================================================================ */

nx_lighting_scene_t* nx_lighting_create(void) {
    nx_lighting_scene_t* scene = calloc(1, sizeof(nx_lighting_scene_t));
    if (!scene) return NULL;
    scene->ambient_color = nx_vec3(0.1f, 0.1f, 0.12f);  /* Slight blue ambient */
    scene->ambient_intensity = 0.1f;
    scene->camera_position = nx_vec3(0, 0, 5);
    return scene;
}

void nx_lighting_destroy(nx_lighting_scene_t* scene) {
    free(scene);
}

int nx_lighting_add_light(nx_lighting_scene_t* scene, nx_light_t light) {
    if (!scene || scene->light_count >= NX_MAX_LIGHTS) return -1;
    scene->lights[scene->light_count] = light;
    return (int)scene->light_count++;
}

void nx_lighting_remove_light(nx_lighting_scene_t* scene, int index) {
    if (!scene || index < 0 || (uint32_t)index >= scene->light_count) return;
    for (uint32_t i = (uint32_t)index; i < scene->light_count - 1; i++) {
        scene->lights[i] = scene->lights[i + 1];
    }
    scene->light_count--;
}

void nx_lighting_set_ambient(nx_lighting_scene_t* scene, nx_vec3_t color, float intensity) {
    if (!scene) return;
    scene->ambient_color = color;
    scene->ambient_intensity = intensity;
}

/* ============================================================================
 * PHYSICS: Light Attenuation
 * ==========================
 * In real life, light intensity decreases with distance squared.
 * This is the INVERSE SQUARE LAW.
 * 
 * A light that is twice as far away appears 4x dimmer.
 * A light that is 3x as far away appears 9x dimmer.
 *
 * We use: attenuation = 1.0 / (constant + linear*d + quadratic*d²)
 * ============================================================================ */

float nx_light_attenuation(const nx_light_t* light, float distance) {
    if (light->type == NX_LIGHT_DIRECTIONAL) return 1.0f;  /* Sun has no attenuation */
    
    return 1.0f / (light->constant + 
                   light->linear * distance + 
                   light->quadratic * distance * distance);
}

/* ============================================================================
 * PHYSICS: Fresnel Effect (Schlick Approximation)
 * ================================================
 * Real surfaces become more reflective at grazing angles.
 * 
 * Look at a table from above: you see the table.
 * Look at a table from the side: you see reflections.
 * 
 * F = F0 + (1 - F0) * (1 - cos_theta)^5
 * 
 * Where F0 = ((n1-n2)/(n1+n2))^2 for dielectrics
 * ============================================================================ */

float nx_fresnel_schlick(float cos_theta, float ior) {
    float f0 = (1.0f - ior) / (1.0f + ior);
    f0 = f0 * f0;
    return f0 + (1.0f - f0) * powf(1.0f - cos_theta, 5.0f);
}

/* ============================================================================
 * PHYSICS: Refraction (Snell's Law)
 * ==================================
 * When light enters a medium (like glass), it bends.
 * 
 * n1 * sin(θ1) = n2 * sin(θ2)
 * 
 * This is why objects look bent when submerged in water.
 * ============================================================================ */

nx_vec3_t nx_calculate_refraction(nx_vec3_t incident, nx_vec3_t normal, float ior) {
    float cos_i = -nx_vec3_dot(incident, normal);
    float sin_t2 = ior * ior * (1.0f - cos_i * cos_i);
    
    if (sin_t2 > 1.0f) {
        /* Total internal reflection - no refraction */
        return nx_vec3_reflect(incident, normal);
    }
    
    float cos_t = sqrtf(1.0f - sin_t2);
    return nx_vec3_add(
        nx_vec3_mul(incident, ior),
        nx_vec3_mul(normal, ior * cos_i - cos_t)
    );
}

nx_vec3_t nx_calculate_reflection(nx_vec3_t incident, nx_vec3_t normal) {
    return nx_vec3_reflect(incident, normal);
}

/* ============================================================================
 * LIGHTING CALCULATION: Single Light Contribution
 * ================================================
 * This is the heart of the lighting system.
 * For each light, we calculate:
 * 
 * 1. Light direction and distance
 * 2. Diffuse contribution (Lambert)
 * 3. Specular contribution (Blinn-Phong)
 * 4. Apply attenuation
 * ============================================================================ */

nx_vec3_t nx_light_contribution(const nx_light_t* light, const nx_lighting_surface_t* surface) {
    if (!light->enabled) return nx_vec3(0, 0, 0);
    
    nx_vec3_t light_dir;
    float attenuation = 1.0f;
    
    /* Calculate light direction based on light type */
    if (light->type == NX_LIGHT_DIRECTIONAL) {
        light_dir = nx_vec3_mul(light->direction, -1.0f);  /* Point toward light */
    } else {
        /* Point or spot light */
        nx_vec3_t to_light = nx_vec3_sub(light->position, surface->position);
        float distance = nx_vec3_length(to_light);
        light_dir = nx_vec3_mul(to_light, 1.0f / distance);
        attenuation = nx_light_attenuation(light, distance);
        
        /* Spot light cone */
        if (light->type == NX_LIGHT_SPOT) {
            float theta = nx_vec3_dot(light_dir, nx_vec3_mul(light->direction, -1.0f));
            float epsilon = light->inner_cone - light->outer_cone;
            float spot_intensity = (theta - light->outer_cone) / epsilon;
            spot_intensity = fmaxf(0.0f, fminf(1.0f, spot_intensity));
            attenuation *= spot_intensity;
        }
    }
    
    /* DIFFUSE: Lambert's cosine law
     * Surfaces facing the light are bright, angled surfaces are dim */
    float n_dot_l = fmaxf(nx_vec3_dot(surface->normal, light_dir), 0.0f);
    nx_vec3_t diffuse = nx_vec3_mul(surface->material.albedo, n_dot_l);
    
    /* SPECULAR: Blinn-Phong
     * Half-vector between light and view direction */
    nx_vec3_t half_vec = nx_vec3_normalize(nx_vec3_add(light_dir, surface->view_dir));
    float n_dot_h = fmaxf(nx_vec3_dot(surface->normal, half_vec), 0.0f);
    
    /* Shininess based on roughness (rougher = wider highlight) */
    float shininess = 2.0f / (surface->material.roughness * surface->material.roughness + 0.0001f);
    float spec_strength = powf(n_dot_h, shininess);
    
    /* Metal uses albedo color for specular, dielectric uses white */
    nx_vec3_t spec_color = surface->material.metallic > 0.5f ? 
        surface->material.albedo : nx_vec3(1, 1, 1);
    nx_vec3_t specular = nx_vec3_mul(spec_color, spec_strength * (1.0f - surface->material.roughness));
    
    /* Combine and apply light color and intensity */
    nx_vec3_t result = nx_vec3_add(diffuse, specular);
    result.x *= light->color.x * light->intensity * attenuation;
    result.y *= light->color.y * light->intensity * attenuation;
    result.z *= light->color.z * light->intensity * attenuation;
    
    return result;
}

/* ============================================================================
 * FULL LIGHTING CALCULATION
 * =========================
 * This simulates how light behaves in a real room:
 *
 * 1. Start with ambient (light bouncing off walls, floor, ceiling)
 * 2. Add contribution from each light source
 * 3. Apply ambient occlusion (corners are darker)
 * 4. Add emissive (self-lit materials like screens)
 * ============================================================================ */

nx_vec3_t nx_lighting_calculate(
    const nx_lighting_scene_t* scene,
    const nx_lighting_surface_t* surface
) {
    if (!scene || !surface) return nx_vec3(0, 0, 0);
    
    /* Ambient: Simulates light bouncing around the room
     * In a dark room with one light, ambient is very low */
    nx_vec3_t result = nx_vec3(
        scene->ambient_color.x * scene->ambient_intensity * surface->material.albedo.x,
        scene->ambient_color.y * scene->ambient_intensity * surface->material.albedo.y,
        scene->ambient_color.z * scene->ambient_intensity * surface->material.albedo.z
    );
    
    /* Apply ambient occlusion (corners and crevices receive less indirect light) */
    result = nx_vec3_mul(result, surface->material.ambient_occlusion);
    
    /* Add contribution from each light */
    for (uint32_t i = 0; i < scene->light_count; i++) {
        nx_vec3_t contribution = nx_light_contribution(&scene->lights[i], surface);
        result = nx_vec3_add(result, contribution);
    }
    
    /* Add emissive (objects that produce their own light) */
    result = nx_vec3_add(result, surface->material.emissive);
    
    /* Clamp to valid range (for non-HDR output) */
    result.x = fminf(1.0f, fmaxf(0.0f, result.x));
    result.y = fminf(1.0f, fmaxf(0.0f, result.y));
    result.z = fminf(1.0f, fmaxf(0.0f, result.z));
    
    return result;
}

/* ============================================================================
 * GPU Interface (Stub for future kernel GPU driver integration)
 * ============================================================================ */

nx_gpu_interface_t* nx_gpu_init(void* platform_context) {
    nx_gpu_interface_t* gpu = calloc(1, sizeof(nx_gpu_interface_t));
    if (!gpu) return NULL;
    gpu->gpu_context = platform_context;
    gpu->gpu_available = (platform_context != NULL);
    return gpu;
}

void nx_gpu_destroy(nx_gpu_interface_t* gpu) {
    free(gpu);
}

void nx_gpu_upload_scene(nx_gpu_interface_t* gpu, const nx_lighting_scene_t* scene) {
    if (!gpu || !gpu->gpu_available || !scene) return;
    
    /* Upload lights to GPU buffer */
    if (gpu->upload_lights) {
        gpu->upload_lights(gpu->gpu_context, scene->lights, scene->light_count);
    }
}

void nx_gpu_render_lit(nx_gpu_interface_t* gpu, const nx_lighting_scene_t* scene) {
    (void)gpu; (void)scene;
    /* GPU rendering path - implemented by kernel graphics driver */
}

/* ============================================================================
 * Matrix Operations (Column-Major Order for GPU compatibility)
 * ============================================================================ */

nx_mat4_t nx_mat4_identity(void) {
    nx_mat4_t m = {0};
    m.m[0] = 1.0f;  m.m[5] = 1.0f;  m.m[10] = 1.0f;  m.m[15] = 1.0f;
    return m;
}

nx_mat4_t nx_mat4_translate(float x, float y, float z) {
    nx_mat4_t m = nx_mat4_identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

nx_mat4_t nx_mat4_scale(float x, float y, float z) {
    nx_mat4_t m = {0};
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    m.m[15] = 1.0f;
    return m;
}

nx_mat4_t nx_mat4_rotate_x(float radians) {
    nx_mat4_t m = nx_mat4_identity();
    float c = cosf(radians);
    float s = sinf(radians);
    m.m[5] = c;   m.m[6] = s;
    m.m[9] = -s;  m.m[10] = c;
    return m;
}

nx_mat4_t nx_mat4_rotate_y(float radians) {
    nx_mat4_t m = nx_mat4_identity();
    float c = cosf(radians);
    float s = sinf(radians);
    m.m[0] = c;   m.m[2] = -s;
    m.m[8] = s;   m.m[10] = c;
    return m;
}

nx_mat4_t nx_mat4_rotate_z(float radians) {
    nx_mat4_t m = nx_mat4_identity();
    float c = cosf(radians);
    float s = sinf(radians);
    m.m[0] = c;   m.m[1] = s;
    m.m[4] = -s;  m.m[5] = c;
    return m;
}

nx_mat4_t nx_mat4_perspective(float fov_y, float aspect, float near, float far) {
    nx_mat4_t m = {0};
    float tan_half = tanf(fov_y * 0.5f);
    m.m[0] = 1.0f / (aspect * tan_half);
    m.m[5] = 1.0f / tan_half;
    m.m[10] = -(far + near) / (far - near);
    m.m[11] = -1.0f;
    m.m[14] = -(2.0f * far * near) / (far - near);
    return m;
}

nx_mat4_t nx_mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    nx_mat4_t m = {0};
    m.m[0] = 2.0f / (right - left);
    m.m[5] = 2.0f / (top - bottom);
    m.m[10] = -2.0f / (far - near);
    m.m[12] = -(right + left) / (right - left);
    m.m[13] = -(top + bottom) / (top - bottom);
    m.m[14] = -(far + near) / (far - near);
    m.m[15] = 1.0f;
    return m;
}

nx_mat4_t nx_mat4_lookat(nx_vec3_t eye, nx_vec3_t center, nx_vec3_t up) {
    nx_vec3_t f = nx_vec3_normalize(nx_vec3_sub(center, eye));
    nx_vec3_t s = nx_vec3_normalize(nx_vec3_cross(f, up));
    nx_vec3_t u = nx_vec3_cross(s, f);
    
    nx_mat4_t m = nx_mat4_identity();
    m.m[0] = s.x;   m.m[4] = s.y;   m.m[8]  = s.z;
    m.m[1] = u.x;   m.m[5] = u.y;   m.m[9]  = u.z;
    m.m[2] = -f.x;  m.m[6] = -f.y;  m.m[10] = -f.z;
    m.m[12] = -nx_vec3_dot(s, eye);
    m.m[13] = -nx_vec3_dot(u, eye);
    m.m[14] = nx_vec3_dot(f, eye);
    return m;
}

nx_mat4_t nx_mat4_multiply(nx_mat4_t a, nx_mat4_t b) {
    nx_mat4_t m = {0};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            m.m[col * 4 + row] = 
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return m;
}

nx_vec4_t nx_mat4_transform_vec4(nx_mat4_t m, nx_vec4_t v) {
    return (nx_vec4_t){
        m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z + m.m[12] * v.w,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z + m.m[13] * v.w,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
        m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w
    };
}

nx_vec3_t nx_mat4_transform_point(nx_mat4_t m, nx_vec3_t p) {
    nx_vec4_t v = nx_mat4_transform_vec4(m, (nx_vec4_t){p.x, p.y, p.z, 1.0f});
    if (fabsf(v.w) > 0.0001f) {
        return (nx_vec3_t){v.x / v.w, v.y / v.w, v.z / v.w};
    }
    return (nx_vec3_t){v.x, v.y, v.z};
}

nx_vec3_t nx_mat4_transform_dir(nx_mat4_t m, nx_vec3_t d) {
    return (nx_vec3_t){
        m.m[0] * d.x + m.m[4] * d.y + m.m[8]  * d.z,
        m.m[1] * d.x + m.m[5] * d.y + m.m[9]  * d.z,
        m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z
    };
}
