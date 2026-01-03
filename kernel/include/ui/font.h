/*
 * NeolyxOS Advanced Font System
 * 
 * Font Types:
 *   1. System UI Font - Sans-serif (Inter-like)
 *      - Desktop UI, installer, settings, dialogs, buttons
 *   2. Monospace Font - Fixed-width
 *      - Terminal, shell, logs, code editor
 * 
 * Font Formats:
 *   - Bitmap (boot & recovery) - fast, predictable
 *   - TTF/OTF (desktop) - smooth, scalable
 * 
 * NOT in Phase 1:
 *   - Fancy fonts, emoji, variable fonts
 *   - Multiple UI font choices
 *   - Font marketplace
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_FONT_H
#define NEOLYX_FONT_H

#include <stdint.h>

/* ============ Font Categories ============ */

typedef enum {
    FONT_CAT_SYSTEM,        /* System UI (sans-serif) */
    FONT_CAT_MONO,          /* Monospace (terminal/code) */
} font_category_t;

/* ============ Font Styles ============ */

typedef enum {
    FONT_STYLE_REGULAR = 0,
    FONT_STYLE_BOLD = 1,
    FONT_STYLE_ITALIC = 2,
    FONT_STYLE_BOLD_ITALIC = 3,
} font_style_t;

/* ============ Font Weights ============ */

typedef enum {
    FONT_WEIGHT_THIN = 100,
    FONT_WEIGHT_LIGHT = 300,
    FONT_WEIGHT_REGULAR = 400,
    FONT_WEIGHT_MEDIUM = 500,
    FONT_WEIGHT_SEMIBOLD = 600,
    FONT_WEIGHT_BOLD = 700,
} font_weight_t;

/* ============ Bitmap Font (Boot/Recovery) ============ */

typedef struct {
    const char *name;
    uint32_t width;
    uint32_t height;
    uint32_t first_char;
    uint32_t last_char;
    const uint8_t *data;
} bitmap_font_t;

/* ============ TrueType Font ============ */

typedef struct {
    const char *name;
    font_category_t category;
    font_style_t style;
    
    /* TTF Data */
    const uint8_t *data;
    uint32_t data_size;
    
    /* Parsed tables */
    void *cmap;             /* Character map */
    void *glyf;             /* Glyph data */
    void *head;             /* Font header */
    void *hhea;             /* Horizontal header */
    void *hmtx;             /* Horizontal metrics */
    void *loca;             /* Glyph locations */
    
    /* Metrics */
    int16_t units_per_em;
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
} ttf_font_t;

/* ============ Glyph Cache ============ */

#define GLYPH_CACHE_SIZE 256

typedef struct {
    uint32_t codepoint;
    int16_t width;
    int16_t height;
    int16_t bearing_x;
    int16_t bearing_y;
    int16_t advance;
    uint8_t *bitmap;        /* Grayscale bitmap */
} cached_glyph_t;

typedef struct {
    ttf_font_t *font;
    uint32_t size;          /* Font size in pixels */
    cached_glyph_t glyphs[GLYPH_CACHE_SIZE];
    int glyph_count;
} font_cache_t;

/* ============ Font Instance ============ */

typedef struct {
    font_category_t category;
    uint32_t size;          /* Size in pixels */
    font_weight_t weight;
    font_style_t style;
    
    /* Backend */
    int use_ttf;            /* 1 = TTF, 0 = bitmap */
    union {
        const bitmap_font_t *bitmap;
        font_cache_t *cache;
    } backend;
} font_t;

/* ============ Built-in Fonts ============ */

extern const bitmap_font_t bitmap_font_system_16;
extern const bitmap_font_t bitmap_font_mono_16;

/* ============ Font System API ============ */

/**
 * Initialize the font subsystem.
 */
void font_init(void);

/**
 * Load a TTF font from memory.
 */
ttf_font_t *font_load_ttf(const uint8_t *data, uint32_t size, const char *name);

/**
 * Create a font instance for rendering.
 * 
 * @param category FONT_CAT_SYSTEM or FONT_CAT_MONO
 * @param size Font size in pixels
 * @param weight Font weight
 */
font_t *font_create(font_category_t category, uint32_t size, font_weight_t weight);

/**
 * Destroy a font instance.
 */
void font_destroy(font_t *font);

/**
 * Get the system UI font at a specific size.
 */
font_t *font_get_system(uint32_t size);

/**
 * Get the monospace font at a specific size.
 */
font_t *font_get_mono(uint32_t size);

/* ============ Text Rendering ============ */

/**
 * Measure text width.
 */
uint32_t font_text_width(font_t *font, const char *text);

/**
 * Measure text height.
 */
uint32_t font_text_height(font_t *font);

/**
 * Draw text.
 * 
 * @param font Font to use
 * @param x, y Position
 * @param text UTF-8 string
 * @param color ARGB color
 */
void font_draw_text(font_t *font, int x, int y, const char *text, uint32_t color);

/**
 * Draw text with background.
 */
void font_draw_text_bg(font_t *font, int x, int y, const char *text,
                       uint32_t fg, uint32_t bg);

/**
 * Draw centered text.
 */
void font_draw_centered(font_t *font, int y, const char *text, uint32_t color);

/**
 * Draw text with shadow.
 */
void font_draw_text_shadow(font_t *font, int x, int y, const char *text,
                           uint32_t fg, uint32_t shadow);

/* ============ Text Layout ============ */

/**
 * Draw text wrapped to width.
 */
int font_draw_wrapped(font_t *font, int x, int y, int max_width,
                      const char *text, uint32_t color);

/**
 * Get number of lines for wrapped text.
 */
int font_count_lines(font_t *font, int max_width, const char *text);

/* ============ Special Characters ============ */

/**
 * Check if character is supported.
 */
int font_has_glyph(font_t *font, uint32_t codepoint);

/* ============ Standard Sizes ============ */

#define FONT_SIZE_SMALL     12
#define FONT_SIZE_NORMAL    14
#define FONT_SIZE_MEDIUM    16
#define FONT_SIZE_LARGE     20
#define FONT_SIZE_XLARGE    24
#define FONT_SIZE_TITLE     32
#define FONT_SIZE_HEADER    48

#endif /* NEOLYX_FONT_H */
