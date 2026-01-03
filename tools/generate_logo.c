/* Copyright (c) 2025 Swanaya Gupta
 * KETIVEEAI License v1.1 - Always Free.
 * See LICENSE in repo root.
 */

/*
 * NeolyxOS Logo BMP Generator
 * 
 * Creates a BMP splash screen logo for the bootloader.
 * The logo is rendered at 256x256 or custom resolution.
 * 
 * Compile: gcc -o gen_logo generate_logo.c -lm
 * Usage: ./gen_logo [output.bmp] [width] [height]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#pragma pack(push, 1)

/* BMP File Header */
typedef struct {
    uint16_t type;          /* 'BM' */
    uint32_t size;          /* File size */
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;        /* Offset to pixel data */
} bmp_file_header_t;

/* BMP Info Header */
typedef struct {
    uint32_t size;          /* Header size (40) */
    int32_t  width;
    int32_t  height;
    uint16_t planes;        /* Always 1 */
    uint16_t bpp;           /* Bits per pixel */
    uint32_t compression;   /* 0 = none */
    uint32_t image_size;
    int32_t  x_ppm;
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;

#pragma pack(pop)

/* Color */
typedef struct {
    uint8_t b, g, r;
} pixel_t;

/* Image buffer */
typedef struct {
    int width, height;
    pixel_t *pixels;
} image_t;

/* Create image */
static image_t* create_image(int w, int h) {
    image_t *img = (image_t*)malloc(sizeof(image_t));
    img->width = w;
    img->height = h;
    img->pixels = (pixel_t*)calloc(w * h, sizeof(pixel_t));
    return img;
}

/* Free image */
static void free_image(image_t *img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        free(img);
    }
}

/* Set pixel */
static void set_pixel(image_t *img, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= 0 && x < img->width && y >= 0 && y < img->height) {
        int idx = y * img->width + x;
        img->pixels[idx].r = r;
        img->pixels[idx].g = g;
        img->pixels[idx].b = b;
    }
}

/* Blend pixel with alpha */
static void blend_pixel(image_t *img, int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (x >= 0 && x < img->width && y >= 0 && y < img->height) {
        int idx = y * img->width + x;
        img->pixels[idx].r = (uint8_t)(img->pixels[idx].r * (1 - alpha) + r * alpha);
        img->pixels[idx].g = (uint8_t)(img->pixels[idx].g * (1 - alpha) + g * alpha);
        img->pixels[idx].b = (uint8_t)(img->pixels[idx].b * (1 - alpha) + b * alpha);
    }
}

/* Fill rectangle */
static void fill_rect(image_t *img, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            set_pixel(img, px, py, r, g, b);
        }
    }
}

/* Draw filled circle with antialiasing */
static void fill_circle(image_t *img, float cx, float cy, float radius, 
                         uint8_t r, uint8_t g, uint8_t b) {
    int x0 = (int)(cx - radius - 2);
    int y0 = (int)(cy - radius - 2);
    int x1 = (int)(cx + radius + 2);
    int y1 = (int)(cy + radius + 2);
    
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = px - cx;
            float dy = py - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            
            if (dist <= radius - 0.7f) {
                set_pixel(img, px, py, r, g, b);
            } else if (dist < radius + 0.7f) {
                float alpha = 1.0f - (dist - radius + 0.7f) / 1.4f;
                blend_pixel(img, px, py, r, g, b, alpha);
            }
        }
    }
}

/* Draw line with antialiasing */
static void draw_line(image_t *img, float x0, float y0, float x1, float y1, 
                       float width, uint8_t r, uint8_t g, uint8_t b) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    
    dx /= len;
    dy /= len;
    
    float nx = -dy;
    float ny = dx;
    
    for (float t = 0; t <= len; t += 0.5f) {
        float cx = x0 + dx * t;
        float cy = y0 + dy * t;
        
        for (float w = -width/2; w <= width/2; w += 0.5f) {
            int px = (int)(cx + nx * w);
            int py = (int)(cy + ny * w);
            
            float alpha = 1.0f - fabsf(w) / (width/2);
            blend_pixel(img, px, py, r, g, b, alpha);
        }
    }
}

/* Draw NeolyxOS "N" logo */
static void draw_neolyx_logo(image_t *img, int offset_x, int offset_y, float scale) {
    /* Colors - white logo on dark purple background */
    uint8_t logo_r = 255, logo_g = 255, logo_b = 255;
    
    /* The NeolyxOS logo is an abstract "N" shape */
    /* Original SVG is 99x133 */
    float orig_w = 99.0f;
    float orig_h = 133.0f;
    
    /* Scale and center */
    float s = scale;
    int cx = offset_x;
    int cy = offset_y;
    
    /* Main shapes approximated from SVG paths */
    
    /* Left vertical stroke */
    float lx1 = 0 * s + cx;
    float ly1 = 60 * s + cy;
    float lx2 = 31 * s + cx;
    float ly2 = 48 * s + cy;
    float lx3 = 32 * s + cx;
    float ly3 = 103 * s + cy;
    float lx4 = 0 * s + cx;
    float ly4 = 124 * s + cy;
    
    /* Draw left part as triangle fill */
    for (int py = (int)ly2; py < (int)ly4; py++) {
        float t = (float)(py - ly2) / (ly4 - ly2);
        int x_start = (int)(lx2 + (lx4 - lx2) * t);
        int x_end = (int)(lx3 + (lx4 - lx3) * t);
        for (int px = x_start - 5; px <= x_end + 5; px++) {
            blend_pixel(img, px, py, logo_r, logo_g, logo_b, 0.9f);
        }
    }
    
    /* Right vertical stroke */
    float rx1 = 64 * s + cx;
    float ry1 = 72 * s + cy;
    float rx2 = 96 * s + cx;
    float ry2 = 99 * s + cy;
    float rx3 = 95 * s + cx;
    float ry3 = 29 * s + cy;
    float rx4 = 62 * s + cx;
    float ry4 = 9 * s + cy;
    
    /* Draw right part */
    for (int py = (int)ry4; py < (int)ry2; py++) {
        float t = (float)(py - ry4) / (ry2 - ry4);
        int x_start = (int)(rx4 + (rx1 - rx4) * t);
        int x_end = (int)(rx4 + 5 + (rx2 - rx4) * t);
        for (int px = x_start; px <= x_end; px++) {
            blend_pixel(img, px, py, logo_r, logo_g, logo_b, 0.9f);
        }
    }
    
    /* Diagonal connecting stroke */
    draw_line(img, 42 * s + cx, 57 * s + cy, 68 * s + cx, 115 * s + cy, 
              20 * s, logo_r, logo_g, logo_b);
    draw_line(img, 24 * s + cx, 43 * s + cy, 96 * s + cx, 86 * s + cy,
              15 * s, logo_r, logo_g, logo_b);
    
    /* Bottom left circle accent */
    fill_circle(img, 17 * s + cx, 115 * s + cy, 12 * s, logo_r, logo_g, logo_b);
    
    /* Top right circle accent */
    fill_circle(img, 80 * s + cx, 16 * s + cy, 12 * s, logo_r, logo_g, logo_b);
    
    /* Left arrow/accent */
    draw_line(img, 0 * s + cx, 63 * s + cy, 24 * s + cx, 43 * s + cy,
              10 * s, logo_r, logo_g, logo_b);
}

/* Add gradient background */
static void draw_gradient_bg(image_t *img) {
    /* Dark purple gradient: #1a1a2e to #16213e */
    for (int y = 0; y < img->height; y++) {
        float t = (float)y / img->height;
        uint8_t r = (uint8_t)(26 + (22 - 26) * t);
        uint8_t g = (uint8_t)(26 + (33 - 26) * t);
        uint8_t b = (uint8_t)(46 + (62 - 46) * t);
        
        for (int x = 0; x < img->width; x++) {
            set_pixel(img, x, y, r, g, b);
        }
    }
}

/* Add subtle glow around logo */
static void add_glow(image_t *img, int cx, int cy, int radius) {
    /* Purple glow */
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            
            if (dist < radius * 2) {
                float intensity = 1.0f - dist / (radius * 2);
                intensity = intensity * intensity * 0.15f;
                
                /* Add purple tint */
                blend_pixel(img, x, y, 100, 80, 180, intensity);
            }
        }
    }
}

/* Save BMP file */
static int save_bmp(const char *filename, image_t *img) {
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
    
    int row_size = ((img->width * 3 + 3) / 4) * 4;
    int image_size = row_size * img->height;
    int file_size = 54 + image_size;
    
    bmp_file_header_t fh = {
        .type = 0x4D42,
        .size = file_size,
        .reserved1 = 0,
        .reserved2 = 0,
        .offset = 54
    };
    
    bmp_info_header_t ih = {
        .size = 40,
        .width = img->width,
        .height = img->height,
        .planes = 1,
        .bpp = 24,
        .compression = 0,
        .image_size = image_size,
        .x_ppm = 2835,
        .y_ppm = 2835,
        .colors_used = 0,
        .colors_important = 0
    };
    
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    
    /* Write pixel data (bottom-up) */
    uint8_t *row = (uint8_t*)malloc(row_size);
    
    for (int y = img->height - 1; y >= 0; y--) {
        memset(row, 0, row_size);
        for (int x = 0; x < img->width; x++) {
            pixel_t *p = &img->pixels[y * img->width + x];
            row[x * 3 + 0] = p->b;
            row[x * 3 + 1] = p->g;
            row[x * 3 + 2] = p->r;
        }
        fwrite(row, 1, row_size, f);
    }
    
    free(row);
    fclose(f);
    
    printf("Saved: %s (%dx%d, %d bytes)\n", filename, img->width, img->height, file_size);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *output = "logo.bmp";
    int width = 256;
    int height = 256;
    
    if (argc > 1) output = argv[1];
    if (argc > 2) width = atoi(argv[2]);
    if (argc > 3) height = atoi(argv[3]);
    
    printf("NeolyxOS Logo Generator\n");
    printf("=======================\n\n");
    
    image_t *img = create_image(width, height);
    
    /* Draw background gradient */
    draw_gradient_bg(img);
    
    /* Add glow effect */
    add_glow(img, width / 2, height / 2, height / 3);
    
    /* Calculate logo scale and position */
    float scale = (float)height / 160.0f;  /* Original ~133px height */
    int logo_x = width / 2 - (int)(50 * scale);
    int logo_y = height / 2 - (int)(70 * scale);
    
    /* Draw the NeolyxOS logo */
    draw_neolyx_logo(img, logo_x, logo_y, scale);
    
    /* Save BMP */
    if (save_bmp(output, img) != 0) {
        fprintf(stderr, "Failed to save %s\n", output);
        free_image(img);
        return 1;
    }
    
    free_image(img);
    
    printf("\nUse this BMP in bootloader splash screen.\n");
    
    return 0;
}
