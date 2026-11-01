/*
 * NeolyxOS - NXCanvas raster tests: path building, exact-area coverage,
 * fill rules, rounded rects and squircles, the stroker, clipping, band and
 * column-chunk seams, huge coordinates and argument validation.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include "../nxc_internal.h"
#include <math.h>

#define PI_D 3.14159265358979323846

/* ============ Helpers ============ */

/* Coverage grid filled by a raster sink; checks every call stays inside
 * the clip and every run starts and ends with nonzero coverage. */
typedef struct {
    int32_t cx, cy, w, h;
    float  *g;
    int     calls, bad_bounds, bad_runs, bad_order;
    int32_t last_y;
} grid_t;

static void grid_init(grid_t *gr, nxc_irect_t clip) {
    memset(gr, 0, sizeof(*gr));
    gr->cx = clip.x; gr->cy = clip.y; gr->w = clip.w; gr->h = clip.h;
    gr->g = calloc((size_t)clip.w * (size_t)clip.h + 1, sizeof(float));
    gr->last_y = -0x7FFFFFFF;
}

static void grid_free(grid_t *gr) { free(gr->g); gr->g = NULL; }

static void grid_sink(void *ctx, int32_t y, int32_t x0, const uint8_t *cov, int32_t n) {
    grid_t *gr = (grid_t *)ctx;
    gr->calls++;
    if (n <= 0 || y < gr->cy || y >= gr->cy + gr->h || x0 < gr->cx || x0 + n > gr->cx + gr->w) {
        gr->bad_bounds++;
        return;
    }
    if (cov[0] == 0 || cov[n - 1] == 0) gr->bad_runs++;
    for (int32_t i = 0; i < n; i++) {
        gr->g[(size_t)(y - gr->cy) * (size_t)gr->w + (size_t)(x0 - gr->cx + i)] += cov[i] / 255.0f;
    }
}

static float grid_at(const grid_t *gr, int32_t x, int32_t y) {
    if (x < gr->cx || y < gr->cy || x >= gr->cx + gr->w || y >= gr->cy + gr->h) return 0.0f;
    return gr->g[(size_t)(y - gr->cy) * (size_t)gr->w + (size_t)(x - gr->cx)];
}

static double grid_sum(const grid_t *gr) {
    double s = 0;
    for (size_t i = 0; i < (size_t)gr->w * (size_t)gr->h; i++) s += gr->g[i];
    return s;
}

static float grid_max(const grid_t *gr) {
    float m = 0;
    for (size_t i = 0; i < (size_t)gr->w * (size_t)gr->h; i++) if (gr->g[i] > m) m = gr->g[i];
    return m;
}

/* Rasterize p into a fresh grid over clip. */
static nxc_status_t raster_grid(const nxc_path_t *p, nxc_fill_rule_t rule, nxc_irect_t clip,
                                grid_t *gr) {
    grid_init(gr, clip);
    nxc_status_t st = nxc__raster_path(p, 1.0f, 0.0f, 0.0f, rule, clip, grid_sink, gr);
    NXC_CHECK(gr->bad_bounds == 0);
    NXC_CHECK(gr->bad_runs == 0);
    NXC_CHECK(grid_max(gr) <= 1.0f + 1e-6f);
    return st;
}

/* Signed area of a path (implicitly closed subpaths), via Green's theorem
 * with dense sampling of the curves, in double. */
static double path_area(const nxc_path_t *p) {
    double area = 0, cx = 0, cy = 0, sx = 0, sy = 0;
    uint32_t pi = 0;
    for (uint32_t v = 0; v < p->nverbs; v++) {
        uint8_t verb = p->verbs[v];
        if (verb == NXC_VERB_MOVE) {
            area += cx * sy - sx * cy;
            sx = cx = p->pts[pi].x; sy = cy = p->pts[pi].y; pi++;
        } else if (verb == NXC_VERB_LINE) {
            double x = p->pts[pi].x, y = p->pts[pi].y; pi++;
            area += cx * y - x * cy; cx = x; cy = y;
        } else if (verb == NXC_VERB_CUBIC || verb == NXC_VERB_QUAD) {
            double q[4][2];
            q[0][0] = cx; q[0][1] = cy;
            if (verb == NXC_VERB_QUAD) {
                double c0 = p->pts[pi].x, c1 = p->pts[pi].y, e0 = p->pts[pi + 1].x, e1 = p->pts[pi + 1].y;
                q[1][0] = cx + 2.0 / 3.0 * (c0 - cx); q[1][1] = cy + 2.0 / 3.0 * (c1 - cy);
                q[2][0] = e0 + 2.0 / 3.0 * (c0 - e0); q[2][1] = e1 + 2.0 / 3.0 * (c1 - e1);
                q[3][0] = e0; q[3][1] = e1; pi += 2;
            } else {
                for (int k = 0; k < 3; k++) { q[k + 1][0] = p->pts[pi + k].x; q[k + 1][1] = p->pts[pi + k].y; }
                pi += 3;
            }
            for (int i = 1; i <= 4096; i++) {
                double t = i / 4096.0, u = 1 - t;
                double x = u * u * u * q[0][0] + 3 * u * u * t * q[1][0] + 3 * u * t * t * q[2][0] + t * t * t * q[3][0];
                double y = u * u * u * q[0][1] + 3 * u * u * t * q[1][1] + 3 * u * t * t * q[2][1] + t * t * t * q[3][1];
                area += cx * y - x * cy; cx = x; cy = y;
            }
        } else if (verb == NXC_VERB_CLOSE) {
            area += cx * sy - sx * cy; cx = sx; cy = sy;
        }
    }
    area += cx * sy - sx * cy;
    return 0.5 * area;
}

/* Area of polygon q (n points) inside pixel [px, px+1) x [py, py+1),
 * by Sutherland-Hodgman clipping (reference for exact-area checks). */
static double poly_pixel_area(const double (*q)[2], int n, int px, int py) {
    double a[64][2], b[64][2];
    int na = n;
    for (int i = 0; i < n; i++) { a[i][0] = q[i][0]; a[i][1] = q[i][1]; }
    for (int side = 0; side < 4 && na > 0; side++) {
        int nb = 0;
        for (int i = 0; i < na; i++) {
            const double *s = a[i], *e = a[(i + 1) % na];
            double ds, de;
            switch (side) {
                case 0: ds = s[0] - px; de = e[0] - px; break;
                case 1: ds = px + 1 - s[0]; de = px + 1 - e[0]; break;
                case 2: ds = s[1] - py; de = e[1] - py; break;
                default: ds = py + 1 - s[1]; de = py + 1 - e[1]; break;
            }
            if (ds >= 0) { b[nb][0] = s[0]; b[nb][1] = s[1]; nb++; }
            if ((ds >= 0) != (de >= 0)) {
                double t = ds / (ds - de);
                b[nb][0] = s[0] + (e[0] - s[0]) * t; b[nb][1] = s[1] + (e[1] - s[1]) * t; nb++;
            }
        }
        na = nb;
        for (int i = 0; i < na; i++) { a[i][0] = b[i][0]; a[i][1] = b[i][1]; }
    }
    double ar = 0;
    for (int i = 0; i < na; i++) ar += a[i][0] * a[(i + 1) % na][1] - a[(i + 1) % na][0] * a[i][1];
    return fabs(0.5 * ar);
}

/* Surface "coverage": the light of the white channel of white-on-black,
 * 0..1. R2 blends edge coverage in linear light, so coverage is the
 * decoded (linear) value, not the sRGB byte. */
static double surf_cov(const nxc_surface_t *s, int32_t x, int32_t y) {
    double v = (double)((nxc_test_pixel(s, x, y) >> 8) & 0xFF) / 255.0;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* The raw sRGB value of the white channel, 0..1. Alpha (paint and global
 * alpha) still composites in sRGB, so alpha checks read this. */
static double surf_srgb(const nxc_surface_t *s, int32_t x, int32_t y) {
    return (double)((nxc_test_pixel(s, x, y) >> 8) & 0xFF) / 255.0;
}

static double surf_sum(const nxc_surface_t *s) {
    double t = 0;
    for (int32_t y = 0; y < s->height; y++)
        for (int32_t x = 0; x < s->width; x++) t += surf_cov(s, x, y);
    return t;
}

/* Write a NEAREST-upscaled copy of region r of s. */
static void write_zoom(const nxc_surface_t *s, nxc_irect_t r, int k, const char *name) {
    nxc_surface_t z;
    if (nxc_surface_create(&z, r.w * k, r.h * k, 0) != NXC_OK) return;
    for (int32_t y = 0; y < r.h * k; y++)
        for (int32_t x = 0; x < r.w * k; x++) {
            int32_t sx = r.x + x / k, sy = r.y + y / k;
            if (sx >= 0 && sy >= 0 && sx < s->width && sy < s->height)
                z.pixels[(size_t)y * z.stride + x] = s->pixels[(size_t)sy * s->stride + sx];
        }
    nxc_test_write_png(&z, nxc_test_path(name));
    nxc_surface_destroy(&z);
}

/* ============ Path building ============ */

static void test_path_building(void) {
    nxc_point_t pts[64];
    uint8_t verbs[64];
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init(&p, pts, 64, verbs, 64));

    /* line_to on an empty path only sets the point; moves collapse. */
    NXC_CHECK_OK(nxc_path_line_to(&p, 5, 6));
    NXC_CHECK(p.nverbs == 1 && p.verbs[0] == NXC_VERB_MOVE && p.npts == 1);
    NXC_CHECK_OK(nxc_path_move_to(&p, 1, 2));
    NXC_CHECK(p.nverbs == 1 && p.pts[0].x == 1 && p.pts[0].y == 2);
    NXC_CHECK_OK(nxc_path_line_to(&p, 10, 2));
    NXC_CHECK_OK(nxc_path_line_to(&p, 10, 9));
    NXC_CHECK_OK(nxc_path_close(&p));
    NXC_CHECK_OK(nxc_path_close(&p));                 /* double close is a no-op */
    NXC_CHECK(p.nverbs == 4 && p.verbs[3] == NXC_VERB_CLOSE);
    /* Segment after close: implicit move to the closed subpath's start. */
    NXC_CHECK_OK(nxc_path_line_to(&p, 3, 3));
    NXC_CHECK(p.nverbs == 6 && p.verbs[4] == NXC_VERB_MOVE && p.verbs[5] == NXC_VERB_LINE);
    NXC_CHECK(p.pts[3].x == 1 && p.pts[3].y == 2);
    nxc_rect_t b;
    NXC_CHECK_OK(nxc_path_bounds(&p, &b));
    NXC_CHECK(b.x == 1 && b.y == 2 && b.w == 9 && b.h == 7);
    NXC_CHECK_OK(nxc_path_transform(&p, 2, 3, 1, -1));
    NXC_CHECK(p.pts[0].x == 3 && p.pts[0].y == 5);
    NXC_CHECK(nxc_path_transform(&p, 3e38f, 1, 0, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(p.pts[0].x == 3 && p.error == NXC_OK);  /* failed transform left it intact */

    /* quad/cubic on empty paths start at their first control point. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_quad_to(&p, 4, 4, 8, 0));
    NXC_CHECK(p.nverbs == 2 && p.verbs[0] == NXC_VERB_MOVE && p.pts[0].x == 4 && p.npts == 3);
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_cubic_to(&p, 1, 1, 2, 2, 3, 3));
    NXC_CHECK(p.nverbs == 2 && p.npts == 4 && p.pts[0].x == 1);

    /* Arc direction: positive sweep is clockwise on screen (0 -> +y). */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_arc(&p, 50, 50, 10, 0.0f, 0.5f * NXC_PI));
    NXC_CHECK(p.nverbs == 2 && p.verbs[1] == NXC_VERB_CUBIC);
    NXC_CHECK(fabsf(p.pts[3].x - 50) < 1e-4f && fabsf(p.pts[3].y - 60) < 1e-4f);
    NXC_CHECK(p.pts[1].y > 50.0f);                    /* first handle heads down */
    /* Negative sweep runs counter-clockwise; a full turn uses 4 cubics. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_arc(&p, 0, 0, 10, 0.0f, -0.5f * NXC_PI));
    NXC_CHECK(fabsf(p.pts[3].y + 10) < 1e-4f);
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_arc(&p, 0, 0, 10, 1.0f, 1.0f + 7.0f * NXC_PI));
    NXC_CHECK(p.nverbs == 5);
    NXC_CHECK(fabsf(p.pts[12].x - p.pts[0].x) < 1e-3f && fabsf(p.pts[12].y - p.pts[0].y) < 1e-3f);
    /* Arc on a non-empty path lines to its start. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, 0, 0));
    NXC_CHECK_OK(nxc_path_add_arc(&p, 0, 0, 5, 0, 1));
    NXC_CHECK(p.verbs[1] == NXC_VERB_LINE && p.pts[1].x == 5.0f);

    /* Degenerate shapes. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_circle(&p, 3, 3, 0));
    NXC_CHECK(p.nverbs == 0);
    NXC_CHECK_OK(nxc_path_add_rrect(&p, nxc_rect(0, 0, 10, 0), 4));
    NXC_CHECK(p.nverbs == 5);                         /* falls back to a rect */
    NXC_CHECK(nxc_path_add_rect(&p, nxc_rect(0, 0, -1, 4)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(p.error == NXC_ERR_INVALID_ARG);        /* sticky */
    NXC_CHECK(nxc_path_line_to(&p, 1, 1) == NXC_ERR_INVALID_ARG);
    nxc_path_reset(&p);
    NXC_CHECK(p.error == NXC_OK && p.nverbs == 0);

    /* Overflow: atomic and sticky. */
    nxc_point_t sp[6];
    uint8_t sv[4];
    nxc_path_t q;
    NXC_CHECK_OK(nxc_path_init(&q, sp, 6, sv, 4));
    NXC_CHECK(nxc_path_add_rect(&q, nxc_rect(0, 0, 4, 4)) == NXC_ERR_OVERFLOW);
    NXC_CHECK(q.nverbs == 0 && q.npts == 0);
    NXC_CHECK(nxc_path_move_to(&q, 0, 0) == NXC_ERR_OVERFLOW);
    nxc_path_reset(&q);
    NXC_CHECK_OK(nxc_path_move_to(&q, 0, 0));
    NXC_CHECK_OK(nxc_path_line_to(&q, 1, 0));
    NXC_CHECK_OK(nxc_path_line_to(&q, 1, 1));
    NXC_CHECK_OK(nxc_path_line_to(&q, 0, 1));
    NXC_CHECK(nxc_path_line_to(&q, 0, 2) == NXC_ERR_OVERFLOW);
    NXC_CHECK(q.error == NXC_ERR_OVERFLOW && q.nverbs == 4);

    /* NaN / inf and bad storage. */
    nxc_path_reset(&p);
    NXC_CHECK(nxc_path_move_to(&p, NAN, 0) == NXC_ERR_INVALID_ARG);
    nxc_path_reset(&p);
    NXC_CHECK(nxc_path_cubic_to(&p, 0, 0, INFINITY, 0, 1, 1) == NXC_ERR_INVALID_ARG);
    nxc_path_reset(&p);
    NXC_CHECK(nxc_path_add_rrect(&p, nxc_rect(0, 0, 10, 10), NAN) == NXC_ERR_INVALID_ARG);
    nxc_path_reset(&p);
    NXC_CHECK(nxc_path_add_squircle(&p, nxc_rect(0, 0, 10, 10), -1) == NXC_ERR_INVALID_ARG);
    nxc_path_reset(&p);
    NXC_CHECK(nxc_path_add_arc(&p, 0, 0, 1, NAN, 1) == NXC_ERR_INVALID_ARG);
    nxc_path_reset(&p);
    NXC_CHECK(nxc_path_add_rect(&p, nxc_rect(3e38f, 0, 3e38f, 1)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_path_init(&q, NULL, 4, sv, 4) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_path_line_to(&q, 1, 1) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_path_init(NULL, sp, 4, sv, 4) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_path_move_to(NULL, 0, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_path_bounds(&p, NULL) == NXC_ERR_INVALID_ARG);

    /* Allocated storage. */
    nxc_path_t a;
    NXC_CHECK_OK(nxc_path_init_alloc(&a, 128, 64));
    NXC_CHECK(a.owned == 1 && a.cap_pts == 128);
    NXC_CHECK_OK(nxc_path_add_squircle(&a, nxc_rect(0, 0, 100, 60), 20));
    NXC_CHECK(a.nverbs > 0 && a.verbs[0] == NXC_VERB_MOVE && a.verbs[a.nverbs - 1] == NXC_VERB_CLOSE);
    nxc_path_destroy(&a);
    NXC_CHECK(a.pts == NULL && a.owned == 0);
    NXC_CHECK(nxc_path_init_alloc(&a, 0, 4) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_path_init_alloc(&a, 1u << 30, 4) == NXC_ERR_INVALID_ARG);
    nxc_path_destroy(NULL);
}

/* ============ Exact-area fills ============ */

static void test_circle_area(void) {
    static const float radii[] = { 2.0f, 3.3f, 4.5f, 7.25f, 12.6f, 33.3f, 64.7f, 150.2f };
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 64, 16));
    double worst = 0, worst_exact = 0;
    for (size_t i = 0; i < sizeof(radii) / sizeof(radii[0]); i++) {
        float r = radii[i];
        float cx = 160.37f + (float)i * 0.131f, cy = 161.71f - (float)i * 0.17f;
        nxc_path_reset(&p);
        NXC_CHECK_OK(nxc_path_add_circle(&p, cx, cy, r));
        grid_t gr;
        NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 330, 330), &gr));
        double sum = grid_sum(&gr);
        double want = PI_D * r * r;
        double rel = fabs(sum - want) / want;
        double rel_exact = fabs(sum - path_area(&p)) / path_area(&p);
        if (rel > worst) worst = rel;
        if (rel_exact > worst_exact) worst_exact = rel_exact;
        fprintf(stderr, "circle r=%7.2f  cov=%11.3f  pi r^2=%11.3f  err=%.4f%%  vs path area %.4f%%\n",
                r, sum, want, 100 * rel, 100 * rel_exact);
        /* Chords lie inside the curve, so tiny circles lose ~(2/3)*tol per
         * perimeter pixel; at r=2 that is under 0.1 px^2 in total. */
        NXC_CHECK(rel < (r < 3.0f ? 0.01 : 0.005));
        /* Symmetry about the center (values may differ by a rounding step). */
        int32_t ix = (int32_t)cx, iy = (int32_t)cy;
        NXC_CHECK(fabsf(grid_at(&gr, ix, iy) - 1.0f) < 1e-6f || r < 1.0f);
        grid_free(&gr);
    }
    fprintf(stderr, "circle area: worst %.4f%% vs pi r^2, %.4f%% vs exact path area\n",
            100 * worst, 100 * worst_exact);
    /* Tiny circles still produce sane coverage. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_circle(&p, 10.3f, 10.6f, 0.4f));
    grid_t gr;
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 20, 20), &gr));
    double s = grid_sum(&gr), want = PI_D * 0.16;
    fprintf(stderr, "tiny circle r=0.4: cov %.4f vs %.4f\n", s, want);
    NXC_CHECK(fabs(s - want) / want < 0.05);
    grid_free(&gr);
    nxc_path_destroy(&p);
}

static void test_polygon_exact(void) {
    /* Fractional rect spanning several bands: every pixel exactly its overlap. */
    nxc_point_t pts[64];
    uint8_t verbs[64];
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init(&p, pts, 64, verbs, 64));
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(10.3f, 5.6f, 20.45f, 77.8f)));
    grid_t gr;
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 64, 100), &gr));
    double maxerr = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 64; x++) {
            double ox = fmax(0, fmin(x + 1, 30.75) - fmax(x, 10.3));
            double oy = fmax(0, fmin(y + 1, 83.4) - fmax(y, 5.6));
            double e = fabs(grid_at(&gr, x, y) - ox * oy);
            if (e > maxerr) maxerr = e;
        }
    fprintf(stderr, "rect exact: max per-pixel error %.5f\n", maxerr);
    NXC_CHECK(maxerr <= 0.5 / 255.0 + 1e-4);
    grid_free(&gr);

    /* Simple concave polygons (both windings) vs clipped reference areas. */
    static const double polys[2][7][2] = {
        { { 3.2, 4.7 }, { 40.9, 2.1 }, { 33.3, 20.5 }, { 21.05, 12.2 }, { 12.8, 37.6 }, { 1.1, 30.3 }, { 7.7, 15.5 } },
        { { 7.7, 15.5 }, { 1.1, 30.3 }, { 12.8, 37.6 }, { 21.05, 12.2 }, { 33.3, 20.5 }, { 40.9, 2.1 }, { 3.2, 4.7 } },
    };
    for (int k = 0; k < 2; k++) {
        nxc_path_reset(&p);
        for (int i = 0; i < 7; i++) {
            if (i == 0) NXC_CHECK_OK(nxc_path_move_to(&p, (float)polys[k][i][0], (float)polys[k][i][1]));
            else NXC_CHECK_OK(nxc_path_line_to(&p, (float)polys[k][i][0], (float)polys[k][i][1]));
        }
        for (int rule = 0; rule < 2; rule++) {
            NXC_CHECK_OK(raster_grid(&p, (nxc_fill_rule_t)rule, nxc_irect(0, 0, 48, 48), &gr));
            maxerr = 0;
            for (int y = 0; y < 48; y++)
                for (int x = 0; x < 48; x++) {
                    double e = fabs(grid_at(&gr, x, y) - poly_pixel_area(polys[k], 7, x, y));
                    if (e > maxerr) maxerr = e;
                }
            NXC_CHECK(maxerr <= 0.5 / 255.0 + 2e-4);
            grid_free(&gr);
        }
    }
    fprintf(stderr, "polygon exact: max per-pixel error %.5f\n", maxerr);
}

static void test_fill_rules(void) {
    nxc_point_t pts[64];
    uint8_t verbs[64];
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init(&p, pts, 64, verbs, 64));
    /* Pentagram drawn in one stroke: the center has winding 2. */
    for (int i = 0; i < 5; i++) {
        double a = -PI_D / 2 + i * 4 * PI_D / 5;
        float x = (float)(50.5 + 40 * cos(a)), y = (float)(50.5 + 40 * sin(a));
        if (i == 0) NXC_CHECK_OK(nxc_path_move_to(&p, x, y));
        else NXC_CHECK_OK(nxc_path_line_to(&p, x, y));
    }
    NXC_CHECK_OK(nxc_path_close(&p));
    grid_t nz, eo;
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 101, 101), &nz));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_EVENODD, nxc_irect(0, 0, 101, 101), &eo));
    NXC_CHECK(fabsf(grid_at(&nz, 50, 50) - 1.0f) < 1e-6f);
    NXC_CHECK(grid_at(&eo, 50, 50) == 0.0f);
    NXC_CHECK(fabsf(grid_at(&eo, 50, 15) - 1.0f) < 1e-6f);    /* a point: winding 1 */
    NXC_CHECK(fabsf(grid_at(&nz, 50, 15) - 1.0f) < 1e-6f);
    NXC_CHECK(grid_sum(&nz) > grid_sum(&eo) + 500.0);
    grid_free(&nz);
    grid_free(&eo);

    /* Two overlapping same-direction squares and a reversed hole. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(10, 10, 40, 40)));
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(30, 30, 40, 40)));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 80, 80), &nz));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_EVENODD, nxc_irect(0, 0, 80, 80), &eo));
    NXC_CHECK(grid_at(&nz, 40, 40) == 1.0f && grid_at(&eo, 40, 40) == 0.0f);
    NXC_CHECK(fabs(grid_sum(&nz) - 2800.0) < 0.5 && fabs(grid_sum(&eo) - 2400.0) < 0.5);
    grid_free(&nz);
    grid_free(&eo);
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(10, 10, 60, 60)));
    NXC_CHECK_OK(nxc_path_move_to(&p, 20, 20));           /* counter-clockwise hole */
    NXC_CHECK_OK(nxc_path_line_to(&p, 20, 60));
    NXC_CHECK_OK(nxc_path_line_to(&p, 60, 60));
    NXC_CHECK_OK(nxc_path_line_to(&p, 60, 20));
    NXC_CHECK_OK(nxc_path_close(&p));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 80, 80), &nz));
    NXC_CHECK(grid_at(&nz, 40, 40) == 0.0f && fabs(grid_sum(&nz) - 2000.0) < 0.5);
    grid_free(&nz);
}

/* ============ Rounded rects and squircles ============ */

static void test_rounded(void) {
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 64, 32));
    static const float sizes[] = { 16, 24, 37.5f, 64, 100, 181, 256, 512 };
    double worst_rr = 0, worst_sq = 0;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        float s = sizes[i];
        for (int kind = 0; kind < 2; kind++) {
            float rad = s * 0.225f;
            float x = 4.25f, y = 3.5f;
            nxc_path_reset(&p);
            if (kind == 0) NXC_CHECK_OK(nxc_path_add_rrect(&p, nxc_rect(x, y, s, s * 0.8f), rad));
            else NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(x, y, s, s * 0.8f), rad));
            grid_t gr;
            NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 524, 420), &gr));
            double exact = path_area(&p);
            double rel = fabs(grid_sum(&gr) - exact) / exact;
            if (kind == 0) {
                double rr = s * s * 0.8 - (4 - PI_D) * rad * rad;
                NXC_CHECK(fabs(exact - rr) / rr < 0.001);
                if (rel > worst_rr) worst_rr = rel;
            } else if (rel > worst_sq) {
                worst_sq = rel;
            }
            NXC_CHECK(rel < 0.002);
            NXC_CHECK(exact > 0);                       /* clockwise on screen */
            grid_free(&gr);
        }
    }
    fprintf(stderr, "rounded area error: rrect %.4f%%, squircle %.4f%%\n", 100 * worst_rr, 100 * worst_sq);

    /* Squircle symmetry: integer-aligned square, mirrored and transposed. */
    for (int k = 0; k < 3; k++) {
        float rad = k == 0 ? 30.0f : (k == 1 ? 60.0f : 200.0f);   /* 200: clamped */
        nxc_path_reset(&p);
        NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(10, 10, 200, 200), rad));
        grid_t gr;
        NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 220, 220), &gr));
        float worst = 0;
        for (int y = 0; y < 220; y++)
            for (int x = 0; x < 220; x++) {
                float v = grid_at(&gr, x, y);
                worst = fmaxf(worst, fabsf(v - grid_at(&gr, 219 - x, y)));
                worst = fmaxf(worst, fabsf(v - grid_at(&gr, x, 219 - y)));
                worst = fmaxf(worst, fabsf(v - grid_at(&gr, y, x)));
            }
        NXC_CHECK(worst <= 2.0f / 255.0f + 1e-5f);
        /* The corner leaves the edge 1.528 r from the corner (or at the middle). */
        float ext = fminf(1.528f * fminf(rad, 100.0f), 100.0f);
        NXC_CHECK(fabsf(p.pts[0].x - (10 + ext)) < 1e-3f && p.pts[0].y == 10.0f);
        if (k == 0) fprintf(stderr, "squircle symmetry: max diff %.5f\n", worst);
        grid_free(&gr);
    }
    /* Squircle is inside the circular rrect's corner region near the edge
     * (continuous corners start earlier) and matches it on the diagonal. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(0, 0, 400, 400), 50));
    nxc_rect_t bb;
    NXC_CHECK_OK(nxc_path_bounds(&p, &bb));
    NXC_CHECK(bb.x == 0 && bb.y == 0 && bb.w == 400 && bb.h == 400);
    nxc_path_destroy(&p);
}

/* ============ Strokes ============ */

static void stroke_one(nxc_surface_t *s, nxc_canvas_t *c, const nxc_path_t *p, float w,
                       nxc_cap_t cap, nxc_join_t join, nxc_color_t color) {
    nxc_stroke_t st;
    st.width = w;
    st.cap = cap;
    st.join = join;
    st.miter_limit = 4;
    NXC_CHECK_OK(nxc_stroke_path(c, p, &st, color));
    (void)s;
}

static void test_strokes(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 120, 80, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_point_t pts[256];
    uint8_t verbs[256];
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init(&p, pts, 256, verbs, 256));
    NXC_CHECK_OK(nxc_path_move_to(&p, 10, 20));
    NXC_CHECK_OK(nxc_path_line_to(&p, 50, 20));

    /* Butt and square caps are exact rects; round adds a disc's area. */
    static const double want[3] = { 160.0, 160.0 + 4 * PI_D, 176.0 };   /* two half discs of r=2 */
    for (int cap = 0; cap < 3; cap++) {
        NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
        stroke_one(&s, &c, &p, 4, (nxc_cap_t)cap, NXC_JOIN_MITER, NXC_RGB(255, 255, 255));
        double sum = surf_sum(&s);
        fprintf(stderr, "cap %d: area %.3f (want %.3f)\n", cap, sum, want[cap]);
        NXC_CHECK(fabs(sum - want[cap]) / want[cap] < 0.005);
        if (cap == 0) {
            NXC_CHECK(surf_cov(&s, 10, 18) == 1.0 && surf_cov(&s, 49, 21) == 1.0);
            NXC_CHECK(surf_cov(&s, 9, 19) == 0.0 && surf_cov(&s, 50, 19) == 0.0 && surf_cov(&s, 20, 22) == 0.0);
        } else if (cap == 2) {
            NXC_CHECK(surf_cov(&s, 8, 18) == 1.0 && surf_cov(&s, 51, 21) == 1.0 && surf_cov(&s, 52, 20) == 0.0);
        }
    }

    /* Retracing path at half alpha: overlaps must not darken twice. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, 15, 40));
    NXC_CHECK_OK(nxc_path_line_to(&p, 100, 40));
    NXC_CHECK_OK(nxc_path_line_to(&p, 20, 40.3f));
    NXC_CHECK_OK(nxc_path_line_to(&p, 90, 41));
    for (int join = 0; join < 3; join++) {
        NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
        stroke_one(&s, &c, &p, 8, NXC_CAP_BUTT, (nxc_join_t)join, NXC_ARGB(128, 255, 255, 255));
        double mx = 0;
        for (int y = 0; y < 80; y++) for (int x = 0; x < 120; x++) mx = fmax(mx, surf_srgb(&s, x, y));
        NXC_CHECK(mx <= 129.0 / 255.0);
        NXC_CHECK(surf_srgb(&s, 50, 40) >= 127.0 / 255.0);
    }

    /* Square outline: miter fills the corner, bevel cuts it, round between. */
    double corner[3];
    for (int join = 0; join < 3; join++) {
        nxc_path_reset(&p);
        NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(20, 20, 40, 40)));
        NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
        stroke_one(&s, &c, &p, 10, NXC_CAP_BUTT, (nxc_join_t)join, NXC_RGB(255, 255, 255));
        corner[join] = surf_cov(&s, 15, 15) + surf_cov(&s, 16, 16) + surf_cov(&s, 64, 64);
        double sum = surf_sum(&s);
        double area = (join == 0) ? 50.0 * 50 - 30.0 * 30
                    : (join == 2) ? 50.0 * 50 - 30.0 * 30 - 4 * 12.5
                                  : 50.0 * 50 - 30.0 * 30 - 4 * (25 - PI_D * 25 / 4);
        fprintf(stderr, "join %d: area %.3f (want %.3f)\n", join, sum, area);
        NXC_CHECK(fabs(sum - area) / area < 0.004);
        NXC_CHECK(surf_cov(&s, 40, 40) == 0.0 && surf_cov(&s, 40, 20) == 1.0);
    }
    NXC_CHECK(corner[0] > corner[1] && corner[1] > corner[2]);

    /* Miter limit: a sharp spike falls back to bevel. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, 10, 70));
    NXC_CHECK_OK(nxc_path_line_to(&p, 60, 10));
    NXC_CHECK_OK(nxc_path_line_to(&p, 11, 70));          /* near reversal: miter ratio >> 4 */
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    stroke_one(&s, &c, &p, 6, NXC_CAP_BUTT, NXC_JOIN_MITER, NXC_RGB(255, 255, 255));
    NXC_CHECK(surf_cov(&s, 61, 2) == 0.0);               /* no long spike above the apex */

    /* Zero-length subpaths: dots for round/square caps, nothing for butt. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, 30.5f, 30.5f));
    NXC_CHECK_OK(nxc_path_line_to(&p, 30.5f, 30.5f));
    static const double dot[3] = { 0.0, PI_D * 25, 100.0 };
    for (int cap = 0; cap < 3; cap++) {
        NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
        stroke_one(&s, &c, &p, 10, (nxc_cap_t)cap, NXC_JOIN_ROUND, NXC_RGB(255, 255, 255));
        double sum = surf_sum(&s);
        NXC_CHECK(fabs(sum - dot[cap]) <= 0.005 * dot[cap] + 0.01);
    }

    /* Hairline strokes keep their area. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, 5.3f, 10.2f));
    NXC_CHECK_OK(nxc_path_line_to(&p, 105.3f, 60.7f));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    stroke_one(&s, &c, &p, 0.25f, NXC_CAP_BUTT, NXC_JOIN_MITER, NXC_RGB(255, 255, 255));
    double len = sqrt(100.0 * 100 + 50.5 * 50.5);
    NXC_CHECK(fabs(surf_sum(&s) - 0.25 * len) / (0.25 * len) < 0.03);

    /* Closed curve stroke: circle ring area 2 pi r w. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_circle(&p, 60.3f, 40.1f, 30));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    stroke_one(&s, &c, &p, 3, NXC_CAP_BUTT, NXC_JOIN_MITER, NXC_RGB(255, 255, 255));
    double ring = 2 * PI_D * 30 * 3;
    fprintf(stderr, "ring: area %.3f (want %.3f)\n", surf_sum(&s), ring);
    /* Adjacent chord quads overlap inside the curve; the accumulation
     * rasterizer over-counts that overlap within edge pixels (~0.7%). */
    NXC_CHECK(fabs(surf_sum(&s) - ring) / ring < 0.01);

    nxc_surface_destroy(&s);
}

/* Archimedean spiral built from cubic arc pieces; returns its length. */
static double build_spiral(nxc_path_t *p, float cx, float cy, float r0, float dr, int turns) {
    double len = 0;
    float a = 0.0f;
    NXC_CHECK_OK(nxc_path_move_to(p, cx + r0, cy));
    float px = cx + r0, py = cy;
    for (int i = 0; i < turns * 16; i++) {
        float a1 = a + (float)(2 * PI_D / 16);
        float rr0 = r0 + dr * a / (float)(2 * PI_D), rr1 = r0 + dr * a1 / (float)(2 * PI_D);
        float k = (float)(4.0 / 3.0 * tan(PI_D / 32));
        float x1 = cx + rr1 * cosf(a1), y1 = cy + rr1 * sinf(a1);
        NXC_CHECK_OK(nxc_path_cubic_to(p, px - k * rr0 * sinf(a), py + k * rr0 * cosf(a),
                                       x1 + k * rr1 * sinf(a1), y1 - k * rr1 * cosf(a1), x1, y1));
        len += sqrt((x1 - px) * (x1 - px) + (y1 - py) * (y1 - py)) * 1.0003;
        px = x1; py = y1; a = a1;
    }
    return len;
}

static void test_spiral(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 256, 256, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 2048, 1024));
    double len = build_spiral(&p, 128.3f, 127.6f, 6, 9, 13);
    nxc_stroke_t st = { 1.0f, NXC_CAP_BUTT, NXC_JOIN_ROUND, 4 };
    NXC_CHECK_OK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)));
    double sum = surf_sum(&s);
    fprintf(stderr, "spiral: coverage %.1f, length %.1f (%.3f%%)\n", sum, len, 100 * fabs(sum - len) / len);
    NXC_CHECK(fabs(sum - len) / len < 0.02);
    nxc_test_write_png(&s, nxc_test_path("raster_spiral.png"));
    write_zoom(&s, nxc_irect(100, 100, 56, 56), 8, "raster_spiral_zoom.png");
    nxc_path_destroy(&p);
    nxc_surface_destroy(&s);
}

/* ============ Clipping, chunks, huge coordinates ============ */

static void test_clipping(void) {
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 64, 32));
    NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(-30.5f, -20.25f, 150, 120), 40));
    NXC_CHECK_OK(nxc_path_add_circle(&p, 90.7f, 70.2f, 33));

    /* Raster clip: identical values inside, nothing outside. */
    grid_t full, part;
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(-64, -64, 256, 256), &full));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(13, 7, 61, 45), &part));
    float worst = 0;
    for (int y = 7; y < 52; y++)
        for (int x = 13; x < 74; x++) worst = fmaxf(worst, fabsf(grid_at(&full, x, y) - grid_at(&part, x, y)));
    NXC_CHECK(worst <= 1.0f / 255.0f + 1e-6f);
    grid_free(&full);
    grid_free(&part);

    /* Canvas: shape hanging off every surface edge equals a crop of a
     * larger render; a clip rect leaves the outside untouched. */
    nxc_surface_t a, b;
    nxc_canvas_t ca, cb;
    NXC_CHECK_OK(nxc_surface_create(&a, 100, 80, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&b, 300, 280, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&ca, &a));
    NXC_CHECK_OK(nxc_canvas_init(&cb, &b));
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_circle(&p, 50.3f, 40.6f, 70));
    NXC_CHECK_OK(nxc_path_add_circle(&p, 50.3f, 40.6f, 30));
    NXC_CHECK_OK(nxc_fill_path(&ca, &p, NXC_FILL_EVENODD, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_translate(&cb, 100, 100));
    NXC_CHECK_OK(nxc_fill_path(&cb, &p, NXC_FILL_EVENODD, NXC_RGB(255, 255, 255)));
    int diff = 0;
    for (int y = 0; y < 80; y++)
        for (int x = 0; x < 100; x++)
            if (!nxc_test_color_near(nxc_test_pixel(&a, x, y), nxc_test_pixel(&b, x + 100, y + 100), 1)) diff++;
    NXC_CHECK(diff == 0);
    NXC_CHECK_OK(nxc_surface_clear(&a, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_save(&ca));
    NXC_CHECK_OK(nxc_clip_rect(&ca, nxc_irect(10, 10, 30, 20)));
    NXC_CHECK_OK(nxc_fill_path(&ca, &p, NXC_FILL_EVENODD, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_restore(&ca));
    diff = 0;
    for (int y = 0; y < 80; y++)
        for (int x = 0; x < 100; x++) {
            uint32_t got = nxc_test_pixel(&a, x, y);
            int inside = x >= 10 && x < 40 && y >= 10 && y < 30;
            uint32_t want = inside ? nxc_test_pixel(&b, x + 100, y + 100) : 0xFF000000u;
            if (!nxc_test_color_near(got, want, 1)) diff++;
        }
    NXC_CHECK(diff == 0);
    /* Rounded clip and global alpha apply to path fills too. */
    NXC_CHECK_OK(nxc_surface_clear(&a, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_save(&ca));
    NXC_CHECK_OK(nxc_clip_rrect(&ca, nxc_rect(5, 5, 90, 70), 20));
    NXC_CHECK_OK(nxc_set_alpha(&ca, 128));
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(-10, -10, 200, 200)));
    NXC_CHECK_OK(nxc_fill_path(&ca, &p, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_restore(&ca));
    NXC_CHECK(surf_srgb(&a, 6, 6) == 0.0 && fabs(surf_srgb(&a, 50, 40) - 0.5) < 0.01);
    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
    nxc_path_destroy(&p);
}

/* Render a test scene into a grid (used with different max_width). */
static void chunk_scene(grid_t *gr) {
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 128, 64));
    NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(-20.3f, 7.7f, 310, 150), 60));
    NXC_CHECK_OK(nxc_path_add_circle(&p, 150.5f, 90.25f, 60.6f));
    NXC_CHECK_OK(nxc_path_add_ellipse(&p, 250.1f, 100.9f, 120, 45.5f));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_EVENODD, nxc_irect(3, 2, 297, 190), gr));
    nxc_path_destroy(&p);
}

static void test_chunks(void) {
    grid_t ref, small;
    chunk_scene(&ref);
    nxc_shutdown();
    nxc_test_init(40, 40);                             /* chunks of ~40 columns */
    chunk_scene(&small);
    nxc_shutdown();
    nxc_test_init(1024, 1024);
    float worst = 0;
    for (size_t i = 0; i < (size_t)ref.w * (size_t)ref.h; i++) worst = fmaxf(worst, fabsf(ref.g[i] - small.g[i]));
    fprintf(stderr, "column chunks: max diff %.5f\n", worst);
    NXC_CHECK(worst <= 1.0f / 255.0f + 1e-6f);
    NXC_CHECK(fabs(grid_sum(&ref) - grid_sum(&small)) < 1.0);
    grid_free(&ref);
    grid_free(&small);

    /* A surface wider than max_width still renders everywhere. */
    nxc_shutdown();
    nxc_test_init(64, 64);
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 500, 40, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 16, 8));
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(0.5f, 10, 499, 20)));
    NXC_CHECK_OK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)));
    NXC_CHECK(fabs(surf_sum(&s) - 499.0 * 20) < 2.0);
    NXC_CHECK(surf_cov(&s, 64, 15) == 1.0 && surf_cov(&s, 400, 29) == 1.0 && surf_cov(&s, 400, 30) == 0.0);
    nxc_path_destroy(&p);
    nxc_surface_destroy(&s);
    nxc_shutdown();
    nxc_test_init(1024, 1024);
}

static void test_huge(void) {
    nxc_point_t pts[64];
    uint8_t verbs[64];
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init(&p, pts, 64, verbs, 64));
    grid_t gr;

    /* Rect reaching far outside: exact edge at x = 100.5. */
    /* 1e6 keeps 1e6 + 100.5 exactly representable in float (spacing 1/16). */
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(-1e6f, -1e6f, 1e6f + 100.5f, 2e6f)));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 200, 100), &gr));
    NXC_CHECK(grid_at(&gr, 0, 0) == 1.0f && grid_at(&gr, 99, 50) == 1.0f);
    NXC_CHECK(fabsf(grid_at(&gr, 100, 50) - 0.5f) < 1.0f / 255.0f && grid_at(&gr, 101, 50) == 0.0f);
    grid_free(&gr);

    /* Triangle with far vertices: rows above y = 50.25 are full. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, -5e8f, 50.25f));
    NXC_CHECK_OK(nxc_path_line_to(&p, 0.0f, -5e8f));
    NXC_CHECK_OK(nxc_path_line_to(&p, 5e8f, 50.25f));
    NXC_CHECK_OK(nxc_path_close(&p));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 200, 100), &gr));
    NXC_CHECK(grid_at(&gr, 3, 0) == 1.0f && grid_at(&gr, 199, 49) == 1.0f);
    NXC_CHECK(fabsf(grid_at(&gr, 120, 50) - 0.25f) < 1.0f / 255.0f && grid_at(&gr, 120, 51) == 0.0f);
    grid_free(&gr);

    /* Giant circle whose leftmost edge crosses the view: compare with the
     * curve's own x per row (dense double sampling of the cubic). */
    nxc_path_reset(&p);
    const double R = 1.0e5, cx = R + 50.5, cy = 40.0;
    NXC_CHECK_OK(nxc_path_add_circle(&p, (float)cx, (float)cy, (float)R));
    NXC_CHECK_OK(raster_grid(&p, NXC_FILL_NONZERO, nxc_irect(0, 0, 100, 80), &gr));
    double worst = 0;
    for (int y = 0; y < 80; y++) {
        for (int x = 40; x < 60; x++) {
            double acc = 0;
            for (int sub = 0; sub < 16; sub++) {
                double yy = y + (sub + 0.5) / 16.0;
                /* Points left of center are 50.5 + R (1 - cos(theta)) ~ 50.5 + dy^2/(2R). */
                double xe = 50.5 + (yy - cy) * (yy - cy) / (2 * R);
                acc += fmax(0, fmin(1, x + 1 - xe));
            }
            worst = fmax(worst, fabs(acc / 16 - grid_at(&gr, x, y)));
        }
    }
    fprintf(stderr, "giant circle edge: max error %.4f\n", worst);
    NXC_CHECK(worst < 0.03);
    grid_free(&gr);

    /* Beyond the coordinate limit: refused, nothing delivered. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(-3e9f, 0, 3e9f + 50, 50)));
    grid_init(&gr, nxc_irect(0, 0, 100, 100));
    NXC_CHECK(nxc__raster_path(&p, 1.0f, 0, 0, NXC_FILL_NONZERO, nxc_irect(0, 0, 100, 100), grid_sink, &gr)
              == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(gr.calls == 0);
    grid_free(&gr);

    /* Huge scale also refuses rather than overflowing. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_circle(&p, 10, 10, 5));
    grid_init(&gr, nxc_irect(0, 0, 100, 100));
    NXC_CHECK(nxc__raster_path(&p, 1e30f, 0, 0, NXC_FILL_NONZERO, nxc_irect(0, 0, 100, 100), grid_sink, &gr)
              == NXC_ERR_UNSUPPORTED);
    /* Scale and offset (the font module's mapping). */
    NXC_CHECK_OK(nxc__raster_path(&p, 2.0f, 3.0f, 4.0f, NXC_FILL_NONZERO, nxc_irect(0, 0, 100, 100), grid_sink, &gr));
    NXC_CHECK(grid_at(&gr, 23, 24) == 1.0f && grid_at(&gr, 23, 13) == 0.0f);
    NXC_CHECK(fabs(grid_sum(&gr) - PI_D * 100) / (PI_D * 100) < 0.005);
    grid_free(&gr);
}

static void test_invalid(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 32, 32, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    nxc_point_t pts[32];
    uint8_t verbs[32];
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init(&p, pts, 32, verbs, 32));
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(2, 2, 20, 20)));
    grid_t gr;
    grid_init(&gr, nxc_irect(0, 0, 32, 32));
    nxc_irect_t cl = nxc_irect(0, 0, 32, 32);
    NXC_CHECK(nxc__raster_path(&p, NAN, 0, 0, NXC_FILL_NONZERO, cl, grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, 0.0f, 0, 0, NXC_FILL_NONZERO, cl, grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, -1.0f, 0, 0, NXC_FILL_NONZERO, cl, grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, 1.0f, INFINITY, 0, NXC_FILL_NONZERO, cl, grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, 1.0f, 0, 0, (nxc_fill_rule_t)7, cl, grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, 1.0f, 0, 0, NXC_FILL_NONZERO, nxc_irect(0, 0, -1, 4), grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, 1.0f, 0, 0, NXC_FILL_NONZERO, nxc_irect(0, 0, 70000, 4), grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(&p, 1.0f, 0, 0, NXC_FILL_NONZERO, cl, NULL, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__raster_path(NULL, 1.0f, 0, 0, NXC_FILL_NONZERO, cl, grid_sink, &gr) == NXC_ERR_INVALID_ARG);
    NXC_CHECK_OK(nxc__raster_path(&p, 1.0f, 0, 0, NXC_FILL_NONZERO, nxc_irect(5, 5, 0, 0), grid_sink, &gr));
    NXC_CHECK(gr.calls == 0);

    /* Corrupt paths are rejected, not walked. */
    nxc_path_t bad = p;
    bad.verbs[1] = 9;
    NXC_CHECK(nxc_fill_path(&c, &bad, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    bad.verbs[1] = NXC_VERB_LINE;
    bad.npts = 99;
    NXC_CHECK(nxc_fill_path(&c, &bad, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    bad.npts = p.npts - 1;
    NXC_CHECK(nxc_fill_path(&c, &bad, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    bad = p;
    bad.verbs = verbs + 1;                              /* starts with a LINE */
    bad.pts = pts + 1;
    bad.nverbs = p.nverbs - 1;
    bad.npts = p.npts - 1;
    NXC_CHECK(nxc_fill_path(&c, &bad, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    p.pts[2].x = NAN;
    NXC_CHECK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    p.pts[2].x = 22;
    NXC_CHECK(surf_sum(&s) == 0.0);                     /* failing draws drew nothing */

    /* Sticky building error propagates to drawing. */
    nxc_path_t q;
    NXC_CHECK_OK(nxc_path_init(&q, pts + 16, 16, verbs + 16, 16));
    NXC_CHECK(nxc_path_line_to(&q, NAN, 1) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_path(&c, &q, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_path(NULL, &p, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_path(&c, NULL, NXC_FILL_NONZERO, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_path(&c, &p, (nxc_fill_rule_t)3, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    /* A transparent fill validates too. */
    NXC_CHECK(nxc_fill_path(&c, &q, NXC_FILL_NONZERO, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK_OK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, 0));
    NXC_CHECK(nxc_fill_path_paint(&c, &p, NXC_FILL_NONZERO, NULL) == NXC_ERR_INVALID_ARG);
    nxc_paint_t bp = nxc_paint_linear(0, 0, NAN, 0, 0xFFFFFFFFu, 0xFF000000u);
    NXC_CHECK(nxc_fill_path_paint(&c, &p, NXC_FILL_NONZERO, &bp) == NXC_ERR_INVALID_ARG);

    /* Stroke parameters. */
    nxc_stroke_t st = { 2.0f, NXC_CAP_BUTT, NXC_JOIN_MITER, 4.0f };
    NXC_CHECK(nxc_stroke_path(&c, &p, NULL, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.width = NAN;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.width = -1;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.width = 0;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.width = 1e7f;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_UNSUPPORTED);
    st.width = 2;
    st.cap = (nxc_cap_t)9;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.cap = NXC_CAP_ROUND;
    st.join = (nxc_join_t)-1;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.join = NXC_JOIN_MITER;
    st.miter_limit = -2;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.miter_limit = INFINITY;
    NXC_CHECK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    st.miter_limit = 0;                                  /* default (4) */
    NXC_CHECK_OK(nxc_stroke_path(&c, &p, &st, NXC_RGB(255, 255, 255)));
    NXC_CHECK(nxc_stroke_path(&c, &q, &st, NXC_RGB(255, 255, 255)) == NXC_ERR_INVALID_ARG);
    /* Huge stroke widths stay bounded. */
    st.width = 5e5f;
    st.join = NXC_JOIN_ROUND;
    NXC_CHECK_OK(nxc_stroke_path(&c, &p, &st, NXC_RGB(0, 0, 255)));
    NXC_CHECK((nxc_test_pixel(&s, 31, 31) & 0xFF) == 0xFF);
    grid_free(&gr);
    nxc_surface_destroy(&s);
}

static void test_paint_fill(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 128, 64, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_translate(&c, 10, 0));
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 64, 32));
    NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(0, 4, 100, 56), 20));
    nxc_paint_t g = nxc_paint_linear(0, 0, 100, 0, NXC_RGB(255, 0, 0), NXC_RGB(0, 0, 255));
    NXC_CHECK_OK(nxc_fill_path_paint(&c, &p, NXC_FILL_NONZERO, &g));
    uint32_t l = nxc_test_pixel(&s, 12, 32), r = nxc_test_pixel(&s, 107, 32);
    NXC_CHECK(((l >> 16) & 0xFF) > 230 && (l & 0xFF) < 30);
    NXC_CHECK(((r >> 16) & 0xFF) < 30 && (r & 0xFF) > 230);
    NXC_CHECK(nxc_test_pixel(&s, 5, 32) == 0xFF000000u);
    nxc_paint_t sol = nxc_paint_solid(NXC_RGB(0, 255, 0));
    NXC_CHECK_OK(nxc_fill_path_paint(&c, &p, NXC_FILL_NONZERO, &sol));
    NXC_CHECK(nxc_test_pixel(&s, 60, 32) == 0xFF00FF00u);
    nxc_path_destroy(&p);
    nxc_surface_destroy(&s);
}

/* ============ Showcase ============ */

static void showcase(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 720, 480, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_paint_t bg = nxc_paint_linear(0, 0, 0, 480, NXC_RGB(28, 30, 40), NXC_RGB(14, 15, 22));
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 4096, 2048));
    NXC_CHECK_OK(nxc_path_add_rect(&p, nxc_rect(0, 0, 720, 480)));
    NXC_CHECK_OK(nxc_fill_path_paint(&c, &p, NXC_FILL_NONZERO, &bg));

    /* App-icon squircles 16..128 with gradients. */
    static const float sz[] = { 16, 24, 32, 48, 64, 96, 128 };
    float x = 16.0f;
    for (int i = 0; i < 7; i++) {
        nxc_path_reset(&p);
        NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(x, 150 - sz[i], sz[i], sz[i]), sz[i] * 0.225f));
        nxc_paint_t g = nxc_paint_linear(x, 150 - sz[i], x, 150, NXC_RGB(90, 200, 250), NXC_RGB(0, 90, 255));
        NXC_CHECK_OK(nxc_fill_path_paint(&c, &p, NXC_FILL_NONZERO, &g));
        x += sz[i] + 14.0f;
    }
    /* Squircle (filled) against a circular rrect outline, same radius. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(500, 14, 200, 200), 60));
    NXC_CHECK_OK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(255, 149, 0)));
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_rrect(&p, nxc_rect(500, 14, 200, 200), 60));
    nxc_stroke_t thin = { 1.0f, NXC_CAP_BUTT, NXC_JOIN_MITER, 4 };
    NXC_CHECK_OK(nxc_stroke_path(&c, &p, &thin, NXC_RGB(255, 255, 255)));

    /* Circles at fractional positions, both fill rules on a star. */
    for (int i = 0; i < 6; i++) {
        nxc_path_reset(&p);
        float r = 2.0f + (float)i * 3.3f;
        NXC_CHECK_OK(nxc_path_add_circle(&p, 24.3f + (float)i * 40.0f + r * 0.1f, 200.4f + (float)i * 0.17f, r));
        NXC_CHECK_OK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(240, 240, 245)));
    }
    for (int rule = 0; rule < 2; rule++) {
        nxc_path_reset(&p);
        for (int i = 0; i < 5; i++) {
            double a = -PI_D / 2 + i * 4 * PI_D / 5;
            float sx = (float)(290.5 + rule * 110 + 48 * cos(a)), sy = (float)(196.5 + 48 * sin(a));
            if (i == 0) NXC_CHECK_OK(nxc_path_move_to(&p, sx, sy));
            else NXC_CHECK_OK(nxc_path_line_to(&p, sx, sy));
        }
        NXC_CHECK_OK(nxc_path_close(&p));
        NXC_CHECK_OK(nxc_fill_path(&c, &p, (nxc_fill_rule_t)rule, NXC_RGB(255, 214, 10)));
    }

    /* Joins (rows) x caps, semi-transparent to reveal any overlap. */
    for (int join = 0; join < 3; join++) {
        for (int cap = 0; cap < 3; cap++) {
            nxc_path_reset(&p);
            float ox = 20.0f + (float)cap * 160.0f, oy = 270.0f + (float)join * 70.0f;
            NXC_CHECK_OK(nxc_path_move_to(&p, ox, oy + 40));
            NXC_CHECK_OK(nxc_path_line_to(&p, ox + 40, oy + 5));
            NXC_CHECK_OK(nxc_path_line_to(&p, ox + 70, oy + 45));
            NXC_CHECK_OK(nxc_path_quad_to(&p, ox + 100, oy - 10, ox + 130, oy + 35));
            nxc_stroke_t st = { 12.0f, (nxc_cap_t)cap, (nxc_join_t)join, 4 };
            NXC_CHECK_OK(nxc_stroke_path(&c, &p, &st, NXC_ARGB(200, 52, 199, 89)));
            NXC_CHECK_OK(nxc_stroke_path(&c, &p, &thin, NXC_RGB(255, 255, 255)));
        }
    }
    /* 1px spiral. */
    nxc_path_reset(&p);
    build_spiral(&p, 610.3f, 370.6f, 4, 7, 12);
    NXC_CHECK_OK(nxc_stroke_path(&c, &p, &thin, NXC_RGB(255, 255, 255)));
    /* Self-intersecting stroke (figure eight) with alpha. */
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_move_to(&p, 500, 300));
    NXC_CHECK_OK(nxc_path_cubic_to(&p, 560, 240, 560, 420, 500, 360));
    NXC_CHECK_OK(nxc_path_cubic_to(&p, 440, 300, 440, 360, 500, 300));
    nxc_stroke_t fat = { 10.0f, NXC_CAP_ROUND, NXC_JOIN_ROUND, 4 };
    NXC_CHECK_OK(nxc_stroke_path(&c, &p, &fat, NXC_ARGB(160, 255, 45, 85)));

    nxc_test_write_png(&s, nxc_test_path("raster_showcase.png"));
    write_zoom(&s, nxc_irect(500, 14, 80, 80), 6, "raster_zoom_squircle.png");
    write_zoom(&s, nxc_irect(14, 100, 120, 56), 6, "raster_zoom_icons.png");
    write_zoom(&s, nxc_irect(14, 260, 150, 70), 5, "raster_zoom_joins.png");
    write_zoom(&s, nxc_irect(10, 180, 240, 40), 5, "raster_zoom_circles.png");
    nxc_path_destroy(&p);
    nxc_surface_destroy(&s);
}

/* Large squircle and rrect side by side, for inspecting the outline. */
static void squircle_sheet(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_surface_create(&s, 1100, 560, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(255, 255, 255)));
    nxc_path_t p;
    NXC_CHECK_OK(nxc_path_init_alloc(&p, 256, 128));
    NXC_CHECK_OK(nxc_path_add_rrect(&p, nxc_rect(24, 24, 512, 512), 512 * 0.225f));
    NXC_CHECK_OK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(52, 120, 246)));
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_path_add_squircle(&p, nxc_rect(564, 24, 512, 512), 512 * 0.225f));
    NXC_CHECK_OK(nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(52, 120, 246)));
    nxc_test_write_png(&s, nxc_test_path("raster_squircle_vs_rrect.png"));
    nxc_path_destroy(&p);
    nxc_surface_destroy(&s);
}

int main(void) {
    nxc_test_init(1024, 1024);
    test_path_building();
    test_circle_area();
    test_polygon_exact();
    test_fill_rules();
    test_rounded();
    test_strokes();
    test_spiral();
    test_clipping();
    test_chunks();
    test_huge();
    test_invalid();
    test_paint_fill();
    showcase();
    squircle_sheet();
    fprintf(stderr, "errors recorded by the engine: %u\n", nxc_error_count());
    nxc_shutdown();
    /* After shutdown the engine refuses work explicitly. */
    nxc_path_t p;
    NXC_CHECK(nxc_path_init_alloc(&p, 8, 8) == NXC_ERR_STATE);
    return nxc_test_finish("raster");
}
