/*
 * NeolyxOS Font Manager - TTF Parser Implementation
 * 
 * Full TrueType font parsing with glyph extraction.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "ttf.h"

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

/* ============ String Helpers ============ */

static void copy_name(char *dst, const uint8_t *src, uint32_t len, uint32_t max) {
    uint32_t i;
    for (i = 0; i < len && i < max - 1; i++) {
        /* Skip null bytes in Unicode strings */
        if (src[i] >= 32 && src[i] < 127) {
            dst[i] = src[i];
        } else if (i > 0 && src[i] == 0) {
            /* Unicode encoding: take every other byte */
            continue;
        } else {
            dst[i] = '?';
        }
    }
    dst[i] = '\0';
}

/* ============ Table Location ============ */

static const uint8_t* find_table(const ttf_font_t *font, uint32_t tag) {
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

static void parse_name_table(ttf_font_t *font) {
    const uint8_t *name = font->name;
    if (!name) return;
    
    uint16_t count = read_u16(name + 2);
    uint16_t string_offset = read_u16(name + 4);
    const uint8_t *records = name + 6;
    
    for (int i = 0; i < count; i++) {
        uint16_t platform = read_u16(records);
        uint16_t encoding = read_u16(records + 2);
        uint16_t name_id = read_u16(records + 6);
        uint16_t length = read_u16(records + 8);
        uint16_t offset = read_u16(records + 10);
        
        /* Accept platform 3 (Windows) or 1 (Mac) */
        if (platform == 3 || platform == 1) {
            const uint8_t *str = name + string_offset + offset;
            
            switch (name_id) {
                case 1:  /* Family name */
                    if (font->family_name[0] == '\0') {
                        /* Handle Unicode (platform 3) */
                        if (platform == 3) {
                            int j = 0;
                            for (int k = 1; k < length && j < 63; k += 2) {
                                font->family_name[j++] = str[k];
                            }
                            font->family_name[j] = '\0';
                        } else {
                            copy_name(font->family_name, str, length, 64);
                        }
                    }
                    break;
                case 2:  /* Style name */
                    if (font->style_name[0] == '\0') {
                        if (platform == 3) {
                            int j = 0;
                            for (int k = 1; k < length && j < 31; k += 2) {
                                font->style_name[j++] = str[k];
                            }
                            font->style_name[j] = '\0';
                        } else {
                            copy_name(font->style_name, str, length, 32);
                        }
                    }
                    break;
                case 4:  /* Full name */
                    if (font->full_name[0] == '\0') {
                        if (platform == 3) {
                            int j = 0;
                            for (int k = 1; k < length && j < 95; k += 2) {
                                font->full_name[j++] = str[k];
                            }
                            font->full_name[j] = '\0';
                        } else {
                            copy_name(font->full_name, str, length, 96);
                        }
                    }
                    break;
                case 5:  /* Version */
                    if (font->version[0] == '\0') {
                        if (platform == 3) {
                            int j = 0;
                            for (int k = 1; k < length && j < 31; k += 2) {
                                font->version[j++] = str[k];
                            }
                            font->version[j] = '\0';
                        } else {
                            copy_name(font->version, str, length, 32);
                        }
                    }
                    break;
            }
        }
        records += 12;
    }
}

/* ============ cmap Parsing ============ */

static void parse_cmap(ttf_font_t *font) {
    const uint8_t *cmap = font->cmap;
    if (!cmap) return;
    
    uint16_t num_subtables = read_u16(cmap + 2);
    const uint8_t *subtable = cmap + 4;
    
    /* Find Unicode mapping (platform 0 or 3) */
    for (int i = 0; i < num_subtables; i++) {
        uint16_t platform = read_u16(subtable);
        uint16_t encoding = read_u16(subtable + 2);
        uint32_t offset = read_u32(subtable + 4);
        
        if ((platform == 0 || platform == 3) && 
            (encoding == 3 || encoding == 1 || encoding == 4 || encoding == 10)) {
            font->cmap_data = cmap + offset;
            font->cmap_format = read_u16(font->cmap_data);
            break;
        }
        subtable += 8;
    }
}

/* ============ Load Font ============ */

int ttf_load(ttf_font_t *font, const uint8_t *data, uint32_t size) {
    if (!font || !data || size < 12) return -1;
    
    /* Clear structure */
    for (uint32_t i = 0; i < sizeof(ttf_font_t); i++) {
        ((uint8_t*)font)[i] = 0;
    }
    
    /* Check signature */
    uint32_t sfnt = read_u32(data);
    if (sfnt != 0x00010000 && sfnt != 0x4F54544F && sfnt != 0x74727565) {
        return -2;  /* Not a valid TrueType/OpenType font */
    }
    
    font->data = data;
    font->size = size;
    
    /* Locate required tables */
    font->cmap = find_table(font, TAG_cmap);
    font->glyf = find_table(font, TAG_glyf);
    font->head = find_table(font, TAG_head);
    font->hhea = find_table(font, TAG_hhea);
    font->hmtx = find_table(font, TAG_hmtx);
    font->loca = find_table(font, TAG_loca);
    font->maxp = find_table(font, TAG_maxp);
    font->name = find_table(font, TAG_name);
    font->os2 = find_table(font, TAG_OS2);
    
    /* Parse head table */
    if (font->head) {
        font->units_per_em = read_u16(font->head + 18);
        font->loca_format = read_i16(font->head + 50);
    }
    
    /* Parse hhea table */
    if (font->hhea) {
        font->ascent = read_i16(font->hhea + 4);
        font->descent = read_i16(font->hhea + 6);
        font->line_gap = read_i16(font->hhea + 8);
        font->num_hmetrics = read_u16(font->hhea + 34);
    }
    
    /* Parse maxp table */
    if (font->maxp) {
        font->num_glyphs = read_u16(font->maxp + 4);
    }
    
    /* Parse OS/2 table for weight */
    if (font->os2) {
        font->weight = read_u16(font->os2 + 4);
        uint16_t fs_selection = read_u16(font->os2 + 62);
        font->is_italic = (fs_selection & 1) ? 1 : 0;
    }
    
    /* Parse name table */
    parse_name_table(font);
    
    /* Parse cmap */
    parse_cmap(font);
    
    return 0;
}

void ttf_free(ttf_font_t *font) {
    /* Font data is externally owned, nothing to free */
    if (font) {
        for (uint32_t i = 0; i < sizeof(ttf_font_t); i++) {
            ((uint8_t*)font)[i] = 0;
        }
    }
}

/* ============ Character Mapping ============ */

uint16_t ttf_get_glyph_index(const ttf_font_t *font, uint32_t codepoint) {
    if (!font || !font->cmap_data) return 0;
    
    const uint8_t *data = font->cmap_data;
    uint16_t format = font->cmap_format;
    
    switch (format) {
        case 4: {
            /* Format 4: Segment mapping */
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
            break;
        }
        case 12: {
            /* Format 12: Segmented coverage */
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
            break;
        }
    }
    
    return 0;  /* .notdef */
}

/* ============ Glyph Metrics ============ */

int ttf_get_hmetrics(const ttf_font_t *font, uint16_t glyph_idx, ttf_hmetrics_t *metrics) {
    if (!font || !font->hmtx || !metrics) return -1;
    
    if (glyph_idx < font->num_hmetrics) {
        metrics->advance_width = read_u16(font->hmtx + glyph_idx * 4);
        metrics->left_bearing = read_i16(font->hmtx + glyph_idx * 4 + 2);
    } else {
        /* Use last advance width */
        metrics->advance_width = read_u16(font->hmtx + (font->num_hmetrics - 1) * 4);
        /* LSB is after hmtx entries */
        uint32_t lsb_offset = font->num_hmetrics * 4 + 
                              (glyph_idx - font->num_hmetrics) * 2;
        metrics->left_bearing = read_i16(font->hmtx + lsb_offset);
    }
    
    return 0;
}

/* ============ Glyph Outline ============ */

static uint32_t get_glyph_offset(const ttf_font_t *font, uint16_t glyph_idx) {
    if (!font->loca) return 0;
    
    if (font->loca_format == 0) {
        /* Short format: offset / 2 */
        return read_u16(font->loca + glyph_idx * 2) * 2;
    } else {
        /* Long format: direct offset */
        return read_u32(font->loca + glyph_idx * 4);
    }
}

int ttf_get_glyph_outline(const ttf_font_t *font, uint16_t glyph_idx,
                          ttf_glyph_point_t *points, uint16_t *contour_ends,
                          uint16_t max_points, uint16_t max_contours) {
    if (!font || !font->glyf || !font->loca || !points || !contour_ends) {
        return -1;
    }
    
    uint32_t offset = get_glyph_offset(font, glyph_idx);
    uint32_t next_offset = get_glyph_offset(font, glyph_idx + 1);
    
    if (offset == next_offset) {
        return 0;  /* Empty glyph (space, etc.) */
    }
    
    const uint8_t *glyph = font->glyf + offset;
    int16_t num_contours = read_i16(glyph);
    
    if (num_contours < 0) {
        /* Compound glyph - TODO: Handle component glyphs */
        return -2;
    }
    
    if (num_contours > max_contours) {
        return -3;
    }
    
    /* Read contour end points */
    const uint8_t *end_pts = glyph + 10;
    uint16_t total_points = 0;
    
    for (int i = 0; i < num_contours; i++) {
        contour_ends[i] = read_u16(end_pts + i * 2);
        if (contour_ends[i] >= total_points) {
            total_points = contour_ends[i] + 1;
        }
    }
    
    if (total_points > max_points) {
        return -4;
    }
    
    /* Skip instructions */
    const uint8_t *ptr = end_pts + num_contours * 2;
    uint16_t instr_len = read_u16(ptr);
    ptr += 2 + instr_len;
    
    /* Read flags */
    uint8_t flags[512];
    int flag_idx = 0;
    int point_idx = 0;
    
    while (point_idx < total_points) {
        uint8_t flag = *ptr++;
        flags[point_idx++] = flag;
        
        if (flag & 0x08) {  /* Repeat flag */
            uint8_t repeat = *ptr++;
            while (repeat-- > 0 && point_idx < total_points) {
                flags[point_idx++] = flag;
            }
        }
    }
    
    /* Read X coordinates */
    int16_t x = 0;
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
    
    /* Read Y coordinates */
    int16_t y = 0;
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
    
    return num_contours;
}

/* ============ OpenType Check ============ */

int ttf_is_opentype(const ttf_font_t *font) {
    if (!font || !font->data) return 0;
    return read_u32(font->data) == 0x4F54544F;  /* "OTTO" */
}

/* ============ Kerning ============ */

int16_t ttf_get_kerning(const ttf_font_t *font, uint16_t left_glyph, uint16_t right_glyph) {
    /* TODO: Implement kern table parsing */
    (void)font; (void)left_glyph; (void)right_glyph;
    return 0;
}
