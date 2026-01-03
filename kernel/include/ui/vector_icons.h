/*
 * NeolyxOS Vector Icons
 * 
 * High-quality scalable icons using path-based rendering
 * Renders at any resolution with anti-aliasing for 5K quality
 * 
 * Format: Each icon is defined as a series of path commands
 * that are rasterized at the target size with sub-pixel accuracy.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef VECTOR_ICONS_H
#define VECTOR_ICONS_H

#include <stdint.h>

/* Vector path command types */
typedef enum {
    VEC_END = 0,      /* End of path */
    VEC_MOVE,         /* Move to (x, y) */
    VEC_LINE,         /* Line to (x, y) */
    VEC_RECT,         /* Filled rectangle (x, y, w, h) */
    VEC_CIRCLE,       /* Filled circle (cx, cy, r) */
    VEC_RRECT,        /* Rounded rect (x, y, w, h, r) */
    VEC_ARC,          /* Arc (cx, cy, r, start_angle, end_angle) */
} vec_cmd_t;

/* Path command structure */
typedef struct {
    uint8_t cmd;      /* Command type */
    int16_t args[5];  /* Command arguments (scaled 0-1000 for 0.0-1.0) */
} vec_path_t;

/* Anti-aliasing quality levels */
#define AA_NONE   1   /* No anti-aliasing (fastest) */
#define AA_LOW    2   /* 2x sampling */
#define AA_MEDIUM 4   /* 4x sampling */
#define AA_HIGH   8   /* 8x sampling (best quality) */

/* ============ Drawing helpers with anti-aliasing ============ */

/* Alpha blend two colors */
static inline uint32_t vec_blend(uint32_t fg, uint32_t bg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;
    
    int r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * (255 - alpha)) >> 8;
    int g = (((fg >> 8)  & 0xFF) * alpha + ((bg >> 8)  & 0xFF) * (255 - alpha)) >> 8;
    int b = ((fg        & 0xFF) * alpha + (bg        & 0xFF) * (255 - alpha)) >> 8;
    return (r << 16) | (g << 8) | b;
}

/* Draw anti-aliased pixel with coverage */
static inline void vec_pixel_aa(volatile uint32_t *fb, uint32_t pitch,
                                 int x, int y, uint32_t color, int coverage, 
                                 int max_x, int max_y) {
    if (x < 0 || y < 0 || x >= max_x || y >= max_y) return;
    if (coverage <= 0) return;
    
    uint32_t *p = (uint32_t*)&fb[y * (pitch / 4) + x];
    if (coverage >= 255) {
        *p = color;
    } else {
        *p = vec_blend(color, *p, coverage);
    }
}

/* Draw filled rectangle with sub-pixel accuracy */
static void vec_fill_rect_aa(volatile uint32_t *fb, uint32_t pitch,
                              float x, float y, float w, float h,
                              uint32_t color, int max_x, int max_y) {
    /* Integer bounds */
    int ix = (int)x;
    int iy = (int)y;
    int iw = (int)(x + w) - ix + 1;
    int ih = (int)(y + h) - iy + 1;
    
    /* Sub-pixel offsets for anti-aliasing */
    float fx = x - ix;
    float fy = y - iy;
    float fx2 = (x + w) - (int)(x + w);
    float fy2 = (y + h) - (int)(y + h);
    
    for (int py = iy; py < iy + ih && py < max_y; py++) {
        for (int px = ix; px < ix + iw && px < max_x; px++) {
            if (px < 0 || py < 0) continue;
            
            /* Calculate coverage for this pixel */
            int coverage = 255;
            
            /* Edge anti-aliasing */
            if (px == ix) coverage = (int)((1.0f - fx) * 255);
            if (px == ix + iw - 1) coverage = (int)(fx2 * 255);
            if (py == iy) coverage = coverage * (int)((1.0f - fy) * 255) / 255;
            if (py == iy + ih - 1) coverage = coverage * (int)(fy2 * 255) / 255;
            
            vec_pixel_aa(fb, pitch, px, py, color, coverage, max_x, max_y);
        }
    }
}

/* Draw filled circle with anti-aliasing */
static void vec_fill_circle_aa(volatile uint32_t *fb, uint32_t pitch,
                                float cx, float cy, float r,
                                uint32_t color, int max_x, int max_y) {
    int x0 = (int)(cx - r) - 1;
    int y0 = (int)(cy - r) - 1;
    int x1 = (int)(cx + r) + 2;
    int y1 = (int)(cy + r) + 2;
    
    float r2 = r * r;
    
    for (int py = y0; py < y1 && py < max_y; py++) {
        for (int px = x0; px < x1 && px < max_x; px++) {
            if (px < 0 || py < 0) continue;
            
            /* Distance from center */
            float dx = (float)px + 0.5f - cx;
            float dy = (float)py + 0.5f - cy;
            float d2 = dx * dx + dy * dy;
            
            /* Calculate coverage */
            int coverage = 0;
            if (d2 < r2 - r) {
                coverage = 255;  /* Fully inside */
            } else if (d2 < r2 + r) {
                /* Anti-aliased edge */
                float d = d2 - r2;
                coverage = 255 - (int)((d / (r * 2)) * 255);
                if (coverage < 0) coverage = 0;
                if (coverage > 255) coverage = 255;
            }
            
            if (coverage > 0) {
                vec_pixel_aa(fb, pitch, px, py, color, coverage, max_x, max_y);
            }
        }
    }
}

/* ============ Vector Icon Definitions ============ */

/* Boot icon - stylized power button */
static const vec_path_t vec_icon_boot[] = {
    {VEC_CIRCLE, {500, 500, 400, 0, 0}},  /* Outer circle */
    {VEC_CIRCLE, {500, 500, 280, 0, 0}},  /* Inner bg (will be cut) */
    {VEC_RECT,   {450, 100, 100, 450, 0}}, /* Power line */
    {VEC_END,    {0, 0, 0, 0, 0}}
};

/* Settings icon - gear shape */
static const vec_path_t vec_icon_settings[] = {
    {VEC_CIRCLE, {500, 500, 450, 0, 0}},  /* Outer */
    {VEC_CIRCLE, {500, 500, 200, 0, 0}},  /* Inner hole */
    /* Gear teeth would go here */
    {VEC_END,    {0, 0, 0, 0, 0}}
};

/* Disk icon - hard drive shape */
static const vec_path_t vec_icon_disk[] = {
    {VEC_RRECT,  {100, 200, 800, 600, 80}},  /* Drive body */
    {VEC_RECT,   {150, 650, 400, 100, 0}},    /* Base plate */
    {VEC_CIRCLE, {700, 450, 100, 0, 0}},      /* Activity LED */
    {VEC_END,    {0, 0, 0, 0, 0}}
};

/* Terminal icon - console */
static const vec_path_t vec_icon_terminal[] = {
    {VEC_RRECT,  {50, 50, 900, 900, 80}},    /* Window frame */
    {VEC_RECT,   {50, 50, 900, 100, 0}},      /* Title bar */
    {VEC_RECT,   {150, 250, 200, 40, 0}},     /* Prompt > */
    {VEC_RECT,   {400, 250, 350, 40, 0}},     /* Cursor line */
    {VEC_END,    {0, 0, 0, 0, 0}}
};

/* ============ Vector Rendering ============ */

/*
 * Render a vector icon at specified size with anti-aliasing
 * 
 * @param fb       Framebuffer pointer
 * @param pitch    Framebuffer pitch in bytes
 * @param x, y     Top-left position
 * @param size     Icon size in pixels (e.g., 32, 48, 64, 128)
 * @param icon     Pointer to vector path array
 * @param fg_color Foreground/fill color
 * @param bg_color Background color (for inner shapes)
 * @param max_x    Framebuffer width for clipping
 * @param max_y    Framebuffer height for clipping
 */
static void render_vector_icon(volatile uint32_t *fb, uint32_t pitch,
                                int x, int y, int size,
                                const vec_path_t *icon,
                                uint32_t fg_color, uint32_t bg_color,
                                int max_x, int max_y) {
    /* Scale factor: icon coordinates are 0-1000 */
    float scale = (float)size / 1000.0f;
    int color_toggle = 0;  /* Alternate fg/bg for inner shapes */
    
    const vec_path_t *cmd = icon;
    while (cmd->cmd != VEC_END) {
        uint32_t color = (color_toggle++ % 2 == 0) ? fg_color : bg_color;
        
        switch (cmd->cmd) {
            case VEC_RECT:
                vec_fill_rect_aa(fb, pitch,
                    x + cmd->args[0] * scale,
                    y + cmd->args[1] * scale,
                    cmd->args[2] * scale,
                    cmd->args[3] * scale,
                    fg_color, max_x, max_y);
                break;
                
            case VEC_CIRCLE:
                vec_fill_circle_aa(fb, pitch,
                    x + cmd->args[0] * scale,
                    y + cmd->args[1] * scale,
                    cmd->args[2] * scale,
                    color, max_x, max_y);
                break;
                
            case VEC_RRECT: {
                /* Approximate rounded rect with rect + corner circles */
                float rx = x + cmd->args[0] * scale;
                float ry = y + cmd->args[1] * scale;
                float rw = cmd->args[2] * scale;
                float rh = cmd->args[3] * scale;
                float rr = cmd->args[4] * scale;
                
                /* Main rectangles */
                vec_fill_rect_aa(fb, pitch, rx + rr, ry, rw - rr*2, rh, fg_color, max_x, max_y);
                vec_fill_rect_aa(fb, pitch, rx, ry + rr, rw, rh - rr*2, fg_color, max_x, max_y);
                
                /* Corner circles */
                vec_fill_circle_aa(fb, pitch, rx + rr, ry + rr, rr, fg_color, max_x, max_y);
                vec_fill_circle_aa(fb, pitch, rx + rw - rr, ry + rr, rr, fg_color, max_x, max_y);
                vec_fill_circle_aa(fb, pitch, rx + rr, ry + rh - rr, rr, fg_color, max_x, max_y);
                vec_fill_circle_aa(fb, pitch, rx + rw - rr, ry + rh - rr, rr, fg_color, max_x, max_y);
                break;
            }
            
            default:
                break;
        }
        cmd++;
    }
}

/*
 * Render a vector icon with glow effect for 5K quality
 */
static void render_vector_icon_glow(volatile uint32_t *fb, uint32_t pitch,
                                     int x, int y, int size,
                                     const vec_path_t *icon,
                                     uint32_t color,
                                     int max_x, int max_y) {
    /* Draw subtle glow layers */
    uint32_t glow1 = vec_blend(color, 0x00000000, 32);
    uint32_t glow2 = vec_blend(color, 0x00000000, 64);
    uint32_t bg = 0x00281830;
    
    render_vector_icon(fb, pitch, x + 2, y + 2, size, icon, glow1, bg, max_x, max_y);
    render_vector_icon(fb, pitch, x + 1, y + 1, size, icon, glow2, bg, max_x, max_y);
    
    /* Main icon on top */
    render_vector_icon(fb, pitch, x, y, size, icon, color, bg, max_x, max_y);
}

#endif /* VECTOR_ICONS_H */
