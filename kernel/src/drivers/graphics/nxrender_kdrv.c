/*
 * NXRender Kernel Driver (nxrender_kdrv)
 * 
 * Kernel-side GUI acceleration driver for NXRender
 * Processes command buffers from user-space NXRender library
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxrender_kdrv.h"
#include "drivers/nxgfx_kdrv.h"
#include "drivers/nxgfx_accel.h"
#include <stdint.h>
#include <stddef.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* From nxgfx_kdrv */
extern nxgfx_framebuffer_t* nxgfx_kdrv_get_framebuffer(int device);
extern int nxgfx_kdrv_wait_vblank(int device);

/* ============ Constants ============ */

#define MAX_TEXTURES        256
#define MAX_SURFACES        16
#define TEXTURE_POOL_SIZE   (32 * 1024 * 1024)  /* 32 MB for textures */

/* ============ Driver State ============ */

static struct {
    int initialized;
    
    /* Command buffer */
    nxrender_cmd_buffer_t cmd_buffer;
    
    /* Framebuffer (from nxgfx_kdrv) */
    uint32_t* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    
    /* Surfaces */
    nxrender_surface_t surfaces[MAX_SURFACES];
    int surface_count;
    int current_surface;
    
    /* Textures */
    nxrender_texture_t textures[MAX_TEXTURES];
    int texture_count;
    uint8_t* texture_pool;
    uint64_t texture_pool_offset;
    
    /* VSync */
    nxrender_vsync_t vsync_mode;
    
    /* Performance */
    nxrender_perf_t perf;
    uint64_t frame_start_us;
    
    /* Scissor state */
    int scissor_enabled;
    int32_t scissor_x, scissor_y, scissor_w, scissor_h;
    
} g_nxrender = {0};

/* Texture pool memory */
static uint8_t g_texture_pool[TEXTURE_POOL_SIZE] __attribute__((aligned(4096)));

/* ============ Helpers ============ */

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

static inline void nx_memset32(uint32_t *dest, uint32_t val, size_t count) {
    while (count--) *dest++ = val;
}

static inline int min_i(int a, int b) { return a < b ? a : b; }
static inline int max_i(int a, int b) { return a > b ? a : b; }
static inline int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ============ Drawing Primitives ============ */

static inline void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= g_nxrender.width || (uint32_t)y >= g_nxrender.height) return;
    
    if (g_nxrender.scissor_enabled) {
        if (x < g_nxrender.scissor_x || x >= g_nxrender.scissor_x + g_nxrender.scissor_w) return;
        if (y < g_nxrender.scissor_y || y >= g_nxrender.scissor_y + g_nxrender.scissor_h) return;
    }
    
    g_nxrender.framebuffer[y * (g_nxrender.pitch / 4) + x] = color;
    g_nxrender.perf.pixels_drawn++;
}

static void draw_rect(int x, int y, int w, int h, uint32_t color, uint32_t radius) {
    (void)radius;  /* TODO: rounded corners */
    
    int x0 = max_i(0, x);
    int y0 = max_i(0, y);
    int x1 = min_i(x + w, (int)g_nxrender.width);
    int y1 = min_i(y + h, (int)g_nxrender.height);
    
    if (g_nxrender.scissor_enabled) {
        x0 = max_i(x0, g_nxrender.scissor_x);
        y0 = max_i(y0, g_nxrender.scissor_y);
        x1 = min_i(x1, g_nxrender.scissor_x + g_nxrender.scissor_w);
        y1 = min_i(y1, g_nxrender.scissor_y + g_nxrender.scissor_h);
    }
    
    uint32_t pitch_px = g_nxrender.pitch / 4;
    
    for (int row = y0; row < y1; row++) {
        uint32_t* line = g_nxrender.framebuffer + row * pitch_px + x0;
        nx_memset32(line, color, x1 - x0);
        g_nxrender.perf.pixels_drawn += (x1 - x0);
    }
}

static void draw_circle(int cx, int cy, int r, uint32_t color) {
    int x0 = max_i(0, cx - r);
    int y0 = max_i(0, cy - r);
    int x1 = min_i(cx + r, (int)g_nxrender.width);
    int y1 = min_i(cy + r, (int)g_nxrender.height);
    
    int r2 = r * r;
    
    for (int py = y0; py < y1; py++) {
        int dy = py - cy;
        int dy2 = dy * dy;
        for (int px = x0; px < x1; px++) {
            int dx = px - cx;
            if (dx * dx + dy2 <= r2) {
                put_pixel(px, py, color);
            }
        }
    }
}

static void draw_line(int x1, int y1, int x2, int y2, uint32_t color, int width) {
    (void)width;  /* TODO: thick lines */
    
    /* Bresenham's line algorithm */
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    dx = dx > 0 ? dx : -dx;
    dy = dy > 0 ? dy : -dy;
    
    int err = (dx > dy ? dx : -dy) / 2;
    
    while (1) {
        put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = err;
        if (e2 > -dx) { err -= dy; x1 += sx; }
        if (e2 < dy)  { err += dx; y1 += sy; }
    }
}

/* ============ Color Conversion ============ */

static inline nxgfx_color_t u32_to_color(uint32_t c) {
    return (nxgfx_color_t){
        .r = (c >> 16) & 0xFF,
        .g = (c >> 8) & 0xFF,
        .b = c & 0xFF,
        .a = (c >> 24) & 0xFF
    };
}

/* ============ Command Processing (using nxgfx_accel) ============ */

static void process_command(const nxrender_cmd_t *cmd) {
    switch (cmd->type) {
        case NXRENDER_CMD_CLEAR: {
            nxgfx_rect_t rect = {0, 0, g_nxrender.width, g_nxrender.height};
            nxgfx_accel_fill_rect(&rect, u32_to_color(cmd->data.clear.color));
            g_nxrender.perf.pixels_drawn += g_nxrender.width * g_nxrender.height;
            break;
        }
            
        case NXRENDER_CMD_RECT: {
            nxgfx_rect_t rect = {
                cmd->data.rect.x, cmd->data.rect.y,
                cmd->data.rect.w, cmd->data.rect.h
            };
            nxgfx_color_t color = u32_to_color(cmd->data.rect.color);
            if (color.a < 255) {
                nxgfx_accel_blend(&rect, color);
            } else {
                nxgfx_accel_fill_rect(&rect, color);
            }
            g_nxrender.perf.pixels_drawn += cmd->data.rect.w * cmd->data.rect.h;
            break;
        }
            
        case NXRENDER_CMD_CIRCLE:
            nxgfx_accel_circle(cmd->data.circle.cx, cmd->data.circle.cy,
                              cmd->data.circle.r, u32_to_color(cmd->data.circle.color));
            break;
            
        case NXRENDER_CMD_LINE:
            nxgfx_accel_line(cmd->data.line.x1, cmd->data.line.y1,
                            cmd->data.line.x2, cmd->data.line.y2,
                            u32_to_color(cmd->data.line.color));
            break;
            
        case NXRENDER_CMD_SCISSOR:
            g_nxrender.scissor_enabled = 1;
            g_nxrender.scissor_x = cmd->data.scissor.x;
            g_nxrender.scissor_y = cmd->data.scissor.y;
            g_nxrender.scissor_w = cmd->data.scissor.w;
            g_nxrender.scissor_h = cmd->data.scissor.h;
            if (cmd->data.scissor.w == 0 || cmd->data.scissor.h == 0) {
                g_nxrender.scissor_enabled = 0;
            }
            break;
            
        case NXRENDER_CMD_BLIT: {
            /* Use nxgfx_accel_blit for texture blitting */
            nxrender_texture_t *tex = &g_nxrender.textures[cmd->data.blit.texture_id];
            if (tex->in_use) {
                nxgfx_rect_t src = {cmd->data.blit.sx, cmd->data.blit.sy, 
                                   cmd->data.blit.sw, cmd->data.blit.sh};
                nxgfx_rect_t dst = {cmd->data.blit.dx, cmd->data.blit.dy,
                                   cmd->data.blit.dw, cmd->data.blit.dh};
                uint32_t *pixels = (uint32_t*)(g_nxrender.texture_pool + tex->vram_offset);
                nxgfx_accel_blit(&src, &dst, pixels);
            }
            break;
        }
            
        case NXRENDER_CMD_PRESENT:
            /* Handled separately */
            break;
            
        case NXRENDER_CMD_FENCE:
            g_nxrender.cmd_buffer.fence_value = cmd->data.fence.fence_id;
            break;
            
        default:
            break;
    }
    
    g_nxrender.perf.commands_processed++;
}

/* ============ Public API ============ */

int nxrender_kdrv_init(void) {
    if (g_nxrender.initialized) return 0;
    
    serial_puts("[NXRender] Initializing kernel driver v" NXRENDER_KDRV_VERSION "\n");
    
    /* Get framebuffer from nxgfx_kdrv */
    nxgfx_framebuffer_t *fb = nxgfx_kdrv_get_framebuffer(0);
    if (!fb || !fb->virt_addr) {
        serial_puts("[NXRender] ERROR: No framebuffer available\n");
        return -1;
    }
    
    g_nxrender.framebuffer = (uint32_t*)fb->virt_addr;
    g_nxrender.width = fb->mode.width;
    g_nxrender.height = fb->mode.height;
    g_nxrender.pitch = fb->mode.pitch;
    
    /* Initialize nxgfx_accel with framebuffer */
    nxgfx_accel_init(g_nxrender.framebuffer, g_nxrender.width, 
                     g_nxrender.height, g_nxrender.pitch);
    
    /* Initialize texture pool */
    g_nxrender.texture_pool = g_texture_pool;
    g_nxrender.texture_pool_offset = 0;
    
    /* Initialize command buffer */
    g_nxrender.cmd_buffer.write_index = 0;
    g_nxrender.cmd_buffer.read_index = 0;
    g_nxrender.cmd_buffer.fence_value = 0;
    
    /* Default VSync on */
    g_nxrender.vsync_mode = NXRENDER_VSYNC_ON;
    
    serial_puts("[NXRender] Framebuffer: ");
    serial_dec(g_nxrender.width);
    serial_putc('x');
    serial_dec(g_nxrender.height);
    serial_puts("\n");
    
    g_nxrender.initialized = 1;
    return 0;
}

void nxrender_kdrv_shutdown(void) {
    if (!g_nxrender.initialized) return;
    g_nxrender.initialized = 0;
    serial_puts("[NXRender] Shutdown complete\n");
}

nxrender_cmd_buffer_t* nxrender_kdrv_get_cmd_buffer(void) {
    return &g_nxrender.cmd_buffer;
}

int nxrender_kdrv_submit(void) {
    if (!g_nxrender.initialized) return -1;
    
    nxrender_cmd_buffer_t *buf = &g_nxrender.cmd_buffer;
    
    /* Process all pending commands */
    while (buf->read_index != buf->write_index) {
        process_command(&buf->commands[buf->read_index]);
        buf->read_index = (buf->read_index + 1) % NXRENDER_CMD_BUFFER_SIZE;
    }
    
    return 0;
}

int nxrender_kdrv_wait_idle(void) {
    /* For now, submit is synchronous so we're always idle after */
    return nxrender_kdrv_submit();
}

int nxrender_kdrv_create_texture(uint32_t width, uint32_t height, uint32_t format) {
    if (g_nxrender.texture_count >= MAX_TEXTURES) return -1;
    
    uint32_t bpp = (format == NXRENDER_TEX_R8) ? 1 : 
                   (format == NXRENDER_TEX_RGB8) ? 3 : 4;
    uint64_t size = (uint64_t)width * height * bpp;
    size = (size + 63) & ~63ULL;  /* Align to 64 bytes */
    
    if (g_nxrender.texture_pool_offset + size > TEXTURE_POOL_SIZE) {
        return -1;
    }
    
    int id = g_nxrender.texture_count++;
    nxrender_texture_t *tex = &g_nxrender.textures[id];
    tex->id = id;
    tex->width = width;
    tex->height = height;
    tex->format = format;
    tex->vram_offset = g_nxrender.texture_pool_offset;
    tex->size = size;
    tex->in_use = 1;
    
    g_nxrender.texture_pool_offset += size;
    
    return id;
}

int nxrender_kdrv_destroy_texture(int texture_id) {
    if (texture_id < 0 || texture_id >= g_nxrender.texture_count) return -1;
    g_nxrender.textures[texture_id].in_use = 0;
    return 0;
}

int nxrender_kdrv_upload_texture(int texture_id, const void *data, size_t size) {
    if (texture_id < 0 || texture_id >= g_nxrender.texture_count) return -1;
    
    nxrender_texture_t *tex = &g_nxrender.textures[texture_id];
    if (!tex->in_use) return -1;
    
    size_t copy_size = size < tex->size ? size : tex->size;
    uint8_t *dst = g_nxrender.texture_pool + tex->vram_offset;
    const uint8_t *src = (const uint8_t*)data;
    
    for (size_t i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }
    
    g_nxrender.perf.bytes_uploaded += copy_size;
    return 0;
}

int nxrender_kdrv_set_vsync(nxrender_vsync_t mode) {
    g_nxrender.vsync_mode = mode;
    return 0;
}

int nxrender_kdrv_wait_vblank(void) {
    if (g_nxrender.vsync_mode == NXRENDER_VSYNC_OFF) return 0;
    return nxgfx_kdrv_wait_vblank(0);
}

int nxrender_kdrv_present(int surface_id) {
    (void)surface_id;
    
    /* Process any remaining commands */
    nxrender_kdrv_submit();
    
    /* Wait for VSync if enabled */
    if (g_nxrender.vsync_mode != NXRENDER_VSYNC_OFF) {
        nxrender_kdrv_wait_vblank();
    }
    
    g_nxrender.perf.frames_rendered++;
    
    /* TODO: Calculate frame time and FPS */
    
    return 0;
}

void nxrender_kdrv_get_perf(nxrender_perf_t *perf) {
    if (perf) *perf = g_nxrender.perf;
}

void nxrender_kdrv_reset_perf(void) {
    g_nxrender.perf.frames_rendered = 0;
    g_nxrender.perf.commands_processed = 0;
    g_nxrender.perf.pixels_drawn = 0;
    g_nxrender.perf.bytes_uploaded = 0;
}

uint32_t* nxrender_kdrv_get_framebuffer(void) {
    return g_nxrender.framebuffer;
}

uint32_t nxrender_kdrv_get_width(void) {
    return g_nxrender.width;
}

uint32_t nxrender_kdrv_get_height(void) {
    return g_nxrender.height;
}

uint32_t nxrender_kdrv_get_pitch(void) {
    return g_nxrender.pitch;
}

void nxrender_kdrv_debug(void) {
    serial_puts("\n=== NXRender KDRV Debug ===\n");
    serial_puts("Resolution: ");
    serial_dec(g_nxrender.width);
    serial_putc('x');
    serial_dec(g_nxrender.height);
    serial_puts("\nTextures: ");
    serial_dec(g_nxrender.texture_count);
    serial_puts("/");
    serial_dec(MAX_TEXTURES);
    serial_puts("\nFrames: ");
    serial_dec((uint32_t)g_nxrender.perf.frames_rendered);
    serial_puts("\nCommands: ");
    serial_dec((uint32_t)g_nxrender.perf.commands_processed);
    serial_puts("\nPixels: ");
    serial_dec((uint32_t)(g_nxrender.perf.pixels_drawn / 1000000));
    serial_puts("M\n");
    serial_puts("===========================\n\n");
}
