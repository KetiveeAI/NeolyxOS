/*
 * NXSVG - Minimal SVG Parser for NXRender
 * 
 * Implements SVG path parsing and rendering with anti-aliased bezier curves.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxsvg.h"
#include "nxgfx/nxgfx.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* Helper: skip whitespace and commas */
static const char* skip_ws(const char* p) {
    while (*p && (isspace(*p) || *p == ',')) p++;
    return p;
}

/* Helper: parse a float value */
static const char* parse_float(const char* p, float* out) {
    p = skip_ws(p);
    char* end;
    *out = strtof(p, &end);
    return end;
}

/* Helper: add a command to path */
static void add_cmd(nxsvg_path_t* path, nxsvg_cmd_t cmd) {
    if (path->count >= path->capacity) {
        path->capacity = path->capacity ? path->capacity * 2 : 32;
        path->commands = realloc(path->commands, path->capacity * sizeof(nxsvg_cmd_t));
    }
    path->commands[path->count++] = cmd;
}

/* Parse SVG path d attribute */
static void parse_path_d(nxsvg_path_t* path, const char* d) {
    if (!d) return;
    
    const char* p = d;
    float cx = 0, cy = 0;  /* Current position */
    float sx = 0, sy = 0;  /* Subpath start */
    char cmd = 0;
    bool relative = false;
    
    while (*p) {
        p = skip_ws(p);
        if (!*p) break;
        
        if (isalpha(*p)) {
            cmd = *p;
            relative = islower(cmd);
            cmd = toupper(cmd);
            p++;
        }
        
        nxsvg_cmd_t c = {0};
        
        switch (cmd) {
            case 'M':  /* Move to */
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) { c.x += cx; c.y += cy; }
                c.type = NXSVG_CMD_MOVE;
                add_cmd(path, c);
                cx = sx = c.x; cy = sy = c.y;
                cmd = 'L';  /* Subsequent coords are line-to */
                break;
                
            case 'L':  /* Line to */
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) { c.x += cx; c.y += cy; }
                c.type = NXSVG_CMD_LINE;
                add_cmd(path, c);
                cx = c.x; cy = c.y;
                break;
                
            case 'H':  /* Horizontal line */
                p = parse_float(p, &c.x);
                if (relative) c.x += cx;
                c.y = cy;
                c.type = NXSVG_CMD_LINE;
                add_cmd(path, c);
                cx = c.x;
                break;
                
            case 'V':  /* Vertical line */
                p = parse_float(p, &c.y);
                if (relative) c.y += cy;
                c.x = cx;
                c.type = NXSVG_CMD_LINE;
                add_cmd(path, c);
                cy = c.y;
                break;
                
            case 'C':  /* Cubic bezier */
                p = parse_float(p, &c.x1);
                p = parse_float(p, &c.y1);
                p = parse_float(p, &c.x2);
                p = parse_float(p, &c.y2);
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) {
                    c.x1 += cx; c.y1 += cy;
                    c.x2 += cx; c.y2 += cy;
                    c.x += cx; c.y += cy;
                }
                c.type = NXSVG_CMD_CUBIC;
                add_cmd(path, c);
                cx = c.x; cy = c.y;
                break;
                
            case 'Q':  /* Quadratic bezier */
                p = parse_float(p, &c.x1);
                p = parse_float(p, &c.y1);
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) {
                    c.x1 += cx; c.y1 += cy;
                    c.x += cx; c.y += cy;
                }
                c.type = NXSVG_CMD_QUAD;
                add_cmd(path, c);
                cx = c.x; cy = c.y;
                break;
                
            case 'A':  /* Arc */
                p = parse_float(p, &c.rx);
                p = parse_float(p, &c.ry);
                p = parse_float(p, &c.rotation);
                { float f; p = parse_float(p, &f); c.large_arc = (f != 0); }
                { float f; p = parse_float(p, &f); c.sweep = (f != 0); }
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) { c.x += cx; c.y += cy; }
                c.type = NXSVG_CMD_ARC;
                add_cmd(path, c);
                cx = c.x; cy = c.y;
                break;
                
            case 'Z':  /* Close path */
                c.type = NXSVG_CMD_CLOSE;
                c.x = sx; c.y = sy;
                add_cmd(path, c);
                cx = sx; cy = sy;
                break;
                
            default:
                p++;  /* Unknown command, skip */
                break;
        }
    }
}

/* Draw cubic bezier curve with AA */
static void draw_cubic_bezier(nx_context_t* ctx, 
                               float x0, float y0, 
                               float x1, float y1, 
                               float x2, float y2, 
                               float x3, float y3,
                               nx_color_t color, float scale) {
    /* Adaptive subdivision based on curve length */
    float dx = x3 - x0, dy = y3 - y0;
    float length = sqrtf(dx*dx + dy*dy) * scale;
    int steps = (int)(length / 2.0f);
    if (steps < 8) steps = 8;
    if (steps > 64) steps = 64;
    
    float prev_x = x0 * scale, prev_y = y0 * scale;
    
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;
        
        /* Cubic bezier formula */
        float x = mt3 * x0 + 3 * mt2 * t * x1 + 3 * mt * t2 * x2 + t3 * x3;
        float y = mt3 * y0 + 3 * mt2 * t * y1 + 3 * mt * t2 * y2 + t3 * y3;
        
        x *= scale;
        y *= scale;
        
        nxgfx_draw_line(ctx, 
                        (nx_point_t){(int)prev_x, (int)prev_y},
                        (nx_point_t){(int)x, (int)y}, 
                        color, 1);
        
        prev_x = x;
        prev_y = y;
    }
}

/* Draw quadratic bezier curve with AA */
static void draw_quad_bezier(nx_context_t* ctx,
                              float x0, float y0,
                              float x1, float y1,
                              float x2, float y2,
                              nx_color_t color, float scale) {
    /* Convert to cubic for rendering */
    float cx1 = x0 + 2.0f/3.0f * (x1 - x0);
    float cy1 = y0 + 2.0f/3.0f * (y1 - y0);
    float cx2 = x2 + 2.0f/3.0f * (x1 - x2);
    float cy2 = y2 + 2.0f/3.0f * (y1 - y2);
    draw_cubic_bezier(ctx, x0, y0, cx1, cy1, cx2, cy2, x2, y2, color, scale);
}

/* Arc to bezier conversion - SVG arc parameterization
 * Converts SVG arc (A command) to cubic bezier curves.
 * Based on W3C SVG implementation notes.
 */
typedef struct {
    float x, y;
} arc_point_t;

/* Flatten arc to points for polygon fill */
static int arc_to_points(float x0, float y0,          /* Start point */
                         float rx, float ry,          /* Radii */
                         float rotation,              /* X-axis rotation (degrees) */
                         bool large_arc, bool sweep,  /* Flags */
                         float x, float y,            /* End point */
                         arc_point_t* points,         /* Output array */
                         int max_points) {            /* Max output size */
    
    if (rx == 0 || ry == 0) {
        /* Degenerate to line */
        if (max_points > 0) {
            points[0].x = x;
            points[0].y = y;
            return 1;
        }
        return 0;
    }
    
    /* Ensure radii are positive */
    rx = fabsf(rx);
    ry = fabsf(ry);
    
    float phi = rotation * 3.14159265f / 180.0f;
    float cos_phi = cosf(phi);
    float sin_phi = sinf(phi);
    
    /* Transform to unit circle space */
    float dx2 = (x0 - x) / 2.0f;
    float dy2 = (y0 - y) / 2.0f;
    
    float x1p = cos_phi * dx2 + sin_phi * dy2;
    float y1p = -sin_phi * dx2 + cos_phi * dy2;
    
    /* Scale radii if arc is impossible */
    float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        float sqrt_lambda = sqrtf(lambda);
        rx *= sqrt_lambda;
        ry *= sqrt_lambda;
    }
    
    /* Compute center in unit circle space */
    float sq_rx = rx * rx;
    float sq_ry = ry * ry;
    float sq_x1p = x1p * x1p;
    float sq_y1p = y1p * y1p;
    
    float num = sq_rx * sq_ry - sq_rx * sq_y1p - sq_ry * sq_x1p;
    float den = sq_rx * sq_y1p + sq_ry * sq_x1p;
    
    float sq = 0;
    if (den > 0 && num > 0) {
        sq = sqrtf(num / den);
    }
    if (large_arc == sweep) {
        sq = -sq;
    }
    
    float cxp = sq * rx * y1p / ry;
    float cyp = -sq * ry * x1p / rx;
    
    /* Transform center back to original space */
    float cx_center = cos_phi * cxp - sin_phi * cyp + (x0 + x) / 2.0f;
    float cy_center = sin_phi * cxp + cos_phi * cyp + (y0 + y) / 2.0f;
    
    /* Compute angle range */
    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    
    float n = sqrtf(ux * ux + uy * uy);
    float theta1 = (uy >= 0 ? 1 : -1) * acosf(ux / n);
    
    n = sqrtf((ux*ux + uy*uy) * (vx*vx + vy*vy));
    float p = ux * vx + uy * vy;
    float dtheta = (ux * vy - uy * vx >= 0 ? 1 : -1) * acosf(p / n);
    
    /* Adjust dtheta based on sweep flag */
    if (!sweep && dtheta > 0) {
        dtheta -= 2.0f * 3.14159265f;
    } else if (sweep && dtheta < 0) {
        dtheta += 2.0f * 3.14159265f;
    }
    
    /* Generate points along arc */
    int num_segments = (int)(fabsf(dtheta) * 8 / 3.14159265f) + 4;
    if (num_segments > max_points) num_segments = max_points;
    if (num_segments < 4) num_segments = 4;
    
    int count = 0;
    for (int i = 1; i <= num_segments; i++) {
        float t = (float)i / num_segments;
        float angle = theta1 + t * dtheta;
        
        /* Point on unit circle */
        float px = cosf(angle);
        float py = sinf(angle);
        
        /* Scale by radii */
        px *= rx;
        py *= ry;
        
        /* Rotate by phi */
        float rx_rot = cos_phi * px - sin_phi * py;
        float ry_rot = sin_phi * px + cos_phi * py;
        
        /* Translate to center */
        points[count].x = rx_rot + cx_center;
        points[count].y = ry_rot + cy_center;
        count++;
        
        if (count >= max_points) break;
    }
    
    return count;
}

/* Parse fill color from string */
static nx_color_t parse_color(const char* str) {
    if (!str) return nx_rgba(255, 255, 255, 255);
    
    if (strcmp(str, "none") == 0) return nx_rgba(0, 0, 0, 0);
    if (strcmp(str, "white") == 0) return nx_rgba(255, 255, 255, 255);
    if (strcmp(str, "black") == 0) return nx_rgba(0, 0, 0, 255);
    
    /* #RRGGBB format */
    if (str[0] == '#' && strlen(str) == 7) {
        unsigned int r, g, b;
        sscanf(str + 1, "%02x%02x%02x", &r, &g, &b);
        return nx_rgba(r, g, b, 255);
    }
    
    return nx_rgba(255, 255, 255, 255);
}

/* Find attribute value in SVG tag */
static const char* find_attr(const char* tag, const char* name, char* buf, size_t buf_size) {
    char search[64];
    snprintf(search, sizeof(search), "%s=\"", name);
    
    const char* start = strstr(tag, search);
    if (!start) return NULL;
    
    start += strlen(search);
    const char* end = strchr(start, '"');
    if (!end) return NULL;
    
    size_t len = end - start;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    
    return buf;
}

/* Parse transform attribute (translate, rotate, scale, matrix) */
static void parse_transform(const char* str, float* tf) {
    /* Identity matrix default */
    tf[0] = 1; tf[1] = 0; tf[2] = 0;
    tf[3] = 1; tf[4] = 0; tf[5] = 0;
    
    if (!str) return;
    
    const char* p = str;
    while (*p) {
        p = skip_ws(p);
        
        if (strncmp(p, "translate", 9) == 0) {
            p += 9;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;
            float tx, ty = 0;
            p = parse_float(p, &tx);
            p = skip_ws(p);
            if (*p != ')') p = parse_float(p, &ty);
            tf[4] += tx;
            tf[5] += ty;
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;
            float sx, sy;
            p = parse_float(p, &sx);
            p = skip_ws(p);
            sy = sx;
            if (*p != ')') p = parse_float(p, &sy);
            tf[0] *= sx; tf[3] *= sy;
        } else if (strncmp(p, "rotate", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;
            float angle;
            p = parse_float(p, &angle);
            float rad = angle * 3.14159265f / 180.0f;
            float c = cosf(rad), s = sinf(rad);
            float a = tf[0], b = tf[1], x = tf[2], y = tf[3];
            tf[0] = a*c - y*s; tf[1] = b*c + x*s;
            tf[2] = a*s + y*c; tf[3] = -b*s + x*c;
        } else if (strncmp(p, "matrix", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;
            p = parse_float(p, &tf[0]);
            p = parse_float(p, &tf[1]);
            p = parse_float(p, &tf[2]);
            p = parse_float(p, &tf[3]);
            p = parse_float(p, &tf[4]);
            p = parse_float(p, &tf[5]);
        } else {
            p++;
        }
        
        while (*p && *p != ')' && *p != ' ') p++;
        if (*p == ')') p++;
    }
}

/* Apply transform to point */
static void transform_point(float x, float y, const float* tf, float* ox, float* oy) {
    *ox = x * tf[0] + y * tf[2] + tf[4];
    *oy = x * tf[1] + y * tf[3] + tf[5];
}

/* Parse gradient from <linearGradient> or <radialGradient> */
static void parse_gradient(const char* tag, nxsvg_gradient_t* grad, bool is_radial) {
    char buf[256];
    memset(grad, 0, sizeof(nxsvg_gradient_t));
    grad->is_radial = is_radial;
    
    /* Parse id */
    if (find_attr(tag, "id", buf, sizeof(buf))) {
        strncpy(grad->id, buf, 63);
    }
    
    if (is_radial) {
        if (find_attr(tag, "cx", buf, sizeof(buf))) grad->cx = strtof(buf, NULL);
        else grad->cx = 0.5f;
        if (find_attr(tag, "cy", buf, sizeof(buf))) grad->cy = strtof(buf, NULL);
        else grad->cy = 0.5f;
        if (find_attr(tag, "r", buf, sizeof(buf))) grad->r = strtof(buf, NULL);
        else grad->r = 0.5f;
        grad->fx = grad->cx;
        grad->fy = grad->cy;
        if (find_attr(tag, "fx", buf, sizeof(buf))) grad->fx = strtof(buf, NULL);
        if (find_attr(tag, "fy", buf, sizeof(buf))) grad->fy = strtof(buf, NULL);
    } else {
        if (find_attr(tag, "x1", buf, sizeof(buf))) grad->x1 = strtof(buf, NULL);
        if (find_attr(tag, "y1", buf, sizeof(buf))) grad->y1 = strtof(buf, NULL);
        if (find_attr(tag, "x2", buf, sizeof(buf))) grad->x2 = strtof(buf, NULL);
        else grad->x2 = 1.0f;
        if (find_attr(tag, "y2", buf, sizeof(buf))) grad->y2 = strtof(buf, NULL);
    }
    
    /* Parse stops */
    const char* stop = tag;
    while ((stop = strstr(stop, "<stop")) != NULL && grad->stop_count < 16) {
        nxsvg_gradient_stop_t* s = &grad->stops[grad->stop_count];
        s->offset = 0;
        s->color = nx_rgba(0, 0, 0, 255);
        
        if (find_attr(stop, "offset", buf, sizeof(buf))) {
            s->offset = strtof(buf, NULL);
            if (strchr(buf, '%')) s->offset /= 100.0f;
        }
        if (find_attr(stop, "stop-color", buf, sizeof(buf))) {
            s->color = parse_color(buf);
        }
        if (find_attr(stop, "style", buf, sizeof(buf))) {
            char* sc = strstr(buf, "stop-color:");
            if (sc) {
                sc += 11;
                while (*sc == ' ') sc++;
                char color_buf[32];
                int i = 0;
                while (*sc && *sc != ';' && i < 31) color_buf[i++] = *sc++;
                color_buf[i] = 0;
                s->color = parse_color(color_buf);
            }
        }
        
        grad->stop_count++;
        stop++;
    }
}

/* Sample gradient color at position t (0-1) */
static nx_color_t sample_gradient_color(const nxsvg_gradient_t* grad, float t) {
    if (grad->stop_count == 0) return nx_rgba(0, 0, 0, 255);
    if (grad->stop_count == 1) return grad->stops[0].color;
    if (t <= grad->stops[0].offset) return grad->stops[0].color;
    if (t >= grad->stops[grad->stop_count - 1].offset) {
        return grad->stops[grad->stop_count - 1].color;
    }
    
    for (uint32_t i = 1; i < grad->stop_count; i++) {
        if (t <= grad->stops[i].offset) {
            float t0 = grad->stops[i-1].offset;
            float t1 = grad->stops[i].offset;
            float blend = (t - t0) / (t1 - t0);
            nx_color_t c0 = grad->stops[i-1].color;
            nx_color_t c1 = grad->stops[i].color;
            return nx_rgba(
                (uint8_t)(c0.r + (c1.r - c0.r) * blend),
                (uint8_t)(c0.g + (c1.g - c0.g) * blend),
                (uint8_t)(c0.b + (c1.b - c0.b) * blend),
                (uint8_t)(c0.a + (c1.a - c0.a) * blend)
            );
        }
    }
    return grad->stops[grad->stop_count - 1].color;
}

/* Find gradient by id */
static int find_gradient_by_url(nxsvg_image_t* img, const char* url) {
    if (!url || strncmp(url, "url(#", 5) != 0) return -1;
    char id[64];
    const char* start = url + 5;
    const char* end = strchr(start, ')');
    if (!end) return -1;
    size_t len = end - start;
    if (len >= 64) len = 63;
    memcpy(id, start, len);
    id[len] = 0;
    
    for (uint32_t i = 0; i < img->gradient_count; i++) {
        if (strcmp(img->gradients[i].id, id) == 0) return (int)i;
    }
    return -1;
}

/* Parse SVG from memory */
nxsvg_image_t* nxsvg_parse(const char* svg_data, size_t len) {
    if (!svg_data || len == 0) return NULL;
    
    nxsvg_image_t* img = calloc(1, sizeof(nxsvg_image_t));
    img->width = 24;
    img->height = 24;
    
    /* Parse viewBox and dimensions */
    char buf[256];
    const char* svg_tag = strstr(svg_data, "<svg");
    if (svg_tag) {
        if (find_attr(svg_tag, "width", buf, sizeof(buf))) {
            img->width = strtof(buf, NULL);
        }
        if (find_attr(svg_tag, "height", buf, sizeof(buf))) {
            img->height = strtof(buf, NULL);
        }
    }
    
    /* Parse gradients from <defs> */
    const char* defs = strstr(svg_data, "<defs");
    if (defs) {
        const char* defs_end = strstr(defs, "</defs>");
        if (!defs_end) defs_end = svg_data + len;
        
        /* Linear gradients */
        const char* g = defs;
        while ((g = strstr(g, "<linearGradient")) != NULL && g < defs_end) {
            const char* grad_end = strstr(g, "</linearGradient>");
            if (!grad_end || grad_end > defs_end) grad_end = strstr(g, "/>");
            if (!grad_end) break;
            
            img->gradients = realloc(img->gradients, 
                (img->gradient_count + 1) * sizeof(nxsvg_gradient_t));
            parse_gradient(g, &img->gradients[img->gradient_count], false);
            img->gradient_count++;
            g = grad_end;
        }
        
        /* Radial gradients */
        g = defs;
        while ((g = strstr(g, "<radialGradient")) != NULL && g < defs_end) {
            const char* grad_end = strstr(g, "</radialGradient>");
            if (!grad_end || grad_end > defs_end) grad_end = strstr(g, "/>");
            if (!grad_end) break;
            
            img->gradients = realloc(img->gradients,
                (img->gradient_count + 1) * sizeof(nxsvg_gradient_t));
            parse_gradient(g, &img->gradients[img->gradient_count], true);
            img->gradient_count++;
            g = grad_end;
        }
    }
    
    /* Find and parse paths */
    const char* p = svg_data;
    while ((p = strstr(p, "<path")) != NULL) {
        const char* tag_end = strchr(p, '>');
        if (!tag_end) break;
        
        /* Extract path attributes */
        nxsvg_path_t* path = calloc(1, sizeof(nxsvg_path_t));
        path->fill = nx_rgba(255, 255, 255, 255);
        path->stroke_width = 1.0f;
        path->width = img->width;
        path->height = img->height;
        path->gradient_id = -1;
        path->has_transform = false;
        
        /* Parse d attribute */
        if (find_attr(p, "d", buf, sizeof(buf))) {
            /* Need larger buffer for path data */
            char* d_buf = malloc(tag_end - p + 1);
            if (find_attr(p, "d", d_buf, tag_end - p)) {
                parse_path_d(path, d_buf);
            }
            free(d_buf);
        }
        
        /* Parse fill - check for gradient reference */
        if (find_attr(p, "fill", buf, sizeof(buf))) {
            int grad_idx = find_gradient_by_url(img, buf);
            if (grad_idx >= 0) {
                path->gradient_id = grad_idx;
            } else {
                path->fill = parse_color(buf);
            }
        }
        
        /* Parse transform */
        if (find_attr(p, "transform", buf, sizeof(buf))) {
            parse_transform(buf, path->transform);
            path->has_transform = true;
        }
        
        /* Add path to image */
        img->paths = realloc(img->paths, (img->path_count + 1) * sizeof(nxsvg_path_t));
        img->paths[img->path_count++] = *path;
        free(path);
        
        p = tag_end;
    }
    
    return img;
}

/* Load SVG from file */
nxsvg_image_t* nxsvg_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
    
    nxsvg_image_t* img = nxsvg_parse(data, len);
    free(data);
    
    return img;
}

/* Render SVG to context - with proper compound path filling */
void nxsvg_render(nx_context_t* ctx, const nxsvg_image_t* svg, 
                  nx_point_t pos, float scale) {
    if (!ctx || !svg) return;
    
    /* Maximum contours and points */
    #define MAX_CONTOURS 32
    #define MAX_POINTS_PER_CONTOUR 256
    
    /* Allocate contour storage */
    nx_point_t** contours = malloc(MAX_CONTOURS * sizeof(nx_point_t*));
    uint32_t* counts = malloc(MAX_CONTOURS * sizeof(uint32_t));
    if (!contours || !counts) {
        free(contours); free(counts);
        return;
    }
    
    for (int i = 0; i < MAX_CONTOURS; i++) {
        contours[i] = malloc(MAX_POINTS_PER_CONTOUR * sizeof(nx_point_t));
    }
    
    for (uint32_t p = 0; p < svg->path_count; p++) {
        const nxsvg_path_t* path = &svg->paths[p];
        
        if (path->fill.a == 0) continue;  /* Skip transparent fills */
        
        float cx = 0, cy = 0;  /* Current position */
        float sx = 0, sy = 0;  /* Subpath start */
        uint32_t num_contours = 0;
        uint32_t current_count = 0;
        
        for (uint32_t i = 0; i < path->count; i++) {
            const nxsvg_cmd_t* cmd = &path->commands[i];
            
            switch (cmd->type) {
                case NXSVG_CMD_MOVE:
                    /* Start new contour - save previous if exists */
                    if (current_count >= 3 && num_contours < MAX_CONTOURS) {
                        counts[num_contours] = current_count;
                        num_contours++;
                    }
                    current_count = 0;
                    cx = cmd->x; cy = cmd->y;
                    sx = cx; sy = cy;
                    if (num_contours < MAX_CONTOURS && current_count < MAX_POINTS_PER_CONTOUR) {
                        contours[num_contours][current_count].x = pos.x + (int32_t)(cx * scale);
                        contours[num_contours][current_count].y = pos.y + (int32_t)(cy * scale);
                        current_count++;
                    }
                    break;
                    
                case NXSVG_CMD_LINE:
                    cx = cmd->x; cy = cmd->y;
                    if (num_contours < MAX_CONTOURS && current_count < MAX_POINTS_PER_CONTOUR) {
                        contours[num_contours][current_count].x = pos.x + (int32_t)(cx * scale);
                        contours[num_contours][current_count].y = pos.y + (int32_t)(cy * scale);
                        current_count++;
                    }
                    break;
                    
                case NXSVG_CMD_CUBIC: {
                    /* Flatten cubic bezier into line segments */
                    float x0 = cx, y0 = cy;
                    float x1 = cmd->x1, y1 = cmd->y1;
                    float x2 = cmd->x2, y2 = cmd->y2;
                    float x3 = cmd->x, y3 = cmd->y;
                    
                    for (int t = 1; t <= 8; t++) {
                        float u = t / 8.0f, u2 = u * u, u3 = u2 * u;
                        float m = 1 - u, m2 = m * m, m3 = m2 * m;
                        float px = m3 * x0 + 3 * m2 * u * x1 + 3 * m * u2 * x2 + u3 * x3;
                        float py = m3 * y0 + 3 * m2 * u * y1 + 3 * m * u2 * y2 + u3 * y3;
                        
                        if (num_contours < MAX_CONTOURS && current_count < MAX_POINTS_PER_CONTOUR) {
                            contours[num_contours][current_count].x = pos.x + (int32_t)(px * scale);
                            contours[num_contours][current_count].y = pos.y + (int32_t)(py * scale);
                            current_count++;
                        }
                    }
                    cx = x3; cy = y3;
                    break;
                }
                    
                case NXSVG_CMD_QUAD: {
                    /* Flatten quadratic bezier */
                    float x0 = cx, y0 = cy;
                    float x1 = cmd->x1, y1 = cmd->y1;
                    float x2 = cmd->x, y2 = cmd->y;
                    
                    for (int t = 1; t <= 8; t++) {
                        float u = t / 8.0f;
                        float m = 1 - u;
                        float px = m * m * x0 + 2 * m * u * x1 + u * u * x2;
                        float py = m * m * y0 + 2 * m * u * y1 + u * u * y2;
                        
                        if (num_contours < MAX_CONTOURS && current_count < MAX_POINTS_PER_CONTOUR) {
                            contours[num_contours][current_count].x = pos.x + (int32_t)(px * scale);
                            contours[num_contours][current_count].y = pos.y + (int32_t)(py * scale);
                            current_count++;
                        }
                    }
                    cx = x2; cy = y2;
                    break;
                }
                    
                case NXSVG_CMD_ARC:
                    /* Simplified: add endpoint */
                    cx = cmd->x; cy = cmd->y;
                    if (num_contours < MAX_CONTOURS && current_count < MAX_POINTS_PER_CONTOUR) {
                        contours[num_contours][current_count].x = pos.x + (int32_t)(cx * scale);
                        contours[num_contours][current_count].y = pos.y + (int32_t)(cy * scale);
                        current_count++;
                    }
                    break;
                    
                case NXSVG_CMD_CLOSE:
                    /* Close current contour */
                    cx = sx; cy = sy;
                    if (current_count >= 3 && num_contours < MAX_CONTOURS) {
                        counts[num_contours] = current_count;
                        num_contours++;
                    }
                    current_count = 0;
                    break;
            }
        }
        
        /* Save any remaining contour */
        if (current_count >= 3 && num_contours < MAX_CONTOURS) {
            counts[num_contours] = current_count;
            num_contours++;
        }
        
        /* Fill all contours together with even-odd rule */
        if (num_contours > 0) {
            nxgfx_fill_complex_path(ctx, (const nx_point_t**)contours, counts, num_contours, path->fill);
        }
    }
    
    /* Free storage */
    for (int i = 0; i < MAX_CONTOURS; i++) {
        free(contours[i]);
    }
    free(contours);
    free(counts);
    
    #undef MAX_CONTOURS
    #undef MAX_POINTS_PER_CONTOUR
}

/* Free SVG image */
void nxsvg_free(nxsvg_image_t* svg) {
    if (!svg) return;
    
    for (uint32_t i = 0; i < svg->path_count; i++) {
        free(svg->paths[i].commands);
    }
    free(svg->paths);
    free(svg->gradients);
    free(svg);
}
