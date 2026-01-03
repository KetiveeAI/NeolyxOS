/*
 * NeolyxOS Font System API
 * 
 * Kernel-level font registration, loading, and rasterization.
 * Supports TTF, OTF, and native NXF formats.
 * 
 * Font Directories:
 *   /System/Library/Fonts/   - Protected system fonts (read-only)
 *   /Library/Fonts/          - User-installed fonts
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_FONT_SYSTEM_H
#define NEOLYX_FONT_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Font Types ============ */

typedef enum {
    NX_FONT_TYPE_TTF = 0,    /* TrueType */
    NX_FONT_TYPE_OTF,        /* OpenType */
    NX_FONT_TYPE_NXF,        /* NeolyxOS native vector font */
    NX_FONT_TYPE_BITMAP      /* Fixed-size bitmap (fallback) */
} nx_font_type_t;

typedef enum {
    NX_FONT_WEIGHT_THIN = 100,
    NX_FONT_WEIGHT_EXTRALIGHT = 200,
    NX_FONT_WEIGHT_LIGHT = 300,
    NX_FONT_WEIGHT_REGULAR = 400,
    NX_FONT_WEIGHT_MEDIUM = 500,
    NX_FONT_WEIGHT_SEMIBOLD = 600,
    NX_FONT_WEIGHT_BOLD = 700,
    NX_FONT_WEIGHT_EXTRABOLD = 800,
    NX_FONT_WEIGHT_BLACK = 900
} nx_font_weight_t;

typedef enum {
    NX_FONT_STYLE_NORMAL = 0,
    NX_FONT_STYLE_ITALIC,
    NX_FONT_STYLE_OBLIQUE
} nx_font_style_t;

/* ============ Font Metrics ============ */

typedef struct {
    int16_t ascent;          /* Distance from baseline to top */
    int16_t descent;         /* Distance from baseline to bottom (negative) */
    int16_t line_gap;        /* Extra spacing between lines */
    int16_t line_height;     /* Total line height (ascent - descent + gap) */
    int16_t x_height;        /* Height of lowercase 'x' */
    int16_t cap_height;      /* Height of capital letters */
    uint16_t units_per_em;   /* Font design units */
} nx_font_metrics_t;

/* ============ Glyph Info ============ */

typedef struct {
    uint32_t codepoint;      /* Unicode codepoint */
    uint16_t glyph_id;       /* Glyph index in font */
    int16_t  advance_x;      /* Horizontal advance */
    int16_t  advance_y;      /* Vertical advance (usually 0) */
    int16_t  bearing_x;      /* Left side bearing */
    int16_t  bearing_y;      /* Top side bearing */
    int16_t  width;          /* Glyph width */
    int16_t  height;         /* Glyph height */
} nx_glyph_info_t;

/* ============ Rendered Glyph ============ */

typedef struct {
    uint8_t *bitmap;         /* Alpha bitmap (8-bit per pixel) */
    int16_t  width;
    int16_t  height;
    int16_t  offset_x;       /* X offset from origin */
    int16_t  offset_y;       /* Y offset from origin */
    int16_t  advance;        /* Horizontal advance to next glyph */
} nx_rendered_glyph_t;

/* ============ Font Handle ============ */

typedef struct nx_font nx_font_t;

/* ============ Font Registry Entry ============ */

typedef struct {
    uint32_t font_id;
    char     name[64];
    char     family[64];
    char     path[256];
    nx_font_type_t type;
    nx_font_weight_t weight;
    nx_font_style_t style;
    bool     is_system;      /* System font (protected) */
    bool     is_loaded;      /* Currently loaded in memory */
    uint32_t glyph_count;
} nx_font_entry_t;

/* ============ Font System API ============ */

/**
 * nx_font_system_init - Initialize font system
 * 
 * Scans font directories and builds registry.
 * Returns: 0 on success, -1 on error
 */
int nx_font_system_init(void);

/**
 * nx_font_register - Register a font file
 * @path: Path to font file
 * @is_system: Whether this is a system font
 * 
 * Returns: Font ID on success, 0 on error
 */
uint32_t nx_font_register(const char *path, bool is_system);

/**
 * nx_font_unregister - Remove font from registry
 * @font_id: Font ID to remove
 * 
 * Returns: 0 on success, -1 if system font or error
 */
int nx_font_unregister(uint32_t font_id);

/**
 * nx_font_get_count - Get number of registered fonts
 */
uint32_t nx_font_get_count(void);

/**
 * nx_font_get_entry - Get font registry entry
 * @index: Index in registry
 */
const nx_font_entry_t* nx_font_get_entry(uint32_t index);

/**
 * nx_font_find_by_name - Find font by family name and style
 * @family: Font family name (e.g., "NX Sans")
 * @weight: Desired weight (e.g., NX_FONT_WEIGHT_REGULAR)
 * @style: Desired style (e.g., NX_FONT_STYLE_NORMAL)
 * 
 * Returns: Font ID, or 0 if not found
 */
uint32_t nx_font_find_by_name(const char *family, 
                              nx_font_weight_t weight,
                              nx_font_style_t style);

/* ============ Font Loading API ============ */

/**
 * nx_font_load - Load font for rendering
 * @font_id: Font ID from registry
 * 
 * Returns: Font handle, or NULL on error
 */
nx_font_t* nx_font_load(uint32_t font_id);

/**
 * nx_font_load_from_data - Load font from memory
 * @data: Font file data
 * @size: Size in bytes
 * 
 * Returns: Font handle, or NULL on error
 */
nx_font_t* nx_font_load_from_data(const uint8_t *data, uint32_t size);

/**
 * nx_font_unload - Unload font and free resources
 */
void nx_font_unload(nx_font_t *font);

/* ============ Font Metrics API ============ */

/**
 * nx_font_get_metrics - Get font metrics at specified size
 * @font: Font handle
 * @size_px: Font size in pixels
 * @metrics: Output metrics structure
 */
void nx_font_get_metrics(nx_font_t *font, uint16_t size_px, 
                         nx_font_metrics_t *metrics);

/**
 * nx_font_get_glyph_info - Get glyph info without rendering
 * @font: Font handle
 * @codepoint: Unicode codepoint
 * @size_px: Font size in pixels
 * @info: Output glyph info
 * 
 * Returns: 0 on success, -1 if glyph not found
 */
int nx_font_get_glyph_info(nx_font_t *font, uint32_t codepoint,
                           uint16_t size_px, nx_glyph_info_t *info);

/* ============ Font Rendering API ============ */

/**
 * nx_font_render_glyph - Render single glyph to bitmap
 * @font: Font handle
 * @codepoint: Unicode codepoint
 * @size_px: Font size in pixels
 * @glyph: Output rendered glyph
 * 
 * Returns: 0 on success, -1 on error
 * Note: Caller must free glyph->bitmap with kfree()
 */
int nx_font_render_glyph(nx_font_t *font, uint32_t codepoint,
                         uint16_t size_px, nx_rendered_glyph_t *glyph);

/**
 * nx_font_measure_text - Measure text dimensions
 * @font: Font handle
 * @text: UTF-8 text string
 * @size_px: Font size in pixels
 * @width: Output width in pixels
 * @height: Output height in pixels
 */
void nx_font_measure_text(nx_font_t *font, const char *text,
                          uint16_t size_px, int32_t *width, int32_t *height);

/**
 * nx_font_draw_text - Draw text to framebuffer
 * @font: Font handle
 * @fb: Framebuffer pointer
 * @pitch: Bytes per scanline
 * @x: X position
 * @y: Y position (baseline)
 * @text: UTF-8 text string
 * @size_px: Font size in pixels
 * @color: Text color (ARGB)
 */
void nx_font_draw_text(nx_font_t *font, volatile uint32_t *fb,
                       uint32_t pitch, int32_t x, int32_t y,
                       const char *text, uint16_t size_px, uint32_t color);

/* ============ System Fonts ============ */

/* Default system font identifiers */
#define NX_FONT_SYSTEM_UI       "NX Sans UI"
#define NX_FONT_SYSTEM_TEXT     "NX Sans Text"
#define NX_FONT_SYSTEM_DISPLAY  "NX Sans Display"
#define NX_FONT_SYSTEM_MONO     "NX Sans Mono"

/**
 * nx_font_get_system - Get default system font
 * @type: "ui", "text", "display", or "mono"
 * @weight: Desired weight
 * 
 * Returns: Font handle (cached, do not unload)
 */
nx_font_t* nx_font_get_system(const char *type, nx_font_weight_t weight);

/* ============ NXF Native Format ============ */

/*
 * NXF (NeolyxOS Font) is a compact vector font format:
 * 
 * Header: 
 *   - Magic: "NXF\0"
 *   - Version: uint8
 *   - Flags: uint8
 *   - Glyph count: uint16
 *   - Units per em: uint16
 *   - Ascent: int16
 *   - Descent: int16
 * 
 * Glyph Table:
 *   - Codepoint: uint32
 *   - Advance: uint16
 *   - Command offset: uint32
 *   - Command count: uint16
 * 
 * Commands (per glyph):
 *   - MOVE_TO(x, y)
 *   - LINE_TO(x, y)
 *   - CURVE_TO(cx, cy, x, y)
 *   - CUBIC_TO(cx1, cy1, cx2, cy2, x, y)
 *   - CLOSE_PATH
 */

#define NXF_MAGIC       0x0046584E  /* 'NXF\0' */
#define NXF_VERSION     1

typedef enum {
    NXF_CMD_MOVE_TO = 0,
    NXF_CMD_LINE_TO,
    NXF_CMD_CURVE_TO,      /* Quadratic bezier */
    NXF_CMD_CUBIC_TO,      /* Cubic bezier */
    NXF_CMD_CLOSE_PATH
} nxf_command_t;

#endif /* NEOLYX_FONT_SYSTEM_H */
