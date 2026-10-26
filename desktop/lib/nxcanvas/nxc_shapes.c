/*
 * NeolyxOS - NXCanvas fast shapes
 *
 * Rects, rounded rects, circles, ring strokes and round-capped lines with
 * analytic anti-aliasing (no path rasterizer). Each row is split into
 * runs: pixels with one constant coverage (the fully covered interior,
 * or the flat top/bottom of a rect whose edge cuts the row) are blended
 * as a single span, and only pixels an outline passes through get a
 * per-pixel coverage value.
 *
 * Coverage model
 *   Rects and rounded rects use the EXACT area of the pixel square inside
 *   the shape: the separable rect overlap minus, in each corner square
 *   the pixel reaches, the part of that square outside the corner circle.
 *   The circle part integrates the arc height sqrt(r^2 - t^2) across the
 *   pixel in closed form. A circle is a rounded rect whose radius is half
 *   its size, so both share one code path.
 *   Strokes are outer shape minus inner shape, exact for hairlines too.
 *   Lines use the exact area of a slab of the line's width crossing the
 *   pixel at any angle; on the round caps the slab is turned toward the
 *   nearest endpoint (tangent approximation of the cap circle).
 *
 * Geometry is evaluated in double precision: device coordinates of any
 * finite float input stay exact, the closed-form circle terms keep their
 * precision for large radii, and scalar double math costs the same as
 * float on x86-64. Coverage is quantized to 0..255 only at the end.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"

/* Largest corner/circle radius and half line width whose coverage double
 * precision still resolves far below 1/255. Larger values never occur in
 * UI drawing and return NXC_ERR_UNSUPPORTED. */
#define SHAPE_MAX_RADIUS 1.0e6

/* Lines shorter than this (px) are drawn as a dot of the line's width. */
#define LINE_MIN_LENGTH 1.0e-4

/* Half diagonal of a pixel (rounded up): no pixel farther than this from
 * an outline can be touched by it. */
#define PIXEL_HALF_DIAG 0.70711

/* ============ Double-precision helpers ============ */

static inline double sqrt_d(double v) {
    if (!(v > 0.0)) return 0.0;
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(v));
    return r;
}
static inline double min_d(double a, double b) { return a < b ? a : b; }
static inline double max_d(double a, double b) { return a > b ? a : b; }
static inline double abs_d(double v) { return v < 0.0 ? -v : v; }

/* floor(v) clamped to [lo, hi]. Safe for any double, including NaN and
 * values far outside the int32 range. */
static int32_t floor_clamp(double v, int32_t lo, int32_t hi) {
    if (!(v > (double)lo)) return lo;
    if (v >= (double)hi) return hi;
    int32_t t = (int32_t)v;
    return ((double)t > v) ? t - 1 : t;
}

/* ceil(v) clamped to [lo, hi]. */
static int32_t ceil_clamp(double v, int32_t lo, int32_t hi) {
    if (!(v > (double)lo)) return lo;
    if (v >= (double)hi) return hi;
    int32_t t = (int32_t)v;
    return ((double)t < v) ? t + 1 : t;
}

/* Coverage fraction -> 0..255 (rounded, clamped; NaN -> 0). */
static inline uint32_t cov_byte(double v) {
    if (!(v > 0.0)) return 0u;
    if (v >= 1.0) return 255u;
    return (uint32_t)(v * 255.0 + 0.5);
}

/*
 * asin(s) for s in [0, 1]. Up to three half-angle steps
 * asin(s) = 2 asin(s / sqrt(2 (1 + sqrt(1 - s^2)))) bring s below 0.35,
 * where the Maclaurin series to s^13 is accurate to ~6e-9 relative.
 */
static double asin_d(double s) {
    if (!(s > 0.0)) return 0.0;
    if (s > 1.0) s = 1.0;
    double scale = 1.0;
    for (int i = 0; i < 3 && s > 0.35; i++) {
        s = s / sqrt_d(2.0 * (1.0 + sqrt_d((1.0 - s) * (1.0 + s))));
        scale *= 2.0;
    }
    double s2 = s * s;
    double p = 231.0 / 13312.0;
    p = 63.0 / 2816.0 + s2 * p;
    p = 35.0 / 1152.0 + s2 * p;
    p = 5.0 / 112.0 + s2 * p;
    p = 3.0 / 40.0 + s2 * p;
    p = 1.0 / 6.0 + s2 * p;
    return scale * s * (1.0 + s2 * p);
}

/* ============ Span emission ============ */

/* Destination of a shape's rows: a solid color or a prepared paint,
 * always through the nxc__span_* functions (clip, rounded clip and
 * global alpha are applied there). */
typedef struct {
    nxc_canvas_t           *c;
    nxc_irect_t             clip;      /* device clip */
    int                     live;      /* clip non-empty and alpha > 0 */
    uint32_t                premul;    /* solid color when paint is NULL */
    const nxc__paint_ctx_t *paint;     /* shader, or NULL */
    uint8_t                *cov;       /* NXC_SCRATCH_COV row buffer */
    int32_t                 cov_cap;   /* its capacity in bytes (> 0) */
} emit_t;

/* Local trampoline for the paint shader: a static function's address is
 * PC-relative, so the object needs no GOT entry under PIE toolchains. */
static void paint_shade(const void *ctx, int32_t x, int32_t y, int32_t n, uint32_t *out) {
    nxc__paint_shade(ctx, x, y, n, out);
}

/* Blend [x0, x1) of row y with one coverage value k (0..255). */
static void emit_const(const emit_t *e, int32_t y, int32_t x0, int32_t x1, uint32_t k) {
    if (k == 0u || x1 <= x0) return;
    if (!e->paint) {
        nxc__span_solid(e->c, y, x0, x1, e->premul, k);
        return;
    }
    if (k >= 255u) {
        nxc__span_shade(e->c, y, x0, 0, x1 - x0, paint_shade, e->paint);
        return;
    }
    /* The shader path takes per-pixel coverage only: fill the row buffer
     * once with k and reuse it for every chunk. */
    int32_t filled = 0;
    while (x0 < x1) {
        int32_t n = nxc__mini(x1 - x0, e->cov_cap);
        for (; filled < n; filled++) e->cov[filled] = (uint8_t)k;
        nxc__span_shade(e->c, y, x0, e->cov, n, paint_shade, e->paint);
        x0 += n;
    }
}

/* Blend e->cov[0..n) at (x0, y). */
static void emit_cov(const emit_t *e, int32_t y, int32_t x0, int32_t n) {
    if (!e->paint) nxc__span_cov(e->c, y, x0, e->cov, n, e->premul);
    else nxc__span_shade(e->c, y, x0, e->cov, n, paint_shade, e->paint);
}

/*
 * One row of the axis-aligned rect [x0, x1) whose vertical coverage in
 * this row is covy: the two edge columns get their exact partial area,
 * everything between is one span. Integer edges produce no fringe.
 */
static void emit_rect_row(const emit_t *e, int32_t y, double x0, double x1, double covy) {
    uint32_t kmid = cov_byte(covy);
    if (kmid == 0u) return;
    int32_t lo = e->clip.x, hi = e->clip.x + e->clip.w;
    /* Clamping to the integer clip is exact: a cut-off edge only ever
     * covered pixels outside the clip. */
    x0 = max_d(x0, (double)lo);
    x1 = min_d(x1, (double)hi);
    if (!(x1 > x0)) return;
    int32_t ix0 = floor_clamp(x0, lo, hi);
    int32_t ix1 = ceil_clamp(x1, lo, hi);
    if (ix1 - ix0 <= 1) {
        emit_const(e, y, ix0, ix0 + 1, cov_byte((x1 - x0) * covy));
        return;
    }
    uint32_t kl = cov_byte(((double)ix0 + 1.0 - x0) * covy);
    uint32_t kr = cov_byte((x1 - (double)(ix1 - 1)) * covy);
    int32_t m0 = ix0 + 1, m1 = ix1 - 1;
    if (kl == kmid) m0 = ix0; else emit_const(e, y, ix0, ix0 + 1, kl);
    if (kr == kmid) m1 = ix1; else emit_const(e, y, ix1 - 1, ix1, kr);
    emit_const(e, y, m0, m1, kmid);
}

/* ============ Rounded rects (exact area) ============ */

typedef struct {
    double x0, y0, x1, y1;   /* device bounds */
    double r;                /* corner radius, 0 <= r <= half the shorter side */
    double cl, cr, ct, cb;   /* corner circle centers: x0+r, x1-r, y0+r, y1-r */
} rrect_t;

/* A row's band inside the top (or bottom) corner squares, in the mirrored
 * frame v = distance from the corner centers' row, plus the arc's
 * half-width where it crosses the band's two edges. */
typedef struct {
    int    on;          /* the row reaches these corner squares */
    double v0, v1;      /* band in v, 0 <= v0 < v1 <= r */
    double t_hi;        /* arc half-width at v1 (narrowest) */
    double t_lo;        /* arc half-width at v0 (widest) */
} corner_band_t;

/* Per-row values shared by all pixels of the row. */
typedef struct {
    double        covy;     /* row overlap with [y0, y1] */
    corner_band_t top, bot;
} rrect_row_t;

/* Coverage profile of one row: zero outside [l, r), the constant v on
 * [a, b), per-pixel elsewhere. Columns are clamped just outside the clip. */
typedef struct {
    int32_t l, a, b, r;
    double  v;
    int     empty;
} row_prof_t;

/* Build a rounded rect from device bounds and a radius >= 0 (clamped). */
static void rrect_make(rrect_t *s, double x0, double y0, double x1, double y1, double radius) {
    s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
    double maxr = 0.5 * min_d(x1 - x0, y1 - y0);
    s->r = max_d(min_d(radius, maxr), 0.0);
    s->cl = x0 + s->r;
    s->cr = x1 - s->r;
    s->ct = y0 + s->r;
    s->cb = y1 - s->r;
}

/* Disk as a rounded rect (corner centers all at the circle center). */
static void circle_make(rrect_t *s, double cx, double cy, double r) {
    s->x0 = cx - r; s->x1 = cx + r;
    s->y0 = cy - r; s->y1 = cy + r;
    s->r = r;
    s->cl = s->cr = cx;
    s->ct = s->cb = cy;
}

/* Integral of sqrt(r^2 - t^2) over [a, b], 0 <= a <= b <= r, given
 * wa = sqrt(r^2 - a^2) and wb = sqrt(r^2 - b^2). The angle difference
 * asin(b/r) - asin(a/r) is formed as asin((b*wa - a*wb) / r^2), so the
 * result does not cancel catastrophically for large radii. */
static double arc_area(double r, double a, double b, double wa, double wb) {
    double r2 = r * r;
    double g = b * wa - a * wb;
    double ang = (g > 0.0) ? r2 * asin_d(g / r2) : 0.0;
    return 0.5 * (b * wb - a * wa + ang);
}

/*
 * Area of the quarter disk {u, v >= 0, u^2 + v^2 <= r^2} inside the box
 * [u0, u1] x [cb->v0, cb->v1] with 0 <= u0 <= u1 <= r. Left of t_hi the
 * box column is full, between t_hi and t_lo the arc bounds it from above,
 * right of t_lo it is empty.
 */
static double quarter_area(double r, double u0, double u1, const corner_band_t *cb) {
    double v0 = cb->v0, v1 = cb->v1;
    double area = 0.0;
    double b = min_d(u1, cb->t_hi);
    if (b > u0) area += (b - u0) * (v1 - v0);
    double a = max_d(u0, cb->t_hi);
    b = min_d(u1, cb->t_lo);
    if (b > a) {
        /* The arc height at t_hi is v1 and at t_lo is v0 by construction. */
        double wa = (a == cb->t_hi) ? v1 : sqrt_d((r - a) * (r + a));
        double wb = (b == cb->t_lo) ? v0 : sqrt_d((r - b) * (r + b));
        area += arc_area(r, a, b, wa, wb) - v0 * (b - a);
    }
    return area;
}

/* Fill a corner band from the row's distances [d0, d1] to the corner
 * centers' row (0 <= d0 < d1 <= r). */
static void corner_band(corner_band_t *cb, double r, double d0, double d1) {
    cb->on = 1;
    cb->v0 = d0;
    cb->v1 = d1;
    cb->t_hi = (d1 >= r) ? 0.0 : sqrt_d((r - d1) * (r + d1));
    cb->t_lo = (d0 >= r) ? 0.0 : sqrt_d((r - d0) * (r + d0));
}

/*
 * Prepare row y of s: per-row values for rrect_pixel and the coverage
 * profile. The constant run is where the corner arcs cover the whole
 * pixel column for the entire row, so its coverage is exactly covy.
 */
static void rrect_row_setup(const rrect_t *s, int32_t y, int32_t lo, int32_t hi,
                            rrect_row_t *row, row_prof_t *p) {
    p->l = p->a = p->b = p->r = lo;
    p->v = 0.0;
    p->empty = 1;
    row->covy = 0.0;
    row->top.on = row->bot.on = 0;
    double by0 = max_d((double)y, s->y0);
    double by1 = min_d((double)y + 1.0, s->y1);
    if (!(by1 > by0)) return;
    double r = s->r;
    row->covy = by1 - by0;
    /* Arc half-width: narrowest at the band edge farthest from the
     * straight section, widest at the nearest one; r when straight. */
    double wmin = r, wmax = r;
    if (r > 0.0) {
        if (by0 < s->ct) {
            corner_band(&row->top, r, max_d(s->ct - by1, 0.0), s->ct - by0);
            wmin = min_d(wmin, row->top.t_hi);
            wmax = min_d(wmax, row->top.t_lo);
        }
        if (by1 > s->cb) {
            corner_band(&row->bot, r, max_d(by0 - s->cb, 0.0), by1 - s->cb);
            wmin = min_d(wmin, row->bot.t_hi);
            wmax = min_d(wmax, row->bot.t_lo);
        }
    }
    p->l = floor_clamp(s->cl - wmax, lo, hi);
    p->r = ceil_clamp(s->cr + wmax, lo, hi);
    p->a = ceil_clamp(s->cl - wmin, lo, hi);
    p->b = floor_clamp(s->cr + wmin, lo, hi);
    if (p->a >= p->b) p->a = p->b = p->l;     /* no constant run */
    p->v = row->covy;
    p->empty = (p->r <= p->l);
}

/* Part of the pixel's box inside the corner squares (u range given, v
 * from the row) that lies outside the corner circles. */
static double corner_notch(double r, double u0, double u1, const rrect_row_t *row) {
    double n = 0.0;
    if (row->top.on) {
        n += (u1 - u0) * (row->top.v1 - row->top.v0) - quarter_area(r, u0, u1, &row->top);
    }
    if (row->bot.on) {
        n += (u1 - u0) * (row->bot.v1 - row->bot.v0) - quarter_area(r, u0, u1, &row->bot);
    }
    return n;
}

/* Exact coverage of pixel column px in a prepared row of s. Left and
 * right corners use mirrored coordinates, so mirrored pixels of a
 * symmetric shape get identical values. */
static double rrect_pixel(const rrect_t *s, const rrect_row_t *row, int32_t px) {
    double xa = (double)px, xb = xa + 1.0;
    double covx = min_d(xb, s->x1) - max_d(xa, s->x0);
    if (!(covx > 0.0)) return 0.0;
    double cov = covx * row->covy;
    if (!row->top.on && !row->bot.on) return cov;
    double r = s->r;
    double u0 = max_d(s->cl - xb, 0.0), u1 = min_d(s->cl - xa, r);    /* left corners */
    if (u1 > u0) cov -= corner_notch(r, u0, u1, row);
    u0 = max_d(xa - s->cr, 0.0);                                       /* right corners */
    u1 = min_d(xb - s->cr, r);
    if (u1 > u0) cov -= corner_notch(r, u0, u1, row);
    return cov;
}

/*
 * One row of outer minus inner (inner may be NULL; it must lie inside
 * outer). The row is cut at every profile boundary; a piece where both
 * shapes are constant is one span, other pieces get per-pixel coverage.
 */
static void emit_rrect_row(const emit_t *e, int32_t y, const rrect_t *outer,
                           const rrect_t *inner) {
    int32_t lo = e->clip.x, hi = e->clip.x + e->clip.w;
    rrect_row_t orow, irow;
    row_prof_t op, ip;
    rrect_row_setup(outer, y, lo - 1, hi + 1, &orow, &op);
    if (op.empty) return;
    ip.l = ip.a = ip.b = ip.r = lo;
    ip.v = 0.0;
    ip.empty = 1;
    if (inner) rrect_row_setup(inner, y, lo - 1, hi + 1, &irow, &ip);

    int32_t x0 = nxc__maxi(op.l, lo), x1 = nxc__mini(op.r, hi);
    if (x1 <= x0) return;
    int32_t cut[8];
    int n = 0;
    cut[n++] = x0;
    const int32_t cand[6] = { op.a, op.b, ip.l, ip.a, ip.b, ip.r };
    int ncand = ip.empty ? 2 : 6;
    for (int i = 0; i < ncand; i++) {
        if (cand[i] > x0 && cand[i] < x1) cut[n++] = cand[i];
    }
    cut[n++] = x1;
    for (int i = 1; i < n; i++) {                  /* insertion sort, n <= 8 */
        int32_t v = cut[i];
        int j = i;
        while (j > 0 && cut[j - 1] > v) { cut[j] = cut[j - 1]; j--; }
        cut[j] = v;
    }

    for (int i = 0; i + 1 < n; i++) {
        int32_t s = cut[i], t = cut[i + 1];
        if (t <= s) continue;
        int ovar = !(s >= op.a && s < op.b);
        int ivar = 0;
        double ival = 0.0;
        if (!ip.empty && s >= ip.l && s < ip.r) {
            if (s >= ip.a && s < ip.b) ival = ip.v;
            else ivar = 1;
        }
        if (!ovar && !ivar) {
            emit_const(e, y, s, t, cov_byte(op.v - ival));
            continue;
        }
        while (s < t) {
            int32_t m = nxc__mini(t - s, e->cov_cap);
            for (int32_t k = 0; k < m; k++) {
                double co = ovar ? rrect_pixel(outer, &orow, s + k) : op.v;
                double ci = ivar ? rrect_pixel(inner, &irow, s + k) : ival;
                e->cov[k] = (uint8_t)cov_byte(co - ci);
            }
            emit_cov(e, y, s, m);
            s += m;
        }
    }
}

/*
 * Fill outer, or the ring outer minus inner. Rows clear of the corners
 * (all rows when r == 0) take the rect fast path; only corner rows and
 * ring rows build a profile.
 */
static void draw_rrect(const emit_t *e, const rrect_t *outer, const rrect_t *inner) {
    double cx0 = (double)e->clip.x, cx1 = cx0 + (double)e->clip.w;
    if (!(outer->x1 > cx0 && outer->x0 < cx1) || !(outer->y1 > outer->y0)) return;
    int32_t ya = floor_clamp(outer->y0, e->clip.y, e->clip.y + e->clip.h);
    int32_t yb = ceil_clamp(outer->y1, e->clip.y, e->clip.y + e->clip.h);
    for (int32_t y = ya; y < yb; y++) {
        double fy = (double)y;
        if (!inner && (outer->r == 0.0 || (fy >= outer->ct && fy + 1.0 <= outer->cb))) {
            double covy = min_d(fy + 1.0, outer->y1) - max_d(fy, outer->y0);
            emit_rect_row(e, y, outer->x0, outer->x1, covy);
        } else {
            emit_rrect_row(e, y, outer, inner);
        }
    }
}

/* ============ Lines (round caps) ============ */

/* Unit direction n given as a = max(|nx|, |ny|), b = min: the projection
 * of a unit pixel square onto n has a trapezoid density on [-half, half]
 * that is flat on [-flat, flat]. */
typedef struct {
    double half, flat, inv_a, inv_2ab;
} slab_dir_t;

static void slab_dir_init(slab_dir_t *d, double nx, double ny) {
    double a = abs_d(nx), b = abs_d(ny);
    if (b > a) { double t = a; a = b; b = t; }
    if (!(a > 0.5)) { a = 1.0; b = 0.0; }          /* not a unit vector: use the x axis */
    d->half = 0.5 * (a + b);
    d->flat = 0.5 * (a - b);
    d->inv_a = 1.0 / a;
    d->inv_2ab = (b > 1e-9) ? 0.5 / (a * b) : 0.0;
}

/* Area of the part of a unit pixel (centered at the origin) where
 * n.q <= s: the cumulative trapezoid, linear in the flat part and
 * quadratic where a pixel corner crosses the line. */
static double slab_cdf(const slab_dir_t *d, double s) {
    if (s >= d->half) return 1.0;
    if (s <= -d->half) return 0.0;
    double t = abs_d(s);
    if (t <= d->flat || d->inv_2ab == 0.0) return 0.5 + s * d->inv_a;
    double k = d->half - t;
    double q = k * k * d->inv_2ab;
    return (s > 0.0) ? 1.0 - q : q;
}

typedef struct {
    double px, py;          /* start point after clipping */
    double ux, uy;          /* unit direction */
    double len;             /* length after clipping */
    double hw;              /* half width */
    int    cap0, cap1;      /* round cap at start / end (0 where clipping cut it) */
    slab_dir_t body;        /* slab across the line */
    double full;            /* |v| <= full: pixel fully covered */
    double body_reach;      /* |v| >= body_reach: pixel untouched */
    double reach;           /* bound for any part of the line */
} line_t;

/* Coverage of the pixel whose center is at (u, v) in the line frame
 * (u along the line from the start, v across it). */
static double line_cov(const line_t *ln, double u, double v) {
    double du;
    if (u < 0.0 && ln->cap0) {
        du = u;
    } else if (u > ln->len && ln->cap1) {
        du = u - ln->len;
    } else {
        double dist = abs_d(v);
        if (dist <= ln->full) return 1.0;
        if (dist >= ln->body_reach) return 0.0;
        return slab_cdf(&ln->body, ln->hw - dist) - slab_cdf(&ln->body, -ln->hw - dist);
    }
    /* Cap: a slab facing the endpoint, at the distance to it. */
    double d2 = du * du + v * v;
    if (d2 >= ln->reach * ln->reach) return 0.0;
    double dist = sqrt_d(d2);
    slab_dir_t rd;
    if (dist > 1e-9) {
        double inv = 1.0 / dist;
        slab_dir_init(&rd, (du * ln->ux - v * ln->uy) * inv, (du * ln->uy + v * ln->ux) * inv);
    } else {
        rd = ln->body;
    }
    return slab_cdf(&rd, ln->hw - dist) - slab_cdf(&rd, -ln->hw - dist);
}

/*
 * Clip the parameter range [*s0, *s1] of the ray p + s*u so the axis
 * value stays within [lo, hi] (Liang-Barsky step for one axis). Returns 0
 * when nothing of the range remains. A ray parallel to the axis is kept
 * only if it already lies inside.
 */
static int clip_axis(double p, double u, double lo, double hi, double *s0, double *s1) {
    if (abs_d(u) < 1e-12) return (p >= lo && p <= hi);
    double ta = (lo - p) / u, tb = (hi - p) / u;
    if (ta > tb) { double t = ta; ta = tb; tb = t; }
    if (ta > *s0) *s0 = ta;
    if (tb < *s1) *s1 = tb;
    return *s0 <= *s1;
}

/*
 * Rasterize a prepared line. Each row visits only the columns within
 * reach of the part of the segment near that row, so the cost follows
 * the line's area rather than its bounding box.
 */
static void draw_line_rows(const emit_t *e, const line_t *ln) {
    double qy = ln->py + ln->len * ln->uy;
    int32_t ylo = e->clip.y, yhi = e->clip.y + e->clip.h;
    int32_t xlo = e->clip.x, xhi = e->clip.x + e->clip.w;
    int32_t ya = floor_clamp(min_d(ln->py, qy) - ln->reach, ylo, yhi);
    int32_t yb = ceil_clamp(max_d(ln->py, qy) + ln->reach, ylo, yhi);
    for (int32_t y = ya; y < yb; y++) {
        double yc = (double)y + 0.5;
        double s0 = 0.0, s1 = ln->len;
        if (ln->uy != 0.0) {
            double p = (yc - ln->reach - ln->py) / ln->uy;
            double q = (yc + ln->reach - ln->py) / ln->uy;
            if (p > q) { double t = p; p = q; q = t; }
            s0 = max_d(s0, p);
            s1 = min_d(s1, q);
            if (s0 > s1) continue;
        } else if (abs_d(ln->py - yc) > ln->reach) {
            continue;
        }
        double xa = ln->px + s0 * ln->ux, xb = ln->px + s1 * ln->ux;
        if (xa > xb) { double t = xa; xa = xb; xb = t; }
        int32_t x0 = floor_clamp(xa - ln->reach, xlo, xhi);
        int32_t x1 = ceil_clamp(xb + ln->reach, xlo, xhi);
        double ox = (double)x0 + 0.5 - ln->px, oy = yc - ln->py;
        double u = ox * ln->ux + oy * ln->uy;
        double v = oy * ln->ux - ox * ln->uy;
        while (x0 < x1) {
            int32_t m = nxc__mini(x1 - x0, e->cov_cap);
            for (int32_t k = 0; k < m; k++) {
                e->cov[k] = (uint8_t)cov_byte(line_cov(ln, u, v));
                u += ln->ux;
                v -= ln->uy;
            }
            emit_cov(e, y, x0, m);
            x0 += m;
        }
    }
}

/* ============ Validation and setup ============ */

static int finite2(float a, float b) { return nxc__finitef(a) && nxc__finitef(b); }

static int rect_valid(nxc_rect_t r) {
    return finite2(r.x, r.y) && finite2(r.w, r.h) && r.w >= 0.0f && r.h >= 0.0f;
}

/*
 * Validate the canvas and prepare an emitter. Returns a status already
 * reported through nxc__fail on error; e->live is 0 when nothing can
 * become visible (empty clip or zero global alpha).
 */
static nxc_status_t begin(nxc_canvas_t *c, const char *where, emit_t *e) {
    if (!c || !c->target || !c->target->pixels || c->target->width <= 0 ||
        c->target->height <= 0 || c->target->stride < c->target->width ||
        !finite2(c->st.tx, c->st.ty)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    uint64_t cap = 0;
    uint8_t *cov = (uint8_t *)nxc__scratch(NXC_SCRATCH_COV, &cap);
    if (!cov || cap == 0) return nxc__fail(NXC_ERR_STATE, where);
    e->c = c;
    e->clip = nxc__device_clip(c);
    e->live = (e->clip.w > 0 && e->clip.h > 0 && c->st.alpha > 0);
    e->premul = 0u;
    e->paint = 0;
    e->cov = cov;
    e->cov_cap = (cap > 0x40000000u) ? 0x40000000 : (int32_t)cap;
    return NXC_OK;
}

/* Bind a paint to the emitter. Solid paints use the plain color path.
 * Paint errors are reported by nxc__paint_prepare. */
static nxc_status_t bind_paint(emit_t *e, const nxc_paint_t *p, nxc__paint_ctx_t *pc) {
    nxc_status_t st = nxc__paint_prepare(p, e->c->st.tx, e->c->st.ty, pc);
    if (st != NXC_OK) return st;
    if (pc->type == NXC_PAINT_SOLID) {
        e->premul = pc->solid;
        e->paint = 0;
    } else {
        e->paint = pc;
    }
    return NXC_OK;
}

/* True when the bound color/paint can change pixels. */
static int has_ink(const emit_t *e) {
    return e->live && (e->paint != 0 || e->premul != 0u);
}

/* Device-space rounded rect for user rect r (validated) and radius. */
static void rrect_from_user(const emit_t *e, nxc_rect_t r, double radius, rrect_t *s) {
    double x0 = (double)r.x + (double)e->c->st.tx;
    double y0 = (double)r.y + (double)e->c->st.ty;
    rrect_make(s, x0, y0, x0 + (double)r.w, y0 + (double)r.h, radius);
}

/* ============ Public API ============ */

nxc_status_t nxc_fill_rect(nxc_canvas_t *c, nxc_rect_t r, nxc_color_t color) {
    if (!rect_valid(r)) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_fill_rect");
    emit_t e;
    nxc_status_t st = begin(c, "nxc_fill_rect", &e);
    if (st != NXC_OK) return st;
    e.premul = nxc__premul(color);
    if (!has_ink(&e)) return NXC_OK;
    rrect_t s;
    rrect_from_user(&e, r, 0.0, &s);
    draw_rrect(&e, &s, 0);
    return NXC_OK;
}

nxc_status_t nxc_fill_rect_paint(nxc_canvas_t *c, nxc_rect_t r, const nxc_paint_t *p) {
    if (!rect_valid(r) || !p) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_fill_rect_paint");
    emit_t e;
    nxc_status_t st = begin(c, "nxc_fill_rect_paint", &e);
    if (st != NXC_OK) return st;
    nxc__paint_ctx_t pc;
    st = bind_paint(&e, p, &pc);
    if (st != NXC_OK) return st;
    if (!has_ink(&e)) return NXC_OK;
    rrect_t s;
    rrect_from_user(&e, r, 0.0, &s);
    draw_rrect(&e, &s, 0);
    return NXC_OK;
}

/* Shared body of the rounded-rect fills: validate, clamp the radius and
 * draw with the emitter's color or paint. */
static nxc_status_t fill_rrect_common(emit_t *e, nxc_rect_t r, float radius, const char *where) {
    rrect_t s;
    rrect_from_user(e, r, (double)radius, &s);
    if (s.r > SHAPE_MAX_RADIUS) return nxc__fail(NXC_ERR_UNSUPPORTED, where);
    if (!has_ink(e)) return NXC_OK;
    draw_rrect(e, &s, 0);
    return NXC_OK;
}

nxc_status_t nxc_fill_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius, nxc_color_t color) {
    if (!rect_valid(r) || !nxc__finitef(radius) || radius < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_fill_rrect");
    }
    emit_t e;
    nxc_status_t st = begin(c, "nxc_fill_rrect", &e);
    if (st != NXC_OK) return st;
    e.premul = nxc__premul(color);
    return fill_rrect_common(&e, r, radius, "nxc_fill_rrect");
}

nxc_status_t nxc_fill_rrect_paint(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                  const nxc_paint_t *p) {
    if (!rect_valid(r) || !nxc__finitef(radius) || radius < 0.0f || !p) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_fill_rrect_paint");
    }
    emit_t e;
    nxc_status_t st = begin(c, "nxc_fill_rrect_paint", &e);
    if (st != NXC_OK) return st;
    nxc__paint_ctx_t pc;
    st = bind_paint(&e, p, &pc);
    if (st != NXC_OK) return st;
    return fill_rrect_common(&e, r, radius, "nxc_fill_rrect_paint");
}

/*
 * Border of the given width inside r: the outer rounded rect minus the
 * rect inset by width with radius max(radius - width, 0), the exact
 * erosion of the outer shape. A width of half the shorter side or more
 * leaves no hole, so the whole shape is filled.
 */
nxc_status_t nxc_stroke_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius,
                              float width, nxc_color_t color) {
    if (!rect_valid(r) || !nxc__finitef(radius) || radius < 0.0f ||
        !nxc__finitef(width) || width < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_stroke_rrect");
    }
    emit_t e;
    nxc_status_t st = begin(c, "nxc_stroke_rrect", &e);
    if (st != NXC_OK) return st;
    e.premul = nxc__premul(color);
    rrect_t outer;
    rrect_from_user(&e, r, (double)radius, &outer);
    if (outer.r > SHAPE_MAX_RADIUS) return nxc__fail(NXC_ERR_UNSUPPORTED, "nxc_stroke_rrect");
    if (!has_ink(&e) || width == 0.0f) return NXC_OK;
    double w = (double)width;
    if (w >= 0.5 * min_d(outer.x1 - outer.x0, outer.y1 - outer.y0)) {
        draw_rrect(&e, &outer, 0);
        return NXC_OK;
    }
    rrect_t inner;
    rrect_make(&inner, outer.x0 + w, outer.y0 + w, outer.x1 - w, outer.y1 - w,
               max_d(outer.r - w, 0.0));
    draw_rrect(&e, &outer, &inner);
    return NXC_OK;
}

/* Shared body of the circle fills. */
static nxc_status_t fill_circle_common(emit_t *e, float cx, float cy, float r) {
    if (!has_ink(e) || r == 0.0f) return NXC_OK;
    rrect_t s;
    circle_make(&s, (double)cx + (double)e->c->st.tx, (double)cy + (double)e->c->st.ty, (double)r);
    draw_rrect(e, &s, 0);
    return NXC_OK;
}

nxc_status_t nxc_fill_circle(nxc_canvas_t *c, float cx, float cy, float r,
                             nxc_color_t color) {
    if (!finite2(cx, cy) || !nxc__finitef(r) || r < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_fill_circle");
    }
    if ((double)r > SHAPE_MAX_RADIUS) return nxc__fail(NXC_ERR_UNSUPPORTED, "nxc_fill_circle");
    emit_t e;
    nxc_status_t st = begin(c, "nxc_fill_circle", &e);
    if (st != NXC_OK) return st;
    e.premul = nxc__premul(color);
    return fill_circle_common(&e, cx, cy, r);
}

nxc_status_t nxc_fill_circle_paint(nxc_canvas_t *c, float cx, float cy, float r,
                                   const nxc_paint_t *p) {
    if (!finite2(cx, cy) || !nxc__finitef(r) || r < 0.0f || !p) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_fill_circle_paint");
    }
    if ((double)r > SHAPE_MAX_RADIUS) {
        return nxc__fail(NXC_ERR_UNSUPPORTED, "nxc_fill_circle_paint");
    }
    emit_t e;
    nxc_status_t st = begin(c, "nxc_fill_circle_paint", &e);
    if (st != NXC_OK) return st;
    nxc__paint_ctx_t pc;
    st = bind_paint(&e, p, &pc);
    if (st != NXC_OK) return st;
    return fill_circle_common(&e, cx, cy, r);
}

/* Ring centered on radius r: disk(r + width/2) minus disk(r - width/2);
 * a filled disk once the inner radius reaches zero. */
nxc_status_t nxc_stroke_circle(nxc_canvas_t *c, float cx, float cy, float r,
                               float width, nxc_color_t color) {
    if (!finite2(cx, cy) || !nxc__finitef(r) || r < 0.0f ||
        !nxc__finitef(width) || width < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_stroke_circle");
    }
    double ro = (double)r + 0.5 * (double)width;
    double ri = (double)r - 0.5 * (double)width;
    if (ro > SHAPE_MAX_RADIUS) return nxc__fail(NXC_ERR_UNSUPPORTED, "nxc_stroke_circle");
    emit_t e;
    nxc_status_t st = begin(c, "nxc_stroke_circle", &e);
    if (st != NXC_OK) return st;
    e.premul = nxc__premul(color);
    if (!has_ink(&e) || width == 0.0f || ro == 0.0) return NXC_OK;
    double dcx = (double)cx + (double)c->st.tx, dcy = (double)cy + (double)c->st.ty;
    rrect_t outer, inner;
    circle_make(&outer, dcx, dcy, ro);
    if (ri > 0.0) {
        circle_make(&inner, dcx, dcy, ri);
        draw_rrect(&e, &outer, &inner);
    } else {
        draw_rrect(&e, &outer, 0);
    }
    return NXC_OK;
}

/*
 * Round-capped line. The segment is first clipped (in double) to the clip
 * rect grown by the half width plus a margin, so far-away endpoints cost
 * nothing and keep their precision; an end cut by clipping loses its cap
 * because the line visibly continues past it.
 */
nxc_status_t nxc_draw_line(nxc_canvas_t *c, float x0, float y0, float x1, float y1,
                           float width, nxc_color_t color) {
    if (!finite2(x0, y0) || !finite2(x1, y1) || !nxc__finitef(width) || width < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_draw_line");
    }
    if (0.5 * (double)width > SHAPE_MAX_RADIUS) {
        return nxc__fail(NXC_ERR_UNSUPPORTED, "nxc_draw_line");
    }
    emit_t e;
    nxc_status_t st = begin(c, "nxc_draw_line", &e);
    if (st != NXC_OK) return st;
    e.premul = nxc__premul(color);
    if (!has_ink(&e) || width == 0.0f) return NXC_OK;

    double hw = 0.5 * (double)width;
    double ax = (double)x0 + (double)c->st.tx, ay = (double)y0 + (double)c->st.ty;
    double bx = (double)x1 + (double)c->st.tx, by = (double)y1 + (double)c->st.ty;
    double dx = bx - ax, dy = by - ay;
    double len = sqrt_d(dx * dx + dy * dy);
    if (!(len >= LINE_MIN_LENGTH)) {
        rrect_t dot;
        circle_make(&dot, 0.5 * (ax + bx), 0.5 * (ay + by), hw);
        draw_rrect(&e, &dot, 0);
        return NXC_OK;
    }

    /* Endpoints far outside the clip (think 1e30) would lose all precision
     * in a + s*u, so first cut the segment to a box a little larger than
     * the clip, assigning the bounding coordinate exactly. */
    {
        double bx0 = (double)e.clip.x - 4.0 * (hw + 2.0), bx1 = (double)(e.clip.x + e.clip.w) + 4.0 * (hw + 2.0);
        double by0 = (double)e.clip.y - 4.0 * (hw + 2.0), by1 = (double)(e.clip.y + e.clip.h) + 4.0 * (hw + 2.0);
        double t0 = 0.0, t1 = 1.0;
        int e0 = -1, e1 = -1;   /* which boundary cut each end: 0 x0, 1 x1, 2 y0, 3 y1 */
        const double pq[4][2] = { { -dx, ax - bx0 }, { dx, bx1 - ax }, { -dy, ay - by0 }, { dy, by1 - ay } };
        for (int k = 0; k < 4; k++) {
            double pk = pq[k][0], qk = pq[k][1];
            if (pk == 0.0) {
                if (qk < 0.0) return NXC_OK;         /* parallel and outside */
                continue;
            }
            double t = qk / pk;
            if (pk < 0.0) { if (t > t0) { t0 = t; e0 = k; } }
            else          { if (t < t1) { t1 = t; e1 = k; } }
        }
        if (t0 > t1) return NXC_OK;
        double nax = ax, nay = ay, nbx = bx, nby = by;
        if (e0 >= 0) {
            nax = (e0 == 0) ? bx0 : (e0 == 1) ? bx1 : ax + t0 * dx;
            nay = (e0 == 2) ? by0 : (e0 == 3) ? by1 : ay + t0 * dy;
        }
        if (e1 >= 0) {
            nbx = (e1 == 0) ? bx0 : (e1 == 1) ? bx1 : ax + t1 * dx;
            nby = (e1 == 2) ? by0 : (e1 == 3) ? by1 : ay + t1 * dy;
        }
        /* An end that was cut keeps no cap: the line continues past it. */
        if (e0 >= 0 || e1 >= 0) {
            ax = nax; ay = nay; bx = nbx; by = nby;
            dx = bx - ax; dy = by - ay;
            len = sqrt_d(dx * dx + dy * dy);
            if (!(len >= LINE_MIN_LENGTH)) return NXC_OK;
        }
    }

    /* Measure from the endpoint nearer the clip so the clipped start
     * point is computed from small offsets. */
    double mx = (double)e.clip.x + 0.5 * (double)e.clip.w;
    double my = (double)e.clip.y + 0.5 * (double)e.clip.h;
    double da = max_d(abs_d(ax - mx), abs_d(ay - my));
    double db = max_d(abs_d(bx - mx), abs_d(by - my));
    if (db < da) {
        double t = ax; ax = bx; bx = t;
        t = ay; ay = by; by = t;
        dx = -dx; dy = -dy;
    }
    line_t ln;
    ln.ux = dx / len;
    ln.uy = dy / len;
    double m = hw + 2.0;
    double s0 = 0.0, s1 = len;
    if (!clip_axis(ax, ln.ux, (double)e.clip.x - m, (double)(e.clip.x + e.clip.w) + m, &s0, &s1) ||
        !clip_axis(ay, ln.uy, (double)e.clip.y - m, (double)(e.clip.y + e.clip.h) + m, &s0, &s1)) {
        return NXC_OK;
    }
    ln.cap0 = (s0 <= 0.0);
    ln.cap1 = (s1 >= len);
    ln.px = ax + s0 * ln.ux;
    ln.py = ay + s0 * ln.uy;
    ln.len = s1 - s0;
    ln.hw = hw;
    slab_dir_init(&ln.body, -ln.uy, ln.ux);
    ln.full = hw - ln.body.half;
    ln.body_reach = hw + ln.body.half;
    ln.reach = hw + PIXEL_HALF_DIAG;
    draw_line_rows(&e, &ln);
    return NXC_OK;
}
