/*
 * SVG Import Implementation
 * 
 * Parses SVG files and converts path data to NXI layer format.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "svg_import.h"
#include "layer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

svg_import_opts_t svg_import_defaults(void) {
    return (svg_import_opts_t){
        .default_color = 0xFFFFFFFF,  /* White */
        .flatten_groups = true,
        .preserve_transform = true
    };
}

/* Skip whitespace and commas */
static const char* skip_ws(const char* p) {
    while (*p && (isspace(*p) || *p == ',')) p++;
    return p;
}

/* Parse a float value */
static const char* parse_float(const char* p, float* out) {
    p = skip_ws(p);
    char* end;
    *out = strtof(p, &end);
    return end;
}

/* Convert elliptical arc to cubic bezier curves (SVG arc endpoint parameterization) */
static void arc_to_bezier(float cx, float cy, float rx, float ry,
                          float phi, float theta1, float dtheta,
                          nxi_path_cmd_t** cmds, uint32_t* count, uint32_t* capacity) {
    /* Number of bezier segments needed (45 degrees each max) */
    int segments = (int)ceilf(fabsf(dtheta) / (M_PI / 4.0f));
    if (segments < 1) segments = 1;
    if (segments > 8) segments = 8;
    
    float delta = dtheta / segments;
    float cos_phi = cosf(phi);
    float sin_phi = sinf(phi);
    
    /* Bezier control point factor */
    float t = tanf(delta / 4.0f);
    float k = 4.0f / 3.0f * t;
    
    float angle = theta1;
    float prev_x = cx + rx * cosf(angle) * cos_phi - ry * sinf(angle) * sin_phi;
    float prev_y = cy + rx * cosf(angle) * sin_phi + ry * sinf(angle) * cos_phi;
    
    for (int i = 0; i < segments; i++) {
        float next_angle = angle + delta;
        
        /* Current point tangent direction */
        float dx1 = -rx * sinf(angle);
        float dy1 = ry * cosf(angle);
        
        /* Next point tangent direction */
        float dx2 = -rx * sinf(next_angle);
        float dy2 = ry * cosf(next_angle);
        
        /* Rotate tangents by phi */
        float tx1 = dx1 * cos_phi - dy1 * sin_phi;
        float ty1 = dx1 * sin_phi + dy1 * cos_phi;
        float tx2 = dx2 * cos_phi - dy2 * sin_phi;
        float ty2 = dx2 * sin_phi + dy2 * cos_phi;
        
        /* End point of this segment */
        float next_x = cx + rx * cosf(next_angle) * cos_phi - ry * sinf(next_angle) * sin_phi;
        float next_y = cy + rx * cosf(next_angle) * sin_phi + ry * sinf(next_angle) * cos_phi;
        
        /* Control points */
        float cp1x = prev_x + k * tx1;
        float cp1y = prev_y + k * ty1;
        float cp2x = next_x - k * tx2;
        float cp2y = next_y - k * ty2;
        
        /* Add cubic bezier command */
        if (*count >= *capacity) {
            *capacity *= 2;
            *cmds = realloc(*cmds, *capacity * sizeof(nxi_path_cmd_t));
        }
        nxi_path_cmd_t c = {0};
        c.type = NXI_PATH_CUBIC;
        c.x1 = cp1x;
        c.y1 = cp1y;
        c.x2 = cp2x;
        c.y2 = cp2y;
        c.x = next_x;
        c.y = next_y;
        (*cmds)[(*count)++] = c;
        
        prev_x = next_x;
        prev_y = next_y;
        angle = next_angle;
    }
}

/* Convert SVG arc endpoint parameters to center parameters */
static void arc_endpoint_to_center(float x1, float y1, float x2, float y2,
                                    float rx, float ry, float phi,
                                    int fa, int fs,
                                    float* cx_out, float* cy_out,
                                    float* theta1_out, float* dtheta_out) {
    float cos_phi = cosf(phi);
    float sin_phi = sinf(phi);
    
    /* Step 1: Compute (x1', y1') */
    float dx = (x1 - x2) / 2.0f;
    float dy = (y1 - y2) / 2.0f;
    float x1p = cos_phi * dx + sin_phi * dy;
    float y1p = -sin_phi * dx + cos_phi * dy;
    
    /* Ensure radii are large enough */
    float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        float sq = sqrtf(lambda);
        rx *= sq;
        ry *= sq;
    }
    
    /* Step 2: Compute (cx', cy') */
    float sq = fmaxf(0.0f, (rx*rx * ry*ry - rx*rx * y1p*y1p - ry*ry * x1p*x1p) /
                          (rx*rx * y1p*y1p + ry*ry * x1p*x1p));
    float coef = sqrtf(sq) * ((fa == fs) ? -1.0f : 1.0f);
    float cxp = coef * rx * y1p / ry;
    float cyp = coef * -ry * x1p / rx;
    
    /* Step 3: Compute (cx, cy) from (cx', cy') */
    *cx_out = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2.0f;
    *cy_out = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2.0f;
    
    /* Step 4: Compute theta1 and dtheta */
    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    
    /* Angle start */
    float n = sqrtf(ux*ux + uy*uy);
    *theta1_out = acosf(fmaxf(-1.0f, fminf(1.0f, ux / n)));
    if (uy < 0) *theta1_out = -*theta1_out;
    
    /* Angle extent */
    n = sqrtf((ux*ux + uy*uy) * (vx*vx + vy*vy));
    float c = (ux*vx + uy*vy) / n;
    *dtheta_out = acosf(fmaxf(-1.0f, fminf(1.0f, c)));
    if (ux*vy - uy*vx < 0) *dtheta_out = -*dtheta_out;
    
    /* Adjust for sweep flag */
    if (fs == 0 && *dtheta_out > 0) *dtheta_out -= 2.0f * M_PI;
    if (fs == 1 && *dtheta_out < 0) *dtheta_out += 2.0f * M_PI;
}

/* Parse SVG path 'd' attribute */
int svg_parse_path_d(const char* d, 
                     nxi_path_cmd_t** out_cmds, 
                     uint32_t* out_count) {
    if (!d || !out_cmds || !out_count) return -1;
    
    uint32_t capacity = 64;
    uint32_t count = 0;
    nxi_path_cmd_t* cmds = calloc(capacity, sizeof(nxi_path_cmd_t));
    if (!cmds) return -1;
    
    const char* p = d;
    float cx = 0, cy = 0;  /* Current position */
    float sx = 0, sy = 0;  /* Subpath start */
    char cmd = 0;
    int relative = 0;
    
    while (*p) {
        p = skip_ws(p);
        if (!*p) break;
        
        if (isalpha(*p)) {
            cmd = *p;
            relative = islower(cmd);
            cmd = toupper(cmd);
            p++;
        }
        
        nxi_path_cmd_t c = {0};
        
        switch (cmd) {
            case 'M':  /* Move to */
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) { c.x += cx; c.y += cy; }
                c.type = NXI_PATH_MOVE;
                cx = sx = c.x;
                cy = sy = c.y;
                cmd = 'L';  /* Subsequent coords are line-to */
                break;
                
            case 'L':  /* Line to */
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) { c.x += cx; c.y += cy; }
                c.type = NXI_PATH_LINE;
                cx = c.x;
                cy = c.y;
                break;
                
            case 'H':  /* Horizontal line */
                p = parse_float(p, &c.x);
                if (relative) c.x += cx;
                c.y = cy;
                c.type = NXI_PATH_LINE;
                cx = c.x;
                break;
                
            case 'V':  /* Vertical line */
                p = parse_float(p, &c.y);
                if (relative) c.y += cy;
                c.x = cx;
                c.type = NXI_PATH_LINE;
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
                c.type = NXI_PATH_CUBIC;
                cx = c.x;
                cy = c.y;
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
                c.type = NXI_PATH_QUAD;
                cx = c.x;
                cy = c.y;
                break;
                
            case 'S': {  /* Smooth cubic bezier */
                /* Reflect previous control point */
                float prev_x2 = cx, prev_y2 = cy;
                if (count > 0 && cmds[count-1].type == NXI_PATH_CUBIC) {
                    prev_x2 = cmds[count-1].x2;
                    prev_y2 = cmds[count-1].y2;
                }
                c.x1 = 2 * cx - prev_x2;  /* Reflection */
                c.y1 = 2 * cy - prev_y2;
                p = parse_float(p, &c.x2);
                p = parse_float(p, &c.y2);
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) {
                    c.x2 += cx; c.y2 += cy;
                    c.x += cx; c.y += cy;
                }
                c.type = NXI_PATH_CUBIC;
                cx = c.x;
                cy = c.y;
                break;
            }
                
            case 'T': {  /* Smooth quadratic bezier */
                /* Reflect previous control point */
                float prev_x1 = cx, prev_y1 = cy;
                if (count > 0 && cmds[count-1].type == NXI_PATH_QUAD) {
                    prev_x1 = cmds[count-1].x1;
                    prev_y1 = cmds[count-1].y1;
                }
                c.x1 = 2 * cx - prev_x1;  /* Reflection */
                c.y1 = 2 * cy - prev_y1;
                p = parse_float(p, &c.x);
                p = parse_float(p, &c.y);
                if (relative) {
                    c.x += cx; c.y += cy;
                }
                c.type = NXI_PATH_QUAD;
                cx = c.x;
                cy = c.y;
                break;
            }
                
            case 'A': {  /* Elliptical arc */
                float rx, ry, x_axis_rot;
                int large_arc, sweep;
                float x2, y2;
                
                p = parse_float(p, &rx);
                p = parse_float(p, &ry);
                p = parse_float(p, &x_axis_rot);
                p = skip_ws(p);
                large_arc = (*p == '1') ? 1 : 0; p++;
                p = skip_ws(p);
                sweep = (*p == '1') ? 1 : 0; p++;
                p = parse_float(p, &x2);
                p = parse_float(p, &y2);
                
                if (relative) {
                    x2 += cx;
                    y2 += cy;
                }
                
                /* Convert to center parameterization and generate bezier curves */
                if (rx > 0 && ry > 0 && (cx != x2 || cy != y2)) {
                    float arc_cx, arc_cy, theta1, dtheta;
                    float phi = x_axis_rot * M_PI / 180.0f;
                    arc_endpoint_to_center(cx, cy, x2, y2, rx, ry, phi,
                                           large_arc, sweep,
                                           &arc_cx, &arc_cy, &theta1, &dtheta);
                    arc_to_bezier(arc_cx, arc_cy, rx, ry, phi, theta1, dtheta,
                                  &cmds, &count, &capacity);
                }
                
                cx = x2;
                cy = y2;
                continue;  /* Arc generated beziers directly, skip normal add */
            }
                
            case 'Z':  /* Close path */
                c.type = NXI_PATH_CLOSE;
                c.x = sx;
                c.y = sy;
                cx = sx;
                cy = sy;
                break;
                
            default:
                p++;  /* Unknown, skip */
                continue;
        }
        
        /* Add command */
        if (count >= capacity) {
            capacity *= 2;
            cmds = realloc(cmds, capacity * sizeof(nxi_path_cmd_t));
        }
        cmds[count++] = c;
    }
    
    *out_cmds = cmds;
    *out_count = count;
    return 0;
}

/* Find attribute value in SVG tag */
static const char* find_attr(const char* tag, const char* name, 
                             char* buf, size_t buf_size) {
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

/* Parse color from SVG string */
static uint32_t parse_svg_color(const char* str) {
    if (!str) return 0xFFFFFFFF;
    
    if (strcmp(str, "none") == 0) return 0x00000000;
    if (strcmp(str, "white") == 0) return 0xFFFFFFFF;
    if (strcmp(str, "black") == 0) return 0x000000FF;
    
    /* #RRGGBB format */
    if (str[0] == '#' && strlen(str) == 7) {
        unsigned int r, g, b;
        sscanf(str + 1, "%02x%02x%02x", &r, &g, &b);
        return (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }
    
    return 0xFFFFFFFF;
}

void svg_get_dimensions(const char* svg_data, 
                        float* out_width, float* out_height) {
    *out_width = 24.0f;
    *out_height = 24.0f;
    
    if (!svg_data) return;
    
    char buf[64];
    const char* svg_tag = strstr(svg_data, "<svg");
    if (!svg_tag) return;
    
    if (find_attr(svg_tag, "width", buf, sizeof(buf))) {
        *out_width = strtof(buf, NULL);
    }
    if (find_attr(svg_tag, "height", buf, sizeof(buf))) {
        *out_height = strtof(buf, NULL);
    }
}

/* Detect shape type from path commands with semantic naming for icon templates */
static const char* detect_shape_type(const nxi_path_cmd_t* cmds, uint32_t count,
                                      int layer_index, float* out_center_dist) {
    if (count < 2) return "Point";
    
    /* Count command types */
    int moves = 0, lines = 0, curves = 0, closes = 0;
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    
    for (uint32_t i = 0; i < count; i++) {
        switch (cmds[i].type) {
            case NXI_PATH_MOVE: moves++; break;
            case NXI_PATH_LINE: lines++; break;
            case NXI_PATH_CUBIC: 
            case NXI_PATH_QUAD: curves++; break;
            case NXI_PATH_CLOSE: closes++; break;
            default: break;
        }
        if (cmds[i].x < min_x) min_x = cmds[i].x;
        if (cmds[i].x > max_x) max_x = cmds[i].x;
        if (cmds[i].y < min_y) min_y = cmds[i].y;
        if (cmds[i].y > max_y) max_y = cmds[i].y;
    }
    
    float width = max_x - min_x;
    float height = max_y - min_y;
    float center_x = (min_x + max_x) / 2.0f;
    float center_y = (min_y + max_y) / 2.0f;
    float dist_from_center = sqrtf((center_x - 0.5f) * (center_x - 0.5f) + 
                                   (center_y - 0.5f) * (center_y - 0.5f));
    
    if (out_center_dist) *out_center_dist = dist_from_center;
    
    /* Full-size background */
    if (layer_index == 0 && width > 0.9f && height > 0.9f) {
        return "Background";
    }
    
    /* Border detection: hollow shape (2+ moves = cutout) */
    if (moves >= 2 && closes >= 1) {
        if (width > 0.9f && height > 0.9f) {
            return "Outer Border";
        }
        if (curves >= 4) {
            return "Inner Border";
        }
        return "Border";
    }
    
    /* Rounded rect: large with curves = icon base shape */
    if (lines > 0 && curves >= 4 && width > 0.5f) {
        return "Icon Base";
    }
    
    /* Circle detection: many curves, roughly square bounds */
    if (curves >= 4 && lines == 0 && fabsf(width - height) < 0.15f) {
        if (width > 0.7f) return "Outer Guide";
        if (width > 0.4f) return "Middle Guide";
        return "Inner Guide";
    }
    
    /* Line detection */
    if (lines >= 1 && lines <= 2 && curves == 0) {
        /* Diagonal */
        if (fabsf(width - height) < 0.2f && width > 0.5f) {
            return "Diagonal Guide";
        }
        /* Vertical centerline */
        if (fabsf(width) < 0.01f && fabsf(center_x - 0.5f) < 0.01f) {
            return "Center V";
        }
        /* Horizontal centerline */
        if (fabsf(height) < 0.01f && fabsf(center_y - 0.5f) < 0.01f) {
            return "Center H";
        }
        /* Vertical grid line */
        if (fabsf(width) < 0.01f) {
            return "Grid V";
        }
        /* Horizontal grid line */
        if (fabsf(height) < 0.01f) {
            return "Grid H";
        }
        return "Line";
    }
    
    /* Rectangle */
    if (lines >= 3 && curves == 0) {
        return "Rectangle";
    }
    
    return "Shape";
}

/* Get short color name from hex */
static const char* get_color_name(uint32_t color) {
    uint32_t rgb = color >> 8;
    
    if (rgb == 0x000000) return "Black";
    if (rgb == 0xFFFFFF) return "White";
    if (rgb == 0x9855F5) return "Purple";
    if (rgb == 0x6A00FF) return "Violet";
    if (rgb == 0x8A889E) return "Gray";
    
    /* More color detection */
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    
    if (r > 200 && g < 100 && b < 100) return "Red";
    if (g > 200 && r < 100 && b < 100) return "Green";
    if (b > 200 && r < 100 && g < 100) return "Blue";
    if (r > 200 && g > 200 && b < 100) return "Yellow";
    if (r > 200 && b > 200 && g < 100) return "Magenta";
    if (g > 200 && b > 200 && r < 100) return "Cyan";
    if (r > 100 && r < 180 && g > 100 && g < 180 && b > 100 && b < 180) return "Gray";
    
    return NULL;
}

/* Import each SVG path as a separate layer with its own color */
int svg_import_multilayer_separate(const char* svg_data, uint32_t len,
                                    nxi_icon_t* icon,
                                    const svg_import_opts_t* opts) {
    if (!svg_data || len == 0 || !icon) return -1;
    
    svg_import_opts_t o = opts ? *opts : svg_import_defaults();
    
    /* Get SVG dimensions for normalization */
    float svg_w, svg_h;
    svg_get_dimensions(svg_data, &svg_w, &svg_h);
    float norm = 1.0f / fmaxf(svg_w, svg_h);
    
    int path_num = 0;
    int shape_counts[10] = {0};  /* Count per shape type for unique names */
    const char* path_tag = svg_data;
    
    while ((path_tag = strstr(path_tag, "<path")) != NULL) {
        /* Skip paths inside <defs> or <clipPath> (they're not renderable) */
        const char* defs_start = strstr(svg_data, "<defs");
        const char* defs_end = strstr(svg_data, "</defs>");
        if (defs_start && defs_end && path_tag > defs_start && path_tag < defs_end) {
            path_tag++;
            continue;  /* Skip this path - it's inside defs */
        }
        
        const char* tag_end = strchr(path_tag, '>');
        if (!tag_end) break;
        
        /* Get d attribute */
        char* d_buf = malloc(8192);
        if (find_attr(path_tag, "d", d_buf, 8192)) {
            nxi_path_cmd_t* cmds = NULL;
            uint32_t count = 0;
            
            if (svg_parse_path_d(d_buf, &cmds, &count) == 0 && count > 0) {
                /* Create a new layer for this path */
                nxi_layer_t* layer = calloc(1, sizeof(nxi_layer_t));
                if (layer) {
                    /* Get fill color first (needed for name) */
                    char fill_buf[32];
                    if (find_attr(path_tag, "fill", fill_buf, sizeof(fill_buf))) {
                        layer->fill_color = parse_svg_color(fill_buf);
                    } else {
                        layer->fill_color = o.default_color;
                    }
                    
                    /* Try to get id attribute for layer name */
                    char id_buf[32];
                    if (find_attr(path_tag, "id", id_buf, sizeof(id_buf))) {
                        strncpy(layer->name, id_buf, 31);
                    } else {
                        /* Generate descriptive name from shape + color */
                        const char* shape = detect_shape_type(cmds, count, path_num, NULL);
                        const char* color = get_color_name(layer->fill_color);
                        
                        /* Names are already semantic from detect_shape_type */
                        
                        if (color) {
                            snprintf(layer->name, sizeof(layer->name), "%s %s", color, shape);
                        } else {
                            snprintf(layer->name, sizeof(layer->name), "%s", shape);
                        }
                        
                        /* Append number if duplicate name would occur */
                        int idx = path_num % 10;
                        if (shape_counts[idx] > 0) {
                            char tmp[32];
                            snprintf(tmp, sizeof(tmp), " %d", shape_counts[idx]);
                            strncat(layer->name, tmp, 31 - strlen(layer->name));
                        }
                        shape_counts[idx]++;
                    }
                    
                    path_num++;
                    layer->visible = true;
                    layer->opacity = 255;
                    layer->pos_x = 0.5f;
                    layer->pos_y = 0.5f;
                    layer->scale = 1.0f;
                    
                    /* Normalize and store path commands */
                    layer->path_capacity = count;
                    layer->paths = cmds;  /* Transfer ownership */
                    layer->path_count = count;
                    
                    for (uint32_t i = 0; i < count; i++) {
                        layer->paths[i].x *= norm;
                        layer->paths[i].y *= norm;
                        layer->paths[i].x1 *= norm;
                        layer->paths[i].y1 *= norm;
                        layer->paths[i].x2 *= norm;
                        layer->paths[i].y2 *= norm;
                    }
                    
                    /* Add layer to icon */
                    if (icon->layer_count >= icon->layer_capacity) {
                        icon->layer_capacity = icon->layer_capacity ? icon->layer_capacity * 2 : 8;
                        icon->layers = realloc(icon->layers, 
                                               icon->layer_capacity * sizeof(nxi_layer_t*));
                    }
                    icon->layers[icon->layer_count++] = layer;
                } else {
                    free(cmds);
                }
            }
        }
        free(d_buf);
        path_tag = tag_end;
    }
    
    return path_num > 0 ? 0 : -1;
}

nxi_layer_t* svg_import_layer_mem(const char* svg_data, uint32_t len,
                                   const svg_import_opts_t* opts) {
    if (!svg_data || len == 0) return NULL;
    
    svg_import_opts_t o = opts ? *opts : svg_import_defaults();
    
    /* Create layer */
    nxi_layer_t* layer = calloc(1, sizeof(nxi_layer_t));
    if (!layer) return NULL;
    
    strcpy(layer->name, "SVG Layer");
    layer->visible = true;
    layer->opacity = 255;
    layer->pos_x = 0.5f;
    layer->pos_y = 0.5f;
    layer->scale = 1.0f;
    layer->fill_color = o.default_color;
    layer->path_capacity = 256;
    layer->paths = calloc(layer->path_capacity, sizeof(nxi_path_cmd_t));
    
    /* Get SVG dimensions for normalization */
    float svg_w, svg_h;
    svg_get_dimensions(svg_data, &svg_w, &svg_h);
    float norm = 1.0f / fmaxf(svg_w, svg_h);
    
    /* Find and parse ALL path elements */
    const char* path_tag = svg_data;
    while ((path_tag = strstr(path_tag, "<path")) != NULL) {
        /* Find the end of this tag */
        const char* tag_end = strchr(path_tag, '>');
        if (!tag_end) break;
        
        /* Get d attribute - need larger buffer for complex paths */
        char* d_buf = malloc(8192);
        if (find_attr(path_tag, "d", d_buf, 8192)) {
            nxi_path_cmd_t* cmds = NULL;
            uint32_t count = 0;
            
            if (svg_parse_path_d(d_buf, &cmds, &count) == 0 && count > 0) {
                /* Expand layer paths if needed */
                while (layer->path_count + count >= layer->path_capacity) {
                    layer->path_capacity *= 2;
                    layer->paths = realloc(layer->paths, 
                                           layer->path_capacity * sizeof(nxi_path_cmd_t));
                }
                
                /* Normalize and add commands */
                for (uint32_t i = 0; i < count; i++) {
                    cmds[i].x *= norm;
                    cmds[i].y *= norm;
                    cmds[i].x1 *= norm;
                    cmds[i].y1 *= norm;
                    cmds[i].x2 *= norm;
                    cmds[i].y2 *= norm;
                    layer->paths[layer->path_count++] = cmds[i];
                }
                free(cmds);
            }
        }
        free(d_buf);
        
        /* Get fill color from first path with color */
        char fill_buf[32];
        if (layer->fill_color == o.default_color && 
            find_attr(path_tag, "fill", fill_buf, sizeof(fill_buf))) {
            layer->fill_color = parse_svg_color(fill_buf);
        }
        
        path_tag = tag_end;
    }
    
    return layer;
}

nxi_layer_t* svg_import_layer(const char* svg_path, 
                               const svg_import_opts_t* opts) {
    if (!svg_path) return NULL;
    
    FILE* f = fopen(svg_path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
    
    nxi_layer_t* layer = svg_import_layer_mem(data, len, opts);
    free(data);
    
    /* Extract filename for layer name */
    if (layer) {
        const char* slash = strrchr(svg_path, '/');
        const char* name = slash ? slash + 1 : svg_path;
        strncpy(layer->name, name, 31);
        
        /* Remove .svg extension */
        char* dot = strrchr(layer->name, '.');
        if (dot) *dot = '\0';
    }
    
    return layer;
}

int svg_import_multilayer(const char* svg_path,
                          nxi_icon_t* icon,
                          const svg_import_opts_t* opts) {
    if (!svg_path || !icon) return -1;
    
    /* Read SVG file */
    FILE* f = fopen(svg_path, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
    
    /* Use separate layer import for proper color preservation */
    int result = svg_import_multilayer_separate(data, len, icon, opts);
    free(data);
    
    return result;
}

