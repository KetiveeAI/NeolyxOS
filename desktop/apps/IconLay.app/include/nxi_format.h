/*
 * NXI Format - NeolyxOS Native Icon Format
 * 
 * Binary format for layered, scalable icons with effects support.
 * Base design size: 1024x1024, exports to multiple device sizes.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXI_FORMAT_H
#define NXI_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

#define NXI_MAGIC "NXI\x02"
#define NXI_VERSION 2
#define NXI_BASE_SIZE 1024

/* Color depth options */
typedef enum {
    NXI_DEPTH_RGB24 = 24,
    NXI_DEPTH_RGBA32 = 32
} nxi_depth_t;

/* Effect flags */
#define NXI_EFFECT_NONE     0x00
#define NXI_EFFECT_SHADOW   0x01
#define NXI_EFFECT_GLOW     0x02
#define NXI_EFFECT_GRADIENT 0x04
#define NXI_EFFECT_GLASS    0x08
#define NXI_EFFECT_FROST    0x10
#define NXI_EFFECT_BLUR     0x20
#define NXI_EFFECT_EDGE     0x40
#define NXI_EFFECT_LIGHT    0x80

/* NXI file header */
typedef struct {
    char magic[4];           /* "NXI\x02" */
    uint8_t version;         /* 2 */
    uint32_t base_size;      /* 1024 (original design size) */
    uint32_t export_size;    /* Actual icon size (16-1024) */
    uint8_t color_depth;     /* 32 (RGBA) */
    uint32_t layer_count;    /* Number of layers */
    uint8_t has_effects;     /* Effects bitmask */
} __attribute__((packed)) nxi_header_t;

/* Layer header */
typedef struct {
    char name[32];           /* Layer name */
    uint8_t visible;         /* Visibility flag */
    uint8_t opacity;         /* 0-255 */
    float pos_x;             /* Position X (0-1 normalized) */
    float pos_y;             /* Position Y (0-1 normalized) */
    float scale;             /* Scale factor (1.0 = 100%) */
    uint32_t fill_color;     /* Fill color (RGBA) */
    uint8_t effect_flags;    /* Per-layer effects */
    uint32_t path_count;     /* Number of path commands */
    /* Followed by: path_cmd_t[path_count] */
} __attribute__((packed)) nxi_layer_header_t;

/* Path command types */
typedef enum {
    NXI_PATH_MOVE = 0,
    NXI_PATH_LINE = 1,
    NXI_PATH_CUBIC = 2,
    NXI_PATH_QUAD = 3,
    NXI_PATH_CLOSE = 4,
    NXI_PATH_ARC = 5      /* Elliptical arc command */
} nxi_path_type_t;

/* Path command */
typedef struct {
    uint8_t type;
    float x, y;
    float x1, y1;   /* Control point 1 (for curves) */
    float x2, y2;   /* Control point 2 (for cubic) */
} __attribute__((packed)) nxi_path_cmd_t;

/* Gradient types */
typedef enum {
    NXI_GRADIENT_NONE = 0,
    NXI_GRADIENT_LINEAR = 1,
    NXI_GRADIENT_RADIAL = 2
} nxi_gradient_type_t;

#define NXI_MAX_GRADIENT_STOPS 8

/* Gradient stop */
typedef struct {
    float position;          /* 0.0 to 1.0 */
    uint32_t color;          /* RGBA */
} nxi_gradient_stop_t;

/* Gradient definition */
typedef struct {
    uint8_t type;            /* nxi_gradient_type_t */
    float angle;             /* For linear: 0-360 degrees */
    float cx, cy;            /* For radial: center (0-1 normalized) */
    float radius;            /* For radial: radius (0-1 normalized) */
    uint8_t stop_count;      /* Number of stops (2-8) */
    nxi_gradient_stop_t stops[NXI_MAX_GRADIENT_STOPS];
} nxi_gradient_t;

/* Effect parameters */
typedef struct {
    /* Shadow */
    float shadow_offset_x;
    float shadow_offset_y;
    float shadow_blur;
    uint32_t shadow_color;
    
    /* Glow */
    float glow_radius;
    uint32_t glow_color;
    
    /* Gradient */
    nxi_gradient_t gradient;
} nxi_effects_t;

/* In-memory layer */
typedef struct {
    char name[32];
    bool visible;
    uint8_t opacity;
    float pos_x, pos_y;
    float scale;
    uint32_t fill_color;
    uint8_t effect_flags;
    nxi_effects_t effects;
    
    /* Path data */
    nxi_path_cmd_t* paths;
    uint32_t path_count;
    uint32_t path_capacity;
} nxi_layer_t;

/* In-memory icon */
typedef struct {
    uint32_t base_size;
    uint32_t export_size;
    
    nxi_layer_t** layers;
    uint32_t layer_count;
    uint32_t layer_capacity;
    
    uint8_t global_effects;
    nxi_effects_t global_effect_params;
} nxi_icon_t;

/* Device export presets */
typedef enum {
    NXI_PRESET_DESKTOP,   /* 16,24,32,48,64,128,256,512 */
    NXI_PRESET_MOBILE,    /* 48,72,96,120,144,192 */
    NXI_PRESET_WATCH,     /* 24,38,42,48 */
    NXI_PRESET_ALL        /* All sizes */
} nxi_preset_t;

/* API Functions */

/* Create new icon with base size */
nxi_icon_t* nxi_create(void);

/* Free icon and all layers */
void nxi_free(nxi_icon_t* icon);

/* Add a new layer */
nxi_layer_t* nxi_add_layer(nxi_icon_t* icon, const char* name);

/* Remove layer by index */
int nxi_remove_layer(nxi_icon_t* icon, uint32_t index);

/* Reorder layer */
int nxi_move_layer(nxi_icon_t* icon, uint32_t from, uint32_t to);

/* Add path command to layer */
int nxi_layer_add_path(nxi_layer_t* layer, nxi_path_cmd_t cmd);

/* Clear layer paths */
void nxi_layer_clear_paths(nxi_layer_t* layer);

/* Save icon to file */
int nxi_save(const nxi_icon_t* icon, const char* path);

/* Load icon from file */
nxi_icon_t* nxi_load(const char* path);

/* Export to specific size */
int nxi_export_size(const nxi_icon_t* icon, uint32_t size, const char* path);

/* Export using device preset */
int nxi_export_preset(const nxi_icon_t* icon, nxi_preset_t preset, const char* output_dir);

/* Get preset sizes array */
const uint32_t* nxi_get_preset_sizes(nxi_preset_t preset, uint32_t* count);

/* PNG Export Functions */

/* Render icon to RGBA buffer (caller must free) */
uint8_t* nxi_render_to_buffer(const nxi_icon_t* icon, uint32_t size, 
                               void* render_ctx);

/* Export icon as PNG to file */
int nxi_export_png(const nxi_icon_t* icon, uint32_t size, 
                   const char* path, void* render_ctx);

/* Export using device preset as PNG files */
int nxi_export_png_preset(const nxi_icon_t* icon, nxi_preset_t preset, 
                          const char* output_dir, void* render_ctx);

#endif /* NXI_FORMAT_H */
