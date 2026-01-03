/*
 * NXBridge - NeolyxOS Cross-Platform Graphics API
 * 
 * Direct GPU access for game developers.
 * Can be used instead of Vulkan for simpler integration.
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXBRIDGE_H
#define NXBRIDGE_H

#include <stdint.h>
#include <stddef.h>

/* ============ Result Codes ============ */

#define NXBRIDGE_OK              0
#define NXBRIDGE_ERR_INIT       -1
#define NXBRIDGE_ERR_NO_GPU     -2
#define NXBRIDGE_ERR_NO_MEMORY  -3
#define NXBRIDGE_ERR_INVALID    -4

/* ============ Buffer Flags ============ */

#define NXBRIDGE_BUF_VERTEX     0x0001
#define NXBRIDGE_BUF_INDEX      0x0002
#define NXBRIDGE_BUF_UNIFORM    0x0004
#define NXBRIDGE_BUF_TEXTURE    0x0008
#define NXBRIDGE_BUF_STAGING    0x0010

/* ============ Texture Formats ============ */

typedef enum {
    NXBRIDGE_FORMAT_RGBA8 = 0,
    NXBRIDGE_FORMAT_BGRA8,
    NXBRIDGE_FORMAT_RGB8,
    NXBRIDGE_FORMAT_R8,
    NXBRIDGE_FORMAT_DEPTH24_STENCIL8,
    NXBRIDGE_FORMAT_DEPTH32F
} nxbridge_format_t;

/* ============ Primitive Types ============ */

typedef enum {
    NXBRIDGE_TRIANGLES = 0,
    NXBRIDGE_TRIANGLE_STRIP,
    NXBRIDGE_LINES,
    NXBRIDGE_LINE_STRIP,
    NXBRIDGE_POINTS
} nxbridge_primitive_t;

/* ============ Opaque Handles ============ */

typedef struct nxbridge_context_s*   nxbridge_context_t;
typedef struct nxbridge_surface_s*   nxbridge_surface_t;
typedef struct nxbridge_buffer_s*    nxbridge_buffer_t;
typedef struct nxbridge_texture_s*   nxbridge_texture_t;
typedef struct nxbridge_shader_s*    nxbridge_shader_t;
typedef struct nxbridge_pipeline_s*  nxbridge_pipeline_t;

/* ============ Structures ============ */

typedef struct {
    uint32_t width;
    uint32_t height;
    nxbridge_format_t format;
    int vsync;
    int double_buffer;
} nxbridge_surface_desc_t;

typedef struct {
    uint64_t size;
    uint32_t flags;
    const void *initial_data;
} nxbridge_buffer_desc_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    nxbridge_format_t format;
    const void *pixels;
} nxbridge_texture_desc_t;

typedef struct {
    float x, y, z, w;
} nxbridge_vec4_t;

typedef struct {
    float m[16];
} nxbridge_mat4_t;

/* ============ Initialization ============ */

/**
 * nxbridge_init - Initialize NXBridge graphics system
 * 
 * Detects GPU, initializes VRAM, prepares for rendering.
 * 
 * Returns: NXBRIDGE_OK on success, negative error code on failure
 */
int nxbridge_init(void);

/**
 * nxbridge_shutdown - Cleanup NXBridge resources
 */
void nxbridge_shutdown(void);

/**
 * nxbridge_get_gpu_name - Get name of detected GPU
 * 
 * @buffer: Output buffer for GPU name
 * @size: Size of output buffer
 * Returns: NXBRIDGE_OK on success
 */
int nxbridge_get_gpu_name(char *buffer, size_t size);

/* ============ Surface Management ============ */

/**
 * nxbridge_create_surface - Create a rendering surface
 * 
 * @desc: Surface description
 * Returns: Surface handle or NULL on failure
 */
nxbridge_surface_t nxbridge_create_surface(const nxbridge_surface_desc_t *desc);

/**
 * nxbridge_destroy_surface - Destroy a rendering surface
 */
void nxbridge_destroy_surface(nxbridge_surface_t surface);

/**
 * nxbridge_resize_surface - Resize an existing surface
 */
int nxbridge_resize_surface(nxbridge_surface_t surface, uint32_t width, uint32_t height);

/* ============ Buffer Management ============ */

/**
 * nxbridge_create_buffer - Create a GPU buffer
 * 
 * @desc: Buffer description (size, flags, initial data)
 * Returns: Buffer handle or NULL on failure
 */
nxbridge_buffer_t nxbridge_create_buffer(const nxbridge_buffer_desc_t *desc);

/**
 * nxbridge_destroy_buffer - Destroy a GPU buffer
 */
void nxbridge_destroy_buffer(nxbridge_buffer_t buffer);

/**
 * nxbridge_upload_buffer - Upload data to GPU buffer
 * 
 * @buffer: Target buffer
 * @offset: Offset in buffer
 * @data: Source data
 * @size: Size to upload
 * Returns: NXBRIDGE_OK on success
 */
int nxbridge_upload_buffer(nxbridge_buffer_t buffer, uint64_t offset, 
                           const void *data, uint64_t size);

/* ============ Texture Management ============ */

/**
 * nxbridge_create_texture - Create a GPU texture
 */
nxbridge_texture_t nxbridge_create_texture(const nxbridge_texture_desc_t *desc);

/**
 * nxbridge_destroy_texture - Destroy a GPU texture
 */
void nxbridge_destroy_texture(nxbridge_texture_t texture);

/* ============ Rendering Commands ============ */

/**
 * nxbridge_begin_frame - Start a new frame
 * 
 * @surface: Target surface
 * Returns: NXBRIDGE_OK on success
 */
int nxbridge_begin_frame(nxbridge_surface_t surface);

/**
 * nxbridge_end_frame - End frame and present
 * 
 * @surface: Target surface
 * Returns: NXBRIDGE_OK on success
 */
int nxbridge_end_frame(nxbridge_surface_t surface);

/**
 * nxbridge_clear - Clear the current surface
 * 
 * @r, g, b, a: Clear color (0.0 - 1.0)
 */
void nxbridge_clear(float r, float g, float b, float a);

/**
 * nxbridge_draw - Draw primitives
 * 
 * @vertex_buffer: Vertex data buffer
 * @primitive: Primitive type (triangles, lines, etc.)
 * @first: First vertex index
 * @count: Number of vertices
 */
void nxbridge_draw(nxbridge_buffer_t vertex_buffer, 
                   nxbridge_primitive_t primitive,
                   uint32_t first, uint32_t count);

/**
 * nxbridge_draw_indexed - Draw indexed primitives
 * 
 * @vertex_buffer: Vertex data buffer
 * @index_buffer: Index buffer
 * @primitive: Primitive type
 * @count: Number of indices
 */
void nxbridge_draw_indexed(nxbridge_buffer_t vertex_buffer,
                           nxbridge_buffer_t index_buffer,
                           nxbridge_primitive_t primitive,
                           uint32_t count);

/* ============ State Management ============ */

/**
 * nxbridge_set_viewport - Set rendering viewport
 */
void nxbridge_set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
 * nxbridge_set_scissor - Set scissor rectangle
 */
void nxbridge_set_scissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
 * nxbridge_bind_texture - Bind texture for rendering
 * 
 * @slot: Texture slot (0-7)
 * @texture: Texture to bind
 */
void nxbridge_bind_texture(uint32_t slot, nxbridge_texture_t texture);

/* ============ Utility Functions ============ */

/**
 * nxbridge_wait_idle - Wait for GPU to finish all work
 */
void nxbridge_wait_idle(void);

/**
 * nxbridge_get_vram_used - Get bytes of VRAM in use
 */
uint64_t nxbridge_get_vram_used(void);

/**
 * nxbridge_get_vram_free - Get bytes of VRAM available
 */
uint64_t nxbridge_get_vram_free(void);

#endif /* NXBRIDGE_H */
