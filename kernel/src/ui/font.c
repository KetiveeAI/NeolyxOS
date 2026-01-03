/*
 * NeolyxOS Kernel Boot Font
 * 
 * MINIMAL bitmap font for:
 *   - Boot console
 *   - Panic screens
 *   - Recovery mode
 * 
 * NOTE: This is KERNEL-LEVEL code for early boot only.
 * For desktop fonts, use userspace FontManager.app which has:
 *   - Full TTF/OTF parsing
 *   - 437+ bundled fonts
 *   - Font installation/management
 * 
 * See: /desktop/apps/FontManager.app/
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "ui/font.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t fb_width, fb_height, fb_pitch;
extern volatile uint32_t *framebuffer;

/* ============ Bitmap Fonts (Boot/Recovery) ============ */

/* 8x16 System UI Bitmap Font - Clean Sans-serif */
static const uint8_t bitmap_system_data[] = {
    /* Same high-quality 8x16 bitmap as before */
    /* 32 ' ' */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 33 '!' */ 0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,
    /* ... A-Z ... */
    /* 65 'A' */ 0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,
    /* 66 'B' */ 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00,
    /* 67 'C' */ 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00,
    /* 68 'D' */ 0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00,
    /* 69 'E' */ 0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,
    /* 70 'F' */ 0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,
    /* 71 'G' */ 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00,
    /* 72 'H' */ 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,
    /* 73 'I' */ 0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,
    /* 74 'J' */ 0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00,
    /* 75 'K' */ 0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,
    /* 76 'L' */ 0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,
    /* 77 'M' */ 0x00,0x00,0xC3,0xE7,0xFF,0xFF,0xDB,0xC3,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,
    /* 78 'N' */ 0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,
    /* 79 'O' */ 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,
    /* 80 'P' */ 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,
    /* 81 'Q' */ 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00,
    /* 82 'R' */ 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,
    /* 83 'S' */ 0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,
    /* 84 'T' */ 0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,
    /* 85 'U' */ 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,
    /* 86 'V' */ 0x00,0x00,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x00,0x00,0x00,0x00,
    /* 87 'W' */ 0x00,0x00,0xC3,0xC3,0xC3,0xC3,0xC3,0xDB,0xDB,0xFF,0x66,0x66,0x00,0x00,0x00,0x00,
    /* 88 'X' */ 0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x3C,0x66,0xC3,0xC3,0x00,0x00,0x00,0x00,
    /* 89 'Y' */ 0x00,0x00,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,
    /* 90 'Z' */ 0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00,
    /* ... lowercase a-z ... */
    /* 97 'a' */ 0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,
    /* 98 'b' */ 0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,
    /* 99 'c' */ 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,
    /* ... digits 0-9 ... */
    /* 48 '0' */ 0x00,0x00,0x3C,0x66,0xC3,0xC3,0xDB,0xDB,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00,
    /* 49 '1' */ 0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,
    /* 50 '2' */ 0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00,
};

const bitmap_font_t bitmap_font_system_16 = {
    .name = "NeoLyx System",
    .width = 8,
    .height = 16,
    .first_char = 32,
    .last_char = 126,
    .data = bitmap_system_data,
};

/* 8x16 Monospace Font - Terminal/Code */
static const uint8_t bitmap_mono_data[] = {
    /* Similar to system but strictly fixed-width with clear 0/O, 1/l/I distinction */
    /* Uses same structure */
    /* Placeholder - uses same data for now */
};

const bitmap_font_t bitmap_font_mono_16 = {
    .name = "NeoLyx Mono",
    .width = 8,
    .height = 16,
    .first_char = 32,
    .last_char = 126,
    .data = bitmap_system_data,  /* Same as system for boot console */
};

/* ============ Active Fonts ============ */

static font_t *system_font_16 = NULL;
static font_t *mono_font_16 = NULL;

/* ============ Font Helpers ============ */

static void *font_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

/* ============ Bitmap Font Rendering ============ */

static void render_bitmap_char(const bitmap_font_t *font, int x, int y, 
                               char c, uint32_t fg, uint32_t bg) {
    if (c < (char)font->first_char || c > (char)font->last_char) {
        c = ' ';
    }
    
    uint32_t idx = (c - font->first_char) * font->height;
    
    for (uint32_t row = 0; row < font->height; row++) {
        uint8_t bits = font->data[idx + row];
        
        for (uint32_t col = 0; col < font->width; col++) {
            int px = x + col;
            int py = y + row;
            
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                if (bits & (0x80 >> col)) {
                    framebuffer[py * (fb_pitch / 4) + px] = fg;
                } else if (bg != 0) {
                    framebuffer[py * (fb_pitch / 4) + px] = bg;
                }
            }
        }
    }
}

/* ============ TTF Parser (Simplified) ============ */

/* TTF table tag */
#define TTF_TAG(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(d))

static uint16_t read_u16_be(const uint8_t *p) {
    return (p[0] << 8) | p[1];
}

static uint32_t read_u32_be(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static int16_t read_i16_be(const uint8_t *p) {
    return (int16_t)read_u16_be(p);
}

ttf_font_t *font_load_ttf(const uint8_t *data, uint32_t size, const char *name) {
    if (size < 12) return NULL;
    
    /* Check for TrueType signature */
    uint32_t sfnt_version = read_u32_be(data);
    if (sfnt_version != 0x00010000 && sfnt_version != 0x4F54544F) {
        serial_puts("[FONT] Invalid TTF signature\n");
        return NULL;
    }
    
    ttf_font_t *font = (ttf_font_t *)kzalloc(sizeof(ttf_font_t));
    if (!font) return NULL;
    
    font->data = data;
    font->data_size = size;
    
    /* Copy name */
    int i = 0;
    while (name[i] && i < 63) {
        /* Font name stored but not used in kernel mode */
        i++;
    }
    
    /* Parse table directory */
    uint16_t num_tables = read_u16_be(data + 4);
    const uint8_t *tables = data + 12;
    
    for (int t = 0; t < num_tables; t++) {
        uint32_t tag = read_u32_be(tables + t * 16);
        uint32_t offset = read_u32_be(tables + t * 16 + 8);
        
        switch (tag) {
            case TTF_TAG('c','m','a','p'):
                font->cmap = (void *)(data + offset);
                break;
            case TTF_TAG('g','l','y','f'):
                font->glyf = (void *)(data + offset);
                break;
            case TTF_TAG('h','e','a','d'):
                font->head = (void *)(data + offset);
                font->units_per_em = read_i16_be(data + offset + 18);
                break;
            case TTF_TAG('h','h','e','a'):
                font->hhea = (void *)(data + offset);
                font->ascent = read_i16_be(data + offset + 4);
                font->descent = read_i16_be(data + offset + 6);
                font->line_gap = read_i16_be(data + offset + 8);
                break;
            case TTF_TAG('h','m','t','x'):
                font->hmtx = (void *)(data + offset);
                break;
            case TTF_TAG('l','o','c','a'):
                font->loca = (void *)(data + offset);
                break;
        }
    }
    
    serial_puts("[FONT] Loaded TTF: ");
    serial_puts(name);
    serial_puts("\n");
    
    return font;
}

/* ============ Glyph Rasterizer ============ */

static cached_glyph_t *rasterize_glyph(font_cache_t *cache, uint32_t codepoint) {
    /* Find free slot in cache */
    if (cache->glyph_count >= GLYPH_CACHE_SIZE) {
        /* Cache full - evict oldest */
        cache->glyph_count = 0;
    }
    
    cached_glyph_t *glyph = &cache->glyphs[cache->glyph_count++];
    glyph->codepoint = codepoint;
    
    /* Kernel mode: fallback to bitmap, no TTF rasterization needed */
    /* Full TTF rendering is in userspace FontManager.app */
    glyph->width = 8;
    glyph->height = cache->size;
    glyph->bearing_x = 0;
    glyph->bearing_y = cache->size;
    glyph->advance = 8;
    glyph->bitmap = NULL;
    
    return glyph;
}

/* ============ Font System API ============ */

void font_init(void) {
    serial_puts("[FONT] Initializing font system...\n");
    
    /* Create default font instances */
    system_font_16 = (font_t *)kzalloc(sizeof(font_t));
    system_font_16->category = FONT_CAT_SYSTEM;
    system_font_16->size = 16;
    system_font_16->weight = FONT_WEIGHT_REGULAR;
    system_font_16->style = FONT_STYLE_REGULAR;
    system_font_16->use_ttf = 0;  /* Use bitmap for now */
    system_font_16->backend.bitmap = &bitmap_font_system_16;
    
    mono_font_16 = (font_t *)kzalloc(sizeof(font_t));
    mono_font_16->category = FONT_CAT_MONO;
    mono_font_16->size = 16;
    mono_font_16->weight = FONT_WEIGHT_REGULAR;
    mono_font_16->style = FONT_STYLE_REGULAR;
    mono_font_16->use_ttf = 0;
    mono_font_16->backend.bitmap = &bitmap_font_mono_16;
    
    serial_puts("[FONT] System font: NeoLyx System 16px\n");
    serial_puts("[FONT] Mono font: NeoLyx Mono 16px\n");
    serial_puts("[FONT] Ready\n");
}

font_t *font_get_system(uint32_t size) {
    /* Kernel boot font is fixed 16px - size parameter ignored */
    (void)size;
    return system_font_16;
}

font_t *font_get_mono(uint32_t size) {
    if (size == 16) return mono_font_16;
    return mono_font_16;
}

font_t *font_create(font_category_t category, uint32_t size, font_weight_t weight) {
    if (category == FONT_CAT_SYSTEM) {
        return font_get_system(size);
    }
    return font_get_mono(size);
}

void font_destroy(font_t *font) {
    /* Don't destroy static fonts */
    if (font == system_font_16 || font == mono_font_16) return;
    if (font) kfree(font);
}

/* ============ Text Rendering ============ */

uint32_t font_text_width(font_t *font, const char *text) {
    uint32_t width = 0;
    if (font->use_ttf) {
        /* TTF width calculation */
        while (*text) { width += 8; text++; }  /* Placeholder */
    } else {
        while (*text) {
            width += font->backend.bitmap->width;
            text++;
        }
    }
    return width;
}

uint32_t font_text_height(font_t *font) {
    if (font->use_ttf) {
        return font->size;
    }
    return font->backend.bitmap->height;
}

void font_draw_text(font_t *font, int x, int y, const char *text, uint32_t color) {
    /* Kernel mode: always use bitmap font */
    const bitmap_font_t *bitmap = font->use_ttf ? &bitmap_font_system_16 : font->backend.bitmap;
    while (*text) {
        render_bitmap_char(bitmap, x, y, *text, color, 0);
        x += bitmap->width;
        text++;
    }
}

void font_draw_text_bg(font_t *font, int x, int y, const char *text,
                       uint32_t fg, uint32_t bg) {
    if (font->use_ttf) {
        /* TTF with background - TODO */
    } else {
        while (*text) {
            render_bitmap_char(font->backend.bitmap, x, y, *text, fg, bg);
            x += font->backend.bitmap->width;
            text++;
        }
    }
}

void font_draw_centered(font_t *font, int y, const char *text, uint32_t color) {
    uint32_t width = font_text_width(font, text);
    int x = (fb_width - width) / 2;
    font_draw_text(font, x, y, text, color);
}

void font_draw_text_shadow(font_t *font, int x, int y, const char *text,
                           uint32_t fg, uint32_t shadow) {
    /* Draw shadow first (offset by 1 pixel) */
    font_draw_text(font, x + 1, y + 1, text, shadow);
    /* Then foreground */
    font_draw_text(font, x, y, text, fg);
}

int font_draw_wrapped(font_t *font, int x, int y, int max_width,
                      const char *text, uint32_t color) {
    int lines = 1;
    int curr_x = x;
    int char_width = font->use_ttf ? 8 : font->backend.bitmap->width;
    int line_height = font_text_height(font);
    
    while (*text) {
        if (*text == '\n') {
            curr_x = x;
            y += line_height;
            lines++;
            text++;
            continue;
        }
        
        if (curr_x + char_width > x + max_width) {
            curr_x = x;
            y += line_height;
            lines++;
        }
        
        font_draw_text(font, curr_x, y, " ", color);  /* Draw one char */
        /* Actually draw the current character */
        if (font->use_ttf) {
            render_bitmap_char(&bitmap_font_system_16, curr_x, y, *text, color, 0);
        } else {
            render_bitmap_char(font->backend.bitmap, curr_x, y, *text, color, 0);
        }
        
        curr_x += char_width;
        text++;
    }
    
    return lines;
}

int font_count_lines(font_t *font, int max_width, const char *text) {
    int lines = 1;
    int curr_width = 0;
    int char_width = font->use_ttf ? 8 : font->backend.bitmap->width;
    
    while (*text) {
        if (*text == '\n') {
            curr_width = 0;
            lines++;
            text++;
            continue;
        }
        
        if (curr_width + char_width > max_width) {
            curr_width = 0;
            lines++;
        }
        
        curr_width += char_width;
        text++;
    }
    
    return lines;
}

int font_has_glyph(font_t *font, uint32_t codepoint) {
    /* Kernel bitmap fonts support ASCII 32-126 */
    if (codepoint >= 32 && codepoint <= 126) return 1;
    return 0;
}
