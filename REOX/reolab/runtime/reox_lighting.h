/*
 * REOX Dynamic Lighting System
 * Real-time lighting effects for NeolyxOS UI
 * 
 * Features:
 * - Point lights with falloff
 * - Ambient lighting
 * - Glow effects
 * - Dynamic shadows
 * - Material system (reflectivity, emission)
 * - Light animation
 */

#ifndef REOX_LIGHTING_H
#define REOX_LIGHTING_H

#include "reox_ui.h"
#include "reox_color_system.h"
#include "reox_animation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Light Types
 * ============================================================================ */

typedef enum rx_light_type {
    RX_LIGHT_POINT,           /* Radial light source */
    RX_LIGHT_DIRECTIONAL,     /* Parallel rays (sun-like) */
    RX_LIGHT_SPOT,            /* Cone-shaped light */
    RX_LIGHT_AREA,            /* Rectangular soft light */
    RX_LIGHT_AMBIENT,         /* Global fill light */
} rx_light_type;

typedef enum rx_light_falloff {
    RX_FALLOFF_NONE,          /* Constant intensity */
    RX_FALLOFF_LINEAR,        /* Linear decrease */
    RX_FALLOFF_QUADRATIC,     /* Realistic falloff */
    RX_FALLOFF_SMOOTH,        /* Smooth hermite falloff */
} rx_light_falloff;

/* ============================================================================
 * Light Structure
 * ============================================================================ */

typedef struct rx_light {
    uint64_t id;
    rx_light_type type;
    bool enabled;
    
    /* Position/Direction */
    rx_point position;        /* World position */
    float z_height;           /* Height above surface (0-100) */
    rx_point direction;       /* For directional/spot lights */
    
    /* Color and Intensity */
    rx_color color;
    float intensity;          /* 0.0 - 10.0 */
    float ambient_strength;   /* Ambient contribution */
    
    /* Range and Falloff */
    float radius;             /* Max influence radius */
    rx_light_falloff falloff;
    float falloff_start;      /* Where falloff begins (0-1) */
    
    /* Spot light specific */
    float inner_angle;        /* Full intensity cone */
    float outer_angle;        /* Falloff cone edge */
    
    /* Area light specific */
    rx_size area_size;
    
    /* Animation */
    bool animated;
    float flicker_amount;
    float flicker_speed;
    float pulse_amount;
    float pulse_speed;
    
    /* Shadow casting */
    bool cast_shadows;
    float shadow_softness;
    rx_color shadow_color;
    
    /* Internal state */
    float _flicker_phase;
    float _pulse_phase;
    
    struct rx_light* next;
} rx_light;

/* ============================================================================
 * Light Creation and Control
 * ============================================================================ */

extern rx_light* rx_light_point(rx_point position, rx_color color, float intensity, float radius);
extern rx_light* rx_light_directional(rx_point direction, rx_color color, float intensity);
extern rx_light* rx_light_spot(rx_point position, rx_point direction, rx_color color, float intensity, float angle);
extern rx_light* rx_light_area(rx_point position, rx_size size, rx_color color, float intensity);
extern rx_light* rx_light_ambient(rx_color color, float intensity);

extern void rx_light_set_position(rx_light* light, rx_point pos);
extern void rx_light_set_color(rx_light* light, rx_color color);
extern void rx_light_set_intensity(rx_light* light, float intensity);
extern void rx_light_set_radius(rx_light* light, float radius);
extern void rx_light_enable_shadows(rx_light* light, bool enable, float softness);
extern void rx_light_set_flicker(rx_light* light, float amount, float speed);
extern void rx_light_set_pulse(rx_light* light, float amount, float speed);
extern void rx_light_destroy(rx_light* light);

/* ============================================================================
 * Glow Effect System
 * ============================================================================ */

typedef struct rx_glow_config {
    rx_color color;
    float radius;             /* Blur radius */
    float intensity;          /* Brightness multiplier */
    float threshold;          /* Brightness threshold for bloom */
    bool inner_glow;          /* Glow inward instead of outward */
    bool additive;            /* Additive or normal blend */
} rx_glow_config;

typedef struct rx_glow {
    rx_view* target;
    rx_glow_config config;
    bool enabled;
    bool animated;
    float pulse_speed;
    float pulse_amount;
    float _phase;
} rx_glow;

extern rx_glow* rx_glow_create(rx_view* view, rx_glow_config config);
extern rx_glow* rx_glow_simple(rx_view* view, rx_color color, float radius);
extern void rx_glow_set_color(rx_glow* glow, rx_color color);
extern void rx_glow_set_radius(rx_glow* glow, float radius);
extern void rx_glow_animate(rx_glow* glow, float pulse_speed, float pulse_amount);
extern void rx_glow_update(rx_glow* glow, float dt);
extern void rx_glow_destroy(rx_glow* glow);

/* Preset glows */
extern rx_glow* rx_glow_neon(rx_view* view, rx_color color);
extern rx_glow* rx_glow_fire(rx_view* view);
extern rx_glow* rx_glow_ice(rx_view* view);
extern rx_glow* rx_glow_magic(rx_view* view, rx_color color);
extern rx_glow* rx_glow_pulse(rx_view* view, rx_color color, float speed);

/* ============================================================================
 * Material System
 * ============================================================================ */

typedef struct rx_material {
    /* Base color (diffuse) */
    rx_color base_color;
    
    /* Surface properties */
    float roughness;          /* 0 = shiny, 1 = matte */
    float metallic;           /* 0 = dielectric, 1 = metal */
    float reflectivity;       /* Environment reflection */
    
    /* Emission */
    rx_color emissive_color;
    float emissive_intensity;
    
    /* Normal mapping strength */
    float bump_strength;
    
    /* Transparency */
    float opacity;
    bool receive_shadows;
    
    /* Fresnel effect */
    bool fresnel_enabled;
    float fresnel_power;
    rx_color fresnel_color;
} rx_material;

extern rx_material rx_material_default(void);
extern rx_material rx_material_matte(rx_color color);
extern rx_material rx_material_metallic(rx_color color);
extern rx_material rx_material_glass(rx_color tint);
extern rx_material rx_material_emissive(rx_color color, float intensity);
extern rx_material rx_material_glossy(rx_color color, float gloss);

extern void rx_view_set_material(rx_view* view, rx_material mat);
extern rx_material* rx_view_get_material(rx_view* view);

/* ============================================================================
 * Lighting Scene
 * ============================================================================ */

typedef struct rx_lighting_scene {
    rx_light* lights;
    size_t light_count;
    
    /* Global ambient */
    rx_color ambient_color;
    float ambient_intensity;
    
    /* Environment */
    rx_color environment_color;
    float environment_intensity;
    
    /* Shadow settings */
    bool shadows_enabled;
    float shadow_intensity;
    rx_color shadow_color;
    
    /* Performance */
    int max_lights_per_pixel;
} rx_lighting_scene;

extern rx_lighting_scene* rx_lighting_scene_create(void);
extern void rx_lighting_scene_add_light(rx_lighting_scene* scene, rx_light* light);
extern void rx_lighting_scene_remove_light(rx_lighting_scene* scene, rx_light* light);
extern void rx_lighting_scene_set_ambient(rx_lighting_scene* scene, rx_color color, float intensity);
extern void rx_lighting_scene_update(rx_lighting_scene* scene, float dt);
extern void rx_lighting_scene_destroy(rx_lighting_scene* scene);

/* Calculate lighting at a point */
extern rx_color rx_lighting_calculate(rx_lighting_scene* scene, rx_point position, rx_point normal, rx_material mat);
extern float rx_lighting_shadow_at(rx_lighting_scene* scene, rx_point position);

/* Global scene */
extern rx_lighting_scene* rx_lighting_scene_shared(void);
extern void rx_lighting_set_scene(rx_lighting_scene* scene);

/* ============================================================================
 * Dynamic Shadow System
 * ============================================================================ */

typedef struct rx_shadow_caster {
    rx_view* view;
    float height;             /* Object height for shadow offset */
    bool soft_shadow;
    float softness;
} rx_shadow_caster;

typedef struct rx_shadow_receiver {
    rx_view* view;
    float opacity;
    rx_color tint;
} rx_shadow_receiver;

extern rx_shadow_caster* rx_shadow_caster_create(rx_view* view, float height);
extern rx_shadow_receiver* rx_shadow_receiver_create(rx_view* view);
extern void rx_shadow_update(rx_lighting_scene* scene, rx_shadow_caster** casters, size_t caster_count);
extern void rx_shadow_render(void* context, rx_shadow_caster* caster, rx_light* light);

/* ============================================================================
 * Light Animation
 * ============================================================================ */

typedef enum rx_light_anim_type {
    RX_LIGHT_ANIM_INTENSITY,
    RX_LIGHT_ANIM_COLOR,
    RX_LIGHT_ANIM_POSITION,
    RX_LIGHT_ANIM_RADIUS,
} rx_light_anim_type;

typedef struct rx_light_animation {
    rx_animation base;
    rx_light* light;
    rx_light_anim_type anim_type;
    
    /* Color animation */
    rx_color from_color;
    rx_color to_color;
} rx_light_animation;

extern rx_animation* rx_light_animate_intensity(rx_light* light, float to, float duration);
extern rx_animation* rx_light_animate_color(rx_light* light, rx_color to, float duration);
extern rx_animation* rx_light_animate_position(rx_light* light, rx_point to, float duration);
extern rx_animation* rx_light_animate_radius(rx_light* light, float to, float duration);

/* ============================================================================
 * Effect Presets
 * ============================================================================ */

/* Cinematic lighting setups */
extern void rx_lighting_setup_day(rx_lighting_scene* scene);
extern void rx_lighting_setup_night(rx_lighting_scene* scene);
extern void rx_lighting_setup_sunset(rx_lighting_scene* scene);
extern void rx_lighting_setup_neon(rx_lighting_scene* scene);
extern void rx_lighting_setup_dramatic(rx_lighting_scene* scene);
extern void rx_lighting_setup_soft(rx_lighting_scene* scene);

/* Cursor/focus glow */
extern rx_light* rx_cursor_light_create(rx_color color, float radius);
extern void rx_cursor_light_update(rx_light* light, rx_point cursor_pos);

/* Interactive lighting */
extern rx_light* rx_focus_light_create(rx_view* view, rx_color color);
extern void rx_focus_light_attach(rx_light* light, rx_view* view);
extern void rx_focus_light_detach(rx_light* light);

#ifdef __cplusplus
}
#endif

#endif /* REOX_LIGHTING_H */
