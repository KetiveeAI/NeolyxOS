/*
 * NeolyxOS - NXGFX Graphics Context
 * Framebuffer-based software renderer
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct nx_context {
    uint32_t* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    nx_rect_t clip;
    bool has_clip;
    /* 3D rendering support */
    float* depth_buffer;
    bool depth_test_enabled;
};

/* Pack color to 32-bit ARGB */
static inline uint32_t color_to_argb(nx_color_t c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

/* Alpha blend two colors */
static inline nx_color_t blend_colors(nx_color_t src, nx_color_t dst) {
    if (src.a == 255) return src;
    if (src.a == 0) return dst;
    uint32_t a = src.a;
    uint32_t inv_a = 255 - a;
    return (nx_color_t){
        .r = (uint8_t)((src.r * a + dst.r * inv_a) / 255),
        .g = (uint8_t)((src.g * a + dst.g * inv_a) / 255),
        .b = (uint8_t)((src.b * a + dst.b * inv_a) / 255),
        .a = 255
    };
}

nx_context_t* nxgfx_init(void* fb, uint32_t w, uint32_t h, uint32_t pitch) {
    if (!fb || w == 0 || h == 0) return NULL;
    nx_context_t* ctx = (nx_context_t*)malloc(sizeof(nx_context_t));
    if (!ctx) return NULL;
    ctx->framebuffer = (uint32_t*)fb;
    ctx->width = w;
    ctx->height = h;
    ctx->pitch = pitch;
    ctx->has_clip = false;
    ctx->clip = nx_rect(0, 0, w, h);
    ctx->depth_buffer = NULL;
    ctx->depth_test_enabled = false;
    return ctx;
}

void nxgfx_destroy(nx_context_t* ctx) {
    if (!ctx) return;
    if (ctx->depth_buffer) free(ctx->depth_buffer);
    free(ctx);
}

uint32_t nxgfx_width(const nx_context_t* ctx) { return ctx ? ctx->width : 0; }
uint32_t nxgfx_height(const nx_context_t* ctx) { return ctx ? ctx->height : 0; }

void nxgfx_put_pixel(nx_context_t* ctx, int32_t x, int32_t y, nx_color_t color) {
    if (!ctx || x < 0 || y < 0 || (uint32_t)x >= ctx->width || (uint32_t)y >= ctx->height) return;
    if (ctx->has_clip && !nx_rect_contains(ctx->clip, (nx_point_t){x, y})) return;
    uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
    row[x] = color_to_argb(color);
}

void nxgfx_clear(nx_context_t* ctx, nx_color_t color) {
    if (!ctx) return;
    uint32_t argb = color_to_argb(color);
    for (uint32_t y = 0; y < ctx->height; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
        for (uint32_t x = 0; x < ctx->width; x++) row[x] = argb;
    }
}

void nxgfx_fill_rect(nx_context_t* ctx, nx_rect_t rect, nx_color_t color) {
    if (!ctx) return;
    int32_t x0 = rect.x < 0 ? 0 : rect.x;
    int32_t y0 = rect.y < 0 ? 0 : rect.y;
    int32_t x1 = (int32_t)(rect.x + rect.width);
    int32_t y1 = (int32_t)(rect.y + rect.height);
    if (x1 > (int32_t)ctx->width) x1 = ctx->width;
    if (y1 > (int32_t)ctx->height) y1 = ctx->height;
    if (ctx->has_clip) {
        if (x0 < ctx->clip.x) x0 = ctx->clip.x;
        if (y0 < ctx->clip.y) y0 = ctx->clip.y;
        if (x1 > (int32_t)(ctx->clip.x + ctx->clip.width)) x1 = ctx->clip.x + ctx->clip.width;
        if (y1 > (int32_t)(ctx->clip.y + ctx->clip.height)) y1 = ctx->clip.y + ctx->clip.height;
    }
    uint32_t argb = color_to_argb(color);
    for (int32_t y = y0; y < y1; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
        for (int32_t x = x0; x < x1; x++) row[x] = argb;
    }
}

void nxgfx_draw_rect(nx_context_t* ctx, nx_rect_t r, nx_color_t c, uint32_t s) {
    if (!ctx || s == 0) return;
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y, r.width, s), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y + r.height - s, r.width, s), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y, s, r.height), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x + r.width - s, r.y, s, r.height), c);
}

/* Anti-aliased pixel setter with alpha blending */
static void put_pixel_alpha(nx_context_t* ctx, int32_t x, int32_t y, nx_color_t color, uint8_t alpha) {
    if (!ctx || x < 0 || y < 0 || (uint32_t)x >= ctx->width || (uint32_t)y >= ctx->height) return;
    if (ctx->has_clip && !nx_rect_contains(ctx->clip, (nx_point_t){x, y})) return;
    
    uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
    uint32_t dst_argb = row[x];
    
    /* Extract destination color */
    nx_color_t dst = {
        .r = (dst_argb >> 16) & 0xFF,
        .g = (dst_argb >> 8) & 0xFF,
        .b = dst_argb & 0xFF,
        .a = 255
    };
    
    /* Apply alpha to source color */
    nx_color_t src = color;
    src.a = (uint8_t)((color.a * alpha) / 255);
    
    nx_color_t blended = blend_colors(src, dst);
    row[x] = color_to_argb(blended);
}

/* Scanline polygon fill with EVEN-ODD rule (handles holes) */
void nxgfx_fill_polygon(nx_context_t* ctx, const nx_point_t* points, uint32_t count, nx_color_t color) {
    if (!ctx || !points || count < 3) return;
    
    /* Find bounding box */
    int32_t min_y = points[0].y, max_y = points[0].y;
    for (uint32_t i = 1; i < count; i++) {
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    
    /* Clip to context bounds */
    if (min_y < 0) min_y = 0;
    if (max_y >= (int32_t)ctx->height) max_y = ctx->height - 1;
    
    /* Allocate edge crossing buffer */
    int32_t* x_crossings = malloc((count + 1) * sizeof(int32_t));
    if (!x_crossings) return;
    
    /* Scanline fill with even-odd rule */
    for (int32_t y = min_y; y <= max_y; y++) {
        uint32_t crossing_count = 0;
        
        /* Find all edge crossings at this Y */
        for (uint32_t i = 0; i < count; i++) {
            uint32_t j = (i + 1) % count;
            int32_t y0 = points[i].y, y1 = points[j].y;
            int32_t x0 = points[i].x, x1 = points[j].x;
            
            /* Check if edge crosses this scanline (not touching endpoints twice) */
            if ((y0 < y && y1 >= y) || (y1 < y && y0 >= y)) {
                /* Calculate X intersection */
                float t = (float)(y - y0) / (float)(y1 - y0);
                int32_t x = x0 + (int32_t)(t * (x1 - x0));
                if (crossing_count < count) {
                    x_crossings[crossing_count++] = x;
                }
            }
        }
        
        if (crossing_count == 0) continue;
        
        /* Sort crossings */
        for (uint32_t i = 0; i < crossing_count - 1; i++) {
            for (uint32_t j = i + 1; j < crossing_count; j++) {
                if (x_crossings[j] < x_crossings[i]) {
                    int32_t tmp = x_crossings[i];
                    x_crossings[i] = x_crossings[j];
                    x_crossings[j] = tmp;
                }
            }
        }
        
        /* Even-odd fill: fill between pairs of crossings */
        for (uint32_t i = 0; i + 1 < crossing_count; i += 2) {
            int32_t x0 = x_crossings[i];
            int32_t x1 = x_crossings[i + 1];
            if (x0 < 0) x0 = 0;
            if (x1 >= (int32_t)ctx->width) x1 = ctx->width - 1;
            
            for (int32_t x = x0; x <= x1; x++) {
                if (color.a < 255) {
                    put_pixel_alpha(ctx, x, y, color, color.a);
                } else {
                    nxgfx_put_pixel(ctx, x, y, color);
                }
            }
        }
    }
    
    free(x_crossings);
}

/* Fill multiple contours with even-odd rule (for SVG compound paths) */
void nxgfx_fill_complex_path(nx_context_t* ctx, 
                              const nx_point_t** contours, 
                              const uint32_t* counts,
                              uint32_t num_contours,
                              nx_color_t color) {
    if (!ctx || !contours || !counts || num_contours == 0) return;
    
    /* Find global bounding box */
    int32_t min_y = INT32_MAX, max_y = INT32_MIN;
    uint32_t total_edges = 0;
    
    for (uint32_t c = 0; c < num_contours; c++) {
        total_edges += counts[c];
        for (uint32_t i = 0; i < counts[c]; i++) {
            if (contours[c][i].y < min_y) min_y = contours[c][i].y;
            if (contours[c][i].y > max_y) max_y = contours[c][i].y;
        }
    }
    
    if (min_y < 0) min_y = 0;
    if (max_y >= (int32_t)ctx->height) max_y = ctx->height - 1;
    
    int32_t* x_crossings = malloc((total_edges + 1) * sizeof(int32_t));
    if (!x_crossings) return;
    
    /* Scanline fill across all contours */
    for (int32_t y = min_y; y <= max_y; y++) {
        uint32_t crossing_count = 0;
        
        /* Collect crossings from ALL contours */
        for (uint32_t c = 0; c < num_contours; c++) {
            uint32_t cnt = counts[c];
            const nx_point_t* pts = contours[c];
            
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t j = (i + 1) % cnt;
                int32_t y0 = pts[i].y, y1 = pts[j].y;
                int32_t x0 = pts[i].x, x1 = pts[j].x;
                
                if ((y0 < y && y1 >= y) || (y1 < y && y0 >= y)) {
                    float t = (float)(y - y0) / (float)(y1 - y0);
                    int32_t x = x0 + (int32_t)(t * (x1 - x0));
                    x_crossings[crossing_count++] = x;
                }
            }
        }
        
        if (crossing_count == 0) continue;
        
        /* Sort crossings */
        for (uint32_t i = 0; i < crossing_count - 1; i++) {
            for (uint32_t j = i + 1; j < crossing_count; j++) {
                if (x_crossings[j] < x_crossings[i]) {
                    int32_t tmp = x_crossings[i];
                    x_crossings[i] = x_crossings[j];
                    x_crossings[j] = tmp;
                }
            }
        }
        
        /* Even-odd: fill between pairs */
        for (uint32_t i = 0; i + 1 < crossing_count; i += 2) {
            int32_t x0 = x_crossings[i];
            int32_t x1 = x_crossings[i + 1];
            if (x0 < 0) x0 = 0;
            if (x1 >= (int32_t)ctx->width) x1 = ctx->width - 1;
            
            for (int32_t x = x0; x <= x1; x++) {
                if (color.a < 255) {
                    put_pixel_alpha(ctx, x, y, color, color.a);
                } else {
                    nxgfx_put_pixel(ctx, x, y, color);
                }
            }
        }
    }
    
    free(x_crossings);
}


void nxgfx_fill_circle(nx_context_t* ctx, nx_point_t center, uint32_t radius, nx_color_t color) {
    if (!ctx || radius == 0) return;
    int32_t r = (int32_t)radius;
    float rf = (float)radius;
    
    /* Tight anti-aliasing (1.5px band) for sharp edges */
    for (int32_t dy = -r - 1; dy <= r + 1; dy++) {
        for (int32_t dx = -r - 1; dx <= r + 1; dx++) {
            float dist_sq = (float)(dx*dx + dy*dy);
            float dist = sqrtf(dist_sq);
            float edge = dist - rf;
            
            if (edge < -1.0f) {
                /* Fully inside - draw full opacity */
                nxgfx_put_pixel(ctx, center.x + dx, center.y + dy, color);
            } else if (edge < 1.0f) {
                /* Edge pixel - 1.5px AA band */
                float coverage = 0.5f - edge * 0.5f;  /* 1.0 at -1, 0.5 at 0, 0.0 at 1 */
                if (coverage > 0.0f) {
                    uint8_t alpha = (uint8_t)(coverage * 255.0f);
                    if (alpha > 0) {
                        put_pixel_alpha(ctx, center.x + dx, center.y + dy, color, alpha);
                    }
                }
            }
            /* Outside - skip */
        }
    }
}

/* Wu's anti-aliased line algorithm */
static float fpart(float x) { return x - (int)x; }
static float rfpart(float x) { return 1.0f - fpart(x); }

void nxgfx_draw_line(nx_context_t* ctx, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t w) {
    if (!ctx) return;
    
    float x0 = (float)p1.x, y0 = (float)p1.y;
    float x1 = (float)p2.x, y1 = (float)p2.y;
    
    /* Minimum stroke width for visibility */
    float stroke_width = w < 1 ? 1.25f : (float)w;
    (void)stroke_width;  /* Used for future thick line support */
    
    int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    
    if (steep) {
        float t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        float t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    
    float dx = x1 - x0;
    float dy = y1 - y0;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;
    
    /* Handle first endpoint */
    float xend = (float)(int)(x0 + 0.5f);
    float yend = y0 + gradient * (xend - x0);
    float xgap = rfpart(x0 + 0.5f);
    int xpxl1 = (int)xend;
    int ypxl1 = (int)yend;
    
    if (steep) {
        put_pixel_alpha(ctx, ypxl1, xpxl1, color, (uint8_t)(rfpart(yend) * xgap * 255));
        put_pixel_alpha(ctx, ypxl1 + 1, xpxl1, color, (uint8_t)(fpart(yend) * xgap * 255));
    } else {
        put_pixel_alpha(ctx, xpxl1, ypxl1, color, (uint8_t)(rfpart(yend) * xgap * 255));
        put_pixel_alpha(ctx, xpxl1, ypxl1 + 1, color, (uint8_t)(fpart(yend) * xgap * 255));
    }
    
    float intery = yend + gradient;
    
    /* Handle second endpoint */
    xend = (float)(int)(x1 + 0.5f);
    yend = y1 + gradient * (xend - x1);
    xgap = fpart(x1 + 0.5f);
    int xpxl2 = (int)xend;
    int ypxl2 = (int)yend;
    
    if (steep) {
        put_pixel_alpha(ctx, ypxl2, xpxl2, color, (uint8_t)(rfpart(yend) * xgap * 255));
        put_pixel_alpha(ctx, ypxl2 + 1, xpxl2, color, (uint8_t)(fpart(yend) * xgap * 255));
    } else {
        put_pixel_alpha(ctx, xpxl2, ypxl2, color, (uint8_t)(rfpart(yend) * xgap * 255));
        put_pixel_alpha(ctx, xpxl2, ypxl2 + 1, color, (uint8_t)(fpart(yend) * xgap * 255));
    }
    
    /* Main loop */
    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            put_pixel_alpha(ctx, (int)intery, x, color, (uint8_t)(rfpart(intery) * 255));
            put_pixel_alpha(ctx, (int)intery + 1, x, color, (uint8_t)(fpart(intery) * 255));
            intery += gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            put_pixel_alpha(ctx, x, (int)intery, color, (uint8_t)(rfpart(intery) * 255));
            put_pixel_alpha(ctx, x, (int)intery + 1, color, (uint8_t)(fpart(intery) * 255));
            intery += gradient;
        }
    }
}

void nxgfx_fill_rounded_rect(nx_context_t* ctx, nx_rect_t r, nx_color_t c, uint32_t rad) {
    if (!ctx) return;
    if (rad == 0) { nxgfx_fill_rect(ctx, r, c); return; }
    nxgfx_fill_rect(ctx, nx_rect(r.x + rad, r.y, r.width - 2*rad, r.height), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y + rad, rad, r.height - 2*rad), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x + r.width - rad, r.y + rad, rad, r.height - 2*rad), c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + rad, r.y + rad}, rad, c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + r.width - rad - 1, r.y + rad}, rad, c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + rad, r.y + r.height - rad - 1}, rad, c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + r.width - rad - 1, r.y + r.height - rad - 1}, rad, c);
}

/* Cubic Bezier using De Casteljau subdivision */
void nxgfx_draw_bezier(nx_context_t* ctx, nx_point_t p0, nx_point_t p1, 
                        nx_point_t p2, nx_point_t p3, nx_color_t color, uint32_t thickness) {
    if (!ctx) return;
    if (thickness == 0) thickness = 1;
    
    /* Calculate number of segments based on curve length approximation */
    float ctrl_len = sqrtf((float)((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y))) +
                     sqrtf((float)((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y))) +
                     sqrtf((float)((p3.x - p2.x) * (p3.x - p2.x) + (p3.y - p2.y) * (p3.y - p2.y)));
    
    int segments = (int)(ctrl_len / 3.0f);
    if (segments < 8) segments = 8;
    if (segments > 128) segments = 128;
    
    float step = 1.0f / segments;
    
    float prev_x = (float)p0.x;
    float prev_y = (float)p0.y;
    
    for (int i = 1; i <= segments; i++) {
        float t = step * i;
        float u = 1.0f - t;
        
        /* Cubic Bezier: B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3 */
        float u2 = u * u;
        float u3 = u2 * u;
        float t2 = t * t;
        float t3 = t2 * t;
        
        float x = u3 * p0.x + 3 * u2 * t * p1.x + 3 * u * t2 * p2.x + t3 * p3.x;
        float y = u3 * p0.y + 3 * u2 * t * p1.y + 3 * u * t2 * p2.y + t3 * p3.y;
        
        nxgfx_draw_line(ctx, (nx_point_t){(int32_t)prev_x, (int32_t)prev_y},
                        (nx_point_t){(int32_t)x, (int32_t)y}, color, thickness);
        
        prev_x = x;
        prev_y = y;
    }
}

/* Quadratic Bezier curve */
void nxgfx_draw_quadratic_bezier(nx_context_t* ctx, nx_point_t p0, nx_point_t p1, 
                                  nx_point_t p2, nx_color_t color, uint32_t thickness) {
    if (!ctx) return;
    if (thickness == 0) thickness = 1;
    
    /* Calculate segments */
    float ctrl_len = sqrtf((float)((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y))) +
                     sqrtf((float)((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y)));
    
    int segments = (int)(ctrl_len / 3.0f);
    if (segments < 8) segments = 8;
    if (segments > 64) segments = 64;
    
    float step = 1.0f / segments;
    
    float prev_x = (float)p0.x;
    float prev_y = (float)p0.y;
    
    for (int i = 1; i <= segments; i++) {
        float t = step * i;
        float u = 1.0f - t;
        
        /* Quadratic Bezier: B(t) = (1-t)²P0 + 2(1-t)tP1 + t²P2 */
        float x = u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x;
        float y = u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y;
        
        nxgfx_draw_line(ctx, (nx_point_t){(int32_t)prev_x, (int32_t)prev_y},
                        (nx_point_t){(int32_t)x, (int32_t)y}, color, thickness);
        
        prev_x = x;
        prev_y = y;
    }
}

void nxgfx_fill_gradient(nx_context_t* ctx, nx_rect_t r, nx_color_t c1, nx_color_t c2, nx_gradient_dir_t dir) {
    if (!ctx || r.width == 0 || r.height == 0) return;
    
    if (dir == NX_GRADIENT_RADIAL) {
        /* Radial gradient: c1 at center, c2 at edges */
        float cx = r.width / 2.0f;
        float cy = r.height / 2.0f;
        float max_dist = sqrtf(cx * cx + cy * cy);
        
        for (uint32_t y = 0; y < r.height; y++) {
            for (uint32_t x = 0; x < r.width; x++) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float t = dist / max_dist;
                if (t > 1.0f) t = 1.0f;
                
                nx_color_t c = {
                    .r = (uint8_t)(c1.r + t * (c2.r - c1.r)),
                    .g = (uint8_t)(c1.g + t * (c2.g - c1.g)),
                    .b = (uint8_t)(c1.b + t * (c2.b - c1.b)),
                    .a = (uint8_t)(c1.a + t * (c2.a - c1.a))
                };
                nxgfx_put_pixel(ctx, r.x + x, r.y + y, c);
            }
        }
        return;
    }
    
    for (uint32_t y = 0; y < r.height; y++) {
        for (uint32_t x = 0; x < r.width; x++) {
            float t = (dir == NX_GRADIENT_H) ? (float)x / r.width :
                      (dir == NX_GRADIENT_V) ? (float)y / r.height :
                      ((float)x + (float)y) / (r.width + r.height);
            nx_color_t c = {
                .r = (uint8_t)(c1.r + t * (c2.r - c1.r)),
                .g = (uint8_t)(c1.g + t * (c2.g - c1.g)),
                .b = (uint8_t)(c1.b + t * (c2.b - c1.b)),
                .a = 255
            };
            nxgfx_put_pixel(ctx, r.x + x, r.y + y, c);
        }
    }
}

void nxgfx_fill_radial_gradient(nx_context_t* ctx, nx_point_t center, uint32_t radius, 
                                 nx_color_t inner, nx_color_t outer) {
    if (!ctx || radius == 0) return;
    
    int32_t r = (int32_t)radius;
    float rf = (float)radius;
    
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            float dist = sqrtf((float)(dx * dx + dy * dy));
            
            if (dist <= rf) {
                float t = dist / rf;
                nx_color_t c = {
                    .r = (uint8_t)(inner.r + t * (outer.r - inner.r)),
                    .g = (uint8_t)(inner.g + t * (outer.g - inner.g)),
                    .b = (uint8_t)(inner.b + t * (outer.b - inner.b)),
                    .a = (uint8_t)(inner.a + t * (outer.a - inner.a))
                };
                
                /* Anti-alias the edge */
                float edge = dist - rf;
                if (edge > -1.5f) {
                    float coverage = 0.5f - edge / 3.0f;
                    if (coverage < 0.0f) continue;
                    uint8_t alpha = (uint8_t)(c.a * coverage);
                    put_pixel_alpha(ctx, center.x + dx, center.y + dy, c, alpha);
                } else {
                    nxgfx_put_pixel(ctx, center.x + dx, center.y + dy, c);
                }
            }
        }
    }
}

/* Draw anti-aliased arc using line segments */
void nxgfx_draw_arc(nx_context_t* ctx, nx_point_t center, uint32_t radius,
                     float start_angle, float end_angle, nx_color_t color, uint32_t thickness) {
    if (!ctx || radius == 0) return;
    if (thickness == 0) thickness = 2;
    
    /* Convert degrees to radians */
    float start_rad = start_angle * 3.14159265f / 180.0f;
    float end_rad = end_angle * 3.14159265f / 180.0f;
    
    /* Calculate number of segments based on arc length */
    float arc_length = fabsf(end_rad - start_rad) * radius;
    int segments = (int)(arc_length / 2.0f);  /* ~2px per segment */
    if (segments < 8) segments = 8;
    if (segments > 64) segments = 64;
    
    float step = (end_rad - start_rad) / segments;
    
    /* Draw arc as connected line segments */
    float angle = start_rad;
    int prev_x = center.x + (int)(radius * cosf(angle));
    int prev_y = center.y + (int)(radius * sinf(angle));
    
    for (int i = 1; i <= segments; i++) {
        angle = start_rad + step * i;
        int curr_x = center.x + (int)(radius * cosf(angle));
        int curr_y = center.y + (int)(radius * sinf(angle));
        
        /* Draw AA line segment */
        nxgfx_draw_line(ctx, (nx_point_t){prev_x, prev_y}, 
                        (nx_point_t){curr_x, curr_y}, color, thickness);
        
        prev_x = curr_x;
        prev_y = curr_y;
    }
}

/* Draw thick arc with proper anti-aliasing */
void nxgfx_draw_thick_arc(nx_context_t* ctx, nx_point_t center, uint32_t radius,
                           float start_angle, float end_angle, nx_color_t color, uint32_t thickness) {
    if (!ctx || radius == 0) return;
    if (thickness < 2) thickness = 2;
    
    /* Draw multiple concentric arcs for thickness */
    uint32_t half_thick = thickness / 2;
    for (uint32_t t = 0; t < thickness; t++) {
        uint32_t r = radius - half_thick + t;
        if (r > 0) {
            nxgfx_draw_arc(ctx, center, r, start_angle, end_angle, color, 1);
        }
    }
}

void nxgfx_set_clip(nx_context_t* ctx, nx_rect_t clip) {
    if (!ctx) return;
    ctx->clip = clip;
    ctx->has_clip = true;
}

void nxgfx_clear_clip(nx_context_t* ctx) {
    if (!ctx) return;
    ctx->has_clip = false;
}

void nxgfx_present(nx_context_t* ctx) { (void)ctx; }

/* ============ IMAGE FUNCTIONS ============ */

struct nx_image {
    uint32_t* data;
    uint32_t width;
    uint32_t height;
    bool owns_data;
};

nx_image_t* nxgfx_image_create(nx_context_t* ctx, const void* data, uint32_t w, uint32_t h) {
    (void)ctx;
    if (!data || w == 0 || h == 0) return NULL;
    
    nx_image_t* img = malloc(sizeof(nx_image_t));
    if (!img) return NULL;
    
    img->width = w;
    img->height = h;
    img->owns_data = true;
    img->data = malloc(w * h * sizeof(uint32_t));
    
    if (!img->data) {
        free(img);
        return NULL;
    }
    
    memcpy(img->data, data, w * h * sizeof(uint32_t));
    return img;
}

void nxgfx_image_destroy(nx_image_t* img) {
    if (!img) return;
    if (img->owns_data && img->data) {
        free(img->data);
    }
    free(img);
}

void nxgfx_draw_image(nx_context_t* ctx, const nx_image_t* img, nx_point_t pos) {
    if (!ctx || !img || !img->data) return;
    
    for (uint32_t y = 0; y < img->height; y++) {
        int32_t dy = pos.y + (int32_t)y;
        if (dy < 0 || (uint32_t)dy >= ctx->height) continue;
        
        for (uint32_t x = 0; x < img->width; x++) {
            int32_t dx = pos.x + (int32_t)x;
            if (dx < 0 || (uint32_t)dx >= ctx->width) continue;
            
            uint32_t px = img->data[y * img->width + x];
            uint8_t a = (px >> 24) & 0xFF;
            
            if (a == 255) {
                uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + dy * ctx->pitch);
                row[dx] = px;
            } else if (a > 0) {
                nx_color_t c = {
                    .r = (px >> 16) & 0xFF,
                    .g = (px >> 8) & 0xFF,
                    .b = px & 0xFF,
                    .a = a
                };
                put_pixel_alpha(ctx, dx, dy, c, a);
            }
        }
    }
}

void nxgfx_draw_image_scaled(nx_context_t* ctx, const nx_image_t* img, nx_rect_t dest) {
    if (!ctx || !img || !img->data) return;
    if (dest.width == 0 || dest.height == 0) return;
    
    /* Fast path: 1:1 scale - no interpolation needed */
    if (dest.width == img->width && dest.height == img->height) {
        nxgfx_draw_image(ctx, img, (nx_point_t){dest.x, dest.y});
        return;
    }
    
    float scale_x = (float)img->width / (float)dest.width;
    float scale_y = (float)img->height / (float)dest.height;
    
    for (uint32_t y = 0; y < dest.height; y++) {
        int32_t dy = dest.y + (int32_t)y;
        if (dy < 0 || (uint32_t)dy >= ctx->height) continue;
        
        uint32_t src_y = (uint32_t)(y * scale_y);
        if (src_y >= img->height) src_y = img->height - 1;
        
        for (uint32_t x = 0; x < dest.width; x++) {
            int32_t dx = dest.x + (int32_t)x;
            if (dx < 0 || (uint32_t)dx >= ctx->width) continue;
            
            uint32_t src_x = (uint32_t)(x * scale_x);
            if (src_x >= img->width) src_x = img->width - 1;
            
            uint32_t px = img->data[src_y * img->width + src_x];
            uint8_t a = (px >> 24) & 0xFF;
            
            if (a == 255) {
                uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + dy * ctx->pitch);
                row[dx] = px;
            } else if (a > 0) {
                nx_color_t c = {
                    .r = (px >> 16) & 0xFF,
                    .g = (px >> 8) & 0xFF,
                    .b = px & 0xFF,
                    .a = a
                };
                put_pixel_alpha(ctx, dx, dy, c, a);
            }
        }
    }
}

/* Bilinear interpolation helper - sample pixel with bounds check */
static uint32_t sample_pixel_safe(const nx_image_t* img, int32_t x, int32_t y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= img->width) x = img->width - 1;
    if ((uint32_t)y >= img->height) y = img->height - 1;
    return img->data[y * img->width + x];
}

/* Bilinear interpolation for high-quality image scaling
 * Eliminates staircase artifacts on icons and UI elements */
void nxgfx_draw_image_scaled_quality(nx_context_t* ctx, const nx_image_t* img, nx_rect_t dest) {
    if (!ctx || !img || !img->data) return;
    if (dest.width == 0 || dest.height == 0) return;
    
    /* Fast path: 1:1 scale - no interpolation needed */
    if (dest.width == img->width && dest.height == img->height) {
        nxgfx_draw_image(ctx, img, (nx_point_t){dest.x, dest.y});
        return;
    }
    
    float scale_x = (float)img->width / (float)dest.width;
    float scale_y = (float)img->height / (float)dest.height;
    
    for (uint32_t y = 0; y < dest.height; y++) {
        int32_t dy = dest.y + (int32_t)y;
        if (dy < 0 || (uint32_t)dy >= ctx->height) continue;
        
        /* Source Y with subpixel position */
        float src_yf = y * scale_y;
        int32_t src_y0 = (int32_t)src_yf;
        int32_t src_y1 = src_y0 + 1;
        float fy = src_yf - src_y0;  /* Fractional part */
        
        for (uint32_t x = 0; x < dest.width; x++) {
            int32_t dx = dest.x + (int32_t)x;
            if (dx < 0 || (uint32_t)dx >= ctx->width) continue;
            
            /* Source X with subpixel position */
            float src_xf = x * scale_x;
            int32_t src_x0 = (int32_t)src_xf;
            int32_t src_x1 = src_x0 + 1;
            float fx = src_xf - src_x0;  /* Fractional part */
            
            /* Sample 4 nearest pixels */
            uint32_t p00 = sample_pixel_safe(img, src_x0, src_y0);
            uint32_t p10 = sample_pixel_safe(img, src_x1, src_y0);
            uint32_t p01 = sample_pixel_safe(img, src_x0, src_y1);
            uint32_t p11 = sample_pixel_safe(img, src_x1, src_y1);
            
            /* Bilinear weights */
            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;
            
            /* Interpolate each channel */
            uint8_t r = (uint8_t)(
                ((p00 >> 16) & 0xFF) * w00 +
                ((p10 >> 16) & 0xFF) * w10 +
                ((p01 >> 16) & 0xFF) * w01 +
                ((p11 >> 16) & 0xFF) * w11
            );
            uint8_t g = (uint8_t)(
                ((p00 >> 8) & 0xFF) * w00 +
                ((p10 >> 8) & 0xFF) * w10 +
                ((p01 >> 8) & 0xFF) * w01 +
                ((p11 >> 8) & 0xFF) * w11
            );
            uint8_t b = (uint8_t)(
                (p00 & 0xFF) * w00 +
                (p10 & 0xFF) * w10 +
                (p01 & 0xFF) * w01 +
                (p11 & 0xFF) * w11
            );
            uint8_t a = (uint8_t)(
                ((p00 >> 24) & 0xFF) * w00 +
                ((p10 >> 24) & 0xFF) * w10 +
                ((p01 >> 24) & 0xFF) * w01 +
                ((p11 >> 24) & 0xFF) * w11
            );
            
            if (a == 0) continue;
            
            if (a >= 250) {
                /* Nearly opaque - direct write */
                uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + dy * ctx->pitch);
                row[dx] = (255 << 24) | (r << 16) | (g << 8) | b;
            } else {
                /* Alpha blend */
                nx_color_t c = { .r = r, .g = g, .b = b, .a = a };
                put_pixel_alpha(ctx, dx, dy, c, a);
            }
        }
    }
}



/* Inner shadow for depth perception (1-2px, subtle) */
void nxgfx_draw_inner_shadow(nx_context_t* ctx, nx_rect_t r, uint32_t radius, 
                              uint8_t shadow_alpha, uint8_t shadow_size) {
    if (!ctx) return;
    if (shadow_size == 0) shadow_size = 2;
    if (shadow_alpha == 0) shadow_alpha = 30;
    
    nx_color_t shadow = {0, 0, 0, shadow_alpha};
    
    /* Top inner shadow (stronger) */
    for (uint8_t i = 0; i < shadow_size; i++) {
        uint8_t a = (uint8_t)(shadow_alpha * (shadow_size - i) / shadow_size);
        shadow.a = a;
        /* Top edge */
        for (uint32_t x = radius; x < r.width - radius; x++) {
            put_pixel_alpha(ctx, r.x + x, r.y + i, shadow, a);
        }
        /* Left edge */
        for (uint32_t y = radius; y < r.height - radius; y++) {
            put_pixel_alpha(ctx, r.x + i, r.y + y, shadow, a / 2);
        }
    }
}

/* Top edge highlight for glass effect */
void nxgfx_draw_top_highlight(nx_context_t* ctx, nx_rect_t r, uint32_t radius,
                               uint8_t highlight_alpha) {
    if (!ctx) return;
    if (highlight_alpha == 0) highlight_alpha = 25;
    
    nx_color_t highlight = {255, 255, 255, highlight_alpha};
    
    /* 1px bright line at top, fading */
    for (uint32_t x = radius + 2; x < r.width - radius - 2; x++) {
        put_pixel_alpha(ctx, r.x + x, r.y + 1, highlight, highlight_alpha);
        put_pixel_alpha(ctx, r.x + x, r.y + 2, highlight, highlight_alpha / 2);
    }
}

/* Focus glow (tight, 2-4px, controlled) */
void nxgfx_draw_focus_glow(nx_context_t* ctx, nx_point_t center, uint32_t radius,
                            nx_color_t glow_color, uint8_t intensity) {
    if (!ctx || radius == 0) return;
    if (intensity == 0) intensity = 40;
    
    /* Tight glow ring: 2-4px outside the shape */
    uint32_t outer = radius + 4;
    float inner_sq = (float)(radius * radius);
    float outer_sq = (float)(outer * outer);
    
    for (int32_t dy = -(int32_t)outer; dy <= (int32_t)outer; dy++) {
        for (int32_t dx = -(int32_t)outer; dx <= (int32_t)outer; dx++) {
            float dist_sq = (float)(dx*dx + dy*dy);
            
            /* Only draw in the glow band (between inner and outer radius) */
            if (dist_sq > inner_sq && dist_sq <= outer_sq) {
                /* Falloff based on distance from inner edge */
                float t = (dist_sq - inner_sq) / (outer_sq - inner_sq);
                uint8_t a = (uint8_t)(intensity * (1.0f - t));
                
                if (a > 0) {
                    nx_color_t c = glow_color;
                    c.a = a;
                    put_pixel_alpha(ctx, center.x + dx, center.y + dy, c, a);
                }
            }
        }
    }
}

/* Bottom shadow for card elevation */
void nxgfx_draw_drop_shadow(nx_context_t* ctx, nx_rect_t r, uint8_t offset_y,
                             uint8_t shadow_alpha, uint8_t blur_size) {
    if (!ctx) return;
    if (offset_y == 0) offset_y = 2;
    if (shadow_alpha == 0) shadow_alpha = 40;
    if (blur_size == 0) blur_size = 4;
    
    nx_color_t shadow = {0, 0, 0, 0};
    
    /* Draw shadow below the card */
    for (uint8_t dy = 0; dy < blur_size; dy++) {
        uint8_t a = (uint8_t)(shadow_alpha * (blur_size - dy) / blur_size);
        shadow.a = a;
        
        int32_t sy = r.y + r.height + offset_y + dy;
        for (uint32_t x = 2; x < r.width - 2; x++) {
            put_pixel_alpha(ctx, r.x + x, sy, shadow, a);
        }
    }
}

/* Translucent card with depth cues (all-in-one) */
void nxgfx_fill_card(nx_context_t* ctx, nx_rect_t r, nx_color_t fill_color,
                      uint32_t radius, bool elevated) {
    if (!ctx) return;
    
    /* Layer 1: Drop shadow (if elevated) - black @ 12% */
    if (elevated) {
        nxgfx_draw_drop_shadow(ctx, r, 2, 30, 3);  /* Subtle, tight shadow */
    }
    
    /* Layer 2: Fill */
    nxgfx_fill_rounded_rect(ctx, r, fill_color, radius);
    
    /* Layer 3: Top highlight - white @ 7% (18/255) for glass effect */
    nx_color_t highlight = {255, 255, 255, 18};
    for (uint32_t x = radius + 2; x < r.width - radius - 2; x++) {
        put_pixel_alpha(ctx, r.x + x, r.y + 1, highlight, 18);
        put_pixel_alpha(ctx, r.x + x, r.y + 2, highlight, 10);
    }
    
    /* Layer 4: Bottom edge shadow - black @ 12% for grounding */
    nx_color_t shadow = {0, 0, 0, 30};
    for (uint32_t x = radius + 2; x < r.width - radius - 2; x++) {
        put_pixel_alpha(ctx, r.x + x, r.y + r.height - 2, shadow, 30);
        put_pixel_alpha(ctx, r.x + x, r.y + r.height - 1, shadow, 20);
    }
    
    /* Layer 5: Left edge subtle shadow - black @ 5% */
    nx_color_t edge_shadow = {0, 0, 0, 12};
    for (uint32_t y = radius + 2; y < r.height - radius - 2; y++) {
        put_pixel_alpha(ctx, r.x + 1, r.y + y, edge_shadow, 12);
    }
}

/* ============ 3D RENDERING FUNCTIONS ============ */

void nxgfx_enable_depth_test(nx_context_t* ctx, bool enable) {
    if (!ctx) return;
    ctx->depth_test_enabled = enable;
    
    /* Allocate depth buffer on first use */
    if (enable && !ctx->depth_buffer) {
        ctx->depth_buffer = malloc(ctx->width * ctx->height * sizeof(float));
        if (ctx->depth_buffer) {
            /* Initialize to max depth (far plane = 1.0) */
            for (uint32_t i = 0; i < ctx->width * ctx->height; i++) {
                ctx->depth_buffer[i] = 1.0f;
            }
        }
    }
}

void nxgfx_clear_depth(nx_context_t* ctx) {
    if (!ctx || !ctx->depth_buffer) return;
    for (uint32_t i = 0; i < ctx->width * ctx->height; i++) {
        ctx->depth_buffer[i] = 1.0f;
    }
}

/* 2D triangle fill using edge function (cross product) */
void nxgfx_fill_triangle(nx_context_t* ctx, nx_point_t p0, nx_point_t p1, nx_point_t p2, nx_color_t color) {
    if (!ctx) return;
    
    /* Find bounding box */
    int32_t min_x = p0.x < p1.x ? (p0.x < p2.x ? p0.x : p2.x) : (p1.x < p2.x ? p1.x : p2.x);
    int32_t max_x = p0.x > p1.x ? (p0.x > p2.x ? p0.x : p2.x) : (p1.x > p2.x ? p1.x : p2.x);
    int32_t min_y = p0.y < p1.y ? (p0.y < p2.y ? p0.y : p2.y) : (p1.y < p2.y ? p1.y : p2.y);
    int32_t max_y = p0.y > p1.y ? (p0.y > p2.y ? p0.y : p2.y) : (p1.y > p2.y ? p1.y : p2.y);
    
    /* Clip to screen */
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int32_t)ctx->width) max_x = ctx->width - 1;
    if (max_y >= (int32_t)ctx->height) max_y = ctx->height - 1;
    
    /* Edge function: returns positive if point is on the left of edge */
    #define EDGE_FN(px, py, ax, ay, bx, by) \
        ((px - ax) * (by - ay) - (py - ay) * (bx - ax))
    
    /* Precompute edge function area (twice the triangle area) */
    float area = (float)EDGE_FN(p2.x, p2.y, p0.x, p0.y, p1.x, p1.y);
    if (fabsf(area) < 0.001f) return; /* Degenerate triangle */
    
    /* Rasterize */
    for (int32_t y = min_y; y <= max_y; y++) {
        for (int32_t x = min_x; x <= max_x; x++) {
            /* Compute barycentric coordinates using edge functions */
            float w0 = (float)EDGE_FN(x, y, p1.x, p1.y, p2.x, p2.y);
            float w1 = (float)EDGE_FN(x, y, p2.x, p2.y, p0.x, p0.y);
            float w2 = (float)EDGE_FN(x, y, p0.x, p0.y, p1.x, p1.y);
            
            /* Check if point is inside triangle (all same sign) */
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                nxgfx_put_pixel(ctx, x, y, color);
            }
        }
    }
    
    #undef EDGE_FN
}

/* 3D triangle with depth testing */
void nxgfx_fill_triangle_3d(nx_context_t* ctx, 
                             float x0, float y0, float z0,
                             float x1, float y1, float z1,
                             float x2, float y2, float z2,
                             nx_color_t color) {
    if (!ctx) return;
    
    /* Find 2D bounding box */
    int32_t min_x = (int32_t)(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
    int32_t max_x = (int32_t)(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2));
    int32_t min_y = (int32_t)(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
    int32_t max_y = (int32_t)(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2));
    
    /* Clip to screen */
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int32_t)ctx->width) max_x = ctx->width - 1;
    if (max_y >= (int32_t)ctx->height) max_y = ctx->height - 1;
    
    /* Edge function */
    #define EDGE_FN_F(px, py, ax, ay, bx, by) \
        ((px - ax) * (by - ay) - (py - ay) * (bx - ax))
    
    /* Triangle area (2x) */
    float area = EDGE_FN_F(x2, y2, x0, y0, x1, y1);
    if (fabsf(area) < 0.001f) return;
    float inv_area = 1.0f / area;
    
    /* Rasterize with depth test */
    for (int32_t y = min_y; y <= max_y; y++) {
        for (int32_t x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            
            float w0 = EDGE_FN_F(px, py, x1, y1, x2, y2) * inv_area;
            float w1 = EDGE_FN_F(px, py, x2, y2, x0, y0) * inv_area;
            float w2 = 1.0f - w0 - w1;
            
            /* Check if inside triangle */
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                /* Interpolate depth */
                float z = w0 * z0 + w1 * z1 + w2 * z2;
                
                /* Depth test */
                if (ctx->depth_test_enabled && ctx->depth_buffer) {
                    uint32_t idx = y * ctx->width + x;
                    if (z < ctx->depth_buffer[idx]) {
                        ctx->depth_buffer[idx] = z;
                        nxgfx_put_pixel(ctx, x, y, color);
                    }
                } else {
                    nxgfx_put_pixel(ctx, x, y, color);
                }
            }
        }
    }
    
    #undef EDGE_FN_F
}

/* ============ ELLIPSE AND RING ============ */

void nxgfx_fill_ellipse(nx_context_t* ctx, nx_point_t center, uint32_t rx, uint32_t ry, nx_color_t color) {
    if (!ctx || rx == 0 || ry == 0) return;
    
    int32_t irx = (int32_t)rx;
    int32_t iry = (int32_t)ry;
    float rxf = (float)rx;
    float ryf = (float)ry;
    
    for (int32_t dy = -iry - 1; dy <= iry + 1; dy++) {
        for (int32_t dx = -irx - 1; dx <= irx + 1; dx++) {
            /* Ellipse equation: (x/rx)² + (y/ry)² - 1 */
            float nx = (float)dx / rxf;
            float ny = (float)dy / ryf;
            float dist = nx * nx + ny * ny;
            float edge = sqrtf(dist) - 1.0f;
            
            if (edge < -0.05f) {
                nxgfx_put_pixel(ctx, center.x + dx, center.y + dy, color);
            } else if (edge < 0.05f) {
                float coverage = 0.5f - edge * 10.0f;
                if (coverage > 0.0f && coverage <= 1.0f) {
                    put_pixel_alpha(ctx, center.x + dx, center.y + dy, color, 
                                    (uint8_t)(coverage * color.a));
                }
            }
        }
    }
}

void nxgfx_draw_ellipse(nx_context_t* ctx, nx_point_t center, uint32_t rx, uint32_t ry, 
                         nx_color_t color, uint32_t stroke) {
    if (!ctx || rx == 0 || ry == 0) return;
    if (stroke == 0) stroke = 1;
    
    /* Convert to parametric form and draw as connected line segments */
    int segments = (int)(3.14159f * (float)(rx + ry) / 2.0f);
    if (segments < 32) segments = 32;
    if (segments > 128) segments = 128;
    
    float step = 6.28318f / segments;
    float prev_x = center.x + (float)rx;
    float prev_y = (float)center.y;
    
    for (int i = 1; i <= segments; i++) {
        float angle = step * i;
        float curr_x = center.x + rx * cosf(angle);
        float curr_y = center.y + ry * sinf(angle);
        
        nxgfx_draw_line(ctx, (nx_point_t){(int32_t)prev_x, (int32_t)prev_y},
                        (nx_point_t){(int32_t)curr_x, (int32_t)curr_y}, color, stroke);
        
        prev_x = curr_x;
        prev_y = curr_y;
    }
}

void nxgfx_draw_ring(nx_context_t* ctx, nx_point_t center, uint32_t outer_r, uint32_t inner_r, 
                      nx_color_t color) {
    if (!ctx || outer_r == 0 || inner_r >= outer_r) return;
    
    int32_t r = (int32_t)outer_r;
    float outer_rf = (float)outer_r;
    float inner_rf = (float)inner_r;
    
    for (int32_t dy = -r - 1; dy <= r + 1; dy++) {
        for (int32_t dx = -r - 1; dx <= r + 1; dx++) {
            float dist = sqrtf((float)(dx*dx + dy*dy));
            
            /* Check if in ring */
            float outer_edge = dist - outer_rf;
            float inner_edge = inner_rf - dist;
            
            if (outer_edge < -1.0f && inner_edge < -1.0f) {
                nxgfx_put_pixel(ctx, center.x + dx, center.y + dy, color);
            } else if (outer_edge < 1.0f && inner_edge < 1.0f) {
                float outer_cov = (outer_edge < -1.0f) ? 1.0f : 
                                  (outer_edge > 1.0f) ? 0.0f : 0.5f - outer_edge * 0.5f;
                float inner_cov = (inner_edge < -1.0f) ? 1.0f :
                                  (inner_edge > 1.0f) ? 0.0f : 0.5f - inner_edge * 0.5f;
                float coverage = outer_cov * inner_cov;
                if (coverage > 0.0f) {
                    put_pixel_alpha(ctx, center.x + dx, center.y + dy, color,
                                    (uint8_t)(coverage * color.a));
                }
            }
        }
    }
}

/* ============ PATH BUILDER ============ */

#define NX_PATH_CMD_MOVE  0
#define NX_PATH_CMD_LINE  1
#define NX_PATH_CMD_QUAD  2
#define NX_PATH_CMD_CUBIC 3
#define NX_PATH_CMD_CLOSE 4

typedef struct {
    uint8_t type;
    float x, y;          /* End point */
    float c1x, c1y;      /* Control point 1 */
    float c2x, c2y;      /* Control point 2 (cubic only) */
} nx_path_cmd_t;

struct nx_path {
    nx_path_cmd_t* cmds;
    uint32_t count;
    uint32_t capacity;
    float start_x, start_y;  /* Start of current subpath */
    float last_x, last_y;    /* Current position */
};

nx_path_t* nxgfx_path_create(void) {
    nx_path_t* path = malloc(sizeof(nx_path_t));
    if (!path) return NULL;
    path->capacity = 64;
    path->cmds = malloc(path->capacity * sizeof(nx_path_cmd_t));
    if (!path->cmds) { free(path); return NULL; }
    path->count = 0;
    path->start_x = path->start_y = 0;
    path->last_x = path->last_y = 0;
    return path;
}

void nxgfx_path_destroy(nx_path_t* path) {
    if (!path) return;
    if (path->cmds) free(path->cmds);
    free(path);
}

static void path_ensure_capacity(nx_path_t* path) {
    if (path->count >= path->capacity) {
        path->capacity *= 2;
        path->cmds = realloc(path->cmds, path->capacity * sizeof(nx_path_cmd_t));
    }
}

void nxgfx_path_move_to(nx_path_t* path, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path);
    path->cmds[path->count++] = (nx_path_cmd_t){ .type = NX_PATH_CMD_MOVE, .x = x, .y = y };
    path->start_x = path->last_x = x;
    path->start_y = path->last_y = y;
}

void nxgfx_path_line_to(nx_path_t* path, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path);
    path->cmds[path->count++] = (nx_path_cmd_t){ .type = NX_PATH_CMD_LINE, .x = x, .y = y };
    path->last_x = x;
    path->last_y = y;
}

void nxgfx_path_quad_to(nx_path_t* path, float cx, float cy, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path);
    path->cmds[path->count++] = (nx_path_cmd_t){ 
        .type = NX_PATH_CMD_QUAD, .x = x, .y = y, .c1x = cx, .c1y = cy 
    };
    path->last_x = x;
    path->last_y = y;
}

void nxgfx_path_cubic_to(nx_path_t* path, float c1x, float c1y, float c2x, float c2y, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path);
    path->cmds[path->count++] = (nx_path_cmd_t){ 
        .type = NX_PATH_CMD_CUBIC, .x = x, .y = y, 
        .c1x = c1x, .c1y = c1y, .c2x = c2x, .c2y = c2y 
    };
    path->last_x = x;
    path->last_y = y;
}

void nxgfx_path_close(nx_path_t* path) {
    if (!path) return;
    path_ensure_capacity(path);
    path->cmds[path->count++] = (nx_path_cmd_t){ 
        .type = NX_PATH_CMD_CLOSE, .x = path->start_x, .y = path->start_y 
    };
    path->last_x = path->start_x;
    path->last_y = path->start_y;
}

/* Flatten path to polygon points for fill */
static nx_point_t* path_flatten(const nx_path_t* path, uint32_t* out_count) {
    if (!path || path->count == 0) { *out_count = 0; return NULL; }
    
    /* Estimate max points */
    uint32_t max_points = path->count * 32;
    nx_point_t* points = malloc(max_points * sizeof(nx_point_t));
    if (!points) { *out_count = 0; return NULL; }
    
    uint32_t count = 0;
    float cur_x = 0, cur_y = 0;
    
    for (uint32_t i = 0; i < path->count && count < max_points - 1; i++) {
        nx_path_cmd_t* cmd = &path->cmds[i];
        
        switch (cmd->type) {
            case NX_PATH_CMD_MOVE:
                cur_x = cmd->x;
                cur_y = cmd->y;
                points[count++] = (nx_point_t){ (int32_t)cur_x, (int32_t)cur_y };
                break;
                
            case NX_PATH_CMD_LINE:
            case NX_PATH_CMD_CLOSE:
                cur_x = cmd->x;
                cur_y = cmd->y;
                points[count++] = (nx_point_t){ (int32_t)cur_x, (int32_t)cur_y };
                break;
                
            case NX_PATH_CMD_QUAD: {
                /* Flatten quadratic bezier to line segments */
                float x0 = cur_x, y0 = cur_y;
                int segs = 8;
                for (int s = 1; s <= segs && count < max_points; s++) {
                    float t = (float)s / segs;
                    float u = 1.0f - t;
                    float x = u*u*x0 + 2*u*t*cmd->c1x + t*t*cmd->x;
                    float y = u*u*y0 + 2*u*t*cmd->c1y + t*t*cmd->y;
                    points[count++] = (nx_point_t){ (int32_t)x, (int32_t)y };
                }
                cur_x = cmd->x;
                cur_y = cmd->y;
                break;
            }
                
            case NX_PATH_CMD_CUBIC: {
                /* Flatten cubic bezier to line segments */
                float x0 = cur_x, y0 = cur_y;
                int segs = 12;
                for (int s = 1; s <= segs && count < max_points; s++) {
                    float t = (float)s / segs;
                    float u = 1.0f - t;
                    float u2 = u*u, u3 = u2*u;
                    float t2 = t*t, t3 = t2*t;
                    float x = u3*x0 + 3*u2*t*cmd->c1x + 3*u*t2*cmd->c2x + t3*cmd->x;
                    float y = u3*y0 + 3*u2*t*cmd->c1y + 3*u*t2*cmd->c2y + t3*cmd->y;
                    points[count++] = (nx_point_t){ (int32_t)x, (int32_t)y };
                }
                cur_x = cmd->x;
                cur_y = cmd->y;
                break;
            }
        }
    }
    
    *out_count = count;
    return points;
}

void nxgfx_path_fill(nx_context_t* ctx, const nx_path_t* path, nx_color_t color) {
    if (!ctx || !path) return;
    
    uint32_t count;
    nx_point_t* points = path_flatten(path, &count);
    if (!points || count < 3) { if (points) free(points); return; }
    
    nxgfx_fill_polygon(ctx, points, count, color);
    free(points);
}

void nxgfx_path_stroke(nx_context_t* ctx, const nx_path_t* path, nx_color_t color, uint32_t width) {
    if (!ctx || !path || path->count == 0) return;
    if (width == 0) width = 1;
    
    float cur_x = 0, cur_y = 0;
    
    for (uint32_t i = 0; i < path->count; i++) {
        nx_path_cmd_t* cmd = &path->cmds[i];
        
        switch (cmd->type) {
            case NX_PATH_CMD_MOVE:
                cur_x = cmd->x;
                cur_y = cmd->y;
                break;
                
            case NX_PATH_CMD_LINE:
            case NX_PATH_CMD_CLOSE:
                nxgfx_draw_line(ctx, (nx_point_t){(int32_t)cur_x, (int32_t)cur_y},
                                (nx_point_t){(int32_t)cmd->x, (int32_t)cmd->y}, color, width);
                cur_x = cmd->x;
                cur_y = cmd->y;
                break;
                
            case NX_PATH_CMD_QUAD:
                nxgfx_draw_quadratic_bezier(ctx, 
                    (nx_point_t){(int32_t)cur_x, (int32_t)cur_y},
                    (nx_point_t){(int32_t)cmd->c1x, (int32_t)cmd->c1y},
                    (nx_point_t){(int32_t)cmd->x, (int32_t)cmd->y}, color, width);
                cur_x = cmd->x;
                cur_y = cmd->y;
                break;
                
            case NX_PATH_CMD_CUBIC:
                nxgfx_draw_bezier(ctx,
                    (nx_point_t){(int32_t)cur_x, (int32_t)cur_y},
                    (nx_point_t){(int32_t)cmd->c1x, (int32_t)cmd->c1y},
                    (nx_point_t){(int32_t)cmd->c2x, (int32_t)cmd->c2y},
                    (nx_point_t){(int32_t)cmd->x, (int32_t)cmd->y}, color, width);
                cur_x = cmd->x;
                cur_y = cmd->y;
                break;
        }
    }
}
