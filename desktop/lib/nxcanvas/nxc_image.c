/*
 * NeolyxOS - NXCanvas images
 *
 * Scaled drawing of premultiplied surfaces and straight-alpha RGBA8 images
 * (NXIB icons). Destination rects may be fractional and scaled; partially
 * covered edge pixels receive their exact area coverage. Sampling maps the
 * center of destination pixel p to source position
 *     u = src.x + (p + 0.5 - dst.x) * (src.w / dst.w)
 * so integer-aligned unscaled draws are exact copies. Filters:
 *   NEAREST   point sample at u
 *   BILINEAR  premultiplied 2x2 interpolation around u, clamp-to-edge
 *   SMOOTH    per axis: exact area averaging (box prefilter over the
 *             source footprint of each destination pixel) when that axis
 *             shrinks below 0.5x, bilinear otherwise. At exactly 0.5x the
 *             two are identical, so the switch is seamless; strong
 *             downscales such as 512 -> 48 px icons stay crisp, not aliased.
 * Unscaled integer-aligned draws use a direct blit fast path. Other rows
 * are assembled in NXC_SCRATCH_LINE0 and written with nxc__span_pixels, so
 * clip, rounded clip and global alpha apply as for every other draw.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"

#define IMG_FMT_PREMUL32  0     /* nxc_surface_t pixels (premultiplied ARGB) */
#define IMG_FMT_RGBA8     1     /* straight-alpha bytes r, g, b, a */
#define IMG_FMT_XRGB32    2     /* opaque 32-bit pixels, top byte ignored */

/* Coordinates beyond this magnitude are rejected (as in nxc_clip_rect). */
#define IMG_COORD_MAX     1.0e8f
/* Destination extents below this cover under 1/512 of any pixel: nothing
 * visible, and it bounds the scale factor for the fixed-point paths. */
#define IMG_MIN_EXTENT    (1.0f / 512.0f)
/* Largest image side accepted (same limit as surfaces). */
#define IMG_DIM_MAX       16384

typedef struct {
    const uint8_t *base;        /* address of pixel (0, 0) */
    int64_t        stride;      /* bytes per row */
    int32_t        w, h;
    int32_t        fmt;         /* IMG_FMT_* */
} img_src_t;

/* Separable filter taps: source indices [first, last], weight wf on first,
 * wl on last and wm on every index strictly between them. */
typedef struct {
    int32_t first, last;
    float   wf, wm, wl;
} img_tap_t;

/* Device-space placement of one draw, shared by all filter paths. */
typedef struct {
    double      x0, y0, x1, y1;     /* destination rect, device space */
    double      sx, sy;             /* source pixels per destination pixel */
    nxc_irect_t sr;                 /* source rect, inside the image, non-empty */
    int32_t     ix0, ix1, iy0, iy1; /* pixels the rect touches, [ix0, ix1) */
    int32_t     vx0, vx1, vy0, vy1; /* visible (clipped) part of those */
    uint32_t    kx_first;           /* coverage of column ix0 (0..255) */
    uint32_t    kx_last;            /* coverage of column ix1 - 1 (255 if == ix0) */
    uint32_t    alpha;              /* caller alpha 0..255 */
} img_job_t;

/* ============ Small helpers ============ */

static inline int64_t img_floor(double v) {
    int64_t t = (int64_t)v;
    return ((double)t > v) ? t - 1 : t;
}

static inline int64_t img_ceil(double v) {
    int64_t t = (int64_t)v;
    return ((double)t < v) ? t + 1 : t;
}

static inline int32_t img_clampi(int64_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : (v > hi ? hi : (int32_t)v);
}

/* Coverage 0..255 of pixel [p, p + 1) by the interval [a, b). */
static uint32_t img_edge_cov(int32_t p, double a, double b) {
    double lo = ((double)p > a) ? (double)p : a;
    double hi = ((double)p + 1.0 < b) ? (double)p + 1.0 : b;
    double cov = hi - lo;
    if (cov >= 1.0) return 255u;
    if (cov <= 0.0) return 0u;
    return (uint32_t)(cov * 255.0 + 0.5);
}

/* Straight-alpha RGBA bytes -> premultiplied ARGB32. */
static inline uint32_t img_rgba8_premul(const uint8_t *p) {
    uint32_t a = p[3];
    if (a == 0u) return 0u;
    uint32_t c = (a << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    if (a == 255u) return c;
    return (nxc__mul_px(c | 0xFF000000u, a) & 0x00FFFFFFu) | (a << 24);
}

static inline const uint8_t *img_row(const img_src_t *s, int32_t y) {
    return s->base + (int64_t)y * s->stride;
}

/* Premultiplied pixel x of a source row. */
static inline uint32_t img_px(const uint8_t *row, int32_t x, int32_t fmt) {
    if (fmt == IMG_FMT_PREMUL32) return ((const uint32_t *)(const void *)row)[x];
    if (fmt == IMG_FMT_XRGB32) return ((const uint32_t *)(const void *)row)[x] | 0xFF000000u;
    return img_rgba8_premul(row + (int64_t)x * 4);
}

/* Premultiplied lerp a -> b by w/256 (w in 0..255), rounded. Equal weights
 * on every channel keep the channel <= alpha invariant. */
static inline uint32_t img_lerp(uint32_t a, uint32_t b, uint32_t w) {
    uint32_t iw = 256u - w;
    uint32_t rb = ((a & 0x00FF00FFu) * iw + (b & 0x00FF00FFu) * w + 0x00800080u) >> 8;
    uint32_t ag = ((a >> 8) & 0x00FF00FFu) * iw + ((b >> 8) & 0x00FF00FFu) * w + 0x00800080u;
    return (rb & 0x00FF00FFu) | (ag & 0xFF00FF00u);
}

/* Write a finished row. With a rounded clip active the row is split so the
 * span blender can take its fast interior path for the middle part. */
static void img_write_row(nxc_canvas_t *c, int32_t y, int32_t x0, const uint32_t *px,
                          int32_t n, uint32_t k) {
    if (n <= 0 || k == 0u) return;
    if (c->st.rclip_radius > 0.0f && n > 8) {
        nxc__span_pixels(c, y, x0, px, 0, 2, k);
        nxc__span_pixels(c, y, x0 + 2, px + 2, 0, n - 4, k);
        nxc__span_pixels(c, y, x0 + n - 2, px + n - 2, 0, 2, k);
        return;
    }
    nxc__span_pixels(c, y, x0, px, 0, n, k);
}

/* Scale the partially covered first/last destination columns that fall in
 * the row segment [x0, x0 + n). */
static void img_apply_xedges(const img_job_t *j, int32_t x0, uint32_t *row, int32_t n) {
    if (j->kx_first < 255u && j->ix0 >= x0 && j->ix0 < x0 + n) {
        row[j->ix0 - x0] = nxc__mul_px(row[j->ix0 - x0], j->kx_first);
    }
    int32_t last = j->ix1 - 1;
    if (j->kx_last < 255u && last >= x0 && last < x0 + n) {
        row[last - x0] = nxc__mul_px(row[last - x0], j->kx_last);
    }
}

/* Constant extra coverage of destination row py (alpha x vertical edge). */
static inline uint32_t img_row_k(const img_job_t *j, int32_t py) {
    return nxc__div255(j->alpha * img_edge_cov(py, j->y0, j->y1));
}

/* ============ Filter taps (SMOOTH path) ============ */

/* Area-average taps for the source interval [a, b), clipped to [lo, hi). */
static void img_tap_area(double a, double b, int32_t lo, int32_t hi, img_tap_t *t) {
    if (a < (double)lo) a = (double)lo;
    if (b > (double)hi) b = (double)hi;
    t->wm = 0.0f;
    t->wl = 0.0f;
    if (!(b - a > 1e-9)) {
        /* Degenerate footprint: nearest source pixel. */
        t->first = t->last = img_clampi(img_floor(a), lo, hi - 1);
        t->wf = 1.0f;
        return;
    }
    int32_t f = img_clampi(img_floor(a), lo, hi - 1);
    int32_t l = img_clampi(img_ceil(b) - 1, lo, hi - 1);
    if (l < f) l = f;
    t->first = f;
    t->last = l;
    if (f == l) {
        t->wf = 1.0f;
        return;
    }
    double inv = 1.0 / (b - a);
    t->wf = (float)(((double)f + 1.0 - a) * inv);
    t->wl = (float)((b - (double)l) * inv);
    t->wm = (float)inv;
}

/* Linear (tent) taps around source position u (pixel-center based). */
static void img_tap_linear(double u, int32_t lo, int32_t hi, img_tap_t *t) {
    double c = u - 0.5;
    int64_t i0 = img_floor(c);
    double f = c - (double)i0;
    if (i0 < lo) { i0 = lo; f = 0.0; }
    else if (i0 >= hi - 1) { i0 = hi - 1; f = 0.0; }
    t->first = (int32_t)i0;
    t->last = (f > 0.0) ? (int32_t)i0 + 1 : (int32_t)i0;
    t->wf = (float)(1.0 - f);
    t->wl = (float)f;
    t->wm = 0.0f;
}

/* ============ Filter paths ============ */

/* Unscaled, integer-aligned copy. Surfaces are passed to the span blender
 * directly; RGBA8 rows are premultiplied into the line buffer first. */
static void img_blit(nxc_canvas_t *c, const img_src_t *src, const img_job_t *j,
                     uint32_t *line, int32_t cap) {
    int32_t sx0 = j->sr.x + (j->vx0 - j->ix0);
    for (int32_t py = j->vy0; py < j->vy1; py++) {
        const uint8_t *row = img_row(src, j->sr.y + (py - j->iy0));
        if (src->fmt == IMG_FMT_PREMUL32) {
            img_write_row(c, py, j->vx0, (const uint32_t *)(const void *)row + sx0,
                          j->vx1 - j->vx0, j->alpha);
            continue;
        }
        for (int32_t x = j->vx0; x < j->vx1; x += cap) {
            int32_t n = nxc__mini(cap, j->vx1 - x);
            const uint8_t *sp = row + (int64_t)(sx0 + (x - j->vx0)) * 4;
            if (src->fmt == IMG_FMT_XRGB32) {
                const uint32_t *sp32 = (const uint32_t *)(const void *)sp;
                for (int32_t i = 0; i < n; i++) line[i] = sp32[i] | 0xFF000000u;
            } else {
                for (int32_t i = 0; i < n; i++) line[i] = img_rgba8_premul(sp + (int64_t)i * 4);
            }
            img_write_row(c, py, x, line, n, j->alpha);
        }
    }
}

/* Point sampling. Column source indices are computed once per chunk. */
static void img_nearest(nxc_canvas_t *c, const img_src_t *src, const img_job_t *j,
                        uint32_t *line, int32_t *xt, int32_t cap) {
    int32_t xlo = j->sr.x, xhi = j->sr.x + j->sr.w - 1;
    int32_t ylo = j->sr.y, yhi = j->sr.y + j->sr.h - 1;
    for (int32_t cx = j->vx0; cx < j->vx1; cx += cap) {
        int32_t n = nxc__mini(cap, j->vx1 - cx);
        for (int32_t i = 0; i < n; i++) {
            double u = (double)j->sr.x + ((double)(cx + i) + 0.5 - j->x0) * j->sx;
            xt[i] = img_clampi(img_floor(u), xlo, xhi);
        }
        for (int32_t py = j->vy0; py < j->vy1; py++) {
            uint32_t k = img_row_k(j, py);
            if (k == 0u) continue;
            double v = (double)j->sr.y + ((double)py + 0.5 - j->y0) * j->sy;
            const uint8_t *row = img_row(src, img_clampi(img_floor(v), ylo, yhi));
            for (int32_t i = 0; i < n; i++) line[i] = img_px(row, xt[i], src->fmt);
            img_apply_xedges(j, cx, line, n);
            img_write_row(c, py, cx, line, n, k);
        }
    }
}

/* Bilinear sampling, 8-bit weights. Column table entries pack the left
 * source index (bits 8+) and the horizontal weight (bits 0..7). */
static void img_bilinear(nxc_canvas_t *c, const img_src_t *src, const img_job_t *j,
                         uint32_t *line, uint32_t *xt, int32_t cap) {
    int32_t xlo = j->sr.x, xhi = j->sr.x + j->sr.w - 1;
    int32_t ylo = j->sr.y, yhi = j->sr.y + j->sr.h - 1;
    for (int32_t cx = j->vx0; cx < j->vx1; cx += cap) {
        int32_t n = nxc__mini(cap, j->vx1 - cx);
        for (int32_t i = 0; i < n; i++) {
            double u = (double)j->sr.x + ((double)(cx + i) + 0.5 - j->x0) * j->sx - 0.5;
            int64_t i0 = img_floor(u);
            uint32_t fx = (uint32_t)((u - (double)i0) * 256.0);
            if (fx > 255u) fx = 255u;
            if (i0 < xlo) { i0 = xlo; fx = 0u; }
            else if (i0 >= xhi) { i0 = xhi; fx = 0u; }
            xt[i] = ((uint32_t)i0 << 8) | fx;
        }
        for (int32_t py = j->vy0; py < j->vy1; py++) {
            uint32_t k = img_row_k(j, py);
            if (k == 0u) continue;
            double v = (double)j->sr.y + ((double)py + 0.5 - j->y0) * j->sy - 0.5;
            int64_t j0 = img_floor(v);
            uint32_t fy = (uint32_t)((v - (double)j0) * 256.0);
            if (fy > 255u) fy = 255u;
            if (j0 < ylo) { j0 = ylo; fy = 0u; }
            else if (j0 >= yhi) { j0 = yhi; fy = 0u; }
            const uint8_t *r0 = img_row(src, (int32_t)j0);
            const uint8_t *r1 = fy ? img_row(src, (int32_t)j0 + 1) : r0;
            for (int32_t i = 0; i < n; i++) {
                uint32_t e = xt[i];
                int32_t i0 = (int32_t)(e >> 8);
                uint32_t fx = e & 0xFFu;
                uint32_t top = img_px(r0, i0, src->fmt);
                if (fx) top = img_lerp(top, img_px(r0, i0 + 1, src->fmt), fx);
                if (fy) {
                    uint32_t bot = img_px(r1, i0, src->fmt);
                    if (fx) bot = img_lerp(bot, img_px(r1, i0 + 1, src->fmt), fx);
                    top = img_lerp(top, bot, fy);
                }
                line[i] = top;
            }
            img_apply_xedges(j, cx, line, n);
            img_write_row(c, py, cx, line, n, k);
        }
    }
}

/* Build the taps of destination pixel p along one axis. */
static void img_axis_tap(int32_t p, double d0, double s0, double scale, int32_t lo,
                         int32_t hi, int area, img_tap_t *t) {
    if (area) {
        double a = s0 + ((double)p - d0) * scale;
        img_tap_area(a, a + scale, lo, hi, t);
    } else {
        img_tap_linear(s0 + ((double)p + 0.5 - d0) * scale, lo, hi, t);
    }
}

/*
 * Separable resampling for SMOOTH: per axis either exact area averaging
 * or linear taps. For each destination row, every source row in its
 * vertical footprint is filtered horizontally and accumulated (float) in
 * NXC_SCRATCH_LINE1, so memory is bounded by the destination width and
 * total work is proportional to the source area actually covered.
 */
static void img_separable(nxc_canvas_t *c, const img_src_t *src, const img_job_t *j,
                          uint32_t *line, img_tap_t *xt, float *acc, int32_t cap,
                          int area_x, int area_y) {
    int32_t xlo = j->sr.x, xhi = j->sr.x + j->sr.w;
    int32_t ylo = j->sr.y, yhi = j->sr.y + j->sr.h;
    for (int32_t cx = j->vx0; cx < j->vx1; cx += cap) {
        int32_t n = nxc__mini(cap, j->vx1 - cx);
        for (int32_t i = 0; i < n; i++) {
            img_axis_tap(cx + i, j->x0, (double)j->sr.x, j->sx, xlo, xhi, area_x, &xt[i]);
        }
        for (int32_t py = j->vy0; py < j->vy1; py++) {
            uint32_t k = img_row_k(j, py);
            if (k == 0u) continue;
            img_tap_t ty;
            img_axis_tap(py, j->y0, (double)j->sr.y, j->sy, ylo, yhi, area_y, &ty);
            nxc__zero(acc, (uint64_t)n * 4u * sizeof(float));
            for (int32_t sy = ty.first; sy <= ty.last; sy++) {
                float wy = (sy == ty.first) ? ty.wf : (sy == ty.last ? ty.wl : ty.wm);
                if (wy == 0.0f) continue;
                const uint8_t *row = img_row(src, sy);
                for (int32_t i = 0; i < n; i++) {
                    const img_tap_t *t = &xt[i];
                    uint32_t p = img_px(row, t->first, src->fmt);
                    float w = t->wf;
                    float s0 = w * (float)(p & 0xFFu), s1 = w * (float)((p >> 8) & 0xFFu);
                    float s2 = w * (float)((p >> 16) & 0xFFu), s3 = w * (float)(p >> 24);
                    if (t->last > t->first) {
                        w = t->wm;
                        for (int32_t sx = t->first + 1; sx < t->last; sx++) {
                            p = img_px(row, sx, src->fmt);
                            s0 += w * (float)(p & 0xFFu);
                            s1 += w * (float)((p >> 8) & 0xFFu);
                            s2 += w * (float)((p >> 16) & 0xFFu);
                            s3 += w * (float)(p >> 24);
                        }
                        p = img_px(row, t->last, src->fmt);
                        w = t->wl;
                        s0 += w * (float)(p & 0xFFu);
                        s1 += w * (float)((p >> 8) & 0xFFu);
                        s2 += w * (float)((p >> 16) & 0xFFu);
                        s3 += w * (float)(p >> 24);
                    }
                    float *a = acc + (int64_t)i * 4;
                    a[0] += wy * s0; a[1] += wy * s1; a[2] += wy * s2; a[3] += wy * s3;
                }
            }
            for (int32_t i = 0; i < n; i++) {
                const float *a = acc + (int64_t)i * 4;
                uint32_t ch[4];
                for (int q = 0; q < 4; q++) {
                    float v = a[q] + 0.5f;
                    ch[q] = (v <= 0.0f) ? 0u : (v >= 255.0f ? 255u : (uint32_t)v);
                }
                /* Rounding must not break the premultiplied invariant. */
                for (int q = 0; q < 3; q++) if (ch[q] > ch[3]) ch[q] = ch[3];
                line[i] = (ch[3] << 24) | (ch[2] << 16) | (ch[1] << 8) | ch[0];
            }
            img_apply_xedges(j, cx, line, n);
            img_write_row(c, py, cx, line, n, k);
        }
    }
}

/* ============ Dispatcher ============ */

/*
 * Draw source rect sr of src into dst (canvas coordinates). Arguments are
 * already validated. The source rect is clipped to the image and dst
 * shrunk proportionally, then the job is clipped to the device clip and
 * handed to the filter path.
 */
static nxc_status_t img_draw(nxc_canvas_t *c, const img_src_t *src, nxc_irect_t sr,
                             nxc_rect_t dst, uint32_t alpha, nxc_filter_t filter,
                             const char *where) {
    if (alpha == 0u || sr.w == 0 || sr.h == 0) return NXC_OK;
    if (!(dst.w >= IMG_MIN_EXTENT) || !(dst.h >= IMG_MIN_EXTENT)) return NXC_OK;

    /* Clip the source rect to the image (64-bit: x + w may overflow). */
    int64_t sx0 = nxc__maxi(sr.x, 0), sy0 = nxc__maxi(sr.y, 0);
    int64_t sx1 = (int64_t)sr.x + sr.w, sy1 = (int64_t)sr.y + sr.h;
    if (sx1 > src->w) sx1 = src->w;
    if (sy1 > src->h) sy1 = src->h;
    if (sx1 <= sx0 || sy1 <= sy0) return NXC_OK;
    double fx = (double)dst.w / (double)sr.w, fy = (double)dst.h / (double)sr.h;
    double dx0 = (double)dst.x + (double)(sx0 - sr.x) * fx;
    double dy0 = (double)dst.y + (double)(sy0 - sr.y) * fy;
    double dw = (double)(sx1 - sx0) * fx, dh = (double)(sy1 - sy0) * fy;
    if (!(dw >= IMG_MIN_EXTENT) || !(dh >= IMG_MIN_EXTENT)) return NXC_OK;

    img_job_t j;
    j.sr = nxc_irect((int32_t)sx0, (int32_t)sy0, (int32_t)(sx1 - sx0), (int32_t)(sy1 - sy0));
    j.x0 = dx0 + (double)c->st.tx;
    j.y0 = dy0 + (double)c->st.ty;
    j.x1 = j.x0 + dw;
    j.y1 = j.y0 + dh;
    if (!(j.x0 >= -IMG_COORD_MAX && j.x1 <= IMG_COORD_MAX &&
          j.y0 >= -IMG_COORD_MAX && j.y1 <= IMG_COORD_MAX)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    j.sx = (double)j.sr.w / dw;
    j.sy = (double)j.sr.h / dh;
    j.alpha = alpha;
    j.ix0 = (int32_t)img_floor(j.x0);
    j.ix1 = (int32_t)img_ceil(j.x1);
    j.iy0 = (int32_t)img_floor(j.y0);
    j.iy1 = (int32_t)img_ceil(j.y1);

    nxc_irect_t cl = nxc__device_clip(c);
    j.vx0 = nxc__maxi(j.ix0, cl.x);
    j.vx1 = nxc__mini(j.ix1, cl.x + cl.w);
    j.vy0 = nxc__maxi(j.iy0, cl.y);
    j.vy1 = nxc__mini(j.iy1, cl.y + cl.h);
    if (cl.w == 0 || j.vx0 >= j.vx1 || j.vy0 >= j.vy1) return NXC_OK;
    j.kx_first = img_edge_cov(j.ix0, j.x0, j.x1);
    j.kx_last = (j.ix1 - 1 > j.ix0) ? img_edge_cov(j.ix1 - 1, j.x0, j.x1) : 255u;

    uint64_t cap0 = 0, cap1 = 0;
    uint32_t *line = (uint32_t *)nxc__scratch(NXC_SCRATCH_LINE0, &cap0);
    void *aux = nxc__scratch(NXC_SCRATCH_LINE1, &cap1);
    if (!line || !aux) return nxc__fail(NXC_ERR_STATE, where);

    /* Unscaled and integer-aligned: every filter reduces to a copy. */
    if (dw == (double)j.sr.w && dh == (double)j.sr.h &&
        j.x0 == (double)j.ix0 && j.y0 == (double)j.iy0) {
        img_blit(c, src, &j, line, (int32_t)nxc__mini((int32_t)(cap0 / 4u), 1 << 20));
        return NXC_OK;
    }

    int area_x = (filter == NXC_FILTER_SMOOTH) && j.sx > 2.0;
    int area_y = (filter == NXC_FILTER_SMOOTH) && j.sy > 2.0;
    if (filter == NXC_FILTER_NEAREST) {
        int32_t cap = (int32_t)((cap0 < cap1 ? cap0 : cap1) / 4u);
        img_nearest(c, src, &j, line, (int32_t *)aux, cap);
    } else if (!area_x && !area_y) {
        int32_t cap = (int32_t)((cap0 < cap1 ? cap0 : cap1) / 4u);
        img_bilinear(c, src, &j, line, (uint32_t *)aux, cap);
    } else {
        /* LINE0 = output row + x taps, LINE1 = 4 float accumulators per column. */
        uint64_t c0 = cap0 / (sizeof(uint32_t) + sizeof(img_tap_t));
        uint64_t c1 = cap1 / (4u * sizeof(float));
        int32_t cap = (int32_t)(c0 < c1 ? c0 : c1);
        if (cap < 1) return nxc__fail(NXC_ERR_OVERFLOW, where);
        img_tap_t *xt = (img_tap_t *)(void *)(line + cap);
        img_separable(c, src, &j, line, xt, (float *)aux, cap, area_x, area_y);
    }
    return NXC_OK;
}

/* ============ Validation ============ */

static int img_surface_ok(const nxc_surface_t *s) {
    return s && s->pixels && s->width > 0 && s->height > 0 && s->stride >= s->width &&
           s->width <= IMG_DIM_MAX && s->height <= IMG_DIM_MAX;
}

static int img_rect_ok(nxc_rect_t r) {
    return nxc__finitef(r.x) && nxc__finitef(r.y) && nxc__finitef(r.w) && nxc__finitef(r.h) &&
           r.w >= 0.0f && r.h >= 0.0f && r.x >= -IMG_COORD_MAX && r.x <= IMG_COORD_MAX &&
           r.y >= -IMG_COORD_MAX && r.y <= IMG_COORD_MAX && r.w <= IMG_COORD_MAX &&
           r.h <= IMG_COORD_MAX;
}

static int img_filter_ok(nxc_filter_t f) {
    return f == NXC_FILTER_NEAREST || f == NXC_FILTER_BILINEAR || f == NXC_FILTER_SMOOTH;
}

/* True when [p, p + bytes) intersects the target's pixel memory. Drawing
 * an image onto the memory it is read from is not supported. */
static int img_overlaps_target(const nxc_canvas_t *c, const void *p, uint64_t bytes) {
    const nxc_surface_t *t = c->target;
    uint64_t tbytes = ((uint64_t)(t->height - 1) * (uint64_t)t->stride + (uint64_t)t->width) * 4u;
    uintptr_t a0 = (uintptr_t)p, a1 = a0 + (uintptr_t)bytes;
    uintptr_t b0 = (uintptr_t)t->pixels, b1 = b0 + (uintptr_t)tbytes;
    return a0 < b1 && b0 < a1;
}

/* Shared argument checks of the surface entry points. */
static nxc_status_t img_check_surface_args(const nxc_canvas_t *c, const nxc_surface_t *src,
                                           nxc_irect_t src_rect, nxc_rect_t dst,
                                           nxc_filter_t filter) {
    if (!c || !img_surface_ok(c->target) || !img_surface_ok(src) || src_rect.w < 0 ||
        src_rect.h < 0 || !img_rect_ok(dst) || !img_filter_ok(filter)) {
        return NXC_ERR_INVALID_ARG;
    }
    if (!nxc__ready()) return NXC_ERR_STATE;
    uint64_t sbytes = ((uint64_t)(src->height - 1) * (uint64_t)src->stride +
                       (uint64_t)src->width) * 4u;
    if (img_overlaps_target(c, src->pixels, sbytes)) return NXC_ERR_UNSUPPORTED;
    return NXC_OK;
}

/* ============ Public API ============ */

nxc_status_t nxc_draw_surface(nxc_canvas_t *c, const nxc_surface_t *src,
                              nxc_irect_t src_rect, nxc_rect_t dst,
                              uint8_t alpha, nxc_filter_t filter) {
    nxc_status_t st = img_check_surface_args(c, src, src_rect, dst, filter);
    if (st != NXC_OK) return nxc__fail(st, "nxc_draw_surface");
    img_src_t s;
    s.base = (const uint8_t *)(const void *)src->pixels;
    s.stride = (int64_t)src->stride * 4;
    s.w = src->width;
    s.h = src->height;
    s.fmt = IMG_FMT_PREMUL32;
    return img_draw(c, &s, src_rect, dst, alpha, filter, "nxc_draw_surface");
}

nxc_status_t nxc_draw_surface_rrect(nxc_canvas_t *c, const nxc_surface_t *src,
                                    nxc_irect_t src_rect, nxc_rect_t dst,
                                    float radius, uint8_t alpha, nxc_filter_t filter) {
    nxc_status_t st = img_check_surface_args(c, src, src_rect, dst, filter);
    if (st == NXC_OK && (!nxc__finitef(radius) || radius < 0.0f)) st = NXC_ERR_INVALID_ARG;
    if (st != NXC_OK) return nxc__fail(st, "nxc_draw_surface_rrect");
    /* nxc_save / nxc_clip_rrect record their own failures. */
    st = nxc_save(c);
    if (st != NXC_OK) return st;
    st = nxc_clip_rrect(c, dst, radius);
    if (st == NXC_OK) st = nxc_draw_surface(c, src, src_rect, dst, alpha, filter);
    nxc_status_t rs = nxc_restore(c);
    return (st != NXC_OK) ? st : rs;
}

nxc_status_t nxc_draw_rgba8(nxc_canvas_t *c, const uint8_t *rgba, int32_t w, int32_t h,
                            int32_t stride_bytes, nxc_rect_t dst, uint8_t alpha,
                            nxc_filter_t filter) {
    static const char where[] = "nxc_draw_rgba8";
    if (!c || !img_surface_ok(c->target) || !rgba || w <= 0 || h <= 0 || w > IMG_DIM_MAX ||
        h > IMG_DIM_MAX || (int64_t)stride_bytes < (int64_t)w * 4 || !img_rect_ok(dst) ||
        !img_filter_ok(filter)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    uint64_t bytes = (uint64_t)(h - 1) * (uint64_t)stride_bytes + (uint64_t)w * 4u;
    if (img_overlaps_target(c, rgba, bytes)) return nxc__fail(NXC_ERR_UNSUPPORTED, where);
    img_src_t s;
    s.base = rgba;
    s.stride = stride_bytes;
    s.w = w;
    s.h = h;
    s.fmt = IMG_FMT_RGBA8;
    return img_draw(c, &s, nxc_irect(0, 0, w, h), dst, alpha, filter, where);
}

nxc_status_t nxc_draw_xrgb32(nxc_canvas_t *c, const uint32_t *px, int32_t w, int32_t h,
                             int32_t stride_bytes, nxc_rect_t dst, uint8_t alpha,
                             nxc_filter_t filter) {
    static const char where[] = "nxc_draw_xrgb32";
    if (!c || !img_surface_ok(c->target) || !px || ((uintptr_t)px & 3u) != 0 || w <= 0 ||
        h <= 0 || w > IMG_DIM_MAX || h > IMG_DIM_MAX || (stride_bytes & 3) != 0 ||
        (int64_t)stride_bytes < (int64_t)w * 4 || !img_rect_ok(dst) || !img_filter_ok(filter)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    uint64_t bytes = (uint64_t)(h - 1) * (uint64_t)stride_bytes + (uint64_t)w * 4u;
    if (img_overlaps_target(c, px, bytes)) return nxc__fail(NXC_ERR_UNSUPPORTED, where);
    img_src_t s;
    s.base = (const uint8_t *)(const void *)px;
    s.stride = stride_bytes;
    s.w = w;
    s.h = h;
    s.fmt = IMG_FMT_XRGB32;
    return img_draw(c, &s, nxc_irect(0, 0, w, h), dst, alpha, filter, where);
}
