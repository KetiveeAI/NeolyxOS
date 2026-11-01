/*
 * NeolyxOS - NXCanvas 2D Rendering Engine (userspace)
 *
 * Public API for the desktop's software renderer: anti-aliased vector
 * shapes, gradients, images, blur/shadow/backdrop materials and TrueType
 * text. Pure freestanding C11: no libc, no syscalls, no third-party code.
 * All memory comes from the allocator handed to nxc_init(); the engine
 * allocates its scratch buffers once at init and never during drawing,
 * except for the bounded glyph and shadow caches it also sizes at init.
 *
 * Pixel format
 *   Surfaces hold 32-bit premultiplied ARGB stored as uint32_t
 *   0xAARRGGBB (little-endian bytes B,G,R,A), which matches the UEFI
 *   GOP BGRX framebuffer. Opaque surfaces (the backbuffer) keep A=0xFF.
 *   Colors passed to the API (nxc_color_t) are NON-premultiplied
 *   0xAARRGGBB; the engine premultiplies internally.
 *
 * Coordinates
 *   Floats in pixels. Pixel (x, y) covers the square [x, x+1) x [y, y+1);
 *   its center is (x + 0.5, y + 0.5). Integer-aligned rects are crisp.
 *
 * Errors
 *   Every fallible call returns nxc_status_t (NXC_OK or a negative code).
 *   Drawing calls validate their arguments and return a status as well;
 *   a failing draw draws nothing. The engine also counts errors and logs
 *   the first occurrence of each code through the env log hook, so a
 *   caller that ignores a draw status does not hide the failure.
 *
 * Threading
 *   Not thread-safe. The desktop renders from a single thread.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXCANVAS_H
#define NXCANVAS_H

#include <stdint.h>
#include <stddef.h>

/* ============ Status codes ============ */

typedef int32_t nxc_status_t;

#define NXC_OK                 0
#define NXC_ERR_INVALID_ARG   -1   /* NULL pointer, NaN, negative size... */
#define NXC_ERR_NO_MEMORY     -2   /* allocator failed or cache too small */
#define NXC_ERR_BAD_FONT      -3   /* malformed or unsupported font data */
#define NXC_ERR_OVERFLOW      -4   /* path/stack/scratch capacity exceeded */
#define NXC_ERR_NOT_FOUND     -5   /* glyph, table or entry not present */
#define NXC_ERR_STATE         -6   /* engine not initialized, unbalanced save */
#define NXC_ERR_UNSUPPORTED   -7   /* valid input the engine does not handle */

/* Human-readable name of a status code (never NULL). */
const char *nxc_status_str(nxc_status_t status);

/* ============ Basic types ============ */

typedef uint32_t nxc_color_t;           /* 0xAARRGGBB, straight alpha */

#define NXC_ARGB(a, r, g, b) \
    ((nxc_color_t)((((uint32_t)(a) & 0xFFu) << 24) | (((uint32_t)(r) & 0xFFu) << 16) | \
                   (((uint32_t)(g) & 0xFFu) << 8)  |  ((uint32_t)(b) & 0xFFu)))
#define NXC_RGB(r, g, b)        NXC_ARGB(0xFF, r, g, b)
/* Replace the alpha of an opaque/any color: a in 0..255 */
#define NXC_WITH_ALPHA(c, a)    ((nxc_color_t)(((c) & 0x00FFFFFFu) | (((uint32_t)(a) & 0xFFu) << 24)))

typedef struct { float x, y; } nxc_point_t;
typedef struct { float x, y, w, h; } nxc_rect_t;          /* w, h >= 0 */
typedef struct { int32_t x, y, w, h; } nxc_irect_t;       /* w, h >= 0 */

static inline nxc_rect_t nxc_rect(float x, float y, float w, float h) {
    nxc_rect_t r; r.x = x; r.y = y; r.w = w; r.h = h; return r;
}
static inline nxc_irect_t nxc_irect(int32_t x, int32_t y, int32_t w, int32_t h) {
    nxc_irect_t r; r.x = x; r.y = y; r.w = w; r.h = h; return r;
}

/* Linear interpolation between two straight-alpha colors, t in [0,1]. */
nxc_color_t nxc_color_lerp(nxc_color_t a, nxc_color_t b, float t);
/* Multiply a color's alpha by k in [0,1]. */
nxc_color_t nxc_color_fade(nxc_color_t c, float k);

/* ============ Engine environment ============ */

typedef struct {
    /* Required. Must return 16-byte aligned memory or NULL. */
    void *(*alloc)(uint64_t size);
    /* Optional (may be NULL or a no-op for bump allocators). */
    void  (*free)(void *ptr);
    /* Optional diagnostic sink; receives NUL-terminated lines. */
    void  (*log)(const char *msg);

    /* Largest surface the engine will ever draw into (sizes scratch). */
    int32_t max_width;
    int32_t max_height;

    /* Cache budgets in bytes; 0 selects the defaults below. */
    uint32_t glyph_cache_bytes;          /* default 2 MiB */
    uint32_t shadow_cache_bytes;         /* default 2 MiB */
} nxc_env_t;

#define NXC_DEFAULT_GLYPH_CACHE_BYTES   (2u * 1024u * 1024u)
#define NXC_DEFAULT_SHADOW_CACHE_BYTES  (2u * 1024u * 1024u)

/* Initialize the engine once. Allocates all scratch and cache memory. */
nxc_status_t nxc_init(const nxc_env_t *env);
/* Release engine memory (through env->free when provided). */
void nxc_shutdown(void);
/* Number of errors recorded since init (diagnostics). */
uint32_t nxc_error_count(void);

/* ============ Surfaces ============ */

#define NXC_SURFACE_OPAQUE   0x1u  /* alpha is always 0xFF (framebuffers) */
#define NXC_SURFACE_OWNED    0x2u  /* pixels allocated by the engine */

typedef struct {
    uint32_t *pixels;      /* premultiplied ARGB32 */
    int32_t   width;
    int32_t   height;
    int32_t   stride;      /* in pixels, >= width */
    uint32_t  flags;
} nxc_surface_t;

/* Wrap caller memory. stride is in pixels. */
nxc_status_t nxc_surface_wrap(nxc_surface_t *s, uint32_t *pixels,
                              int32_t width, int32_t height,
                              int32_t stride, uint32_t flags);
/* Allocate a surface through the env allocator (contents zeroed). */
nxc_status_t nxc_surface_create(nxc_surface_t *s, int32_t width,
                                int32_t height, uint32_t flags);
void nxc_surface_destroy(nxc_surface_t *s);
/* Fill the whole surface with one color (no blending). */
nxc_status_t nxc_surface_clear(nxc_surface_t *s, nxc_color_t color);
/* Copy a region between surfaces (no blending, no scaling, clipped). */
nxc_status_t nxc_surface_copy(nxc_surface_t *dst, int32_t dx, int32_t dy,
                              const nxc_surface_t *src, nxc_irect_t src_rect);

/* ============ Canvas (drawing state) ============ */

#define NXC_STATE_STACK_MAX 16

typedef struct {
    nxc_irect_t clip;          /* device-space rect clip */
    nxc_rect_t  rclip;         /* device-space rounded clip bounds */
    float       rclip_radius;  /* 0 = no rounded clip */
    float       tx, ty;        /* translation applied to all coordinates */
    uint8_t     alpha;         /* global alpha 0..255 */
} nxc_state_t;

typedef struct {
    nxc_surface_t *target;
    nxc_state_t    st;
    nxc_state_t    stack[NXC_STATE_STACK_MAX];
    int32_t        depth;
} nxc_canvas_t;

nxc_status_t nxc_canvas_init(nxc_canvas_t *c, nxc_surface_t *target);
/* Push/pop clip, rounded clip, translation and alpha. */
nxc_status_t nxc_save(nxc_canvas_t *c);
nxc_status_t nxc_restore(nxc_canvas_t *c);
/* Intersect the clip with a rect (in current, translated coordinates). */
nxc_status_t nxc_clip_rect(nxc_canvas_t *c, nxc_irect_t r);
/* Intersect the clip with a rounded rect. One rounded clip is active at a
 * time: a nested call keeps the bounds intersection and the new radius. */
nxc_status_t nxc_clip_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius);
nxc_status_t nxc_translate(nxc_canvas_t *c, float dx, float dy);
/* Multiply the global alpha by a (0..255). */
nxc_status_t nxc_set_alpha(nxc_canvas_t *c, uint8_t a);

/* ============ Paint (solid / gradients) ============ */

#define NXC_MAX_STOPS 8

typedef enum {
    NXC_PAINT_SOLID  = 0,
    NXC_PAINT_LINEAR = 1,   /* along p0 -> p1 */
    NXC_PAINT_RADIAL = 2    /* center p0, radius r */
} nxc_paint_type_t;

typedef struct { float pos; nxc_color_t color; } nxc_stop_t;   /* pos 0..1 */

typedef struct {
    nxc_paint_type_t type;
    nxc_color_t      color;                  /* SOLID */
    nxc_point_t      p0, p1;                 /* LINEAR: start/end; RADIAL: p0=center */
    float            radius;                 /* RADIAL */
    nxc_stop_t       stops[NXC_MAX_STOPS];   /* sorted by pos */
    int32_t          nstops;                 /* 2..NXC_MAX_STOPS */
    int32_t          dither;                 /* 1 = ordered dither (no banding) */
} nxc_paint_t;

nxc_paint_t nxc_paint_solid(nxc_color_t color);
nxc_paint_t nxc_paint_linear(float x0, float y0, float x1, float y1,
                             nxc_color_t c0, nxc_color_t c1);
nxc_paint_t nxc_paint_radial(float cx, float cy, float radius,
                             nxc_color_t inner, nxc_color_t outer);
/* Append a stop (keeps order). Returns NXC_ERR_OVERFLOW when full. */
nxc_status_t nxc_paint_add_stop(nxc_paint_t *p, float pos, nxc_color_t color);

/* ============ Fast shapes (analytic anti-aliasing) ============ */

nxc_status_t nxc_fill_rect(nxc_canvas_t *c, nxc_rect_t r, nxc_color_t color);
nxc_status_t nxc_fill_rect_paint(nxc_canvas_t *c, nxc_rect_t r, const nxc_paint_t *p);
nxc_status_t nxc_fill_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius, nxc_color_t color);
nxc_status_t nxc_fill_rrect_paint(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                  const nxc_paint_t *p);
/* Border drawn inside r (width <= half the smaller side). */
nxc_status_t nxc_stroke_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius,
                              float width, nxc_color_t color);
nxc_status_t nxc_fill_circle(nxc_canvas_t *c, float cx, float cy, float r,
                             nxc_color_t color);
nxc_status_t nxc_fill_circle_paint(nxc_canvas_t *c, float cx, float cy, float r,
                                   const nxc_paint_t *p);
nxc_status_t nxc_stroke_circle(nxc_canvas_t *c, float cx, float cy, float r,
                               float width, nxc_color_t color);
/* Line with round caps. */
nxc_status_t nxc_draw_line(nxc_canvas_t *c, float x0, float y0, float x1, float y1,
                           float width, nxc_color_t color);

/* ============ Paths ============ */

#define NXC_VERB_MOVE   0
#define NXC_VERB_LINE   1
#define NXC_VERB_QUAD   2   /* 2 points: control, end */
#define NXC_VERB_CUBIC  3   /* 3 points: c1, c2, end */
#define NXC_VERB_CLOSE  4

typedef struct {
    nxc_point_t *pts;
    uint8_t     *verbs;
    uint32_t     npts, nverbs;
    uint32_t     cap_pts, cap_verbs;
    nxc_status_t error;        /* sticky: first failure while building */
    uint32_t     owned;        /* storage allocated by nxc_path_init_alloc */
} nxc_path_t;

/* Use caller storage (no allocation). */
nxc_status_t nxc_path_init(nxc_path_t *p, nxc_point_t *pts, uint32_t cap_pts,
                           uint8_t *verbs, uint32_t cap_verbs);
/* Allocate storage through the env allocator. */
nxc_status_t nxc_path_init_alloc(nxc_path_t *p, uint32_t cap_pts, uint32_t cap_verbs);
void nxc_path_destroy(nxc_path_t *p);
void nxc_path_reset(nxc_path_t *p);            /* clears contents and error */

nxc_status_t nxc_path_move_to(nxc_path_t *p, float x, float y);
nxc_status_t nxc_path_line_to(nxc_path_t *p, float x, float y);
nxc_status_t nxc_path_quad_to(nxc_path_t *p, float cx, float cy, float x, float y);
nxc_status_t nxc_path_cubic_to(nxc_path_t *p, float c1x, float c1y,
                               float c2x, float c2y, float x, float y);
nxc_status_t nxc_path_close(nxc_path_t *p);
nxc_status_t nxc_path_add_rect(nxc_path_t *p, nxc_rect_t r);
nxc_status_t nxc_path_add_rrect(nxc_path_t *p, nxc_rect_t r, float radius);
/* Continuous-curvature ("squircle") rounded rect, as used by app icons. */
nxc_status_t nxc_path_add_squircle(nxc_path_t *p, nxc_rect_t r, float radius);
nxc_status_t nxc_path_add_circle(nxc_path_t *p, float cx, float cy, float r);
nxc_status_t nxc_path_add_ellipse(nxc_path_t *p, float cx, float cy, float rx, float ry);
/* Arc from angle a0 to a1 (radians, clockwise in screen space). Starts a
 * new subpath with a move when the path is empty, else lines to the start. */
nxc_status_t nxc_path_add_arc(nxc_path_t *p, float cx, float cy, float r,
                              float a0, float a1);
/* Apply x' = x*sx + tx, y' = y*sy + ty to every point. */
nxc_status_t nxc_path_transform(nxc_path_t *p, float sx, float sy, float tx, float ty);
nxc_status_t nxc_path_bounds(const nxc_path_t *p, nxc_rect_t *out);

typedef enum { NXC_FILL_NONZERO = 0, NXC_FILL_EVENODD = 1 } nxc_fill_rule_t;
typedef enum { NXC_CAP_BUTT = 0, NXC_CAP_ROUND = 1, NXC_CAP_SQUARE = 2 } nxc_cap_t;
typedef enum { NXC_JOIN_MITER = 0, NXC_JOIN_ROUND = 1, NXC_JOIN_BEVEL = 2 } nxc_join_t;

typedef struct {
    float      width;          /* > 0 */
    nxc_cap_t  cap;
    nxc_join_t join;
    float      miter_limit;    /* default 4 */
} nxc_stroke_t;

nxc_status_t nxc_fill_path(nxc_canvas_t *c, const nxc_path_t *p,
                           nxc_fill_rule_t rule, nxc_color_t color);
nxc_status_t nxc_fill_path_paint(nxc_canvas_t *c, const nxc_path_t *p,
                                 nxc_fill_rule_t rule, const nxc_paint_t *paint);
nxc_status_t nxc_stroke_path(nxc_canvas_t *c, const nxc_path_t *p,
                             const nxc_stroke_t *s, nxc_color_t color);

/* ============ Images and masks ============ */

typedef enum {
    NXC_FILTER_NEAREST  = 0,
    NXC_FILTER_BILINEAR = 1,   /* upscaling and mild downscaling */
    NXC_FILTER_SMOOTH   = 2    /* area-averaged downscale + bilinear */
} nxc_filter_t;

/* Draw src_rect of src into dst (scaled), src-over, multiplied by alpha. */
nxc_status_t nxc_draw_surface(nxc_canvas_t *c, const nxc_surface_t *src,
                              nxc_irect_t src_rect, nxc_rect_t dst,
                              uint8_t alpha, nxc_filter_t filter);
/* Same, masked to a rounded rect of the given radius. */
nxc_status_t nxc_draw_surface_rrect(nxc_canvas_t *c, const nxc_surface_t *src,
                                    nxc_irect_t src_rect, nxc_rect_t dst,
                                    float radius, uint8_t alpha, nxc_filter_t filter);
/* Draw straight-alpha RGBA8 bytes (r,g,b,a order, e.g. NXIB icons). */
nxc_status_t nxc_draw_rgba8(nxc_canvas_t *c, const uint8_t *rgba, int32_t w, int32_t h,
                            int32_t stride_bytes, nxc_rect_t dst, uint8_t alpha,
                            nxc_filter_t filter);
/* Draw opaque 32-bit pixels (0x??RRGGBB, top byte ignored), such as an
 * app's shared-memory window buffer. px and stride_bytes must be 4-byte
 * aligned. */
nxc_status_t nxc_draw_xrgb32(nxc_canvas_t *c, const uint32_t *px, int32_t w, int32_t h,
                             int32_t stride_bytes, nxc_rect_t dst, uint8_t alpha,
                             nxc_filter_t filter);
/* Colorize an 8-bit coverage mask at integer position (x, y). */
nxc_status_t nxc_draw_mask(nxc_canvas_t *c, const uint8_t *mask, int32_t w, int32_t h,
                           int32_t stride, int32_t x, int32_t y, nxc_color_t color);

/* ============ Effects ============ */

/* Gaussian blur (three box passes) of a surface region, in place. */
nxc_status_t nxc_blur_surface(nxc_surface_t *s, nxc_irect_t region, float sigma);
/* Gaussian blur of an 8-bit mask, in place. */
nxc_status_t nxc_blur_mask(uint8_t *mask, int32_t w, int32_t h, int32_t stride, float sigma);

/* Soft drop shadow of a rounded rect, drawn OUTSIDE-and-under r (callers
 * draw the shape itself afterwards). offset (dx, dy), spread grows the
 * shape before blurring. Masks are cached by (size, radius, sigma). */
nxc_status_t nxc_draw_shadow_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                   float sigma, float dx, float dy, float spread,
                                   nxc_color_t color);

typedef struct {
    float       blur_sigma;    /* used only when blurring live (source NULL) */
    float       saturation;    /* 1 = unchanged, >1 more vivid (glass ~1.6) */
    nxc_color_t tint;          /* blended over the blurred backdrop */
    uint8_t     noise;         /* 0..16: grain amplitude (reduces banding) */
    nxc_color_t border;        /* 1px inner highlight; alpha 0 = none */
} nxc_material_t;

/* Frosted glass: fills rrect r with the backdrop seen through it.
 * blurred_source: a pre-blurred copy of what lies behind (same size and
 * coordinates as the canvas target), or NULL to blur the target live in r. */
nxc_status_t nxc_draw_backdrop(nxc_canvas_t *c, nxc_rect_t r, float radius,
                               const nxc_surface_t *blurred_source,
                               const nxc_material_t *m);

/* ============ Fonts and text ============ */

typedef struct nxc_font nxc_font_t;   /* opaque parsed TrueType face */

/* Parse and validate a TrueType (glyf) font. data must outlive the font. */
nxc_status_t nxc_font_load(nxc_font_t **out, const uint8_t *data, uint32_t size);
void nxc_font_destroy(nxc_font_t *font);

typedef struct {
    float units_per_em;
    float ascent;      /* px above baseline (positive) */
    float descent;     /* px below baseline (positive) */
    float line_gap;    /* px */
    float cap_height;  /* px */
    float x_height;    /* px */
} nxc_font_metrics_t;

nxc_status_t nxc_font_metrics(const nxc_font_t *font, float px_size,
                              nxc_font_metrics_t *out);

typedef enum { NXC_ALIGN_LEFT = 0, NXC_ALIGN_CENTER = 1, NXC_ALIGN_RIGHT = 2 } nxc_align_t;

typedef struct {
    float       letter_spacing;  /* extra px between glyphs (tracking) */
    nxc_align_t align;           /* relative to x */
    float       max_width;       /* > 0: truncate with an ellipsis */
    int32_t     no_kerning;      /* 1 = disable kerning */
} nxc_text_opts_t;

/* UTF-8 text; len < 0 means NUL-terminated. Invalid sequences render as
 * U+FFFD. Width in px including kerning and letter spacing. */
float nxc_text_width(const nxc_font_t *font, float px_size,
                     const char *utf8, int32_t len, const nxc_text_opts_t *opts);
/* Draw at baseline y. opts may be NULL. */
nxc_status_t nxc_draw_text(nxc_canvas_t *c, const nxc_font_t *font, float px_size,
                           float x, float baseline_y, const char *utf8, int32_t len,
                           nxc_color_t color, const nxc_text_opts_t *opts);
/* Append the outline of one code point to a path (for effects/icons). */
nxc_status_t nxc_font_glyph_path(const nxc_font_t *font, uint32_t codepoint,
                                 float px_size, float x, float baseline_y,
                                 nxc_path_t *out, float *advance_out);

/* ============ Font information, coverage and fallback ============ */

/* Strings from the font's 'name' table, converted to UTF-8 (control
 * characters become spaces) and truncated at a character boundary to
 * the buffer size. Missing names are empty strings. */
typedef struct {
    char     family[64];        /* typographic family (name 16, else 1) */
    char     style[48];         /* typographic subfamily (name 17, else 2) */
    char     full_name[96];     /* name 4 */
    char     version[64];       /* name 5 */
    char     copyright[160];    /* name 0 */
    char     license[512];      /* name 13 (license description) */
    uint16_t weight;            /* OS/2 usWeightClass (1..1000; 400 if absent) */
    uint8_t  italic;            /* 1 = italic/oblique face */
    uint8_t  monospace;         /* 1 = post.isFixedPitch */
    uint32_t num_glyphs;
    uint32_t units_per_em;
    uint32_t file_size;         /* bytes of font data */
    uint32_t font_revision;     /* head.fontRevision, 16.16 fixed */
    uint8_t  kerning;           /* 0 none, 1 'kern' table, 2 GPOS pair kerning */
    uint8_t  reserved[3];
} nxc_font_info_t;

/* Why the most recent nxc_font_load call rejected its data, as a short
 * static English phrase ("table outside the file", ...); "" when the last
 * load succeeded or failed for a non-format reason. */
const char *nxc_font_last_reject(void);

/* Fill *out from a loaded font. Never allocates. */
nxc_status_t nxc_font_info(const nxc_font_t *font, nxc_font_info_t *out);

/* 1 when the font maps code point cp to a real glyph (fallbacks ignored). */
int nxc_font_has_char(const nxc_font_t *font, uint32_t cp);

/* Smallest code point >= from that the font maps to a glyph, or
 * NXC_NO_CHAR when there is none (iterate a character map with
 * cp = nxc_font_next_char(f, cp + 1)). */
#define NXC_NO_CHAR 0xFFFFFFFFu
uint32_t nxc_font_next_char(const nxc_font_t *font, uint32_t from);

/*
 * Fallback chain: text drawn with `font` uses the first face in `chain`
 * that maps a code point the font itself lacks. One level only (the
 * fallbacks' own chains are not followed), at most NXC_FONT_MAX_FALLBACKS
 * faces, no self references. count 0 clears the chain. The caller keeps
 * every face alive while it is in a chain; a face destroyed anyway is
 * detected (by id) and skipped.
 */
#define NXC_FONT_MAX_FALLBACKS 4
nxc_status_t nxc_font_set_fallbacks(nxc_font_t *font, const nxc_font_t *const *chain,
                                    uint32_t count);

/* ============ Text rendering quality ============ */

/*
 * Engine-wide rasterization settings for text. Changing them drops the
 * glyph cache. Defaults (nxc_text_set_quality(NULL)):
 *   vertical_hinting 1, hint_max_px 24, subpixel_phases 4, contrast 0.5,
 *   gamma 1 (polarity- and size-aware coverage correction).
 */
typedef struct {
    int32_t vertical_hinting;   /* 1 = snap x-height to whole pixels at UI sizes */
    float   hint_max_px;        /* largest size that is hinted (8..64) */
    int32_t subpixel_phases;    /* horizontal glyph positions per px: 1, 2 or 4 */
    float   contrast;           /* 0..1 edge contrast boost at small sizes */
    int32_t gamma;              /* 1 = correct coverage for text polarity and size */
} nxc_text_quality_t;

/* NULL restores the defaults. NXC_ERR_INVALID_ARG for out-of-range values. */
nxc_status_t nxc_text_set_quality(const nxc_text_quality_t *q);
void nxc_text_get_quality(nxc_text_quality_t *out);

typedef struct {
    uint32_t hits;              /* glyphs drawn from the cache */
    uint32_t misses;            /* glyphs rasterized into the cache */
    uint32_t uncached;          /* glyphs rasterized straight to the canvas */
    uint32_t evictions;         /* cached bitmaps found overwritten by newer ones */
    uint32_t fallbacks;         /* code points resolved through a fallback face */
    uint32_t capacity;          /* cache table slots */
    uint32_t arena_bytes;       /* bitmap ring size */
} nxc_text_stats_t;

/* Glyph cache counters since the cache was last (re)built. */
void nxc_text_stats(nxc_text_stats_t *out);

#endif /* NXCANVAS_H */
