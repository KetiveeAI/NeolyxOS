/*
 * NeolyxOS - NXGFX internal interfaces
 *
 * Shared by the NXGFX translation units only (context.c, text.c,
 * nxgfx_canvas.c). Nothing outside src/nxgfx/ may include this header.
 *
 * Since R1 (docs/desktop/nxgfx-on-nxcanvas.md) NXGFX draws its UI
 * primitives and text through NXCanvas, the engine the desktop shell
 * uses: analytic area-coverage anti-aliasing and TrueType outlines. The
 * pre-R1 integer rasterizers and the 8x16 bitmap font remain as the
 * fallback when the engine cannot start or no font was registered; every
 * fallback is logged once.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXGFX_INTERNAL_H
#define NXGFX_INTERNAL_H

#include "nxgfx/nxgfx.h"
#include "../../../../desktop/include/nxcanvas.h"

struct nx_context {
    uint32_t* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;              /* bytes */
    nx_rect_t clip;
    bool has_clip;
    /* 3D rendering support */
    float* depth_buffer;
    bool depth_test_enabled;
    /* NXCanvas view of the same pixels (valid when nxc_ok) */
    nxc_surface_t nxc_surface;
    nxc_canvas_t  nxc_canvas;
    bool nxc_ok;
};

/* Write one diagnostic line through the sink set by nxgfx_set_log_sink
 * (no-op without one). Lines are prefixed with "[NXGFX] " by the caller. */
void nxgfx__log(const char* msg);

/* Log `msg` only the first time `*once` is seen clear, then set it. */
void nxgfx__log_once(bool* once, const char* msg);

/*
 * Prepare ctx for NXCanvas drawing: starts the engine on first use and
 * wraps ctx's pixels. Called by nxgfx_init. Failure is not fatal: ctx
 * keeps nxc_ok = false and the legacy rasterizers draw instead.
 */
void nxgfx__canvas_attach(nx_context_t* ctx);

/*
 * The canvas for ctx with its clip set to ctx's current clip, or NULL
 * when ctx has no NXCanvas view (callers then use the legacy path).
 */
nxc_canvas_t* nxgfx__canvas(nx_context_t* ctx);

/* The registered UI face, or NULL (text then uses the bitmap font). */
const nxc_font_t* nxgfx__ui_font(void);

/* nx_color_t (straight RGBA) -> NXCanvas 0xAARRGGBB (straight alpha). */
static inline nxc_color_t nxgfx__color(nx_color_t c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

#endif /* NXGFX_INTERNAL_H */
