/*
 * NeolyxOS NXI Icon Parser (nxnxi.c)
 * 
 * Parser for .nxi vector icon format. Rasterizes to RGBA32.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nximage.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* NXI signature check */
#define NXI_HEADER "# NeolyxOS NXI"

/* Shape types */
typedef enum {
    NXI_CIRCLE,
    NXI_RECT,
    NXI_PATH,
    NXI_LINE
} nxi_shape_type_t;

/* Color structure */
typedef struct {
    uint8_t r, g, b, a;
} nxi_color_t;

/* Circle shape */
typedef struct {
    float cx, cy, radius;
    nxi_color_t fill;
    nxi_color_t stroke;
    float stroke_width;
} nxi_circle_t;

/* Rectangle shape */
typedef struct {
    float x, y, w, h;
    nxi_color_t fill;
    nxi_color_t stroke;
    float stroke_width;
} nxi_rect_t;

/* Path command */
typedef struct {
    char type;
    float args[6];
    int arg_count;
} nxi_path_cmd_t;

/* Parsed NXI document */
typedef struct {
    int size;
    int viewbox_x, viewbox_y, viewbox_w, viewbox_h;
    nxi_circle_t circles[32];
    int circle_count;
    nxi_rect_t rects[32];
    int rect_count;
    nxi_path_cmd_t paths[256];
    int path_count;
    nxi_color_t colors[16];
    char color_names[16][32];
    int color_count;
} nxi_doc_t;

/* ============ String Helpers ============ */

static int nxi_isspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int nxi_isdigit(char c) { return c >= '0' && c <= '9'; }

static const char *skip_ws(const char *s) {
    while (*s && nxi_isspace(*s)) s++;
    return s;
}

static const char *skip_line(const char *s) {
    while (*s && *s != '\n') s++;
    if (*s == '\n') s++;
    return s;
}

static float parse_float(const char **s) {
    const char *p = *s;
    p = skip_ws(p);
    float val = 0, frac = 0, div = 1;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    while (nxi_isdigit(*p)) { val = val * 10 + (*p - '0'); p++; }
    if (*p == '.') {
        p++;
        while (nxi_isdigit(*p)) { frac = frac * 10 + (*p - '0'); div *= 10; p++; }
    }
    *s = p;
    return neg ? -(val + frac/div) : (val + frac/div);
}

static nxi_color_t parse_hex_color(const char *hex) {
    nxi_color_t c = {0, 0, 0, 255};
    if (*hex == '#') hex++;
    int len = 0;
    while (hex[len] && ((hex[len] >= '0' && hex[len] <= '9') || 
           (hex[len] >= 'a' && hex[len] <= 'f') || 
           (hex[len] >= 'A' && hex[len] <= 'F'))) len++;
    
    if (len == 3) {
        c.r = ((hex[0] >= 'a' ? hex[0] - 'a' + 10 : hex[0] >= 'A' ? hex[0] - 'A' + 10 : hex[0] - '0') * 17);
        c.g = ((hex[1] >= 'a' ? hex[1] - 'a' + 10 : hex[1] >= 'A' ? hex[1] - 'A' + 10 : hex[1] - '0') * 17);
        c.b = ((hex[2] >= 'a' ? hex[2] - 'a' + 10 : hex[2] >= 'A' ? hex[2] - 'A' + 10 : hex[2] - '0') * 17);
    } else if (len == 6) {
        for (int i = 0; i < 3; i++) {
            int hi = hex[i*2] >= 'a' ? hex[i*2] - 'a' + 10 : hex[i*2] >= 'A' ? hex[i*2] - 'A' + 10 : hex[i*2] - '0';
            int lo = hex[i*2+1] >= 'a' ? hex[i*2+1] - 'a' + 10 : hex[i*2+1] >= 'A' ? hex[i*2+1] - 'A' + 10 : hex[i*2+1] - '0';
            ((uint8_t*)&c)[i] = (hi << 4) | lo;
        }
    }
    return c;
}

/* ============ Rasterizer ============ */

static void set_pixel(uint8_t *buf, int w, int h, int x, int y, nxi_color_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int idx = (y * w + x) * 4;
    /* Alpha blend */
    if (c.a == 255) {
        buf[idx] = c.r;
        buf[idx+1] = c.g;
        buf[idx+2] = c.b;
        buf[idx+3] = 255;
    } else if (c.a > 0) {
        int a = c.a, ia = 255 - a;
        buf[idx] = (c.r * a + buf[idx] * ia) / 255;
        buf[idx+1] = (c.g * a + buf[idx+1] * ia) / 255;
        buf[idx+2] = (c.b * a + buf[idx+2] * ia) / 255;
        buf[idx+3] = buf[idx+3] + (255 - buf[idx+3]) * a / 255;
    }
}

static void fill_circle(uint8_t *buf, int w, int h, float cx, float cy, float r, nxi_color_t c) {
    int x0 = (int)(cx - r), x1 = (int)(cx + r + 1);
    int y0 = (int)(cy - r), y1 = (int)(cy + r + 1);
    float r2 = r * r;
    
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx + 0.5f, dy = y - cy + 0.5f;
            if (dx*dx + dy*dy <= r2) {
                set_pixel(buf, w, h, x, y, c);
            }
        }
    }
}

static void fill_rect(uint8_t *buf, int w, int h, int rx, int ry, int rw, int rh, nxi_color_t c) {
    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            set_pixel(buf, w, h, x, y, c);
        }
    }
}

static void stroke_circle(uint8_t *buf, int w, int h, float cx, float cy, float r, nxi_color_t c, float sw) {
    int x0 = (int)(cx - r - sw), x1 = (int)(cx + r + sw + 1);
    int y0 = (int)(cy - r - sw), y1 = (int)(cy + r + sw + 1);
    float outer2 = (r + sw/2) * (r + sw/2);
    float inner2 = (r - sw/2) * (r - sw/2);
    
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx + 0.5f, dy = y - cy + 0.5f;
            float d2 = dx*dx + dy*dy;
            if (d2 <= outer2 && d2 >= inner2) {
                set_pixel(buf, w, h, x, y, c);
            }
        }
    }
}

/* ============ NXI Parser ============ */

static int parse_nxi(const char *data, size_t len, nxi_doc_t *doc) {
    memset(doc, 0, sizeof(*doc));
    doc->size = 64;
    doc->viewbox_w = 64;
    doc->viewbox_h = 64;
    
    const char *p = data;
    const char *end = data + len;
    
    while (p < end) {
        p = skip_ws(p);
        if (p >= end) break;
        
        if (*p == '#' || *p == '\n') {
            p = skip_line(p);
            continue;
        }
        
        /* Section header */
        if (*p == '[') {
            p = skip_line(p);
            continue;
        }
        
        /* Key = value */
        const char *key_start = p;
        while (p < end && *p != '=' && *p != '\n') p++;
        if (*p != '=') {
            p = skip_line(p);
            continue;
        }
        
        int key_len = p - key_start;
        while (key_len > 0 && nxi_isspace(key_start[key_len-1])) key_len--;
        p++; /* skip = */
        p = skip_ws(p);
        
        const char *val_start = p;
        while (p < end && *p != '\n') p++;
        int val_len = p - val_start;
        while (val_len > 0 && nxi_isspace(val_start[val_len-1])) val_len--;
        
        /* Parse known keys */
        if (key_len == 4 && memcmp(key_start, "size", 4) == 0) {
            doc->size = (int)parse_float(&val_start);
        } else if (key_len == 7 && memcmp(key_start, "viewbox", 7) == 0) {
            doc->viewbox_x = (int)parse_float(&val_start);
            doc->viewbox_y = (int)parse_float(&val_start);
            doc->viewbox_w = (int)parse_float(&val_start);
            doc->viewbox_h = (int)parse_float(&val_start);
        }
        
        /* Circle: center=X,Y radius=R fill=COLOR */
        if (key_len == 6 && memcmp(key_start, "circle", 6) == 0 && doc->circle_count < 32) {
            nxi_circle_t *c = &doc->circles[doc->circle_count++];
            c->fill = (nxi_color_t){0, 0, 0, 255};
            c->stroke = (nxi_color_t){0, 0, 0, 0};
            c->stroke_width = 0;
            
            /* Parse circle attributes */
            const char *attr = val_start;
            while (attr < val_start + val_len) {
                attr = skip_ws(attr);
                if (memcmp(attr, "center=", 7) == 0) {
                    attr += 7;
                    c->cx = parse_float(&attr);
                    if (*attr == ',') attr++;
                    c->cy = parse_float(&attr);
                } else if (memcmp(attr, "radius=", 7) == 0) {
                    attr += 7;
                    c->radius = parse_float(&attr);
                } else if (memcmp(attr, "fill=", 5) == 0) {
                    attr += 5;
                    c->fill = parse_hex_color(attr);
                    while (*attr && !nxi_isspace(*attr)) attr++;
                } else if (memcmp(attr, "stroke=", 7) == 0) {
                    attr += 7;
                    c->stroke = parse_hex_color(attr);
                    while (*attr && !nxi_isspace(*attr)) attr++;
                } else if (memcmp(attr, "stroke-width=", 13) == 0) {
                    attr += 13;
                    c->stroke_width = parse_float(&attr);
                } else {
                    attr++;
                }
            }
        }
        
        p = skip_line(p);
    }
    
    return NXI_OK;
}

/* ============ Public API ============ */

int nxnxi_load(const uint8_t *data, size_t len, nximage_t *img) {
    nxi_doc_t doc;
    int err = parse_nxi((const char *)data, len, &doc);
    if (err != NXI_OK) return err;
    
    /* Allocate output */
    int size = doc.size > 0 ? doc.size : 64;
    img->width = size;
    img->height = size;
    img->bpp = 4;
    img->format = NXIMG_RGBA32;
    img->type = NXIMG_TYPE_NXI;
    img->pixel_size = size * size * 4;
    img->pixels = (uint8_t *)malloc(img->pixel_size);
    if (!img->pixels) return NXI_ERR_MEMORY;
    
    /* Clear to transparent */
    memset(img->pixels, 0, img->pixel_size);
    
    /* Scale factor */
    float scale = (float)size / (float)doc.viewbox_w;
    
    /* Render circles */
    for (int i = 0; i < doc.circle_count; i++) {
        nxi_circle_t *c = &doc.circles[i];
        float cx = (c->cx - doc.viewbox_x) * scale;
        float cy = (c->cy - doc.viewbox_y) * scale;
        float r = c->radius * scale;
        
        if (c->fill.a > 0) {
            fill_circle(img->pixels, size, size, cx, cy, r, c->fill);
        }
        if (c->stroke.a > 0 && c->stroke_width > 0) {
            stroke_circle(img->pixels, size, size, cx, cy, r, c->stroke, c->stroke_width * scale);
        }
    }
    
    return NXI_OK;
}

int nxnxi_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h) {
    /* Quick parse for size */
    const char *p = (const char *)data;
    const char *end = p + len;
    int size = 64;
    
    while (p < end) {
        p = skip_ws(p);
        if (memcmp(p, "size", 4) == 0) {
            p += 4;
            while (*p == ' ' || *p == '=') p++;
            size = (int)parse_float(&p);
            break;
        }
        p = skip_line(p);
    }
    
    if (w) *w = size;
    if (h) *h = size;
    return NXI_OK;
}

/* External malloc/free */
#ifndef malloc
extern void *nx_malloc(size_t size);
extern void nx_free(void *ptr);
#define malloc nx_malloc
#define free nx_free
#endif
