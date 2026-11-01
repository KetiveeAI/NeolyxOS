/*
 * NeolyxOS - NXGFX on NXCanvas (engine bridge)
 *
 * Starts the NXCanvas engine for the process on first use, wraps each
 * NXGFX context's pixels as an NXCanvas surface, keeps the registered UI
 * typeface, and routes diagnostics to the log sink.
 *
 * Memory: the engine allocates once, at start, through malloc: scratch
 * rows sized for NXGFX_MAX_DIM, a 1 MiB glyph cache and a 256 KiB shadow
 * cache (about 2 MiB in total). Drawing never allocates.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx_internal.h"
#include <stdlib.h>

/* Largest context NXCanvas draws into; larger ones use the legacy path. */
#define NXGFX_MAX_DIM          4096
#define NXGFX_GLYPH_CACHE      (1u * 1024u * 1024u)
#define NXGFX_SHADOW_CACHE     (256u * 1024u)
/* Accepted TrueType size range for a registered face. */
#define NXGFX_FONT_MIN_BYTES   1024u
#define NXGFX_FONT_MAX_BYTES   (8u * 1024u * 1024u)

/* ============ Logging ============ */

static void (*g_log_sink)(const char* msg) = NULL;

void nxgfx_set_log_sink(void (*sink)(const char* msg)) {
    g_log_sink = sink;
}

void nxgfx__log(const char* msg) {
    if (g_log_sink && msg) g_log_sink(msg);
}

void nxgfx__log_once(bool* once, const char* msg) {
    if (!once || *once) return;
    *once = true;
    nxgfx__log(msg);
}

/* NXCanvas lines already carry their own "[NXC] " prefix */
static void engine_log(const char* msg) {
    nxgfx__log(msg);
}

/* ============ Engine ============ */

typedef enum { ENGINE_UNTRIED = 0, ENGINE_READY = 1, ENGINE_FAILED = -1 } engine_state_t;

static engine_state_t g_engine = ENGINE_UNTRIED;
static nxc_font_t* g_ui_font = NULL;
static bool g_warned_big = false;
static bool g_warned_wrap = false;

/* NXCanvas requires 16-byte aligned blocks; refuse rather than misalign. */
static void* engine_alloc(uint64_t size) {
    if (size == 0 || size > (uint64_t)SIZE_MAX) return NULL;
    void* p = malloc((size_t)size);
    if (p && ((uintptr_t)p & 15u) != 0) {
        free(p);
        nxgfx__log("[NXGFX] allocator returned a block that is not 16-byte aligned\n");
        return NULL;
    }
    return p;
}

static void engine_free(void* p) {
    free(p);
}

/* Start the engine once; returns true when it is ready. */
static bool engine_start(void) {
    if (g_engine == ENGINE_READY) return true;
    if (g_engine == ENGINE_FAILED) return false;

    nxc_env_t env = {
        .alloc = engine_alloc,
        .free = engine_free,
        .log = engine_log,
        .max_width = NXGFX_MAX_DIM,
        .max_height = NXGFX_MAX_DIM,
        .glyph_cache_bytes = NXGFX_GLYPH_CACHE,
        .shadow_cache_bytes = NXGFX_SHADOW_CACHE,
    };
    nxc_status_t st = nxc_init(&env);
    if (st != NXC_OK) {
        g_engine = ENGINE_FAILED;
        nxgfx__log("[NXGFX] NXCanvas engine failed to start (");
        nxgfx__log(nxc_status_str(st));
        nxgfx__log("); drawing with the legacy rasterizer and bitmap font\n");
        return false;
    }
    g_engine = ENGINE_READY;
    nxgfx__log("[NXGFX] NXCanvas engine ready (anti-aliased shapes)\n");
    return true;
}

bool nxgfx_engine_ready(void) {
    return g_engine == ENGINE_READY;
}

/* ============ Contexts ============ */

void nxgfx__canvas_attach(nx_context_t* ctx) {
    if (!ctx) return;
    ctx->nxc_ok = false;
    if (!engine_start()) return;

    if (ctx->width > NXGFX_MAX_DIM || ctx->height > NXGFX_MAX_DIM) {
        nxgfx__log_once(&g_warned_big,
            "[NXGFX] context larger than 4096x4096; it draws with the legacy rasterizer\n");
        return;
    }
    if (ctx->pitch % 4u != 0 || ctx->pitch / 4u < ctx->width ||
        ctx->pitch / 4u > (uint32_t)INT32_MAX) {
        nxgfx__log_once(&g_warned_wrap,
            "[NXGFX] context pitch is not a whole number of pixels >= width; legacy rasterizer\n");
        return;
    }
    /* NXGFX targets are opaque XRGB8888 (window buffers, framebuffers) */
    nxc_status_t st = nxc_surface_wrap(&ctx->nxc_surface, ctx->framebuffer,
                                       (int32_t)ctx->width, (int32_t)ctx->height,
                                       (int32_t)(ctx->pitch / 4u), NXC_SURFACE_OPAQUE);
    if (st == NXC_OK) st = nxc_canvas_init(&ctx->nxc_canvas, &ctx->nxc_surface);
    if (st != NXC_OK) {
        nxgfx__log_once(&g_warned_wrap,
            "[NXGFX] could not wrap the context for NXCanvas; legacy rasterizer\n");
        return;
    }
    ctx->nxc_ok = true;
}

nxc_canvas_t* nxgfx__canvas(nx_context_t* ctx) {
    if (!ctx || !ctx->nxc_ok || g_engine != ENGINE_READY) return NULL;
    nxc_canvas_t* c = &ctx->nxc_canvas;
    /* Re-derive the clip from ctx each call: NXGFX clips are absolute,
     * NXCanvas clips only intersect. Resetting is a few stores. */
    if (nxc_canvas_init(c, &ctx->nxc_surface) != NXC_OK) return NULL;
    if (ctx->has_clip) {
        nxc_irect_t r = { ctx->clip.x, ctx->clip.y,
                          (int32_t)(ctx->clip.width > (uint32_t)INT32_MAX ? INT32_MAX : ctx->clip.width),
                          (int32_t)(ctx->clip.height > (uint32_t)INT32_MAX ? INT32_MAX : ctx->clip.height) };
        if (nxc_clip_rect(c, r) != NXC_OK) return NULL;
    }
    return c;
}

/* ============ Fonts ============ */

int nxgfx_font_register(const uint8_t* data, uint32_t size) {
    if (!data) {
        nxgfx__log("[NXGFX] font_register: data is NULL\n");
        return -1;
    }
    if (size < NXGFX_FONT_MIN_BYTES || size > NXGFX_FONT_MAX_BYTES) {
        nxgfx__log("[NXGFX] font_register: size outside 1 KiB..8 MiB\n");
        return -1;
    }
    if (!engine_start()) return -1;

    nxc_font_t* font = NULL;
    nxc_status_t st = nxc_font_load(&font, data, size);
    if (st != NXC_OK || !font) {
        nxgfx__log("[NXGFX] font_register: not a usable TrueType font (");
        nxgfx__log(nxc_status_str(st));
        nxgfx__log(")\n");
        return -1;
    }
    if (g_ui_font) nxc_font_destroy(g_ui_font);
    g_ui_font = font;
    nxgfx__log("[NXGFX] UI font registered (outline text)\n");
    return 0;
}

const nxc_font_t* nxgfx__ui_font(void) {
    return g_engine == ENGINE_READY ? g_ui_font : NULL;
}
