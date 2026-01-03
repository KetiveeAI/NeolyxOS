/*
 * NeolyxOS Font System Implementation
 * 
 * Kernel-level TTF/OTF parsing, glyph rasterization, and text rendering.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/core/font_system.h"
#include <stddef.h>

/* Forward declarations for kernel functions */
extern void* kmalloc(uint32_t size);
extern void kfree(void* ptr);
extern void serial_puts(const char* s);
extern void serial_printf(const char* fmt, ...);

/* ============ Byte Order Helpers ============ */

static uint16_t read_u16(const uint8_t *p) {
    return (p[0] << 8) | p[1];
}

static int16_t read_i16(const uint8_t *p) {
    return (int16_t)read_u16(p);
}

static uint32_t read_u32(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

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
#define TAG_OS2  TTF_TAG('O','S','/','2')

/* ============ Internal Font Structure ============ */

struct nx_font {
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
    int16_t loca_format;
    
    /* cmap info */
    uint16_t cmap_format;
    const uint8_t *cmap_data;
    
    /* Font info */
    char family[64];
    char style[32];
    uint16_t weight;
    uint8_t is_italic;
    
    nx_font_type_t type;
};

/* ============ Font Registry ============ */

#define MAX_FONTS 128

static struct {
    nx_font_entry_t entries[MAX_FONTS];
    uint32_t count;
    uint32_t next_id;
    bool initialized;
} g_font_registry;

/* Cached system fonts */
static nx_font_t *g_system_ui_font = NULL;

/* ============ Table Finding ============ */

static const uint8_t* find_table(nx_font_t *font, uint32_t tag) {
    if (!font || !font->data || font->size < 12) return NULL;
    
    uint16_t num_tables = read_u16(font->data + 4);
    const uint8_t *entry = font->data + 12;
    
    for (int i = 0; i < num_tables; i++) {
        if (read_u32(entry) == tag) {
            uint32_t offset = read_u32(entry + 8);
            if (offset < font->size) {
                return font->data + offset;
            }
        }
        entry += 16;
    }
    return NULL;
}

/* ============ Name Table Parsing ============ */

static void parse_name_table(nx_font_t *font) {
    if (!font->name) return;
    
    uint16_t count = read_u16(font->name + 2);
    uint16_t string_offset = read_u16(font->name + 4);
    const uint8_t *records = font->name + 6;
    
    for (int i = 0; i < count; i++) {
        uint16_t platform = read_u16(records);
        uint16_t name_id = read_u16(records + 6);
        uint16_t length = read_u16(records + 8);
        uint16_t offset = read_u16(records + 10);
        
        if (platform == 3) {  /* Windows Unicode */
            const uint8_t *str = font->name + string_offset + offset;
            
            if (name_id == 1 && font->family[0] == '\0') {
                int j = 0;
                for (int k = 1; k < length && j < 63; k += 2) {
                    if (str[k] >= 32 && str[k] < 127) {
                        font->family[j++] = str[k];
                    }
                }
                font->family[j] = '\0';
            }
            else if (name_id == 2 && font->style[0] == '\0') {
                int j = 0;
                for (int k = 1; k < length && j < 31; k += 2) {
                    if (str[k] >= 32 && str[k] < 127) {
                        font->style[j++] = str[k];
                    }
                }
                font->style[j] = '\0';
            }
        }
        records += 12;
    }
}

/* ============ cmap Parsing ============ */

static void parse_cmap(nx_font_t *font) {
    if (!font->cmap) return;
    
    uint16_t num_subtables = read_u16(font->cmap + 2);
    const uint8_t *subtable = font->cmap + 4;
    
    for (int i = 0; i < num_subtables; i++) {
        uint16_t platform = read_u16(subtable);
        uint16_t encoding = read_u16(subtable + 2);
        uint32_t offset = read_u32(subtable + 4);
        
        if ((platform == 0 || platform == 3) &&
            (encoding == 3 || encoding == 1 || encoding == 4 || encoding == 10)) {
            font->cmap_data = font->cmap + offset;
            font->cmap_format = read_u16(font->cmap_data);
            break;
        }
        subtable += 8;
    }
}

/* ============ Glyph Index Lookup ============ */

static uint16_t get_glyph_index(nx_font_t *font, uint32_t codepoint) {
    if (!font || !font->cmap_data) return 0;
    
    const uint8_t *data = font->cmap_data;
    
    if (font->cmap_format == 4) {
        uint16_t seg_count = read_u16(data + 6) / 2;
        const uint8_t *end_codes = data + 14;
        const uint8_t *start_codes = end_codes + seg_count * 2 + 2;
        const uint8_t *id_deltas = start_codes + seg_count * 2;
        const uint8_t *id_range_offsets = id_deltas + seg_count * 2;
        
        for (int i = 0; i < seg_count; i++) {
            uint16_t end = read_u16(end_codes + i * 2);
            if (codepoint <= end) {
                uint16_t start = read_u16(start_codes + i * 2);
                if (codepoint >= start) {
                    uint16_t offset = read_u16(id_range_offsets + i * 2);
                    if (offset == 0) {
                        int16_t delta = read_i16(id_deltas + i * 2);
                        return (codepoint + delta) & 0xFFFF;
                    } else {
                        const uint8_t *ptr = id_range_offsets + i * 2 + offset +
                                             (codepoint - start) * 2;
                        uint16_t glyph = read_u16(ptr);
                        if (glyph) {
                            int16_t delta = read_i16(id_deltas + i * 2);
                            return (glyph + delta) & 0xFFFF;
                        }
                    }
                }
                break;
            }
        }
    }
    else if (font->cmap_format == 12) {
        uint32_t num_groups = read_u32(data + 12);
        const uint8_t *groups = data + 16;
        
        for (uint32_t i = 0; i < num_groups; i++) {
            uint32_t start = read_u32(groups + i * 12);
            uint32_t end = read_u32(groups + i * 12 + 4);
            uint32_t start_glyph = read_u32(groups + i * 12 + 8);
            
            if (codepoint >= start && codepoint <= end) {
                return start_glyph + (codepoint - start);
            }
        }
    }
    
    return 0;
}

/* ============ Glyph Offset ============ */

static uint32_t get_glyph_offset(nx_font_t *font, uint16_t glyph_idx) {
    if (!font->loca) return 0;
    
    if (font->loca_format == 0) {
        return read_u16(font->loca + glyph_idx * 2) * 2;
    } else {
        return read_u32(font->loca + glyph_idx * 4);
    }
}

/* ============ Font System API ============ */

int nx_font_system_init(void) {
    if (g_font_registry.initialized) return 0;
    
    for (int i = 0; i < MAX_FONTS; i++) {
        g_font_registry.entries[i].font_id = 0;
    }
    g_font_registry.count = 0;
    g_font_registry.next_id = 1;
    g_font_registry.initialized = true;
    
    serial_puts("[FONT] System initialized\n");
    return 0;
}

uint32_t nx_font_register(const char *path, bool is_system) {
    if (!g_font_registry.initialized || g_font_registry.count >= MAX_FONTS) {
        return 0;
    }
    
    nx_font_entry_t *entry = &g_font_registry.entries[g_font_registry.count];
    entry->font_id = g_font_registry.next_id++;
    entry->is_system = is_system;
    entry->is_loaded = false;
    
    /* Copy path */
    int i;
    for (i = 0; path[i] && i < 255; i++) {
        entry->path[i] = path[i];
    }
    entry->path[i] = '\0';
    
    g_font_registry.count++;
    return entry->font_id;
}

int nx_font_unregister(uint32_t font_id) {
    for (uint32_t i = 0; i < g_font_registry.count; i++) {
        if (g_font_registry.entries[i].font_id == font_id) {
            if (g_font_registry.entries[i].is_system) {
                return -1;
            }
            /* Shift remaining entries */
            for (uint32_t j = i; j < g_font_registry.count - 1; j++) {
                g_font_registry.entries[j] = g_font_registry.entries[j + 1];
            }
            g_font_registry.count--;
            return 0;
        }
    }
    return -1;
}

uint32_t nx_font_get_count(void) {
    return g_font_registry.count;
}

const nx_font_entry_t* nx_font_get_entry(uint32_t index) {
    if (index >= g_font_registry.count) return NULL;
    return &g_font_registry.entries[index];
}

/* ============ Font Loading ============ */

nx_font_t* nx_font_load_from_data(const uint8_t *data, uint32_t size) {
    if (!data || size < 12) return NULL;
    
    /* Check signature */
    uint32_t sfnt = read_u32(data);
    if (sfnt != 0x00010000 && sfnt != 0x4F54544F && sfnt != 0x74727565) {
        return NULL;
    }
    
    nx_font_t *font = (nx_font_t*)kmalloc(sizeof(nx_font_t));
    if (!font) return NULL;
    
    /* Zero init */
    uint8_t *p = (uint8_t*)font;
    for (uint32_t i = 0; i < sizeof(nx_font_t); i++) {
        p[i] = 0;
    }
    
    font->data = data;
    font->size = size;
    font->type = (sfnt == 0x4F54544F) ? NX_FONT_TYPE_OTF : NX_FONT_TYPE_TTF;
    
    /* Locate tables */
    font->cmap = find_table(font, TAG_cmap);
    font->glyf = find_table(font, TAG_glyf);
    font->head = find_table(font, TAG_head);
    font->hhea = find_table(font, TAG_hhea);
    font->hmtx = find_table(font, TAG_hmtx);
    font->loca = find_table(font, TAG_loca);
    font->maxp = find_table(font, TAG_maxp);
    font->name = find_table(font, TAG_name);
    font->os2 = find_table(font, TAG_OS2);
    
    /* Parse head */
    if (font->head) {
        font->units_per_em = read_u16(font->head + 18);
        font->loca_format = read_i16(font->head + 50);
    }
    
    /* Parse hhea */
    if (font->hhea) {
        font->ascent = read_i16(font->hhea + 4);
        font->descent = read_i16(font->hhea + 6);
        font->line_gap = read_i16(font->hhea + 8);
        font->num_hmetrics = read_u16(font->hhea + 34);
    }
    
    /* Parse maxp */
    if (font->maxp) {
        font->num_glyphs = read_u16(font->maxp + 4);
    }
    
    /* Parse OS/2 */
    if (font->os2) {
        font->weight = read_u16(font->os2 + 4);
        uint16_t fs = read_u16(font->os2 + 62);
        font->is_italic = (fs & 1) ? 1 : 0;
    }
    
    parse_name_table(font);
    parse_cmap(font);
    
    serial_printf("[FONT] Loaded: %s %s\n", font->family, font->style);
    return font;
}

void nx_font_unload(nx_font_t *font) {
    if (font) {
        kfree(font);
    }
}

/* ============ Font Metrics ============ */

void nx_font_get_metrics(nx_font_t *font, uint16_t size_px, 
                         nx_font_metrics_t *metrics) {
    if (!font || !metrics) return;
    
    int32_t scale = (int32_t)size_px * 1000 / font->units_per_em;
    
    metrics->ascent = (font->ascent * scale) / 1000;
    metrics->descent = (font->descent * scale) / 1000;
    metrics->line_gap = (font->line_gap * scale) / 1000;
    metrics->line_height = metrics->ascent - metrics->descent + metrics->line_gap;
    metrics->units_per_em = font->units_per_em;
    metrics->x_height = (int16_t)((font->ascent * 2 / 3) * scale / 1000);
    metrics->cap_height = metrics->ascent;
}

/* ============ Glyph Info ============ */

int nx_font_get_glyph_info(nx_font_t *font, uint32_t codepoint,
                           uint16_t size_px, nx_glyph_info_t *info) {
    if (!font || !info) return -1;
    
    uint16_t glyph_id = get_glyph_index(font, codepoint);
    info->codepoint = codepoint;
    info->glyph_id = glyph_id;
    
    /* Get advance */
    if (font->hmtx && glyph_id < font->num_glyphs) {
        uint16_t idx = (glyph_id < font->num_hmetrics) ? glyph_id : font->num_hmetrics - 1;
        uint16_t advance = read_u16(font->hmtx + idx * 4);
        int16_t lsb = read_i16(font->hmtx + idx * 4 + 2);
        
        int32_t scale = (int32_t)size_px * 1000 / font->units_per_em;
        info->advance_x = (advance * scale) / 1000;
        info->bearing_x = (lsb * scale) / 1000;
    }
    
    return 0;
}

/* ============ Glyph Rasterization ============ */

typedef struct {
    int16_t x, y;
    uint8_t on_curve;
} glyph_point_t;

static void rasterize_line(uint8_t *bitmap, int16_t w, int16_t h,
                           int x0, int y0, int x1, int y1) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            bitmap[y0 * w + x0] = 255;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void rasterize_bezier(uint8_t *bitmap, int16_t w, int16_t h,
                             int x0, int y0, int x1, int y1, int x2, int y2) {
    /* Quadratic bezier via subdivision */
    for (int t = 0; t <= 16; t++) {
        int t1 = t * 256 / 16;
        int t0 = 256 - t1;
        
        int ax = (t0 * t0 * x0 + 2 * t0 * t1 * x1 + t1 * t1 * x2) >> 16;
        int ay = (t0 * t0 * y0 + 2 * t0 * t1 * y1 + t1 * t1 * y2) >> 16;
        
        if (ax >= 0 && ax < w && ay >= 0 && ay < h) {
            bitmap[ay * w + ax] = 255;
        }
    }
}

int nx_font_render_glyph(nx_font_t *font, uint32_t codepoint,
                         uint16_t size_px, nx_rendered_glyph_t *glyph) {
    if (!font || !glyph || !font->glyf || !font->loca) return -1;
    
    uint16_t glyph_id = get_glyph_index(font, codepoint);
    
    uint32_t offset = get_glyph_offset(font, glyph_id);
    uint32_t next_offset = get_glyph_offset(font, glyph_id + 1);
    
    if (offset == next_offset) {
        /* Empty glyph (space) */
        glyph->bitmap = NULL;
        glyph->width = 0;
        glyph->height = 0;
        
        /* Get advance */
        if (font->hmtx) {
            uint16_t idx = (glyph_id < font->num_hmetrics) ? glyph_id : font->num_hmetrics - 1;
            uint16_t advance = read_u16(font->hmtx + idx * 4);
            glyph->advance = (advance * size_px) / font->units_per_em;
        } else {
            glyph->advance = size_px / 2;
        }
        return 0;
    }
    
    const uint8_t *glyf = font->glyf + offset;
    int16_t num_contours = read_i16(glyf);
    int16_t x_min = read_i16(glyf + 2);
    int16_t y_min = read_i16(glyf + 4);
    int16_t x_max = read_i16(glyf + 6);
    int16_t y_max = read_i16(glyf + 8);
    
    if (num_contours < 0) {
        /* Compound glyph - not yet supported */
        return -2;
    }
    
    /* Scale factor */
    int32_t scale = (int32_t)size_px * 256 / font->units_per_em;
    
    int16_t w = ((x_max - x_min) * scale / 256) + 2;
    int16_t h = ((y_max - y_min) * scale / 256) + 2;
    
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 256) w = 256;
    if (h > 256) h = 256;
    
    glyph->width = w;
    glyph->height = h;
    glyph->offset_x = (x_min * scale) / 256;
    glyph->offset_y = -((y_max * scale) / 256);
    
    /* Get advance */
    if (font->hmtx) {
        uint16_t idx = (glyph_id < font->num_hmetrics) ? glyph_id : font->num_hmetrics - 1;
        uint16_t advance = read_u16(font->hmtx + idx * 4);
        glyph->advance = (advance * scale) / 256;
    } else {
        glyph->advance = w;
    }
    
    /* Allocate bitmap */
    glyph->bitmap = (uint8_t*)kmalloc(w * h);
    if (!glyph->bitmap) return -1;
    
    for (int i = 0; i < w * h; i++) {
        glyph->bitmap[i] = 0;
    }
    
    /* Parse contours and rasterize */
    const uint8_t *end_pts = glyf + 10;
    uint16_t total_points = read_u16(end_pts + (num_contours - 1) * 2) + 1;
    
    if (total_points > 512) return -3;
    
    /* Read contour ends */
    uint16_t contour_ends[64];
    for (int i = 0; i < num_contours && i < 64; i++) {
        contour_ends[i] = read_u16(end_pts + i * 2);
    }
    
    /* Skip instructions */
    const uint8_t *ptr = end_pts + num_contours * 2;
    uint16_t instr_len = read_u16(ptr);
    ptr += 2 + instr_len;
    
    /* Read flags */
    uint8_t flags[512];
    int point_idx = 0;
    while (point_idx < total_points) {
        uint8_t flag = *ptr++;
        flags[point_idx++] = flag;
        if (flag & 0x08) {
            uint8_t repeat = *ptr++;
            while (repeat-- > 0 && point_idx < total_points) {
                flags[point_idx++] = flag;
            }
        }
    }
    
    /* Read coordinates */
    glyph_point_t points[512];
    int16_t x = 0, y = 0;
    
    for (int i = 0; i < total_points; i++) {
        uint8_t flag = flags[i];
        if (flag & 0x02) {
            int16_t dx = *ptr++;
            x += (flag & 0x10) ? dx : -dx;
        } else if (!(flag & 0x10)) {
            x += read_i16(ptr);
            ptr += 2;
        }
        points[i].x = x;
        points[i].on_curve = (flag & 0x01) ? 1 : 0;
    }
    
    for (int i = 0; i < total_points; i++) {
        uint8_t flag = flags[i];
        if (flag & 0x04) {
            int16_t dy = *ptr++;
            y += (flag & 0x20) ? dy : -dy;
        } else if (!(flag & 0x20)) {
            y += read_i16(ptr);
            ptr += 2;
        }
        points[i].y = y;
    }
    
    /* Rasterize contours */
    int start = 0;
    for (int c = 0; c < num_contours; c++) {
        int end = contour_ends[c];
        
        for (int i = start; i <= end; i++) {
            int next = (i == end) ? start : i + 1;
            
            int px0 = ((points[i].x - x_min) * scale / 256);
            int py0 = h - 1 - ((points[i].y - y_min) * scale / 256);
            int px1 = ((points[next].x - x_min) * scale / 256);
            int py1 = h - 1 - ((points[next].y - y_min) * scale / 256);
            
            if (points[i].on_curve && points[next].on_curve) {
                rasterize_line(glyph->bitmap, w, h, px0, py0, px1, py1);
            } else if (points[i].on_curve && !points[next].on_curve) {
                int next2 = (next == end) ? start : next + 1;
                int px2 = ((points[next2].x - x_min) * scale / 256);
                int py2 = h - 1 - ((points[next2].y - y_min) * scale / 256);
                rasterize_bezier(glyph->bitmap, w, h, px0, py0, px1, py1, px2, py2);
            }
        }
        
        start = end + 1;
    }
    
    return 0;
}

/* ============ Text Measurement ============ */

void nx_font_measure_text(nx_font_t *font, const char *text,
                          uint16_t size_px, int32_t *width, int32_t *height) {
    if (!font || !text || !width || !height) return;
    
    *width = 0;
    nx_font_metrics_t metrics;
    nx_font_get_metrics(font, size_px, &metrics);
    *height = metrics.line_height;
    
    while (*text) {
        uint32_t cp = (uint8_t)*text++;
        
        /* Basic UTF-8 decode */
        if (cp >= 0xC0 && *text) {
            cp = ((cp & 0x1F) << 6) | (*text++ & 0x3F);
        }
        
        nx_glyph_info_t info;
        if (nx_font_get_glyph_info(font, cp, size_px, &info) == 0) {
            *width += info.advance_x;
        }
    }
}

/* ============ Text Drawing ============ */

void nx_font_draw_text(nx_font_t *font, volatile uint32_t *fb,
                       uint32_t pitch, int32_t x, int32_t y,
                       const char *text, uint16_t size_px, uint32_t color) {
    if (!font || !fb || !text) return;
    
    nx_font_metrics_t metrics;
    nx_font_get_metrics(font, size_px, &metrics);
    
    int32_t pen_x = x;
    int32_t baseline_y = y + metrics.ascent;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    while (*text) {
        uint32_t cp = (uint8_t)*text++;
        
        /* UTF-8 decode */
        if (cp >= 0xC0 && *text) {
            cp = ((cp & 0x1F) << 6) | (*text++ & 0x3F);
        }
        
        nx_rendered_glyph_t glyph;
        if (nx_font_render_glyph(font, cp, size_px, &glyph) == 0) {
            if (glyph.bitmap) {
                int gx = pen_x + glyph.offset_x;
                int gy = baseline_y + glyph.offset_y;
                
                for (int py = 0; py < glyph.height; py++) {
                    for (int px = 0; px < glyph.width; px++) {
                        uint8_t alpha = glyph.bitmap[py * glyph.width + px];
                        if (alpha > 0) {
                            int fx = gx + px;
                            int fy = gy + py;
                            if (fx >= 0 && fy >= 0) {
                                uint32_t idx = fy * (pitch / 4) + fx;
                                uint32_t bg = fb[idx];
                                
                                uint8_t bg_r = (bg >> 16) & 0xFF;
                                uint8_t bg_g = (bg >> 8) & 0xFF;
                                uint8_t bg_b = bg & 0xFF;
                                
                                uint8_t out_r = ((r * alpha) + (bg_r * (255 - alpha))) / 255;
                                uint8_t out_g = ((g * alpha) + (bg_g * (255 - alpha))) / 255;
                                uint8_t out_b = ((b * alpha) + (bg_b * (255 - alpha))) / 255;
                                
                                fb[idx] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
                            }
                        }
                    }
                }
                
                kfree(glyph.bitmap);
            }
            
            pen_x += glyph.advance;
        }
    }
}

/* ============ System Font ============ */

nx_font_t* nx_font_get_system(const char *type, nx_font_weight_t weight) {
    (void)type;
    (void)weight;
    return g_system_ui_font;
}
