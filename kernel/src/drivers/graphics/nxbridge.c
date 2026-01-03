/*
 * NXBridge Implementation
 * 
 * Connects to NXGFX kernel driver for GPU operations.
 * Provides high-level API for game/app developers.
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxbridge.h"
#include "drivers/nxgfx_kdrv.h"

/* External kernel functions */
extern void serial_puts(const char *s);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* String copy helper */
static void nxb_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Internal state */
static struct {
    int initialized;
    int device_index;
    char gpu_name[64];
    nxgfx_framebuffer_t *framebuffer;
} g_nxbridge = {0};

/* Surface implementation */
struct nxbridge_surface_s {
    uint32_t width;
    uint32_t height;
    nxbridge_format_t format;
    int vsync;
    uint32_t *pixels;
};

/* Buffer implementation (software buffers for now) */
struct nxbridge_buffer_s {
    uint64_t size;
    uint32_t flags;
    void *data;
};

/* Texture implementation */
struct nxbridge_texture_s {
    uint32_t width;
    uint32_t height;
    nxbridge_format_t format;
    void *pixels;
};

/* Current render state */
static nxbridge_surface_t g_current_surface = NULL;

/* ============ Initialization ============ */

int nxbridge_init(void) {
    if (g_nxbridge.initialized) {
        return NXBRIDGE_OK;
    }
    
    serial_puts("[NXBridge] Initializing...\n");
    
    /* Initialize NXGFX kernel driver */
    if (nxgfx_kdrv_init() != 0) {
        serial_puts("[NXBridge] Failed to init NXGFX\n");
        return NXBRIDGE_ERR_INIT;
    }
    
    /* Check for GPU */
    int gpu_count = nxgfx_kdrv_device_count();
    if (gpu_count <= 0) {
        serial_puts("[NXBridge] No GPU detected\n");
        return NXBRIDGE_ERR_NO_GPU;
    }
    
    /* Get GPU info */
    nxgfx_device_info_t info;
    if (nxgfx_kdrv_device_info(0, &info) == 0) {
        nxb_strcpy(g_nxbridge.gpu_name, info.name, 64);
    }
    
    /* Get framebuffer */
    g_nxbridge.framebuffer = nxgfx_kdrv_get_framebuffer(0);
    if (!g_nxbridge.framebuffer) {
        serial_puts("[NXBridge] No framebuffer available\n");
        return NXBRIDGE_ERR_INIT;
    }
    
    g_nxbridge.device_index = 0;
    g_nxbridge.initialized = 1;
    
    serial_puts("[NXBridge] Ready - GPU: ");
    serial_puts(g_nxbridge.gpu_name);
    serial_puts("\n");
    
    return NXBRIDGE_OK;
}

void nxbridge_shutdown(void) {
    if (!g_nxbridge.initialized) return;
    
    nxgfx_kdrv_shutdown();
    g_nxbridge.initialized = 0;
    g_nxbridge.framebuffer = NULL;
    
    serial_puts("[NXBridge] Shutdown\n");
}

int nxbridge_get_gpu_name(char *buffer, size_t size) {
    if (!buffer || size == 0) return NXBRIDGE_ERR_INVALID;
    
    nxb_strcpy(buffer, g_nxbridge.gpu_name, size);
    return NXBRIDGE_OK;
}

/* ============ Surface Management ============ */

nxbridge_surface_t nxbridge_create_surface(const nxbridge_surface_desc_t *desc) {
    if (!desc) return NULL;
    
    struct nxbridge_surface_s *surface = kmalloc(sizeof(struct nxbridge_surface_s));
    if (!surface) return NULL;
    
    surface->width = desc->width;
    surface->height = desc->height;
    surface->format = desc->format;
    surface->vsync = desc->vsync;
    
    /* Point to kernel framebuffer */
    if (g_nxbridge.framebuffer && g_nxbridge.framebuffer->virt_addr) {
        surface->pixels = (uint32_t *)g_nxbridge.framebuffer->virt_addr;
    } else {
        surface->pixels = NULL;
    }
    
    serial_puts("[NXBridge] Surface created\n");
    return surface;
}

void nxbridge_destroy_surface(nxbridge_surface_t surface) {
    if (!surface) return;
    kfree(surface);
}

int nxbridge_resize_surface(nxbridge_surface_t surface, uint32_t width, uint32_t height) {
    if (!surface) return NXBRIDGE_ERR_INVALID;
    
    surface->width = width;
    surface->height = height;
    
    /* Request mode change from kernel driver */
    nxgfx_mode_t mode;
    mode.width = width;
    mode.height = height;
    mode.bpp = 32;
    mode.refresh = 60;
    mode.pitch = width * 4;
    mode.format = NXGFX_FMT_XRGB8888;
    
    return nxgfx_kdrv_set_mode(g_nxbridge.device_index, &mode);
}

/* ============ Buffer Management ============ */

nxbridge_buffer_t nxbridge_create_buffer(const nxbridge_buffer_desc_t *desc) {
    if (!desc || desc->size == 0) return NULL;
    
    struct nxbridge_buffer_s *buffer = kmalloc(sizeof(struct nxbridge_buffer_s));
    if (!buffer) return NULL;
    
    /* Allocate buffer data */
    buffer->data = kmalloc(desc->size);
    if (!buffer->data) {
        kfree(buffer);
        return NULL;
    }
    
    buffer->size = desc->size;
    buffer->flags = desc->flags;
    
    /* Copy initial data if provided */
    if (desc->initial_data) {
        uint8_t *dst = (uint8_t *)buffer->data;
        const uint8_t *src = (const uint8_t *)desc->initial_data;
        for (uint64_t i = 0; i < desc->size; i++) {
            dst[i] = src[i];
        }
    }
    
    return buffer;
}

void nxbridge_destroy_buffer(nxbridge_buffer_t buffer) {
    if (!buffer) return;
    if (buffer->data) kfree(buffer->data);
    kfree(buffer);
}

int nxbridge_upload_buffer(nxbridge_buffer_t buffer, uint64_t offset, 
                           const void *data, uint64_t size) {
    if (!buffer || !data) return NXBRIDGE_ERR_INVALID;
    if (offset + size > buffer->size) return NXBRIDGE_ERR_INVALID;
    
    /* Direct memory copy */
    uint8_t *dst = (uint8_t *)buffer->data + offset;
    const uint8_t *src = (const uint8_t *)data;
    for (uint64_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    
    return NXBRIDGE_OK;
}

/* ============ Texture Management ============ */

nxbridge_texture_t nxbridge_create_texture(const nxbridge_texture_desc_t *desc) {
    if (!desc) return NULL;
    
    struct nxbridge_texture_s *tex = kmalloc(sizeof(struct nxbridge_texture_s));
    if (!tex) return NULL;
    
    tex->width = desc->width;
    tex->height = desc->height;
    tex->format = desc->format;
    
    /* Allocate pixel storage */
    uint64_t pixel_size = tex->width * tex->height * 4;  /* RGBA */
    tex->pixels = kmalloc(pixel_size);
    if (!tex->pixels) {
        kfree(tex);
        return NULL;
    }
    
    /* Copy initial pixels if provided */
    if (desc->pixels) {
        uint8_t *dst = (uint8_t *)tex->pixels;
        const uint8_t *src = (const uint8_t *)desc->pixels;
        for (uint64_t i = 0; i < pixel_size; i++) {
            dst[i] = src[i];
        }
    }
    
    return tex;
}

void nxbridge_destroy_texture(nxbridge_texture_t texture) {
    if (!texture) return;
    if (texture->pixels) kfree(texture->pixels);
    kfree(texture);
}

/* ============ Rendering Commands ============ */

int nxbridge_begin_frame(nxbridge_surface_t surface) {
    if (!surface) return NXBRIDGE_ERR_INVALID;
    g_current_surface = surface;
    return NXBRIDGE_OK;
}

int nxbridge_end_frame(nxbridge_surface_t surface) {
    if (!surface) return NXBRIDGE_ERR_INVALID;
    
    /* Present framebuffer (page flip) */
    nxgfx_kdrv_flip(g_nxbridge.device_index);
    
    /* Wait for VSync if enabled */
    if (surface->vsync) {
        nxgfx_kdrv_wait_vblank(g_nxbridge.device_index);
    }
    
    g_current_surface = NULL;
    return NXBRIDGE_OK;
}

void nxbridge_clear(float r, float g, float b, float a) {
    (void)a;  /* Alpha ignored for now */
    
    if (!g_current_surface || !g_current_surface->pixels) return;
    
    uint8_t ri = (uint8_t)(r * 255.0f);
    uint8_t gi = (uint8_t)(g * 255.0f);
    uint8_t bi = (uint8_t)(b * 255.0f);
    uint32_t color = (0xFF << 24) | (ri << 16) | (gi << 8) | bi;
    
    uint32_t count = g_current_surface->width * g_current_surface->height;
    uint32_t *fb = g_current_surface->pixels;
    for (uint32_t i = 0; i < count; i++) {
        fb[i] = color;
    }
}

void nxbridge_draw(nxbridge_buffer_t vertex_buffer, 
                   nxbridge_primitive_t primitive,
                   uint32_t first, uint32_t count) {
    /* Software rasterization stub - requires full 3D pipeline */
    (void)vertex_buffer;
    (void)primitive;
    (void)first;
    (void)count;
}

void nxbridge_draw_indexed(nxbridge_buffer_t vertex_buffer,
                           nxbridge_buffer_t index_buffer,
                           nxbridge_primitive_t primitive,
                           uint32_t count) {
    /* Software rasterization stub */
    (void)vertex_buffer;
    (void)index_buffer;
    (void)primitive;
    (void)count;
}

/* ============ State Management ============ */

void nxbridge_set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    (void)x; (void)y; (void)width; (void)height;
}

void nxbridge_set_scissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    (void)x; (void)y; (void)width; (void)height;
}

void nxbridge_bind_texture(uint32_t slot, nxbridge_texture_t texture) {
    (void)slot; (void)texture;
}

/* ============ Utility Functions ============ */

void nxbridge_wait_idle(void) {
    /* Memory barrier */
    __asm__ volatile("mfence" ::: "memory");
}

uint64_t nxbridge_get_vram_used(void) {
    /* Estimate based on framebuffer size */
    if (g_nxbridge.framebuffer) {
        return g_nxbridge.framebuffer->size;
    }
    return 0;
}

uint64_t nxbridge_get_vram_free(void) {
    /* Not tracked precisely - return 0 */
    return 0;
}
