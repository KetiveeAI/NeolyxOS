/*
 * NeolyxOS Font Manager - TTF/OTF Font Parser
 * 
 * Parses TrueType and OpenType fonts for preview and installation.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef FONTMANAGER_TTF_H
#define FONTMANAGER_TTF_H

#include <stdint.h>

/* ============ TTF Table Tags ============ */

#define TTF_TAG(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(d))

#define TAG_cmap TTF_TAG('c','m','a','p')
#define TAG_glyf TTF_TAG('g','l','y','f')
#define TAG_head TTF_TAG('h','e','a','d')
#define TAG_hhea TTF_TAG('h','h','e','a')
#define TAG_hmtx TTF_TAG('h','m','t','x')
#define TAG_loca TTF_TAG('l','o','c','a')
#define TAG_maxp TTF_TAG('m','a','x','p')
#define TAG_name TTF_TAG('n','a','m','e')
#define TAG_post TTF_TAG('p','o','s','t')
#define TAG_CFF  TTF_TAG('C','F','F',' ')
#define TAG_GPOS TTF_TAG('G','P','O','S')
#define TAG_GSUB TTF_TAG('G','S','U','B')
#define TAG_kern TTF_TAG('k','e','r','n')
#define TAG_OS2  TTF_TAG('O','S','/','2')

/* ============ Font Structures ============ */

typedef struct {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} ttf_table_entry_t;

typedef struct {
    int16_t num_contours;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
} ttf_glyph_header_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t on_curve;
} ttf_glyph_point_t;

typedef struct {
    uint16_t advance_width;
    int16_t left_bearing;
} ttf_hmetrics_t;

typedef struct {
    /* Raw file data */
    const uint8_t *data;
    uint32_t size;
    
    /* Table pointers */
    const uint8_t *cmap;
    const uint8_t *glyf;
    const uint8_t *head;
    const uint8_t *hhea;
    const uint8_t *hmtx;
    const uint8_t *loca;
    const uint8_t *maxp;
    const uint8_t *name;
    const uint8_t *os2;
    
    /* Font metrics */
    uint16_t units_per_em;
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t num_glyphs;
    uint16_t num_hmetrics;
    int16_t loca_format;  /* 0 = short, 1 = long */
    
    /* Character mapping */
    uint16_t cmap_format;
    const uint8_t *cmap_data;
    
    /* Font info (extracted from 'name' table) */
    char family_name[64];
    char style_name[32];
    char full_name[96];
    char version[32];
    
    /* Calculated values */
    uint16_t weight;  /* From OS/2 table */
    uint8_t is_italic;
    uint8_t is_monospace;
} ttf_font_t;

/* ============ API Functions ============ */

/* Load TTF from file data */
int ttf_load(ttf_font_t *font, const uint8_t *data, uint32_t size);

/* Free font resources */
void ttf_free(ttf_font_t *font);

/* Get glyph index for unicode codepoint */
uint16_t ttf_get_glyph_index(const ttf_font_t *font, uint32_t codepoint);

/* Get glyph metrics */
int ttf_get_hmetrics(const ttf_font_t *font, uint16_t glyph_idx, ttf_hmetrics_t *metrics);

/* Get glyph outline (for rasterization) */
int ttf_get_glyph_outline(const ttf_font_t *font, uint16_t glyph_idx,
                          ttf_glyph_point_t *points, uint16_t *contour_ends,
                          uint16_t max_points, uint16_t max_contours);

/* Scale font units to pixels */
static inline int32_t ttf_scale(int16_t value, uint16_t units_per_em, uint16_t size_px) {
    return ((int32_t)value * size_px) / units_per_em;
}

/* ============ OpenType Support ============ */

/* Check if font is OpenType (CFF outlines) */
int ttf_is_opentype(const ttf_font_t *font);

/* Get kerning value for pair */
int16_t ttf_get_kerning(const ttf_font_t *font, uint16_t left_glyph, uint16_t right_glyph);

/* ============ NeolyxOS Native Font Format ============ */

/*
 * .nxf - NeolyxOS Font Format
 * 
 * Header:
 *   magic[4]: "NXF\0"
 *   version: uint16
 *   flags: uint16
 *   num_glyphs: uint16
 *   units_per_em: uint16
 *   ascent: int16
 *   descent: int16
 *   line_gap: int16
 *   
 * Glyph table:
 *   For each glyph:
 *     codepoint: uint32
 *     advance: uint16
 *     bearing_x: int16
 *     bearing_y: int16
 *     width: uint16
 *     height: uint16
 *     path_offset: uint32
 *     path_count: uint16
 *     
 * Path data:
 *   cmd: uint8 (MOVE/LINE/CURVE/END)
 *   x: int16
 *   y: int16
 */

#define NXF_MAGIC "NXF\0"
#define NXF_VERSION 1

typedef struct {
    uint8_t magic[4];
    uint16_t version;
    uint16_t flags;
    uint16_t num_glyphs;
    uint16_t units_per_em;
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
} nxf_header_t;

typedef struct {
    uint32_t codepoint;
    uint16_t advance;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t width;
    uint16_t height;
    uint32_t path_offset;
    uint16_t path_count;
    uint16_t _reserved;
} nxf_glyph_t;

#define NXF_CMD_END   0
#define NXF_CMD_MOVE  1
#define NXF_CMD_LINE  2
#define NXF_CMD_CURVE 3  /* Quadratic bezier */

typedef struct {
    uint8_t cmd;
    int16_t x;
    int16_t y;
    int16_t cx;  /* Control point for curve */
    int16_t cy;
} nxf_path_t;

/* Load NXF font */
int nxf_load(ttf_font_t *font, const uint8_t *data, uint32_t size);

/* Convert TTF to NXF */
int ttf_to_nxf(const ttf_font_t *ttf, uint8_t *out_data, uint32_t *out_size);

#endif /* FONTMANAGER_TTF_H */
