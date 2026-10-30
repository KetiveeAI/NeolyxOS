/*
 * NeolyxOS - NXCanvas paths, rasterizer and stroker
 *
 * Path building (lines, quadratic/cubic Beziers, rects, rounded rects,
 * continuous-curvature "squircles", ellipses and arcs), adaptive curve
 * flattening, the banded exact-area coverage rasterizer and the stroker.
 *
 * Rasterizer
 *   Every line segment deposits signed area and cover into a float
 *   accumulation buffer with one cell per pixel. A running prefix sum
 *   along each row then gives the exact fraction of every pixel covered
 *   by the path (nonzero: min(1, |winding area|), even-odd: the winding
 *   folded with a triangle wave). Tall paths are processed in bands of
 *   NXC_BAND_ROWS rows and wide ones in column chunks, so the buffer
 *   stays bounded by the scratch sizes chosen at init. Geometry left of
 *   a chunk is folded onto its left edge (it still contributes cover),
 *   geometry right of it is dropped, bands outside the clip are skipped.
 *   Rows are handed to the sink in increasing y within a column chunk;
 *   only targets wider than the init max_width have more than one chunk.
 *
 * Stroker
 *   The path is flattened in device space; every segment becomes a quad,
 *   every vertex a join wedge (miter, round, bevel) and every open end a
 *   cap (butt, round, square). All pieces are convex polygons wound the
 *   same way and accumulated together with the nonzero rule, so pieces
 *   that overlap do not darken twice. Nothing is allocated: the pieces
 *   are generated again for each band, culled by their bounds.
 *
 * Path conventions
 *   A segment verb with no current point starts a subpath implicitly
 *   (HTML canvas rules): on an empty path line_to(x, y) acts as
 *   move_to(x, y), quad_to/cubic_to start at their first control point;
 *   after a close, the new subpath starts at the closed subpath's first
 *   point. Consecutive moves collapse into the last one. The shape
 *   helpers emit closed subpaths running clockwise on screen.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"

/* ============ Tunables ============ */

#define NXC_KAPPA             0.5522847498f   /* cubic handle of a 90 degree arc (per radius) */
#define NXC_SQUIRCLE_EXTENT   1.528f          /* squircle corner start, in radii (full smoothing) */
#define NXC_FLAT_TOL          (1.0f / 64.0f)  /* max flattening deviation, device px (<= 0.2) */
#define NXC_FLAT_LEAF_MAX     64              /* segments emitted for one (sub)curve */
#define NXC_FLAT_MAX_DEPTH    16              /* curve subdivision depth bound */
#define NXC_FLAT_MAX_SPLITS   1024            /* subdivisions per curve */
#define NXC_FLAT_MAX_SEGS     8192u           /* hard cap on segments per curve */
#define NXC_ARC_MAX_STEPS     256             /* polygon steps of one round join/cap/dot */
#define NXC_COORD_LIMIT       1.0e9f          /* largest |device coordinate| rasterized */
#define NXC_STROKE_MAX_WIDTH  1.0e6f          /* widest stroke accepted */
#define NXC_MITER_MAX         1.0e4f          /* largest miter limit accepted */
#define NXC_STROKE_EPS2       1.0e-10f        /* squared length below which a segment is a point */
#define NXC_PATH_MAX_CAP      (1u << 26)      /* nxc_path_init_alloc capacity bound */
#define NXC_SURFACE_DIM_MAX   16384           /* largest surface side (see nxc_surface_wrap) */
#define NXC_RUN_GAP           8               /* zero pixels that split a coverage run */
#define NXC_SOLID_RUN_MIN     8               /* opaque run length worth a span_solid */

/* ============ Small geometry helpers ============ */

static inline nxc_point_t pt(float x, float y) {
    nxc_point_t q;
    q.x = x;
    q.y = y;
    return q;
}

static inline nxc_point_t pt_add(nxc_point_t a, nxc_point_t b) { return pt(a.x + b.x, a.y + b.y); }
static inline nxc_point_t pt_sub(nxc_point_t a, nxc_point_t b) { return pt(a.x - b.x, a.y - b.y); }
static inline nxc_point_t pt_mid(nxc_point_t a, nxc_point_t b) {
    return pt(0.5f * (a.x + b.x), 0.5f * (a.y + b.y));
}

/* Device position of a path point: p * scale + (tx, ty). */
static inline nxc_point_t pt_dev(nxc_point_t p, float scale, float tx, float ty) {
    return pt(p.x * scale + tx, p.y * scale + ty);
}

/* ============ Path building ============ */

/* Points consumed by a verb, or -1 for an unknown verb. */
static int32_t verb_points(uint8_t verb) {
    switch (verb) {
        case NXC_VERB_MOVE:  return 1;
        case NXC_VERB_LINE:  return 1;
        case NXC_VERB_QUAD:  return 2;
        case NXC_VERB_CUBIC: return 3;
        case NXC_VERB_CLOSE: return 0;
        default:             return -1;
    }
}

/* Record the first building failure on the path (sticky) and report it. */
static nxc_status_t path_error(nxc_path_t *p, nxc_status_t st, const char *where) {
    if (p->error == NXC_OK) p->error = st;
    return nxc__fail(st, where);
}

/* Entry check shared by the builders: NULL, sticky error, corrupt counts. */
static nxc_status_t path_begin(nxc_path_t *p, const char *where) {
    if (!p) return nxc__fail(NXC_ERR_INVALID_ARG, where);
    if (p->error != NXC_OK) return p->error;
    if (!p->pts || !p->verbs || p->npts > p->cap_pts || p->nverbs > p->cap_verbs) {
        return path_error(p, NXC_ERR_INVALID_ARG, where);
    }
    return NXC_OK;
}

/* Ensure room for npts more points and nverbs more verbs (sticky overflow). */
static nxc_status_t path_reserve(nxc_path_t *p, uint32_t npts, uint32_t nverbs, const char *where) {
    if ((uint64_t)p->npts + npts > p->cap_pts || (uint64_t)p->nverbs + nverbs > p->cap_verbs) {
        return path_error(p, NXC_ERR_OVERFLOW, where);
    }
    return NXC_OK;
}

static inline void put_verb(nxc_path_t *p, uint8_t v) { p->verbs[p->nverbs++] = v; }

static inline void put_pt(nxc_path_t *p, float x, float y) {
    p->pts[p->npts].x = x;
    p->pts[p->npts].y = y;
    p->npts++;
}

static inline uint8_t last_verb(const nxc_path_t *p) {
    return p->nverbs ? p->verbs[p->nverbs - 1] : (uint8_t)0xFF;
}

static int finite2(float a, float b) { return nxc__finitef(a) && nxc__finitef(b); }

/* First point of the subpath the last verb belongs to (scans back to its
 * MOVE; bounded by nverbs). Returns 0 when the path is inconsistent. */
static int subpath_start(const nxc_path_t *p, nxc_point_t *out) {
    uint32_t pi = p->npts;
    for (uint32_t v = p->nverbs; v > 0; v--) {
        int32_t k = verb_points(p->verbs[v - 1]);
        if (k < 0 || (uint32_t)k > pi) return 0;
        pi -= (uint32_t)k;
        if (p->verbs[v - 1] == NXC_VERB_MOVE) {
            *out = p->pts[pi];
            return 1;
        }
    }
    return 0;
}

/* 1 when the next segment needs an implicit move first. */
static uint32_t path_move_need(const nxc_path_t *p) {
    return (p->nverbs == 0 || last_verb(p) == NXC_VERB_CLOSE) ? 1u : 0u;
}

/* Give the next segment a current point (room already reserved): an empty
 * path starts at (x, y), a closed subpath restarts at its first point. */
static nxc_status_t path_implicit_move(nxc_path_t *p, float x, float y, const char *where) {
    if (p->nverbs == 0) {
        put_verb(p, NXC_VERB_MOVE);
        put_pt(p, x, y);
    } else if (last_verb(p) == NXC_VERB_CLOSE) {
        nxc_point_t s;
        if (!subpath_start(p, &s)) return path_error(p, NXC_ERR_INVALID_ARG, where);
        put_verb(p, NXC_VERB_MOVE);
        put_pt(p, s.x, s.y);
    }
    return NXC_OK;
}

/* Start a new subpath at (x, y), replacing a dangling move (room reserved). */
static void path_new_subpath(nxc_path_t *p, float x, float y) {
    if (last_verb(p) == NXC_VERB_MOVE && p->npts > 0) {
        p->pts[p->npts - 1].x = x;
        p->pts[p->npts - 1].y = y;
        return;
    }
    put_verb(p, NXC_VERB_MOVE);
    put_pt(p, x, y);
}

nxc_status_t nxc_path_init(nxc_path_t *p, nxc_point_t *pts, uint32_t cap_pts,
                           uint8_t *verbs, uint32_t cap_verbs) {
    if (!p) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_init");
    nxc__zero(p, sizeof(*p));
    if (!pts || !verbs || cap_pts == 0 || cap_verbs == 0) {
        p->error = NXC_ERR_INVALID_ARG;          /* later use fails safely */
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_init");
    }
    p->pts = pts;
    p->verbs = verbs;
    p->cap_pts = cap_pts;
    p->cap_verbs = cap_verbs;
    return NXC_OK;
}

nxc_status_t nxc_path_init_alloc(nxc_path_t *p, uint32_t cap_pts, uint32_t cap_verbs) {
    if (!p) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_init_alloc");
    nxc__zero(p, sizeof(*p));
    if (cap_pts == 0 || cap_verbs == 0 || cap_pts > NXC_PATH_MAX_CAP || cap_verbs > NXC_PATH_MAX_CAP) {
        p->error = NXC_ERR_INVALID_ARG;
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_init_alloc");
    }
    if (!nxc__ready()) {
        p->error = NXC_ERR_STATE;
        return nxc__fail(NXC_ERR_STATE, "nxc_path_init_alloc");
    }
    nxc_point_t *pts = (nxc_point_t *)nxc__alloc((uint64_t)cap_pts * sizeof(nxc_point_t));
    uint8_t *verbs = (uint8_t *)nxc__alloc(((uint64_t)cap_verbs + 15u) & ~(uint64_t)15u);
    if (!pts || !verbs) {
        nxc__free(pts);
        nxc__free(verbs);
        p->error = NXC_ERR_NO_MEMORY;
        return nxc__fail(NXC_ERR_NO_MEMORY, "nxc_path_init_alloc");
    }
    p->pts = pts;
    p->verbs = verbs;
    p->cap_pts = cap_pts;
    p->cap_verbs = cap_verbs;
    p->owned = 1;
    return NXC_OK;
}

void nxc_path_destroy(nxc_path_t *p) {
    if (!p) return;
    if (p->owned) {
        nxc__free(p->pts);
        nxc__free(p->verbs);
    }
    nxc__zero(p, sizeof(*p));
}

void nxc_path_reset(nxc_path_t *p) {
    if (!p) {
        (void)nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_reset");
        return;
    }
    p->npts = 0;
    p->nverbs = 0;
    p->error = NXC_OK;
}

nxc_status_t nxc_path_move_to(nxc_path_t *p, float x, float y) {
    const char *fn = "nxc_path_move_to";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(x, y)) return path_error(p, NXC_ERR_INVALID_ARG, fn);
    if (last_verb(p) != NXC_VERB_MOVE || p->npts == 0) {
        st = path_reserve(p, 1, 1, fn);
        if (st != NXC_OK) return st;
    }
    path_new_subpath(p, x, y);
    return NXC_OK;
}

nxc_status_t nxc_path_line_to(nxc_path_t *p, float x, float y) {
    const char *fn = "nxc_path_line_to";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(x, y)) return path_error(p, NXC_ERR_INVALID_ARG, fn);
    if (p->nverbs == 0) {
        /* No subpath yet: a line to (x, y) only establishes the point. */
        st = path_reserve(p, 1, 1, fn);
        if (st != NXC_OK) return st;
        path_new_subpath(p, x, y);
        return NXC_OK;
    }
    uint32_t need = path_move_need(p);
    st = path_reserve(p, 1 + need, 1 + need, fn);
    if (st != NXC_OK) return st;
    st = path_implicit_move(p, x, y, fn);
    if (st != NXC_OK) return st;
    put_verb(p, NXC_VERB_LINE);
    put_pt(p, x, y);
    return NXC_OK;
}

nxc_status_t nxc_path_quad_to(nxc_path_t *p, float cx, float cy, float x, float y) {
    const char *fn = "nxc_path_quad_to";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(cx, cy) || !finite2(x, y)) return path_error(p, NXC_ERR_INVALID_ARG, fn);
    uint32_t need = path_move_need(p);
    st = path_reserve(p, 2 + need, 1 + need, fn);
    if (st != NXC_OK) return st;
    st = path_implicit_move(p, cx, cy, fn);
    if (st != NXC_OK) return st;
    put_verb(p, NXC_VERB_QUAD);
    put_pt(p, cx, cy);
    put_pt(p, x, y);
    return NXC_OK;
}

nxc_status_t nxc_path_cubic_to(nxc_path_t *p, float c1x, float c1y,
                               float c2x, float c2y, float x, float y) {
    const char *fn = "nxc_path_cubic_to";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(c1x, c1y) || !finite2(c2x, c2y) || !finite2(x, y)) {
        return path_error(p, NXC_ERR_INVALID_ARG, fn);
    }
    uint32_t need = path_move_need(p);
    st = path_reserve(p, 3 + need, 1 + need, fn);
    if (st != NXC_OK) return st;
    st = path_implicit_move(p, c1x, c1y, fn);
    if (st != NXC_OK) return st;
    put_verb(p, NXC_VERB_CUBIC);
    put_pt(p, c1x, c1y);
    put_pt(p, c2x, c2y);
    put_pt(p, x, y);
    return NXC_OK;
}

nxc_status_t nxc_path_close(nxc_path_t *p) {
    const char *fn = "nxc_path_close";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    uint8_t lv = last_verb(p);
    /* Nothing to close: empty path, already closed, or a lone move. */
    if (p->nverbs == 0 || lv == NXC_VERB_CLOSE || lv == NXC_VERB_MOVE) return NXC_OK;
    st = path_reserve(p, 0, 1, fn);
    if (st != NXC_OK) return st;
    put_verb(p, NXC_VERB_CLOSE);
    return NXC_OK;
}

/* Validate a shape rect: finite, w/h >= 0, far corner representable. */
static int rect_ok(nxc_rect_t r) {
    return finite2(r.x, r.y) && finite2(r.w, r.h) && r.w >= 0.0f && r.h >= 0.0f &&
           finite2(r.x + r.w, r.y + r.h);
}

nxc_status_t nxc_path_add_rect(nxc_path_t *p, nxc_rect_t r) {
    const char *fn = "nxc_path_add_rect";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!rect_ok(r)) return path_error(p, NXC_ERR_INVALID_ARG, fn);
    st = path_reserve(p, 4, 5, fn);
    if (st != NXC_OK) return st;
    float x1 = r.x + r.w, y1 = r.y + r.h;
    path_new_subpath(p, r.x, r.y);
    put_verb(p, NXC_VERB_LINE); put_pt(p, x1, r.y);
    put_verb(p, NXC_VERB_LINE); put_pt(p, x1, y1);
    put_verb(p, NXC_VERB_LINE); put_pt(p, r.x, y1);
    put_verb(p, NXC_VERB_CLOSE);
    return NXC_OK;
}

/* ============ Rounded corners (rrect and squircle) ============ */

/*
 * A corner is described in a local frame: u runs along the incoming edge
 * (towards the corner, which is at the origin), v along the outgoing edge.
 * The curve leaves the incoming edge at (-ext, 0) and joins the outgoing
 * edge at (0, ext) through `ncubic` cubic Beziers (3 points each). The
 * same local shape is used, rotated, for all four corners.
 */
typedef struct {
    float   ext;
    int32_t ncubic;
    float   c[9][2];
} nxc_corner_shape_t;

typedef struct { float kx, ky, e1x, e1y, e2x, e2y; } nxc_corner_frame_t;

static inline nxc_point_t corner_map(const nxc_corner_frame_t *f, float u, float v) {
    return pt(f->kx + u * f->e1x + v * f->e2x, f->ky + u * f->e1y + v * f->e2y);
}

/* Mirror a local point across the corner diagonal (swaps the two edges). */
static inline void corner_mirror(const float in[2], float out[2]) {
    out[0] = -in[1];
    out[1] = -in[0];
}

/* Circular corner of radius r: one cubic with the standard kappa handles. */
static void corner_shape_circular(nxc_corner_shape_t *s, float r) {
    float h = r * (1.0f - NXC_KAPPA);
    s->ext = r;
    s->ncubic = 1;
    s->c[0][0] = -h;   s->c[0][1] = 0.0f;
    s->c[1][0] = 0.0f; s->c[1][1] = h;
    s->c[2][0] = 0.0f; s->c[2][1] = r;
}

/*
 * Continuous-curvature corner. The corner keeps a circular arc of radius
 * r (same center as a plain rounded corner) in its middle, and replaces
 * the abrupt edge-to-arc transitions by cubics whose first three control
 * points lie on the edge (zero curvature where they leave it) and whose
 * curvature where they meet the arc equals the arc's, so curvature is
 * continuous (G2) along the whole outline.
 *
 * `ext` is where the curve leaves the edge, r <= ext <= 1.528 r. The
 * smoothing s = (ext - r) / (0.528 r) sets how much of the arc each
 * transition replaces: theta = s * 45 degrees per side. At s = 1 the arc
 * vanishes and the two transitions meet on the diagonal; at s = 0 the
 * corner is the plain circular one. With the arc's tangent and curvature
 * fixed, the transition's second and third control points follow:
 *   P3 = arc point at theta, P2 = its tangent line meeting the edge,
 *   |P1 P2| = 1.5 k |P2 P3|^2 / sin(theta)   (curvature k at P3).
 */
static void corner_shape_squircle(nxc_corner_shape_t *s, float r, float ext) {
    float smooth = (ext - r) / ((NXC_SQUIRCLE_EXTENT - 1.0f) * r);
    if (!(smooth > 1.0e-3f)) {
        corner_shape_circular(s, r);
        return;
    }
    if (smooth > 1.0f) smooth = 1.0f;
    float th = smooth * (0.25f * NXC_PI);
    float st = nxc__sinf(th), ct = nxc__cosf(th);
    float delta = 0.5f * NXC_PI - 2.0f * th;           /* arc left in the middle */

    /* Arc cubic and the curvature at its ends (exact for the cubic, so the
     * transition matches the drawn arc, not just the ideal circle). */
    float k_arc = 0.0f, kappa_end = 1.0f / r;
    int has_arc = delta > 1.0e-3f;
    if (has_arc) {
        float q = 0.25f * delta;
        k_arc = (4.0f / 3.0f) * (nxc__sinf(q) / nxc__cosf(q)) * r;
        float sh = nxc__sinf(0.5f * delta);
        kappa_end = (2.0f / 3.0f) * (2.0f * r * sh * sh - k_arc * nxc__sinf(delta)) / (k_arc * k_arc);
    }

    float tan_half = st / (1.0f + ct);                  /* tan(theta / 2) */
    float sl = r * tan_half;                            /* |P2 P3| */
    float p3u = -r + r * st, p3v = r - r * ct;
    float p2u = p3u - sl * ct;
    float a = 1.5f * kappa_end * sl * sl / st;
    float p1u = p2u - a;
    if (p1u < -ext) p1u = -ext;                         /* keep P1 between P0 and P2 */

    float t[4][2] = { { -ext, 0.0f }, { p1u, 0.0f }, { p2u, 0.0f }, { p3u, p3v } };
    int32_t n = 0;
    s->ext = ext;
    /* Transition in. */
    for (int i = 1; i < 4; i++) { s->c[n][0] = t[i][0]; s->c[n][1] = t[i][1]; n++; }
    /* Arc, from theta to 90 - theta. */
    if (has_arc) {
        float a3[2];
        corner_mirror(t[3], a3);
        s->c[n][0] = p3u + k_arc * ct;   s->c[n][1] = p3v + k_arc * st;   n++;
        s->c[n][0] = a3[0] - k_arc * st; s->c[n][1] = a3[1] - k_arc * ct; n++;
        s->c[n][0] = a3[0];              s->c[n][1] = a3[1];              n++;
    }
    /* Transition out: the mirror image, traversed backwards. */
    for (int i = 2; i >= 0; i--) {
        corner_mirror(t[i], s->c[n]);
        n++;
    }
    s->ncubic = n / 3;
}

/* Append a closed rounded rect whose four corners use `shape`. */
static nxc_status_t path_add_rounded(nxc_path_t *p, nxc_rect_t r,
                                     const nxc_corner_shape_t *shape, const char *where) {
    uint32_t nc = (uint32_t)shape->ncubic;
    nxc_status_t st = path_reserve(p, 1 + 4 + 12 * nc, 1 + 4 + 4 * nc + 1, where);
    if (st != NXC_OK) return st;
    float x1 = r.x + r.w, y1 = r.y + r.h;
    /* Clockwise on screen: top-right, bottom-right, bottom-left, top-left. */
    const nxc_corner_frame_t fr[4] = {
        { x1,  r.y,  1.0f,  0.0f,  0.0f,  1.0f },
        { x1,  y1,   0.0f,  1.0f, -1.0f,  0.0f },
        { r.x, y1,  -1.0f,  0.0f,  0.0f, -1.0f },
        { r.x, r.y,  0.0f, -1.0f,  1.0f,  0.0f },
    };
    nxc_point_t cur = corner_map(&fr[3], 0.0f, shape->ext);
    path_new_subpath(p, cur.x, cur.y);
    for (int k = 0; k < 4; k++) {
        nxc_point_t in = corner_map(&fr[k], -shape->ext, 0.0f);
        if (in.x != cur.x || in.y != cur.y) {
            put_verb(p, NXC_VERB_LINE);
            put_pt(p, in.x, in.y);
        }
        for (uint32_t c = 0; c < nc; c++) {
            put_verb(p, NXC_VERB_CUBIC);
            for (int j = 0; j < 3; j++) {
                nxc_point_t q = corner_map(&fr[k], shape->c[c * 3 + j][0], shape->c[c * 3 + j][1]);
                put_pt(p, q.x, q.y);
                cur = q;
            }
        }
    }
    put_verb(p, NXC_VERB_CLOSE);
    return NXC_OK;
}

nxc_status_t nxc_path_add_rrect(nxc_path_t *p, nxc_rect_t r, float radius) {
    const char *fn = "nxc_path_add_rrect";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!rect_ok(r) || !nxc__finitef(radius) || radius < 0.0f) {
        return path_error(p, NXC_ERR_INVALID_ARG, fn);
    }
    float rad = nxc__minf(radius, 0.5f * nxc__minf(r.w, r.h));
    if (!(rad > 0.0f)) return nxc_path_add_rect(p, r);
    nxc_corner_shape_t shape;
    corner_shape_circular(&shape, rad);
    return path_add_rounded(p, r, &shape, fn);
}

nxc_status_t nxc_path_add_squircle(nxc_path_t *p, nxc_rect_t r, float radius) {
    const char *fn = "nxc_path_add_squircle";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!rect_ok(r) || !nxc__finitef(radius) || radius < 0.0f) {
        return path_error(p, NXC_ERR_INVALID_ARG, fn);
    }
    float half = 0.5f * nxc__minf(r.w, r.h);
    float rad = nxc__minf(radius, half);
    if (!(rad > 0.0f)) return nxc_path_add_rect(p, r);
    /* The smoothing gives way when the corners would meet: every corner
     * uses the same extent so the shape stays symmetric. */
    float ext = nxc__minf(NXC_SQUIRCLE_EXTENT * rad, half);
    nxc_corner_shape_t shape;
    corner_shape_squircle(&shape, rad, ext);
    return path_add_rounded(p, r, &shape, fn);
}

nxc_status_t nxc_path_add_ellipse(nxc_path_t *p, float cx, float cy, float rx, float ry) {
    const char *fn = "nxc_path_add_ellipse";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(cx, cy) || !finite2(rx, ry) || rx < 0.0f || ry < 0.0f ||
        !finite2(cx + rx, cy + ry) || !finite2(cx - rx, cy - ry)) {
        return path_error(p, NXC_ERR_INVALID_ARG, fn);
    }
    if (rx == 0.0f || ry == 0.0f) return NXC_OK;        /* degenerate: nothing to add */
    st = path_reserve(p, 13, 6, fn);
    if (st != NXC_OK) return st;
    float kx = rx * NXC_KAPPA, ky = ry * NXC_KAPPA;
    path_new_subpath(p, cx + rx, cy);
    put_verb(p, NXC_VERB_CUBIC);
    put_pt(p, cx + rx, cy + ky); put_pt(p, cx + kx, cy + ry); put_pt(p, cx, cy + ry);
    put_verb(p, NXC_VERB_CUBIC);
    put_pt(p, cx - kx, cy + ry); put_pt(p, cx - rx, cy + ky); put_pt(p, cx - rx, cy);
    put_verb(p, NXC_VERB_CUBIC);
    put_pt(p, cx - rx, cy - ky); put_pt(p, cx - kx, cy - ry); put_pt(p, cx, cy - ry);
    put_verb(p, NXC_VERB_CUBIC);
    put_pt(p, cx + kx, cy - ry); put_pt(p, cx + rx, cy - ky); put_pt(p, cx + rx, cy);
    put_verb(p, NXC_VERB_CLOSE);
    return NXC_OK;
}

nxc_status_t nxc_path_add_circle(nxc_path_t *p, float cx, float cy, float r) {
    return nxc_path_add_ellipse(p, cx, cy, r, r);
}

/*
 * Arc of radius r around (cx, cy) from angle a0 to a1. Angles are measured
 * clockwise on screen (0 = +x, pi/2 = +y). The sweep is a1 - a0: positive
 * runs clockwise, negative counter-clockwise, and |sweep| is limited to a
 * full turn. Each cubic spans at most 90 degrees.
 */
nxc_status_t nxc_path_add_arc(nxc_path_t *p, float cx, float cy, float r,
                              float a0, float a1) {
    const char *fn = "nxc_path_add_arc";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(cx, cy) || !nxc__finitef(r) || r < 0.0f || !finite2(a0, a1) ||
        nxc__absf(a0) > 1.0e6f || nxc__absf(a1) > 1.0e6f ||
        !finite2(cx + r, cy + r) || !finite2(cx - r, cy - r)) {
        return path_error(p, NXC_ERR_INVALID_ARG, fn);
    }
    float sweep = nxc__clampf(a1 - a0, -2.0f * NXC_PI, 2.0f * NXC_PI);
    int32_t nseg = 0;
    if (r > 0.0f && sweep != 0.0f) {
        nseg = (int32_t)nxc__ceilf(nxc__absf(sweep) / (0.5f * NXC_PI) - 1.0e-4f);
        if (nseg < 1) nseg = 1;
        if (nseg > 4) nseg = 4;
    }
    float sx = cx + r * nxc__cosf(a0), sy = cy + r * nxc__sinf(a0);
    uint32_t need = (p->nverbs > 0) ? path_move_need(p) : 0u;
    st = path_reserve(p, 1 + need + 3u * (uint32_t)nseg, 1 + need + (uint32_t)nseg, fn);
    if (st != NXC_OK) return st;
    if (p->nverbs == 0) {
        put_verb(p, NXC_VERB_MOVE);
        put_pt(p, sx, sy);
    } else {
        st = path_implicit_move(p, sx, sy, fn);
        if (st != NXC_OK) return st;
        nxc_point_t lp = p->pts[p->npts - 1];
        if (lp.x != sx || lp.y != sy) {
            put_verb(p, NXC_VERB_LINE);
            put_pt(p, sx, sy);
        }
    }
    if (nseg == 0) return NXC_OK;
    float step = sweep / (float)nseg;
    float q = 0.25f * step;
    float h = (4.0f / 3.0f) * (nxc__sinf(q) / nxc__cosf(q)) * r;   /* signed handle */
    float ca = nxc__cosf(a0), sa = nxc__sinf(a0);
    float px = sx, py = sy;
    for (int32_t i = 1; i <= nseg; i++) {
        float ang = (i == nseg) ? a0 + sweep : a0 + step * (float)i;
        float cb = nxc__cosf(ang), sb = nxc__sinf(ang);
        float ex = cx + r * cb, ey = cy + r * sb;
        put_verb(p, NXC_VERB_CUBIC);
        put_pt(p, px - h * sa, py + h * ca);
        put_pt(p, ex + h * sb, ey - h * cb);
        put_pt(p, ex, ey);
        px = ex; py = ey; ca = cb; sa = sb;
    }
    return NXC_OK;
}

nxc_status_t nxc_path_transform(nxc_path_t *p, float sx, float sy, float tx, float ty) {
    const char *fn = "nxc_path_transform";
    nxc_status_t st = path_begin(p, fn);
    if (st != NXC_OK) return st;
    if (!finite2(sx, sy) || !finite2(tx, ty)) return nxc__fail(NXC_ERR_INVALID_ARG, fn);
    /* Check every result first so a failing transform leaves the path intact. */
    for (uint32_t i = 0; i < p->npts; i++) {
        if (!finite2(p->pts[i].x * sx + tx, p->pts[i].y * sy + ty)) {
            return nxc__fail(NXC_ERR_INVALID_ARG, fn);
        }
    }
    for (uint32_t i = 0; i < p->npts; i++) {
        p->pts[i].x = p->pts[i].x * sx + tx;
        p->pts[i].y = p->pts[i].y * sy + ty;
    }
    return NXC_OK;
}

nxc_status_t nxc_path_bounds(const nxc_path_t *p, nxc_rect_t *out) {
    if (!p || !out) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_bounds");
    *out = nxc_rect(0.0f, 0.0f, 0.0f, 0.0f);
    if (p->error != NXC_OK) return p->error;
    if (p->npts > p->cap_pts || (p->npts > 0 && !p->pts)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_path_bounds");
    }
    if (p->npts == 0) return NXC_OK;
    /* Control-point bounds: they contain every curve of the path. */
    float x0 = p->pts[0].x, y0 = p->pts[0].y, x1 = x0, y1 = y0;
    for (uint32_t i = 1; i < p->npts; i++) {
        x0 = nxc__minf(x0, p->pts[i].x); x1 = nxc__maxf(x1, p->pts[i].x);
        y0 = nxc__minf(y0, p->pts[i].y); y1 = nxc__maxf(y1, p->pts[i].y);
    }
    *out = nxc_rect(x0, y0, x1 - x0, y1 - y0);
    return NXC_OK;
}

/* ============ Path validation and walking ============ */

typedef struct { float x0, y0, x1, y1; } nxc_bounds_t;

/*
 * Check a path before rasterizing: verbs known, points consistent with
 * the verbs, a move before the first segment, every point finite and
 * within NXC_COORD_LIMIT once mapped to device space. Computes the device
 * bounds of all points. *empty is set when there is nothing to draw.
 */
static nxc_status_t path_scan(const nxc_path_t *p, float scale, float tx, float ty,
                              nxc_bounds_t *bb, int *empty) {
    *empty = 1;
    if (!p) return NXC_ERR_INVALID_ARG;
    if (p->error != NXC_OK) return p->error;
    if (p->nverbs == 0) return NXC_OK;
    if (!p->pts || !p->verbs || p->npts > p->cap_pts || p->nverbs > p->cap_verbs) {
        return NXC_ERR_INVALID_ARG;
    }
    uint32_t pi = 0;
    int have_move = 0, first = 1;
    for (uint32_t v = 0; v < p->nverbs; v++) {
        uint8_t verb = p->verbs[v];
        int32_t k = verb_points(verb);
        if (k < 0) return NXC_ERR_INVALID_ARG;
        if (verb == NXC_VERB_MOVE) have_move = 1;
        else if (!have_move) return NXC_ERR_INVALID_ARG;
        if ((uint64_t)pi + (uint32_t)k > p->npts) return NXC_ERR_INVALID_ARG;
        for (int32_t j = 0; j < k; j++) {
            nxc_point_t s = p->pts[pi + (uint32_t)j];
            if (!finite2(s.x, s.y)) return NXC_ERR_INVALID_ARG;
            nxc_point_t d = pt_dev(s, scale, tx, ty);
            if (!(nxc__absf(d.x) <= NXC_COORD_LIMIT) || !(nxc__absf(d.y) <= NXC_COORD_LIMIT)) {
                return NXC_ERR_UNSUPPORTED;
            }
            if (first) {
                bb->x0 = bb->x1 = d.x;
                bb->y0 = bb->y1 = d.y;
                first = 0;
            } else {
                bb->x0 = nxc__minf(bb->x0, d.x); bb->x1 = nxc__maxf(bb->x1, d.x);
                bb->y0 = nxc__minf(bb->y0, d.y); bb->y1 = nxc__maxf(bb->y1, d.y);
            }
        }
        pi += (uint32_t)k;
    }
    if (pi != p->npts) return NXC_ERR_INVALID_ARG;
    *empty = 0;
    return NXC_OK;
}

/* Callbacks a path walk drives; points are in device space. */
typedef struct {
    void (*begin)(void *ctx, nxc_point_t p);
    void (*line)(void *ctx, nxc_point_t a, nxc_point_t b);
    void (*cubic)(void *ctx, nxc_point_t a, nxc_point_t c1, nxc_point_t c2, nxc_point_t b);
    void (*end)(void *ctx, nxc_point_t cur, nxc_point_t start, int closed);
} nxc_walk_ops_t;

/*
 * Walk a path validated by path_scan, mapping points to device space.
 * Quads are raised to cubics (exact). Every subpath is reported with
 * begin ... end; a segment after a close restarts at the closed start.
 */
static void path_walk(const nxc_path_t *p, float scale, float tx, float ty,
                      const nxc_walk_ops_t *ops, void *ctx) {
    uint32_t pi = 0;
    int open = 0;
    nxc_point_t cur = pt(0.0f, 0.0f), start = cur;
    for (uint32_t v = 0; v < p->nverbs; v++) {
        uint8_t verb = p->verbs[v];
        int32_t k = verb_points(verb);
        if (k < 0 || (uint64_t)pi + (uint32_t)k > p->npts) return;
        if (verb == NXC_VERB_MOVE) {
            if (open) ops->end(ctx, cur, start, 0);
            start = cur = pt_dev(p->pts[pi], scale, tx, ty);
            ops->begin(ctx, start);
            open = 1;
        } else if (verb == NXC_VERB_CLOSE) {
            if (open) ops->end(ctx, cur, start, 1);
            cur = start;
            open = 0;
        } else {
            if (!open) {
                ops->begin(ctx, start);
                cur = start;
                open = 1;
            }
            if (verb == NXC_VERB_LINE) {
                nxc_point_t b = pt_dev(p->pts[pi], scale, tx, ty);
                ops->line(ctx, cur, b);
                cur = b;
            } else if (verb == NXC_VERB_QUAD) {
                nxc_point_t c = pt_dev(p->pts[pi], scale, tx, ty);
                nxc_point_t b = pt_dev(p->pts[pi + 1], scale, tx, ty);
                nxc_point_t c1 = pt(cur.x + (2.0f / 3.0f) * (c.x - cur.x), cur.y + (2.0f / 3.0f) * (c.y - cur.y));
                nxc_point_t c2 = pt(b.x + (2.0f / 3.0f) * (c.x - b.x), b.y + (2.0f / 3.0f) * (c.y - b.y));
                ops->cubic(ctx, cur, c1, c2, b);
                cur = b;
            } else {
                nxc_point_t c1 = pt_dev(p->pts[pi], scale, tx, ty);
                nxc_point_t c2 = pt_dev(p->pts[pi + 1], scale, tx, ty);
                nxc_point_t b = pt_dev(p->pts[pi + 2], scale, tx, ty);
                ops->cubic(ctx, cur, c1, c2, b);
                cur = b;
            }
        }
        pi += (uint32_t)k;
    }
    if (open) ops->end(ctx, cur, start, 0);
}

/* ============ Band accumulation ============ */

typedef struct {
    float   *acc;                    /* rows x stride accumulation cells */
    int32_t  stride;                 /* w + 2: two guard cells per row */
    int32_t  w, rows;                /* chunk width, band height */
    int32_t  x0, y0;                 /* device position of cell (0, 0) */
    float    left, right, top, bot;  /* band window in device space */
    int32_t  rmin[NXC_BAND_ROWS];    /* first touched cell per row */
    int32_t  rmax[NXC_BAND_ROWS];    /* last touched cell per row */
} nxc_band_t;

/*
 * Deposit the part of an edge inside band row r running from x = xa to
 * x = xb (both within [0, w]) with signed height dy. Each crossed cell
 * receives the area between the edge and the cell's right side; the rest
 * of the edge's height goes to the following cell, so the row's prefix
 * sum carries the cover to every pixel further right.
 */
static void deposit_row(nxc_band_t *b, int32_t r, float xa, float xb, float dy) {
    float lo = xa < xb ? xa : xb;
    float hi = xa < xb ? xb : xa;
    int32_t i0 = (int32_t)lo;
    int32_t i1 = (int32_t)hi;
    if (i1 > b->w) i1 = b->w;
    if (i1 < 0) i1 = 0;
    if (i0 > i1) i0 = i1;
    if (i0 < 0) i0 = 0;
    float *row = b->acc + (int64_t)r * b->stride;
    if (i0 < b->rmin[r]) b->rmin[r] = i0;
    if (i1 + 1 > b->rmax[r]) b->rmax[r] = i1 + 1;
    if (i0 == i1) {
        float f = 0.5f * (lo + hi) - (float)i0;        /* mean x within the cell */
        row[i0] += dy * (1.0f - f);
        row[i0 + 1] += dy * f;
        return;
    }
    float k = dy / (hi - lo);                          /* height per unit of x */
    float w0 = (float)(i0 + 1) - lo;
    float h0 = k * w0;
    float area0 = 0.5f * h0 * w0;
    row[i0] += area0;
    float carry = h0 - area0;
    float half = 0.5f * k;
    for (int32_t i = i0 + 1; i < i1; i++) {
        row[i] += carry + half;
        carry = half;
    }
    float w1 = hi - (float)i1;
    float h1 = k * w1;
    float area1 = 0.5f * h1 * w1;
    row[i1] += carry + h1 - area1;
    row[i1 + 1] += area1;
}

/* Walk an edge in band-local coordinates (ya < yb inside [0, rows],
 * x inside [0, w]) one row at a time. dir is the original y direction. */
static void walk_line(nxc_band_t *b, float xa, float ya, float xb, float yb, float dir) {
    if (!(yb > ya)) return;
    float dxdy = (xb - xa) / (yb - ya);
    float xlo = nxc__minf(xa, xb), xhi = nxc__maxf(xa, xb);
    int32_t r = (int32_t)ya;
    if (r < 0) r = 0;
    float yc = ya, xc = xa;
    for (; r < b->rows; r++) {
        float yn = (float)(r + 1);
        float xn;
        int last = 0;
        if (yn >= yb) {
            yn = yb;
            xn = xb;
            last = 1;
        } else {
            xn = nxc__clampf(xa + (yn - ya) * dxdy, xlo, xhi);
        }
        if (yn > yc) deposit_row(b, r, xc, xn, dir * (yn - yc));
        if (last) return;
        xc = xn;
        yc = yn;
    }
}

/*
 * Add one edge (device space) to the band: clip it to the band's rows,
 * fold the part left of the chunk onto x = 0 (it still contributes its
 * cover), drop the part right of it, and walk the rest. The clipping is
 * done in double so far-away endpoints keep their precision.
 */
static void band_line(nxc_band_t *b, float ax, float ay, float bx, float by) {
    if (ay == by) return;
    if ((ay <= b->top && by <= b->top) || (ay >= b->bot && by >= b->bot)) return;
    if (ax >= b->right && bx >= b->right) return;
    double x0 = (double)ax - b->x0, y0 = (double)ay - b->y0;
    double x1 = (double)bx - b->x0, y1 = (double)by - b->y0;
    float dir = 1.0f;
    if (y0 > y1) {
        double t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        dir = -1.0f;
    }
    double h = (double)b->rows, w = (double)b->w;
    double dxdy = (x1 - x0) / (y1 - y0);
    double ya = y0 > 0.0 ? y0 : 0.0;
    double yb = y1 < h ? y1 : h;
    if (!(yb > ya)) return;
    double xa = (ya == y0) ? x0 : x0 + (ya - y0) * dxdy;
    double xb = (yb == y1) ? x1 : x0 + (yb - y0) * dxdy;

    /* Breakpoints in y order: the ends plus crossings of x = 0 and x = w. */
    double py[4], px[4];
    int32_t n = 0;
    py[n] = ya; px[n] = xa; n++;
    double cy[2], cx[2];
    int32_t nc = 0;
    if ((xa < 0.0) != (xb < 0.0)) {
        cy[nc] = ya + (0.0 - xa) * (yb - ya) / (xb - xa);
        cx[nc] = 0.0;
        nc++;
    }
    if ((xa < w) != (xb < w)) {
        cy[nc] = ya + (w - xa) * (yb - ya) / (xb - xa);
        cx[nc] = w;
        nc++;
    }
    if (nc == 2 && cy[0] > cy[1]) {
        double t = cy[0]; cy[0] = cy[1]; cy[1] = t;
        t = cx[0]; cx[0] = cx[1]; cx[1] = t;
    }
    for (int32_t i = 0; i < nc; i++) {
        py[n] = cy[i] < ya ? ya : (cy[i] > yb ? yb : cy[i]);
        px[n] = cx[i];
        n++;
    }
    py[n] = yb; px[n] = xb; n++;

    for (int32_t i = 0; i + 1 < n; i++) {
        double ys = py[i], ye = py[i + 1];
        if (!(ye > ys)) continue;
        double xs = px[i], xe = px[i + 1];
        double xm = 0.5 * (xs + xe);
        if (xm >= w) continue;                         /* right of the chunk: invisible */
        if (xm <= 0.0) { xs = 0.0; xe = 0.0; }         /* left of it: pure cover */
        xs = xs < 0.0 ? 0.0 : (xs > w ? w : xs);
        xe = xe < 0.0 ? 0.0 : (xe > w ? w : xe);
        walk_line(b, (float)xs, (float)ys, (float)xe, (float)ye, dir);
    }
}

/* Coverage byte from an accumulated winding area. */
static inline uint32_t cov_from_acc(float s, int evenodd) {
    float v = s < 0.0f ? -s : s;
    if (evenodd) {
        v = v - 2.0f * nxc__floorf(v * 0.5f);          /* fold: 0..2 */
        if (v > 1.0f) v = 2.0f - v;
    }
    if (v >= 1.0f) return 255u;
    if (!(v > 0.0f)) return 0u;
    return (uint32_t)(v * 255.0f + 0.5f);
}

/* Hand the non-empty runs of cov[a..end) to the sink; zero gaps longer
 * than NXC_RUN_GAP split a run. */
static void emit_runs(const uint8_t *cov, int32_t a, int32_t end, int32_t y, int32_t x0,
                      nxc__cov_sink_fn sink, void *ctx) {
    int32_t x = a;
    while (x < end) {
        while (x < end && cov[x] == 0) x++;
        if (x >= end) break;
        int32_t start = x, last = x;
        while (x < end) {
            if (cov[x]) last = x;
            else if (x - last > NXC_RUN_GAP) break;
            x++;
        }
        sink(ctx, y, x0 + start, cov + start, last - start + 1);
        x = last + 1;
    }
}

/* Turn the band's rows into coverage for the sink and clear every touched
 * cell, so the buffer is all zero again for the next band or call. */
static void band_resolve(nxc_band_t *b, int evenodd, uint8_t *cov,
                         nxc__cov_sink_fn sink, void *ctx) {
    for (int32_t r = 0; r < b->rows; r++) {
        int32_t a = b->rmin[r], e = b->rmax[r];
        if (a > e) continue;                           /* no edge in this row */
        float *row = b->acc + (int64_t)r * b->stride;
        int32_t vis = e < b->w ? e : b->w - 1;         /* last visible touched cell */
        float s = 0.0f;
        for (int32_t x = a; x <= vis; x++) {
            s += row[x];
            row[x] = 0.0f;
            cov[x] = (uint8_t)cov_from_acc(s, evenodd);
        }
        for (int32_t x = (vis + 1 > a ? vis + 1 : a); x <= e; x++) row[x] = 0.0f;
        int32_t end = vis + 1;
        if (end < b->w) {
            /* Past the last edge the winding is constant (the path goes on
             * beyond the chunk's right side when it is not zero). */
            uint32_t tail = cov_from_acc(s, evenodd);
            if (tail) {
                for (int32_t x = end; x < b->w; x++) cov[x] = (uint8_t)tail;
                end = b->w;
            }
        }
        emit_runs(cov, a, end, b->y0 + r, b->x0, sink, ctx);
    }
}

typedef void (*nxc_emit_fn)(nxc_band_t *b, void *ctx);

/*
 * Drive a rasterization: the region is the clip intersected with the
 * geometry bounds; it is cut into column chunks that fit the scratch
 * buffers and bands of NXC_BAND_ROWS rows. For every band `emit` deposits
 * the geometry's edges, then the band is resolved into the sink.
 */
static nxc_status_t raster_run(nxc_irect_t clip, nxc_bounds_t gb, int evenodd,
                               nxc_emit_fn emit, void *ectx,
                               nxc__cov_sink_fn sink, void *sctx) {
    uint64_t acc_bytes = 0, cov_bytes = 0;
    float *acc = (float *)nxc__scratch(NXC_SCRATCH_ACCUM, &acc_bytes);
    uint8_t *cov = (uint8_t *)nxc__scratch(NXC_SCRATCH_COV, &cov_bytes);
    if (!acc || !cov) return NXC_ERR_STATE;
    int64_t chunk = (int64_t)(acc_bytes / sizeof(float)) / NXC_BAND_ROWS - 2;
    if (chunk > (int64_t)cov_bytes) chunk = (int64_t)cov_bytes;
    if (chunk < 1) return NXC_ERR_STATE;

    /* Region in double first: geometry bounds may lie far outside the clip. */
    double cx0 = (double)clip.x, cx1 = (double)clip.x + (double)clip.w;
    double cy0 = (double)clip.y, cy1 = (double)clip.y + (double)clip.h;
    double gx0 = (double)nxc__floorf(gb.x0), gx1 = (double)nxc__ceilf(gb.x1);
    double gy0 = (double)nxc__floorf(gb.y0), gy1 = (double)nxc__ceilf(gb.y1);
    double lx = gx0 > cx0 ? gx0 : cx0, hx = gx1 < cx1 ? gx1 : cx1;
    double ly = gy0 > cy0 ? gy0 : cy0, hy = gy1 < cy1 ? gy1 : cy1;
    if (!(hx > lx) || !(hy > ly)) return NXC_OK;
    int64_t rx0 = (int64_t)lx, rx1 = (int64_t)hx;
    int64_t ry0 = (int64_t)ly, ry1 = (int64_t)hy;

    nxc_band_t band;
    band.acc = acc;
    for (int64_t cx = rx0; cx < rx1; cx += chunk) {
        int64_t w = rx1 - cx < chunk ? rx1 - cx : chunk;
        band.w = (int32_t)w;
        band.stride = (int32_t)w + 2;
        band.x0 = (int32_t)cx;
        band.left = (float)cx;
        band.right = (float)(cx + w);
        for (int64_t by = ry0; by < ry1; by += NXC_BAND_ROWS) {
            int64_t rows = ry1 - by < NXC_BAND_ROWS ? ry1 - by : NXC_BAND_ROWS;
            band.rows = (int32_t)rows;
            band.y0 = (int32_t)by;
            band.top = (float)by;
            band.bot = (float)(by + rows);
            for (int32_t r = 0; r < band.rows; r++) {
                band.rmin[r] = 0x7FFFFFFF;
                band.rmax[r] = -1;
            }
            emit(&band, ectx);
            band_resolve(&band, evenodd, cov, sink, sctx);
        }
    }
    return NXC_OK;
}

/* ============ Curve flattening ============ */

/* Receives flattened pieces; `culled` gets sub-curves that are invisible
 * in the current window (left = 1 when entirely left of it). */
typedef struct {
    float  top, bot, left, right;    /* window (device px) */
    float  margin;                   /* geometry extent around the curve */
    float  turn_step;                /* > 0: max turning per segment (strokes) */
    void (*seg)(void *ctx, nxc_point_t a, nxc_point_t b);
    void (*culled)(void *ctx, nxc_point_t a, nxc_point_t b, int left);
    void  *ctx;
} nxc_flat_t;

typedef struct {
    nxc_point_t p[4];
    int32_t     depth;
} nxc_cubic_t;

/* Split a cubic at t = 1/2 (de Casteljau). */
static void cubic_split(const nxc_point_t *q, nxc_point_t *l, nxc_point_t *r) {
    nxc_point_t m01 = pt_mid(q[0], q[1]), m12 = pt_mid(q[1], q[2]), m23 = pt_mid(q[2], q[3]);
    nxc_point_t m012 = pt_mid(m01, m12), m123 = pt_mid(m12, m23);
    nxc_point_t m = pt_mid(m012, m123);
    l[0] = q[0]; l[1] = m01; l[2] = m012; l[3] = m;
    r[0] = m; r[1] = m123; r[2] = m23; r[3] = q[3];
}

/* Total turning of the control polygon (bounds the curve's turning). */
static float control_turn(const nxc_point_t *q) {
    nxc_point_t legs[3];
    int32_t n = 0;
    for (int i = 0; i < 3; i++) {
        nxc_point_t d = pt_sub(q[i + 1], q[i]);
        if (d.x * d.x + d.y * d.y > 1.0e-12f) legs[n++] = d;
    }
    float turn = 0.0f;
    for (int32_t i = 0; i + 1 < n; i++) {
        float dot = legs[i].x * legs[i + 1].x + legs[i].y * legs[i + 1].y;
        float crs = legs[i].x * legs[i + 1].y - legs[i].y * legs[i + 1].x;
        turn += nxc__atan2f(nxc__absf(crs), dot);
    }
    return turn;
}

/*
 * Flatten a device-space cubic. With uniform parameter steps h = 1/n the
 * chord error is at most h^2/8 * max|B''| and |B''| <= 6 max(|d1|, |d2|)
 * (the control polygon's second differences), so n = sqrt(0.75 M / tol)
 * keeps the deviation under NXC_FLAT_TOL. Strokes also bound the turning
 * per segment so their offset outline stays within tolerance. Sub-curves
 * outside the window are culled; curves needing more than
 * NXC_FLAT_LEAF_MAX segments are split (explicit stack, bounded depth and
 * split count) so only the visible parts of huge curves get refined.
 * Pieces are emitted in curve order; at most about NXC_FLAT_MAX_SEGS.
 */
static void flatten_cubic(const nxc_flat_t *f, nxc_point_t p0, nxc_point_t p1,
                          nxc_point_t p2, nxc_point_t p3) {
    nxc_cubic_t stack[NXC_FLAT_MAX_DEPTH + 2];
    int32_t sp = 0;
    uint32_t budget = NXC_FLAT_MAX_SEGS;
    int32_t splits = NXC_FLAT_MAX_SPLITS;
    stack[0].p[0] = p0; stack[0].p[1] = p1; stack[0].p[2] = p2; stack[0].p[3] = p3;
    stack[0].depth = 0;
    sp = 1;
    while (sp > 0) {
        nxc_cubic_t cv = stack[--sp];
        const nxc_point_t *q = cv.p;
        float mnx = q[0].x, mxx = q[0].x, mny = q[0].y, mxy = q[0].y;
        for (int i = 1; i < 4; i++) {
            mnx = nxc__minf(mnx, q[i].x); mxx = nxc__maxf(mxx, q[i].x);
            mny = nxc__minf(mny, q[i].y); mxy = nxc__maxf(mxy, q[i].y);
        }
        float m = f->margin;
        if (mxy + m <= f->top || mny - m >= f->bot || mnx - m >= f->right) {
            f->culled(f->ctx, q[0], q[3], 0);
            continue;
        }
        if (mxx + m <= f->left) {
            f->culled(f->ctx, q[0], q[3], 1);
            continue;
        }
        float d1x = q[0].x - 2.0f * q[1].x + q[2].x, d1y = q[0].y - 2.0f * q[1].y + q[2].y;
        float d2x = q[1].x - 2.0f * q[2].x + q[3].x, d2y = q[1].y - 2.0f * q[2].y + q[3].y;
        float m2 = nxc__maxf(d1x * d1x + d1y * d1y, d2x * d2x + d2y * d2y);
        float segs = nxc__sqrtf(0.75f * nxc__sqrtf(m2) / NXC_FLAT_TOL);
        if (f->turn_step > 0.0f) segs = nxc__maxf(segs, control_turn(q) / f->turn_step);
        if (segs > (float)NXC_FLAT_LEAF_MAX && cv.depth < NXC_FLAT_MAX_DEPTH && splits > 0 &&
            sp + 2 <= (int32_t)(sizeof(stack) / sizeof(stack[0])) && budget > NXC_FLAT_LEAF_MAX) {
            nxc_cubic_t l, r;
            cubic_split(q, l.p, r.p);
            l.depth = r.depth = cv.depth + 1;
            stack[sp++] = r;                           /* left half is popped first */
            stack[sp++] = l;
            splits--;
            continue;
        }
        uint32_t n = segs >= (float)NXC_FLAT_LEAF_MAX ? (uint32_t)NXC_FLAT_LEAF_MAX
                                                      : (uint32_t)nxc__ceilf(segs);
        if (n < 1) n = 1;
        if (n > budget) n = budget > 0 ? budget : 1;
        budget = budget > n ? budget - n : 0;
        /* B(t) = p0 + t (c1 + t (c2 + t c3)), evaluated relative to p0. */
        float c1x = 3.0f * (q[1].x - q[0].x), c1y = 3.0f * (q[1].y - q[0].y);
        float c2x = 3.0f * (q[2].x - 2.0f * q[1].x + q[0].x);
        float c2y = 3.0f * (q[2].y - 2.0f * q[1].y + q[0].y);
        float c3x = q[3].x - q[0].x + 3.0f * (q[1].x - q[2].x);
        float c3y = q[3].y - q[0].y + 3.0f * (q[1].y - q[2].y);
        float inv = 1.0f / (float)n;
        nxc_point_t prev = q[0];
        for (uint32_t i = 1; i <= n; i++) {
            nxc_point_t cur;
            if (i == n) {
                cur = q[3];
            } else {
                float t = (float)i * inv;
                cur = pt(q[0].x + t * (c1x + t * (c2x + t * c3x)),
                         q[0].y + t * (c1y + t * (c2y + t * c3y)));
            }
            f->seg(f->ctx, prev, cur);
            prev = cur;
        }
    }
}

/* ============ Fill ============ */

typedef struct {
    const nxc_path_t *path;
    float             scale, tx, ty;
    nxc_band_t       *band;
    nxc_flat_t        flat;
} nxc_fill_job_t;

static void fill_seg(void *ctx, nxc_point_t a, nxc_point_t b) {
    nxc_fill_job_t *j = (nxc_fill_job_t *)ctx;
    band_line(j->band, a.x, a.y, b.x, b.y);
}

/* A curve entirely left of the window covers each row exactly like its
 * chord does, so the chord stands in for it; other culled curves add
 * nothing to this window. */
static void fill_culled(void *ctx, nxc_point_t a, nxc_point_t b, int left) {
    if (left) fill_seg(ctx, a, b);
}

static void fill_begin(void *ctx, nxc_point_t p) { (void)ctx; (void)p; }

static void fill_line(void *ctx, nxc_point_t a, nxc_point_t b) { fill_seg(ctx, a, b); }

static void fill_cubic(void *ctx, nxc_point_t a, nxc_point_t c1, nxc_point_t c2, nxc_point_t b) {
    nxc_fill_job_t *j = (nxc_fill_job_t *)ctx;
    flatten_cubic(&j->flat, a, c1, c2, b);
}

/* Fills close every subpath implicitly. */
static void fill_end(void *ctx, nxc_point_t cur, nxc_point_t start, int closed) {
    (void)closed;
    fill_seg(ctx, cur, start);
}

static const nxc_walk_ops_t g_fill_ops = { fill_begin, fill_line, fill_cubic, fill_end };

static void fill_emit(nxc_band_t *b, void *ctx) {
    nxc_fill_job_t *j = (nxc_fill_job_t *)ctx;
    j->band = b;
    j->flat.top = b->top;
    j->flat.bot = b->bot;
    j->flat.left = b->left;
    j->flat.right = b->right;
    j->flat.margin = 0.0f;
    j->flat.turn_step = 0.0f;
    j->flat.seg = fill_seg;
    j->flat.culled = fill_culled;
    j->flat.ctx = j;
    path_walk(j->path, j->scale, j->tx, j->ty, &g_fill_ops, j);
}

/* Validate and rasterize a fill (no error reporting: callers name it). */
static nxc_status_t fill_impl(const nxc_path_t *p, float scale, float tx, float ty,
                              nxc_fill_rule_t rule, nxc_irect_t clip,
                              nxc__cov_sink_fn sink, void *sink_ctx) {
    if (!p || !sink) return NXC_ERR_INVALID_ARG;
    if (!nxc__finitef(scale) || !(scale > 0.0f) || !finite2(tx, ty)) return NXC_ERR_INVALID_ARG;
    if ((int32_t)rule != NXC_FILL_NONZERO && (int32_t)rule != NXC_FILL_EVENODD) {
        return NXC_ERR_INVALID_ARG;
    }
    if (clip.w < 0 || clip.h < 0 || clip.w > NXC_SURFACE_DIM_MAX || clip.h > NXC_SURFACE_DIM_MAX) {
        return NXC_ERR_INVALID_ARG;
    }
    if (!nxc__ready()) return NXC_ERR_STATE;
    nxc_bounds_t gb;
    int empty = 1;
    nxc_status_t st = path_scan(p, scale, tx, ty, &gb, &empty);
    if (st != NXC_OK) return st;
    if (empty || clip.w == 0 || clip.h == 0) return NXC_OK;
    nxc_fill_job_t job;
    nxc__zero(&job, sizeof(job));
    job.path = p;
    job.scale = scale;
    job.tx = tx;
    job.ty = ty;
    return raster_run(clip, gb, (int32_t)rule == NXC_FILL_EVENODD, fill_emit, &job, sink, sink_ctx);
}

nxc_status_t nxc__raster_path(const nxc_path_t *p, float scale, float tx, float ty,
                              nxc_fill_rule_t rule, nxc_irect_t clip,
                              nxc__cov_sink_fn sink, void *sink_ctx) {
    nxc_status_t st = fill_impl(p, scale, tx, ty, rule, clip, sink, sink_ctx);
    return (st == NXC_OK) ? NXC_OK : nxc__fail(st, "nxc__raster_path");
}

/* ============ Canvas sinks ============ */

typedef struct {
    nxc_canvas_t *c;
    uint32_t      premul;
} nxc_solid_sink_t;

/* Solid color: long fully covered runs go through span_solid (bulk fill),
 * the anti-aliased rest through span_cov. */
static void sink_solid(void *ctx, int32_t y, int32_t x0, const uint8_t *cov, int32_t n) {
    const nxc_solid_sink_t *s = (const nxc_solid_sink_t *)ctx;
    int32_t start = 0, i = 0;
    while (i < n) {
        if (cov[i] != 255u) { i++; continue; }
        int32_t j = i;
        while (j < n && cov[j] == 255u) j++;
        if (j - i >= NXC_SOLID_RUN_MIN) {
            if (i > start) nxc__span_cov(s->c, y, x0 + start, cov + start, i - start, s->premul);
            nxc__span_solid(s->c, y, x0 + i, x0 + j, s->premul, 255u);
            start = j;
        }
        i = j;
    }
    if (n > start) nxc__span_cov(s->c, y, x0 + start, cov + start, n - start, s->premul);
}

typedef struct {
    nxc_canvas_t           *c;
    const nxc__paint_ctx_t *pc;
} nxc_paint_sink_t;

/* Local shader trampoline: taking a static function's address keeps the
 * object free of GOT references under position-independent builds. */
static void shade_paint(const void *ctx, int32_t x, int32_t y, int32_t n, uint32_t *out) {
    nxc__paint_shade(ctx, x, y, n, out);
}

static void sink_paint(void *ctx, int32_t y, int32_t x0, const uint8_t *cov, int32_t n) {
    const nxc_paint_sink_t *s = (const nxc_paint_sink_t *)ctx;
    nxc__span_shade(s->c, y, x0, cov, n, shade_paint, s->pc);
}

/* Canvas with a usable target surface. */
static int canvas_ok(const nxc_canvas_t *c) {
    return c && c->target && c->target->pixels && c->target->width > 0 &&
           c->target->height > 0 && c->target->stride >= c->target->width;
}

nxc_status_t nxc_fill_path(nxc_canvas_t *c, const nxc_path_t *p,
                           nxc_fill_rule_t rule, nxc_color_t color) {
    const char *fn = "nxc_fill_path";
    if (!canvas_ok(c) || !p) return nxc__fail(NXC_ERR_INVALID_ARG, fn);
    nxc_solid_sink_t sk;
    sk.c = c;
    sk.premul = nxc__premul(color);
    /* Transparent fills still validate their arguments (empty clip). */
    nxc_irect_t clip = (sk.premul == 0 || c->st.alpha == 0) ? nxc_irect(0, 0, 0, 0)
                                                             : nxc__device_clip(c);
    nxc_status_t st = fill_impl(p, 1.0f, c->st.tx, c->st.ty, rule, clip, sink_solid, &sk);
    return (st == NXC_OK) ? NXC_OK : nxc__fail(st, fn);
}

nxc_status_t nxc_fill_path_paint(nxc_canvas_t *c, const nxc_path_t *p,
                                 nxc_fill_rule_t rule, const nxc_paint_t *paint) {
    const char *fn = "nxc_fill_path_paint";
    if (!canvas_ok(c) || !p || !paint) return nxc__fail(NXC_ERR_INVALID_ARG, fn);
    if (paint->type == NXC_PAINT_SOLID) return nxc_fill_path(c, p, rule, paint->color);
    nxc__paint_ctx_t pc;
    nxc_status_t st = nxc__paint_prepare(paint, c->st.tx, c->st.ty, &pc);
    if (st != NXC_OK) return st;                       /* reported by the paint module */
    nxc_paint_sink_t sk;
    sk.c = c;
    sk.pc = &pc;
    nxc_irect_t clip = (c->st.alpha == 0) ? nxc_irect(0, 0, 0, 0) : nxc__device_clip(c);
    st = fill_impl(p, 1.0f, c->st.tx, c->st.ty, rule, clip, sink_paint, &sk);
    return (st == NXC_OK) ? NXC_OK : nxc__fail(st, fn);
}

/* ============ Stroker ============ */

typedef struct {
    const nxc_path_t *path;
    float             tx, ty;
    nxc_band_t       *band;
    nxc_flat_t        flat;
    float             hw;            /* half width */
    float             margin;        /* farthest any piece reaches from the path */
    nxc_join_t        join;
    nxc_cap_t         cap;
    float             miter_limit2;  /* miter_limit^2 */
    float             arc_step;      /* max radians per round join/cap step */
    float             round_flat;    /* (1+cos)/2 above which a round join is one chord */
    /* Current subpath. */
    nxc_point_t       start, cur;
    nxc_point_t       first_dir, last_dir;
    int32_t           first_state;   /* 0 no segment yet, 1 first_dir valid, 2 first culled */
    int32_t           has_last;      /* last_dir valid and its end visible */
    int32_t           internal;      /* next join lies inside one flattened curve */
    int32_t           drew;          /* subpath has a drawing verb */
} nxc_stroker_t;

/*
 * Deposit one convex polygon of the stroke. The orientation is normalized
 * (by its signed area) so every piece adds winding of the same sign and
 * overlaps saturate under the nonzero rule. Polygons outside the band or
 * entirely left of it (a closed polygon there adds no net cover) are
 * skipped.
 */
static void stroke_poly(nxc_stroker_t *s, const nxc_point_t *q, int32_t n) {
    if (n < 3) return;
    const nxc_band_t *b = s->band;
    float mnx = q[0].x, mxx = q[0].x, mny = q[0].y, mxy = q[0].y;
    for (int32_t i = 1; i < n; i++) {
        mnx = nxc__minf(mnx, q[i].x); mxx = nxc__maxf(mxx, q[i].x);
        mny = nxc__minf(mny, q[i].y); mxy = nxc__maxf(mxy, q[i].y);
    }
    if (mxy <= b->top || mny >= b->bot || mnx >= b->right || mxx <= b->left) return;
    float area2 = 0.0f;
    for (int32_t i = 1; i + 1 < n; i++) {
        float ax = q[i].x - q[0].x, ay = q[i].y - q[0].y;
        float bx = q[i + 1].x - q[0].x, by = q[i + 1].y - q[0].y;
        area2 += ax * by - bx * ay;
    }
    if (area2 == 0.0f) return;                         /* degenerate: covers nothing */
    for (int32_t i = 0; i < n; i++) {
        nxc_point_t a = q[i], c = q[(i + 1 < n) ? i + 1 : 0];
        if (area2 > 0.0f) band_line(s->band, a.x, a.y, c.x, c.y);
        else band_line(s->band, c.x, c.y, a.x, a.y);
    }
}

/* Fan polygon around c: optional center, then c + v0 rotated by `angle`
 * radians (sign = rotation direction, +1 turns +x towards +y) in steps of
 * at most arc_step, ending exactly at c + *end when given. */
static void stroke_fan(nxc_stroker_t *s, nxc_point_t c, nxc_point_t v0, const nxc_point_t *end,
                       float angle, float sign, int center) {
    nxc_point_t poly[NXC_ARC_MAX_STEPS + 2];
    int32_t steps = (int32_t)nxc__ceilf(angle / s->arc_step);
    if (steps < 1) steps = 1;
    if (steps > NXC_ARC_MAX_STEPS) steps = NXC_ARC_MAX_STEPS;
    float ph = angle / (float)steps;
    float cs = nxc__cosf(ph), sn = sign * nxc__sinf(ph);
    int32_t n = 0;
    if (center) poly[n++] = c;
    float vx = v0.x, vy = v0.y;
    poly[n++] = pt(c.x + vx, c.y + vy);
    for (int32_t i = 1; i < steps; i++) {
        float nx = vx * cs - vy * sn;
        vy = vx * sn + vy * cs;
        vx = nx;
        poly[n++] = pt(c.x + vx, c.y + vy);
    }
    if (end) poly[n++] = pt(c.x + end->x, c.y + end->y);
    stroke_poly(s, poly, n);
}

/* Join wedge at p between unit directions u1 (incoming) and u2 (outgoing),
 * filling the gap on the outer side of the turn. Joins inside a curve use
 * the round rule (a single chord when that is within tolerance). */
static void stroke_join(nxc_stroker_t *s, nxc_point_t p, nxc_point_t u1, nxc_point_t u2,
                        int internal) {
    float dot = u1.x * u2.x + u1.y * u2.y;
    float crs = u1.x * u2.y - u1.y * u2.x;
    if (dot > 0.0f && nxc__absf(crs) < 1.0e-6f) return;   /* straight on */
    float hw = s->hw;
    float side = crs > 0.0f ? -1.0f : 1.0f;              /* outer side of the turn */
    nxc_point_t o1 = pt(-u1.y * hw * side, u1.x * hw * side);
    nxc_point_t o2 = pt(-u2.y * hw * side, u2.x * hw * side);
    nxc_join_t j = internal ? NXC_JOIN_ROUND : s->join;
    if (j == NXC_JOIN_ROUND && 0.5f * (1.0f + dot) >= s->round_flat) j = NXC_JOIN_BEVEL;
    if (j == NXC_JOIN_MITER) {
        /* Miter length / width = 1 / cos(turn / 2); cos^2(turn / 2) = (1 + dot) / 2. */
        float qd = 1.0f + dot;
        if (qd > 1.0e-6f && qd * s->miter_limit2 >= 2.0f) {
            nxc_point_t tip = pt(p.x + (o1.x + o2.x) / qd, p.y + (o1.y + o2.y) / qd);
            nxc_point_t poly[4] = { p, pt_add(p, o1), tip, pt_add(p, o2) };
            stroke_poly(s, poly, 4);
            return;
        }
        j = NXC_JOIN_BEVEL;
    }
    if (j == NXC_JOIN_ROUND) {
        /* The arc passes through the outer bisector; at a full reversal it
         * passes through u1 (rotating o1 = n1 negatively). */
        float ang = nxc__atan2f(nxc__absf(crs), dot);
        stroke_fan(s, p, o1, &o2, ang, crs > 0.0f ? 1.0f : -1.0f, 1);
        return;
    }
    nxc_point_t tri[3] = { p, pt_add(p, o1), pt_add(p, o2) };
    stroke_poly(s, tri, 3);
}

/* Cap at p; d is the unit direction pointing out of the stroke. */
static void stroke_cap(nxc_stroker_t *s, nxc_point_t p, nxc_point_t d) {
    float hw = s->hw;
    nxc_point_t n = pt(-d.y * hw, d.x * hw);
    if (s->cap == NXC_CAP_SQUARE) {
        nxc_point_t e = pt(d.x * hw, d.y * hw);
        nxc_point_t poly[4] = { pt_add(p, n), pt_add(pt_add(p, n), e),
                                pt_add(pt_sub(p, n), e), pt_sub(p, n) };
        stroke_poly(s, poly, 4);
    } else if (s->cap == NXC_CAP_ROUND) {
        nxc_point_t e = pt(-n.x, -n.y);
        stroke_fan(s, p, n, &e, NXC_PI, -1.0f, 1);
    }
}

/* Zero-length subpath: a dot (round cap) or a square (square cap). */
static void stroke_dot(nxc_stroker_t *s, nxc_point_t p) {
    float hw = s->hw;
    if (s->cap == NXC_CAP_ROUND) {
        stroke_fan(s, p, pt(hw, 0.0f), 0, 2.0f * NXC_PI, 1.0f, 0);
    } else if (s->cap == NXC_CAP_SQUARE) {
        nxc_point_t poly[4] = { pt(p.x - hw, p.y - hw), pt(p.x + hw, p.y - hw),
                                pt(p.x + hw, p.y + hw), pt(p.x - hw, p.y + hw) };
        stroke_poly(s, poly, 4);
    }
}

/* A piece that cannot reach the band: its joins and caps cannot either,
 * so continuity is simply broken (no join with the next piece). */
static void stroke_hide(nxc_stroker_t *s, nxc_point_t b) {
    s->has_last = 0;
    if (s->first_state == 0) s->first_state = 2;
    s->internal = 1;
    s->cur = b;
}

/* One flattened segment: join with the previous one, then its quad. */
static void stroke_segment(nxc_stroker_t *s, nxc_point_t a, nxc_point_t b) {
    const nxc_band_t *bd = s->band;
    float m = s->margin;
    s->drew = 1;
    if (nxc__minf(a.y, b.y) - m >= bd->bot || nxc__maxf(a.y, b.y) + m <= bd->top ||
        nxc__minf(a.x, b.x) - m >= bd->right || nxc__maxf(a.x, b.x) + m <= bd->left) {
        stroke_hide(s, b);
        return;
    }
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx * dx + dy * dy;
    if (len2 <= NXC_STROKE_EPS2) {                     /* a point: keeps the direction */
        s->cur = b;
        return;
    }
    float inv = 1.0f / nxc__sqrtf(len2);
    nxc_point_t u = pt(dx * inv, dy * inv);
    if (s->first_state == 0) {
        s->first_state = 1;
        s->first_dir = u;
    } else if (s->has_last) {
        stroke_join(s, a, s->last_dir, u, s->internal);
    }
    nxc_point_t n = pt(-u.y * s->hw, u.x * s->hw);
    nxc_point_t quad[4] = { pt_add(a, n), pt_add(b, n), pt_sub(b, n), pt_sub(a, n) };
    stroke_poly(s, quad, 4);
    s->last_dir = u;
    s->has_last = 1;
    s->internal = 1;
    s->cur = b;
}

static void stroke_flat_seg(void *ctx, nxc_point_t a, nxc_point_t b) {
    stroke_segment((nxc_stroker_t *)ctx, a, b);
}

/* Culled stroke pieces add nothing: closed polygons left of the band
 * cancel out, everything else is outside it. */
static void stroke_flat_culled(void *ctx, nxc_point_t a, nxc_point_t b, int left) {
    nxc_stroker_t *s = (nxc_stroker_t *)ctx;
    (void)a;
    (void)left;
    s->drew = 1;
    stroke_hide(s, b);
}

static void stroke_begin(void *ctx, nxc_point_t p) {
    nxc_stroker_t *s = (nxc_stroker_t *)ctx;
    s->start = p;
    s->cur = p;
    s->first_state = 0;
    s->has_last = 0;
    s->internal = 0;
    s->drew = 0;
}

static void stroke_line(void *ctx, nxc_point_t a, nxc_point_t b) {
    nxc_stroker_t *s = (nxc_stroker_t *)ctx;
    s->internal = 0;
    stroke_segment(s, a, b);
}

static void stroke_cubic(void *ctx, nxc_point_t a, nxc_point_t c1, nxc_point_t c2, nxc_point_t b) {
    nxc_stroker_t *s = (nxc_stroker_t *)ctx;
    s->internal = 0;
    s->drew = 1;
    flatten_cubic(&s->flat, a, c1, c2, b);
}

/* End of a subpath: closing segment and join, or the two caps; a subpath
 * that never moved becomes a dot. */
static void stroke_end(void *ctx, nxc_point_t cur, nxc_point_t start, int closed) {
    nxc_stroker_t *s = (nxc_stroker_t *)ctx;
    (void)cur;
    (void)start;
    if (closed) {
        s->internal = 0;
        stroke_segment(s, s->cur, s->start);
        if (s->first_state == 1 && s->has_last) {
            stroke_join(s, s->start, s->last_dir, s->first_dir, 0);
        }
    } else if (s->first_state == 1) {
        stroke_cap(s, s->start, pt(-s->first_dir.x, -s->first_dir.y));
        if (s->has_last) stroke_cap(s, s->cur, s->last_dir);
    }
    if (s->first_state == 0 && s->drew) stroke_dot(s, s->start);
    s->first_state = 0;
    s->has_last = 0;
    s->drew = 0;
}

static const nxc_walk_ops_t g_stroke_ops = { stroke_begin, stroke_line, stroke_cubic, stroke_end };

static void stroke_emit(nxc_band_t *b, void *ctx) {
    nxc_stroker_t *s = (nxc_stroker_t *)ctx;
    s->band = b;
    s->flat.top = b->top;
    s->flat.bot = b->bot;
    s->flat.left = b->left;
    s->flat.right = b->right;
    s->flat.ctx = s;
    path_walk(s->path, 1.0f, s->tx, s->ty, &g_stroke_ops, s);
}

nxc_status_t nxc_stroke_path(nxc_canvas_t *c, const nxc_path_t *p,
                             const nxc_stroke_t *s, nxc_color_t color) {
    const char *fn = "nxc_stroke_path";
    if (!canvas_ok(c) || !p || !s) return nxc__fail(NXC_ERR_INVALID_ARG, fn);
    if (!nxc__finitef(s->width) || !(s->width > 0.0f) ||
        ((int32_t)s->cap != NXC_CAP_BUTT && (int32_t)s->cap != NXC_CAP_ROUND &&
         (int32_t)s->cap != NXC_CAP_SQUARE) ||
        ((int32_t)s->join != NXC_JOIN_MITER && (int32_t)s->join != NXC_JOIN_ROUND &&
         (int32_t)s->join != NXC_JOIN_BEVEL) ||
        !nxc__finitef(s->miter_limit) || s->miter_limit < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, fn);
    }
    if (s->width > NXC_STROKE_MAX_WIDTH || s->miter_limit > NXC_MITER_MAX) {
        return nxc__fail(NXC_ERR_UNSUPPORTED, fn);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, fn);
    nxc_bounds_t gb;
    int empty = 1;
    nxc_status_t st = path_scan(p, 1.0f, c->st.tx, c->st.ty, &gb, &empty);
    if (st != NXC_OK) return nxc__fail(st, fn);
    uint32_t premul = nxc__premul(color);
    if (empty || premul == 0 || c->st.alpha == 0) return NXC_OK;
    nxc_irect_t clip = nxc__device_clip(c);
    if (clip.w == 0 || clip.h == 0) return NXC_OK;

    nxc_stroker_t sk;
    nxc__zero(&sk, sizeof(sk));
    sk.path = p;
    sk.tx = c->st.tx;
    sk.ty = c->st.ty;
    sk.hw = 0.5f * s->width;
    sk.join = s->join;
    sk.cap = s->cap;
    /* 0 selects the default limit 4; limits below 1 can never miter. */
    float limit = (s->miter_limit == 0.0f) ? 4.0f : nxc__maxf(s->miter_limit, 1.0f);
    sk.miter_limit2 = limit * limit;
    /* An arc step phi deviates hw (1 - cos(phi/2)) ~ hw phi^2 / 8 from the circle. */
    sk.arc_step = nxc__minf(0.25f * NXC_PI, nxc__sqrtf(8.0f * NXC_FLAT_TOL / sk.hw));
    float flat = sk.hw > NXC_FLAT_TOL ? 1.0f - NXC_FLAT_TOL / sk.hw : 0.0f;
    sk.round_flat = flat * flat;
    float reach = 1.0f;
    if (s->cap == NXC_CAP_SQUARE) reach = 1.41421356f;
    if (s->join == NXC_JOIN_MITER) reach = nxc__maxf(reach, limit);
    sk.margin = sk.hw * reach + 1.0f;
    sk.flat.margin = sk.margin;
    sk.flat.turn_step = sk.arc_step;
    sk.flat.seg = stroke_flat_seg;
    sk.flat.culled = stroke_flat_culled;

    gb.x0 -= sk.margin; gb.y0 -= sk.margin;
    gb.x1 += sk.margin; gb.y1 += sk.margin;
    nxc_solid_sink_t sink;
    sink.c = c;
    sink.premul = premul;
    st = raster_run(clip, gb, 0, stroke_emit, &sk, sink_solid, &sink);
    return (st == NXC_OK) ? NXC_OK : nxc__fail(st, fn);
}
