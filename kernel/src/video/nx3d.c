/*
 * NX3D - NeolyxOS Basic 3D Rendering Implementation
 * 
 * Software 3D pipeline.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/video/nx3d.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ Math Helpers ============ */

static inline float nxf_abs(float x) { return x < 0 ? -x : x; }
static inline float nxf_min(float a, float b) { return a < b ? a : b; }
static inline float nxf_max(float a, float b) { return a > b ? a : b; }
static inline int nxi_min(int a, int b) { return a < b ? a : b; }
static inline int nxi_max(int a, int b) { return a > b ? a : b; }

/* Approximate sqrt using Newton-Raphson */
static float nxf_sqrt(float x) {
    if (x <= 0) return 0;
    float guess = x * 0.5f;
    for (int i = 0; i < 8; i++) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

/* Approximate tan using Taylor series */
static float nxf_tan(float x) {
    /* Normalize to -PI/2 to PI/2 range */
    float x2 = x * x;
    return x + (x * x2) / 3.0f + (2.0f * x * x2 * x2) / 15.0f;
}

/* ============ Initialization ============ */

int nx3d_init(nx3d_context_t *ctx, uint32_t *color_buf, float *depth_buf,
              uint32_t width, uint32_t height, uint32_t pitch) {
    if (!ctx || !color_buf || !depth_buf) return -1;
    
    ctx->color_buffer = color_buf;
    ctx->depth_buffer = depth_buf;
    ctx->width = width;
    ctx->height = height;
    ctx->pitch = pitch / sizeof(uint32_t);
    
    nx3d_mat4_identity(&ctx->model);
    nx3d_mat4_identity(&ctx->view);
    nx3d_mat4_identity(&ctx->projection);
    nx3d_mat4_identity(&ctx->mvp);
    
    serial_puts("[NX3D] Initialized\n");
    return 0;
}

void nx3d_shutdown(nx3d_context_t *ctx) {
    if (ctx) {
        ctx->color_buffer = NULL;
        ctx->depth_buffer = NULL;
    }
}

/* ============ Clear ============ */

void nx3d_clear(nx3d_context_t *ctx, uint32_t color, float depth) {
    if (!ctx) return;
    
    uint32_t total = ctx->width * ctx->height;
    for (uint32_t i = 0; i < total; i++) {
        ctx->color_buffer[i] = color;
        ctx->depth_buffer[i] = depth;
    }
}

void nx3d_clear_depth(nx3d_context_t *ctx, float depth) {
    if (!ctx) return;
    
    uint32_t total = ctx->width * ctx->height;
    for (uint32_t i = 0; i < total; i++) {
        ctx->depth_buffer[i] = depth;
    }
}

/* ============ Matrix Operations ============ */

void nx3d_mat4_identity(nx3d_mat4_t *m) {
    for (int i = 0; i < 16; i++) m->m[i] = 0;
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

void nx3d_mat4_multiply(nx3d_mat4_t *out, const nx3d_mat4_t *a, const nx3d_mat4_t *b) {
    nx3d_mat4_t tmp;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a->m[k * 4 + row] * b->m[col * 4 + k];
            }
            tmp.m[col * 4 + row] = sum;
        }
    }
    *out = tmp;
}

void nx3d_perspective(nx3d_mat4_t *m, float fov, float aspect, float near, float far) {
    float f = 1.0f / nxf_tan(fov * 0.5f);
    
    for (int i = 0; i < 16; i++) m->m[i] = 0;
    
    m->m[0] = f / aspect;
    m->m[5] = f;
    m->m[10] = (far + near) / (near - far);
    m->m[11] = -1.0f;
    m->m[14] = (2.0f * far * near) / (near - far);
}

void nx3d_lookat(nx3d_mat4_t *m, nx3d_vec3_t eye, nx3d_vec3_t center, nx3d_vec3_t up) {
    nx3d_vec3_t f = nx3d_vec3_normalize(nx3d_vec3_sub(center, eye));
    nx3d_vec3_t s = nx3d_vec3_normalize(nx3d_vec3_cross(f, up));
    nx3d_vec3_t u = nx3d_vec3_cross(s, f);
    
    nx3d_mat4_identity(m);
    m->m[0] = s.x;  m->m[4] = s.y;  m->m[8]  = s.z;
    m->m[1] = u.x;  m->m[5] = u.y;  m->m[9]  = u.z;
    m->m[2] = -f.x; m->m[6] = -f.y; m->m[10] = -f.z;
    m->m[12] = -nx3d_vec3_dot(s, eye);
    m->m[13] = -nx3d_vec3_dot(u, eye);
    m->m[14] = nx3d_vec3_dot(f, eye);
}

void nx3d_translate(nx3d_mat4_t *m, float x, float y, float z) {
    nx3d_mat4_identity(m);
    m->m[12] = x;
    m->m[13] = y;
    m->m[14] = z;
}

void nx3d_rotate_y(nx3d_mat4_t *m, float angle) {
    /* Simple sin/cos approximation */
    float s = angle - (angle * angle * angle) / 6.0f;  /* sin approx */
    float c = 1.0f - (angle * angle) / 2.0f;           /* cos approx */
    
    nx3d_mat4_identity(m);
    m->m[0] = c;
    m->m[2] = s;
    m->m[8] = -s;
    m->m[10] = c;
}

void nx3d_scale(nx3d_mat4_t *m, float sx, float sy, float sz) {
    nx3d_mat4_identity(m);
    m->m[0] = sx;
    m->m[5] = sy;
    m->m[10] = sz;
}

/* ============ Vector Operations ============ */

nx3d_vec3_t nx3d_vec3_add(nx3d_vec3_t a, nx3d_vec3_t b) {
    return (nx3d_vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

nx3d_vec3_t nx3d_vec3_sub(nx3d_vec3_t a, nx3d_vec3_t b) {
    return (nx3d_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

nx3d_vec3_t nx3d_vec3_mul(nx3d_vec3_t v, float s) {
    return (nx3d_vec3_t){v.x * s, v.y * s, v.z * s};
}

float nx3d_vec3_dot(nx3d_vec3_t a, nx3d_vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

nx3d_vec3_t nx3d_vec3_cross(nx3d_vec3_t a, nx3d_vec3_t b) {
    return (nx3d_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

nx3d_vec3_t nx3d_vec3_normalize(nx3d_vec3_t v) {
    float len = nxf_sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 0.0001f) return (nx3d_vec3_t){0, 0, 0};
    return (nx3d_vec3_t){v.x / len, v.y / len, v.z / len};
}

/* ============ Transform ============ */

nx3d_vec4_t nx3d_transform(const nx3d_mat4_t *m, nx3d_vec4_t v) {
    return (nx3d_vec4_t){
        m->m[0] * v.x + m->m[4] * v.y + m->m[8]  * v.z + m->m[12] * v.w,
        m->m[1] * v.x + m->m[5] * v.y + m->m[9]  * v.z + m->m[13] * v.w,
        m->m[2] * v.x + m->m[6] * v.y + m->m[10] * v.z + m->m[14] * v.w,
        m->m[3] * v.x + m->m[7] * v.y + m->m[11] * v.z + m->m[15] * v.w
    };
}

void nx3d_update_mvp(nx3d_context_t *ctx) {
    nx3d_mat4_t mv;
    nx3d_mat4_multiply(&mv, &ctx->view, &ctx->model);
    nx3d_mat4_multiply(&ctx->mvp, &ctx->projection, &mv);
}

/* ============ Triangle Rasterization ============ */

static float edge_func(float ax, float ay, float bx, float by, float cx, float cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

void nx3d_draw_triangle(nx3d_context_t *ctx,
                        const nx3d_vertex_t *v0,
                        const nx3d_vertex_t *v1,
                        const nx3d_vertex_t *v2) {
    if (!ctx || !v0 || !v1 || !v2) return;
    
    /* Transform vertices */
    nx3d_vec4_t p0 = nx3d_transform(&ctx->mvp, (nx3d_vec4_t){v0->position.x, v0->position.y, v0->position.z, 1.0f});
    nx3d_vec4_t p1 = nx3d_transform(&ctx->mvp, (nx3d_vec4_t){v1->position.x, v1->position.y, v1->position.z, 1.0f});
    nx3d_vec4_t p2 = nx3d_transform(&ctx->mvp, (nx3d_vec4_t){v2->position.x, v2->position.y, v2->position.z, 1.0f});
    
    /* Perspective divide */
    if (nxf_abs(p0.w) < 0.0001f || nxf_abs(p1.w) < 0.0001f || nxf_abs(p2.w) < 0.0001f) return;
    
    float x0 = p0.x / p0.w, y0 = p0.y / p0.w, z0 = p0.z / p0.w;
    float x1 = p1.x / p1.w, y1 = p1.y / p1.w, z1 = p1.z / p1.w;
    float x2 = p2.x / p2.w, y2 = p2.y / p2.w, z2 = p2.z / p2.w;
    
    /* NDC to screen coords */
    float hw = (float)ctx->width * 0.5f;
    float hh = (float)ctx->height * 0.5f;
    
    float sx0 = (x0 + 1.0f) * hw, sy0 = (1.0f - y0) * hh;
    float sx1 = (x1 + 1.0f) * hw, sy1 = (1.0f - y1) * hh;
    float sx2 = (x2 + 1.0f) * hw, sy2 = (1.0f - y2) * hh;
    
    /* Bounding box */
    int minX = nxi_max(0, (int)nxf_min(nxf_min(sx0, sx1), sx2));
    int maxX = nxi_min((int)ctx->width - 1, (int)nxf_max(nxf_max(sx0, sx1), sx2));
    int minY = nxi_max(0, (int)nxf_min(nxf_min(sy0, sy1), sy2));
    int maxY = nxi_min((int)ctx->height - 1, (int)nxf_max(nxf_max(sy0, sy1), sy2));
    
    /* Triangle area */
    float area = edge_func(sx0, sy0, sx1, sy1, sx2, sy2);
    if (nxf_abs(area) < 0.0001f) return;
    float inv_area = 1.0f / area;
    
    /* Rasterize */
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            
            /* Barycentric coords */
            float w0 = edge_func(sx1, sy1, sx2, sy2, px, py) * inv_area;
            float w1 = edge_func(sx2, sy2, sx0, sy0, px, py) * inv_area;
            float w2 = edge_func(sx0, sy0, sx1, sy1, px, py) * inv_area;
            
            /* Inside test */
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                /* Interpolate depth */
                float z = z0 * w0 + z1 * w1 + z2 * w2;
                
                uint32_t idx = y * ctx->pitch + x;
                
                /* Depth test */
                if (z < ctx->depth_buffer[idx]) {
                    ctx->depth_buffer[idx] = z;
                    
                    /* Interpolate color */
                    uint32_t c0 = v0->color, c1 = v1->color, c2 = v2->color;
                    uint8_t r = (uint8_t)(((c0 >> 16) & 0xFF) * w0 + ((c1 >> 16) & 0xFF) * w1 + ((c2 >> 16) & 0xFF) * w2);
                    uint8_t g = (uint8_t)(((c0 >> 8) & 0xFF) * w0 + ((c1 >> 8) & 0xFF) * w1 + ((c2 >> 8) & 0xFF) * w2);
                    uint8_t b = (uint8_t)((c0 & 0xFF) * w0 + (c1 & 0xFF) * w1 + (c2 & 0xFF) * w2);
                    
                    ctx->color_buffer[idx] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}

void nx3d_draw_mesh(nx3d_context_t *ctx, const nx3d_mesh_t *mesh) {
    if (!ctx || !mesh) return;
    
    for (uint32_t i = 0; i < mesh->triangle_count; i++) {
        const nx3d_triangle_t *tri = &mesh->triangles[i];
        nx3d_draw_triangle(ctx, 
                          &mesh->vertices[tri->v0],
                          &mesh->vertices[tri->v1],
                          &mesh->vertices[tri->v2]);
    }
}

/* ============ Primitive: Cube ============ */

nx3d_mesh_t* nx3d_create_cube(float size) {
    nx3d_mesh_t *mesh = (nx3d_mesh_t*)kmalloc(sizeof(nx3d_mesh_t));
    if (!mesh) return NULL;
    
    float s = size * 0.5f;
    
    /* 8 vertices */
    mesh->vertex_count = 8;
    mesh->vertices = (nx3d_vertex_t*)kmalloc(sizeof(nx3d_vertex_t) * 8);
    
    /* Define corners */
    nx3d_vec3_t corners[8] = {
        {-s, -s, -s}, { s, -s, -s}, { s,  s, -s}, {-s,  s, -s},  /* Back */
        {-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s}   /* Front */
    };
    
    uint32_t colors[8] = {
        0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00,
        0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF, 0xFF808080
    };
    
    for (int i = 0; i < 8; i++) {
        mesh->vertices[i].position = corners[i];
        mesh->vertices[i].color = colors[i];
    }
    
    /* 12 triangles (6 faces * 2 tris) */
    mesh->triangle_count = 12;
    mesh->triangles = (nx3d_triangle_t*)kmalloc(sizeof(nx3d_triangle_t) * 12);
    
    uint32_t indices[36] = {
        0, 2, 1, 0, 3, 2,  /* Back */
        4, 5, 6, 4, 6, 7,  /* Front */
        0, 1, 5, 0, 5, 4,  /* Bottom */
        2, 3, 7, 2, 7, 6,  /* Top */
        0, 4, 7, 0, 7, 3,  /* Left */
        1, 2, 6, 1, 6, 5   /* Right */
    };
    
    for (int i = 0; i < 12; i++) {
        mesh->triangles[i].v0 = indices[i * 3 + 0];
        mesh->triangles[i].v1 = indices[i * 3 + 1];
        mesh->triangles[i].v2 = indices[i * 3 + 2];
    }
    
    return mesh;
}

void nx3d_destroy_mesh(nx3d_mesh_t *mesh) {
    if (!mesh) return;
    if (mesh->vertices) kfree(mesh->vertices);
    if (mesh->triangles) kfree(mesh->triangles);
    kfree(mesh);
}
