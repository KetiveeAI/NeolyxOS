/*
 * NeolyxOS - NXCanvas font engine tests: table parsing, cmap formats 4
 * and 12, metrics, hmtx, legacy kern and GPOS kerning, simple and
 * composite outlines, depth/budget limits, hostile-input fuzzing and a
 * rendered glyph sheet.
 *
 * Expected values for the bundled NXSans fonts were produced by an
 * independent parser (and cross-checked against HarfBuzz pair kerning);
 * the synthetic fonts below are built byte by byte so every structure the
 * engine supports is exercised with known answers.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include "../nxc_internal.h"
#include "../nxc_font_internal.h"
#include <math.h>

/* Font directory relative to tests/ (override with NXC_FONT_DIR). */
static const char *font_dir(void) {
    const char *d = getenv("NXC_FONT_DIR");
    return (d && *d) ? d : "../../../../resources/fonts/";
}
#define FONT_DIR font_dir()

/* ============ Engine environment with a countable log ============ */

static int g_quiet = 0;
static unsigned g_log_lines = 0;

static void *t_alloc(uint64_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 16, (size_t)size) != 0) return NULL;
    return p;
}
static void t_free(void *p) { free(p); }
static void t_log(const char *msg) {
    g_log_lines++;
    if (!g_quiet) fputs(msg, stderr);
}

static void t_init(int32_t w, int32_t h) {
    nxc_env_t env;
    memset(&env, 0, sizeof(env));
    env.alloc = t_alloc;
    env.free = t_free;
    env.log = t_log;
    env.max_width = w;
    env.max_height = h;
    if (nxc_init(&env) != NXC_OK) { fprintf(stderr, "nxc_init failed\n"); exit(2); }
}

/* ============ Path fallback (used only when nxc_raster.c is not linked) ============ */

__attribute__((weak)) nxc_status_t nxc_path_init(nxc_path_t *p, nxc_point_t *pts, uint32_t cap_pts,
                                                 uint8_t *verbs, uint32_t cap_verbs) {
    if (!p || !pts || !verbs) return NXC_ERR_INVALID_ARG;
    memset(p, 0, sizeof(*p));
    p->pts = pts; p->verbs = verbs; p->cap_pts = cap_pts; p->cap_verbs = cap_verbs;
    return NXC_OK;
}
__attribute__((weak)) void nxc_path_reset(nxc_path_t *p) {
    if (p) { p->npts = 0; p->nverbs = 0; p->error = NXC_OK; }
}
static nxc_status_t tp_push(nxc_path_t *p, uint8_t verb, const float *xy, uint32_t n) {
    if (!p) return NXC_ERR_INVALID_ARG;
    if (p->error != NXC_OK) return p->error;
    for (uint32_t i = 0; i < 2 * n; i++) if (!isfinite(xy[i])) return p->error = NXC_ERR_INVALID_ARG;
    if (verb != NXC_VERB_MOVE && (p->nverbs == 0 || p->verbs[p->nverbs - 1] == NXC_VERB_CLOSE)) {
        return p->error = NXC_ERR_STATE;
    }
    if (p->nverbs + 1 > p->cap_verbs || p->npts + n > p->cap_pts) return p->error = NXC_ERR_OVERFLOW;
    for (uint32_t i = 0; i < n; i++) { p->pts[p->npts].x = xy[2 * i]; p->pts[p->npts].y = xy[2 * i + 1]; p->npts++; }
    p->verbs[p->nverbs++] = verb;
    return NXC_OK;
}
__attribute__((weak)) nxc_status_t nxc_path_move_to(nxc_path_t *p, float x, float y) {
    float v[2] = { x, y }; return tp_push(p, NXC_VERB_MOVE, v, 1);
}
__attribute__((weak)) nxc_status_t nxc_path_line_to(nxc_path_t *p, float x, float y) {
    float v[2] = { x, y }; return tp_push(p, NXC_VERB_LINE, v, 1);
}
__attribute__((weak)) nxc_status_t nxc_path_quad_to(nxc_path_t *p, float cx, float cy, float x, float y) {
    float v[4] = { cx, cy, x, y }; return tp_push(p, NXC_VERB_QUAD, v, 2);
}
__attribute__((weak)) nxc_status_t nxc_path_close(nxc_path_t *p) {
    return tp_push(p, NXC_VERB_CLOSE, NULL, 0);
}

/* Real rasterizer when nxc_raster.c is linked, NULL otherwise. */
extern __typeof__(nxc_fill_path) nxc_fill_path __attribute__((weak));

/* ============ Helpers ============ */

typedef struct { uint8_t *data; uint32_t size; } blob_t;

static blob_t read_file(const char *path) {
    blob_t b = { NULL, 0 };
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); nxc_test_failures++; return b; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) {
        b.data = malloc((size_t)n);
        if (b.data && fread(b.data, 1, (size_t)n, f) == (size_t)n) b.size = (uint32_t)n;
    }
    fclose(f);
    return b;
}

/* FNV-1a over 16-bit little-endian values (matches the oracle script). */
typedef struct { uint32_t h; } fnv_t;
static void fnv_init(fnv_t *f) { f->h = 0x811c9dc5u; }
static void fnv_u16(fnv_t *f, uint32_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    for (int i = 0; i < 2; i++) { f->h ^= b[i]; f->h *= 0x01000193u; }
}

#define PATH_CAP 65536
static nxc_point_t g_pts[PATH_CAP];
static uint8_t g_verbs[PATH_CAP];

static void path_new(nxc_path_t *p) {
    NXC_CHECK_OK(nxc_path_init(p, g_pts, PATH_CAP, g_verbs, PATH_CAP));
}

typedef struct {
    uint32_t moves, lines, quads, closes;
    float minx, miny, maxx, maxy;
    double area;            /* exact signed area of the quad/line outline */
} path_info_t;

static double cross2(nxc_point_t a, nxc_point_t b) { return (double)a.x * b.y - (double)a.y * b.x; }

/* Walk the path verbs: counts, control-point bounds and exact area
 * (Green's theorem for quadratic segments), from verb index v0. */
static path_info_t path_info(const nxc_path_t *p, uint32_t v0, uint32_t p0) {
    path_info_t in;
    memset(&in, 0, sizeof(in));
    in.minx = in.miny = 1e30f; in.maxx = in.maxy = -1e30f;
    uint32_t pi = p0;
    nxc_point_t cur = { 0, 0 }, start = { 0, 0 };
    for (uint32_t v = v0; v < p->nverbs; v++) {
        uint8_t vb = p->verbs[v];
        uint32_t n = vb == NXC_VERB_MOVE || vb == NXC_VERB_LINE ? 1 : vb == NXC_VERB_QUAD ? 2 : vb == NXC_VERB_CUBIC ? 3 : 0;
        for (uint32_t k = 0; k < n; k++) {
            nxc_point_t q = p->pts[pi + k];
            if (q.x < in.minx) in.minx = q.x;
            if (q.y < in.miny) in.miny = q.y;
            if (q.x > in.maxx) in.maxx = q.x;
            if (q.y > in.maxy) in.maxy = q.y;
        }
        if (vb == NXC_VERB_MOVE) { in.moves++; cur = start = p->pts[pi]; }
        else if (vb == NXC_VERB_LINE) { in.lines++; in.area += 0.5 * cross2(cur, p->pts[pi]); cur = p->pts[pi]; }
        else if (vb == NXC_VERB_QUAD) {
            in.quads++;
            nxc_point_t c = p->pts[pi], e = p->pts[pi + 1];
            in.area += (2.0 * cross2(cur, c) + 2.0 * cross2(c, e) + cross2(cur, e)) / 6.0;
            cur = e;
        } else if (vb == NXC_VERB_CLOSE) { in.closes++; in.area += 0.5 * cross2(cur, start); cur = start; }
        pi += n;
    }
    return in;
}

/* ============ Real fonts ============ */

typedef struct {
    const char *file;
    uint32_t upem, num_glyphs;
    int32_t asc, desc, gap, cap, xh;
    uint32_t cmap_hash, adv_hash, bbox_hash, kern_hash;
    int32_t k_av, k_to, k_wo, k_ta;
    double area_o, area_at, area_aacute;    /* y-up oracle areas */
    uint32_t last_adv;
} font_expect_t;

static const font_expect_t g_fonts[] = {
    { "NXSans/NXSans-Text.ttf", 1000, 2478, 1024, 400, 0, 660, 486,
      0x7421723au, 0x5227c29au, 0x568ab04cu, 0xd333fbf9u, -14, -66, 0, -73,
      -98993.2917, -205123.3750, -133252.9167, 425 },
    { "NXSans/NXSans-UI.ttf", 1000, 2478, 1024, 400, 0, 660, 489,
      0x7421723au, 0x394bf5c8u, 0x29ec3cfdu, 0x8bad1eacu, -15, -69, 0, -70,
      -115783.5000, -241216.1667, -159644.9167, 435 },
    { "NXSans/NXSans-Bold.ttf", 1000, 2478, 1024, 400, 0, 660, 496,
      0x7421723au, 0xf60efe9fu, 0x53e77e1fu, 0x23b1d9cfu, -18, -76, 0, -64,
      -157818.6667, -314930.6667, -232837.1667, 463 },
    { "NXSans/NXSans-Display.ttf", 1000, 2478, 1024, 400, 0, 660, 491,
      0x7421723au, 0xeff31cc5u, 0xaaa46e9fu, 0x13cbc876u, -16, -71, 0, -68,
      -130588.8333, -274838.7917, -184264.3333, 444 },
    { "NXSans/NXSans-Italic.ttf", 1000, 1998, 1024, 400, 0, 660, 486,
      0x5fe24550u, 0x5bd739fbu, 0x7234e207u, 0xf36d1184u, -49, -73, -7, -73,
      -90535.7917, -191653.3333, -125155.7500, 409 },
    { "NXSansMono/NXSans-Mono.ttf", 2048, 1780, 1900, 480, 0, 1420, 1060,
      0xfde844e4u, 0x392bb9a9u, 0xe74a1557u, 0x2d24b9cdu, 0, 0, 0, 0,
      -549230.0000, -950268.2083, -753258.0000, 1200 },
};

static nxc_font_t *load_font(const char *rel, blob_t *b) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", FONT_DIR, rel);
    *b = read_file(path);
    if (!b->data) return NULL;
    nxc_font_t *f = NULL;
    NXC_CHECK_OK(nxc_font_load(&f, b->data, b->size));
    return f;
}

/* 1 when glyph g is a composite (raw glyf header read, test only). */
static int is_composite(const nxc_font_t *f, uint32_t g) {
    const uint8_t *d = f->data;
    uint32_t a;
    if (f->loc_long) {
        const uint8_t *q = d + f->loca_off + 4u * g;
        a = (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3];
    } else {
        const uint8_t *q = d + f->loca_off + 2u * g;
        a = 2u * ((uint32_t)q[0] << 8 | q[1]);
    }
    return (d[f->glyf_off + a] & 0x80) != 0;
}

static int32_t kern_cp(const nxc_font_t *f, char a, char b) {
    return nxc__font_kerning(f, nxc__font_glyph_index(f, (uint8_t)a), nxc__font_glyph_index(f, (uint8_t)b));
}

/* Check one bundled font against the oracle values. */
static void test_real_font(const font_expect_t *e) {
    blob_t b;
    nxc_font_t *f = load_font(e->file, &b);
    if (!f) { free(b.data); return; }
    fprintf(stderr, "font %s\n", e->file);

    /* Metrics */
    NXC_CHECK(nxc__font_units_per_em(f) == (float)e->upem);
    NXC_CHECK(nxc__font_num_glyphs(f) == e->num_glyphs);
    int32_t asc, desc, gap, cap, xh;
    NXC_CHECK_OK(nxc__font_vmetrics(f, &asc, &desc, &gap, &cap, &xh));
    NXC_CHECK(asc == e->asc && desc == e->desc && gap == e->gap && cap == e->cap && xh == e->xh);
    nxc_font_metrics_t m;
    NXC_CHECK_OK(nxc_font_metrics(f, 20.0f, &m));
    float s = 20.0f / (float)e->upem;
    NXC_CHECK(fabsf(m.ascent - e->asc * s) < 1e-4f && fabsf(m.descent - e->desc * s) < 1e-4f);
    NXC_CHECK(fabsf(m.cap_height - e->cap * s) < 1e-4f && fabsf(m.x_height - e->xh * s) < 1e-4f);
    NXC_CHECK(m.units_per_em == (float)e->upem && m.line_gap == (float)e->gap * s);
    NXC_CHECK(nxc_font_metrics(f, NAN, &m) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_metrics(f, -3.0f, &m) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_metrics(f, 1e9f, &m) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_metrics(f, 12.0f, NULL) == NXC_ERR_INVALID_ARG);

    /* cmap over U+0000..U+1FFFF, advances and glyph boxes of every glyph */
    fnv_t h;
    fnv_init(&h);
    for (uint32_t cp = 0; cp < 0x20000u; cp++) fnv_u16(&h, nxc__font_glyph_index(f, cp));
    NXC_CHECK(h.h == e->cmap_hash);
    NXC_CHECK(nxc__font_glyph_index(f, 0x110000u) == 0 && nxc__font_glyph_index(f, 0xFFFFFFFFu) == 0);
    NXC_CHECK(nxc__font_glyph_index(f, 0xD800u) == 0);
    fnv_init(&h);
    for (uint32_t g = 0; g < e->num_glyphs; g++) fnv_u16(&h, (uint32_t)nxc__font_advance(f, g));
    NXC_CHECK(h.h == e->adv_hash);
    NXC_CHECK(nxc__font_advance(f, e->num_glyphs - 1) == (int32_t)e->last_adv);
    NXC_CHECK(nxc__font_advance(f, e->num_glyphs) == 0);          /* out of range */
    fnv_init(&h);
    for (uint32_t g = 0; g < e->num_glyphs; g++) {
        int16_t x0, y0, x1, y1;
        NXC_CHECK_OK(nxc__font_glyph_bounds(f, g, &x0, &y0, &x1, &y1));
        fnv_u16(&h, (uint16_t)x0); fnv_u16(&h, (uint16_t)y0);
        fnv_u16(&h, (uint16_t)x1); fnv_u16(&h, (uint16_t)y1);
    }
    NXC_CHECK(h.h == e->bbox_hash);

    /* Kerning: full printable-ASCII pair table and named pairs */
    fnv_init(&h);
    for (int a = 32; a < 127; a++)
        for (int c = 32; c < 127; c++) fnv_u16(&h, (uint16_t)kern_cp(f, (char)a, (char)c));
    NXC_CHECK(h.h == e->kern_hash);
    NXC_CHECK(kern_cp(f, 'A', 'V') == e->k_av);
    NXC_CHECK(kern_cp(f, 'T', 'o') == e->k_to);
    NXC_CHECK(kern_cp(f, 'W', 'o') == e->k_wo);
    NXC_CHECK(kern_cp(f, 'T', 'a') == e->k_ta);
    NXC_CHECK(kern_cp(f, 'H', 'H') == 0 && kern_cp(f, 'o', 'o') == 0);
    NXC_CHECK(nxc__font_kerning(f, 0, 0) == 0);
    NXC_CHECK(nxc__font_kerning(f, e->num_glyphs, 1) == 0);
    NXC_CHECK(nxc__font_has_kerning(f) == (e->k_av != 0));

    /* Every glyph outlines; for simple glyphs the control-point box equals
     * the header box exactly, composites within rounding. */
    nxc_path_t p;
    path_new(&p);
    uint32_t bad_box = 0, failures = 0;
    for (uint32_t g = 0; g < e->num_glyphs; g++) {
        nxc_path_reset(&p);
        nxc_status_t st = nxc__font_glyph_outline(f, g, &p, 1.0f, 0.0f, 0.0f);
        if (st != NXC_OK) { failures++; continue; }
        int16_t x0, y0, x1, y1;
        (void)nxc__font_glyph_bounds(f, g, &x0, &y0, &x1, &y1);
        if (p.nverbs == 0) continue;
        path_info_t in = path_info(&p, 0, 0);
        if (in.moves != in.closes) bad_box++;
        float tol = is_composite(f, g) ? 1.01f : 0.0f;
        if (fabsf(in.minx - x0) > tol || fabsf(in.maxx - x1) > tol ||
            fabsf(in.miny + y1) > tol || fabsf(in.maxy + y0) > tol) bad_box++;
    }
    NXC_CHECK(failures == 0);
    NXC_CHECK(bad_box == 0);
    if (failures || bad_box) fprintf(stderr, "  outline failures %u, box mismatches %u\n", failures, bad_box);

    /* Exact areas (y-down device space flips the sign) and contour counts. */
    const struct { uint32_t cp; double area; } ar[3] = {
        { 'o', e->area_o }, { '@', e->area_at }, { 0xC1, e->area_aacute } };
    for (int i = 0; i < 3; i++) {
        nxc_path_reset(&p);
        NXC_CHECK_OK(nxc__font_glyph_outline(f, nxc__font_glyph_index(f, ar[i].cp), &p, 1.0f, 0.0f, 0.0f));
        path_info_t in = path_info(&p, 0, 0);
        NXC_CHECK(fabs(in.area + ar[i].area) < 0.05);
        if (fabs(in.area + ar[i].area) >= 0.05) fprintf(stderr, "  area U+%04X %.4f want %.4f\n", ar[i].cp, in.area, -ar[i].area);
    }

    /* Scale and origin: outline at 2x shifted equals 1x mapped. */
    nxc_path_t q;
    static nxc_point_t qpts[4096];
    static uint8_t qverbs[4096];
    NXC_CHECK_OK(nxc_path_init(&q, qpts, 4096, qverbs, 4096));
    uint32_t go = nxc__font_glyph_index(f, 'g');
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc__font_glyph_outline(f, go, &p, 1.0f, 0.0f, 0.0f));
    NXC_CHECK_OK(nxc__font_glyph_outline(f, go, &q, 2.0f, 100.0f, 50.0f));
    int same = p.npts == q.npts && p.nverbs == q.nverbs;
    for (uint32_t i = 0; same && i < p.npts; i++) {
        same = fabsf(q.pts[i].x - (p.pts[i].x * 2.0f + 100.0f)) < 1e-3f &&
               fabsf(q.pts[i].y - (p.pts[i].y * 2.0f + 50.0f)) < 1e-3f;
    }
    NXC_CHECK(same);

    /* Public wrapper: advance in px, NOT_FOUND for unmapped code points. */
    float adv = -1.0f;
    nxc_path_reset(&p);
    NXC_CHECK_OK(nxc_font_glyph_path(f, 'H', 32.0f, 10.0f, 40.0f, &p, &adv));
    NXC_CHECK(fabsf(adv - nxc__font_advance(f, nxc__font_glyph_index(f, 'H')) * 32.0f / e->upem) < 1e-4f);
    NXC_CHECK(p.nverbs > 0);
    uint32_t nv = p.nverbs;
    NXC_CHECK(nxc_font_glyph_path(f, 0x10FFFDu, 32.0f, 10.0f, 40.0f, &p, &adv) == NXC_ERR_NOT_FOUND);
    NXC_CHECK(adv == 0.0f && p.nverbs == nv);
    NXC_CHECK_OK(nxc_font_glyph_path(f, ' ', 32.0f, 0.0f, 0.0f, &p, NULL));      /* empty glyph */
    NXC_CHECK(p.nverbs == nv);
    NXC_CHECK(nxc_font_glyph_path(f, 'H', 0.0f, 0, 0, &p, &adv) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_glyph_path(f, 'H', 12.0f, NAN, 0, &p, &adv) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_glyph_path(f, 'H', 12.0f, 0, INFINITY, &p, &adv) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_glyph_path(f, 'H', 12.0f, 2e7f, 0, &p, &adv) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_glyph_path(f, 'H', 12.0f, 0, 0, NULL, &adv) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_glyph_path(NULL, 'H', 12.0f, 0, 0, &p, &adv) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__font_glyph_outline(f, 5, &p, 0.0f, 0, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__font_glyph_outline(f, 5, &p, 2000.0f, 0, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc__font_glyph_outline(f, e->num_glyphs, &p, 1.0f, 0, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(p.nverbs == nv);

    /* Path too small: OVERFLOW, previous contents intact. */
    nxc_path_t small;
    nxc_point_t spts[8];
    uint8_t sverbs[8];
    NXC_CHECK_OK(nxc_path_init(&small, spts, 8, sverbs, 8));
    NXC_CHECK_OK(nxc_path_move_to(&small, 1.0f, 2.0f));
    NXC_CHECK_OK(nxc_path_line_to(&small, 3.0f, 4.0f));
    NXC_CHECK(nxc__font_glyph_outline(f, nxc__font_glyph_index(f, '@'), &small, 0.01f, 0, 0) == NXC_ERR_OVERFLOW);
    NXC_CHECK(small.npts == 2 && small.nverbs == 2 && small.error == NXC_OK);
    NXC_CHECK(small.pts[1].x == 3.0f && small.verbs[1] == NXC_VERB_LINE);

    /* Huge but valid scale and origin stay finite. */
    nxc_path_reset(&p);
    nxc_status_t st = nxc__font_glyph_outline(f, nxc__font_glyph_index(f, 'W'), &p, NXC_FONT_MAX_SCALE,
                                              NXC_FONT_MAX_COORD, -NXC_FONT_MAX_COORD);
    NXC_CHECK(st == NXC_OK || (st == NXC_ERR_INVALID_ARG && p.nverbs == 0));
    for (uint32_t i = 0; i < p.npts; i++) NXC_CHECK(isfinite(p.pts[i].x) && isfinite(p.pts[i].y));

    NXC_CHECK(nxc__font_id(f) != 0);
    nxc_font_destroy(f);
    free(b.data);
}

/* Force the format 4 subtable by renaming the full-repertoire cmap records
 * and compare every BMP code point with the format 12 result. */
static void test_cmap_format4(void) {
    blob_t b;
    nxc_font_t *f12 = load_font("NXSans/NXSans-Text.ttf", &b);
    if (!f12) { free(b.data); return; }
    uint8_t *copy = malloc(b.size);
    memcpy(copy, b.data, b.size);
    uint32_t ntab = (uint32_t)(copy[4] << 8 | copy[5]), cmap = 0;
    for (uint32_t i = 0; i < ntab; i++) {
        uint8_t *r = copy + 12 + 16 * i;
        if (!memcmp(r, "cmap", 4)) cmap = (uint32_t)r[8] << 24 | (uint32_t)r[9] << 16 | (uint32_t)r[10] << 8 | r[11];
    }
    uint32_t n = (uint32_t)(copy[cmap + 2] << 8 | copy[cmap + 3]);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *r = copy + cmap + 4 + 8 * i;
        uint32_t sub = cmap + ((uint32_t)r[4] << 24 | (uint32_t)r[5] << 16 | (uint32_t)r[6] << 8 | r[7]);
        if (copy[sub] == 0 && copy[sub + 1] == 12) { r[0] = 0x7F; r[1] = 0x7F; }   /* unknown platform */
    }
    nxc_font_t *f4 = NULL;
    NXC_CHECK_OK(nxc_font_load(&f4, copy, b.size));
    if (f4) {
        NXC_CHECK(f4->cmap_format == 4 && f12->cmap_format == 12);
        uint32_t diff = 0, mapped = 0;
        for (uint32_t cp = 0; cp <= 0xFFFFu; cp++) {
            uint32_t a = nxc__font_glyph_index(f12, cp), c = nxc__font_glyph_index(f4, cp);
            if (a != c) diff++;
            if (c) mapped++;
        }
        NXC_CHECK(diff == 0);
        NXC_CHECK(mapped > 1000);
        NXC_CHECK(nxc__font_glyph_index(f4, 0x1F600u) == 0);
        nxc_font_destroy(f4);
    }
    nxc_font_destroy(f12);
    free(copy);
    free(b.data);
}

/* ============ Synthetic font builder ============ */

typedef struct { uint8_t *b; size_t n, cap; } buf_t;

static void b_grow(buf_t *b, size_t add) {
    if (b->n + add <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->n + add) nc *= 2;
    b->b = realloc(b->b, nc);
    memset(b->b + b->cap, 0, nc - b->cap);
    b->cap = nc;
}
static void b_u8(buf_t *b, uint32_t v) { b_grow(b, 1); b->b[b->n++] = (uint8_t)v; }
static void b_u16(buf_t *b, uint32_t v) { b_u8(b, v >> 8); b_u8(b, v); }
static void b_s16(buf_t *b, int32_t v) { b_u16(b, (uint32_t)v & 0xFFFFu); }
static void b_u32(buf_t *b, uint32_t v) { b_u16(b, v >> 16); b_u16(b, v); }
static void b_tag(buf_t *b, const char *t) { for (int i = 0; i < 4; i++) b_u8(b, (uint8_t)t[i]); }
static void b_zero(buf_t *b, size_t n) { for (size_t i = 0; i < n; i++) b_u8(b, 0); }
static void b_pad(buf_t *b, size_t a) { while (b->n % a) b_u8(b, 0); }
static void b_put16(buf_t *b, size_t at, uint32_t v) { b->b[at] = (uint8_t)(v >> 8); b->b[at + 1] = (uint8_t)v; }
static void b_put32(buf_t *b, size_t at, uint32_t v) { b_put16(b, at, v >> 16); b_put16(b, at + 2, v); }
static void b_free(buf_t *b) { free(b->b); memset(b, 0, sizeof(*b)); }

typedef struct { int32_t x, y, on; } spt_t;

/* Encode a simple glyph: flags with repeat compression, short/same/word
 * deltas chosen per point (so every encoding path appears). */
static void enc_simple(buf_t *g, const spt_t *pts, const uint16_t *ends, int nc) {
    int np = ends[nc - 1] + 1;
    int32_t x0 = pts[0].x, x1 = pts[0].x, y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < np; i++) {
        if (pts[i].x < x0) x0 = pts[i].x;
        if (pts[i].x > x1) x1 = pts[i].x;
        if (pts[i].y < y0) y0 = pts[i].y;
        if (pts[i].y > y1) y1 = pts[i].y;
    }
    b_s16(g, nc); b_s16(g, x0); b_s16(g, y0); b_s16(g, x1); b_s16(g, y1);
    for (int c = 0; c < nc; c++) b_u16(g, ends[c]);
    b_u16(g, 0);                                         /* no instructions */
    uint8_t fl[64];
    int32_t px = 0, py = 0;
    for (int i = 0; i < np; i++) {
        int32_t dx = pts[i].x - px, dy = pts[i].y - py;
        uint8_t f = pts[i].on ? 0x01 : 0x00;
        if (dx == 0) f |= 0x10;
        else if (dx > -256 && dx < 256) f |= 0x02 | (dx > 0 ? 0x10 : 0);
        if (dy == 0) f |= 0x20;
        else if (dy > -256 && dy < 256) f |= 0x04 | (dy > 0 ? 0x20 : 0);
        fl[i] = f;
        px = pts[i].x; py = pts[i].y;
    }
    for (int i = 0; i < np;) {
        int run = 1;
        while (i + run < np && fl[i + run] == fl[i] && run < 255) run++;
        if (run >= 2) { b_u8(g, fl[i] | 0x08); b_u8(g, (uint32_t)(run - 1)); }
        else b_u8(g, fl[i]);
        i += run;
    }
    px = 0;
    for (int i = 0; i < np; i++) {
        int32_t dx = pts[i].x - px;
        if (fl[i] & 0x02) b_u8(g, (uint32_t)(dx < 0 ? -dx : dx));
        else if (!(fl[i] & 0x10)) b_s16(g, dx);
        px = pts[i].x;
    }
    py = 0;
    for (int i = 0; i < np; i++) {
        int32_t dy = pts[i].y - py;
        if (fl[i] & 0x04) b_u8(g, (uint32_t)(dy < 0 ? -dy : dy));
        else if (!(fl[i] & 0x20)) b_s16(g, dy);
        py = pts[i].y;
    }
}

static void comp_header(buf_t *g) { b_s16(g, -1); b_s16(g, -500); b_s16(g, -500); b_s16(g, 2000); b_s16(g, 2000); }

/* One component: flags must already include CG flags; args words when the
 * values do not fit in a signed byte. */
static void comp(buf_t *g, uint32_t flags, uint32_t glyph, int32_t a1, int32_t a2,
                 const int32_t *xf, int nxf) {
    int words = a1 < -128 || a1 > 127 || a2 < -128 || a2 > 127;
    if (words) flags |= 0x0001;
    b_u16(g, flags); b_u16(g, glyph);
    if (words) { b_s16(g, a1); b_s16(g, a2); } else { b_u8(g, (uint32_t)a1 & 0xFF); b_u8(g, (uint32_t)a2 & 0xFF); }
    for (int i = 0; i < nxf; i++) b_s16(g, xf[i]);
}

#define SYN_GLYPHS 22

/* Glyph programs of the synthetic font (see test_synthetic for the
 * expected outlines). */
static void build_glyph(buf_t *g, int id) {
    static const spt_t rect[4] = { { 100, 0, 1 }, { 100, 700, 1 }, { 500, 700, 1 }, { 500, 0, 1 } };
    static const uint16_t rect_end[1] = { 3 };
    static const spt_t g2[] = {
        { 10, 10, 1 }, { 10, 210, 1 }, { 210, 210, 1 }, { 210, 10, 1 },            /* C0 square */
        { 400, 100, 0 }, { 500, 200, 0 }, { 600, 100, 0 }, { 500, 0, 0 },          /* C1 all off */
        { 700, 0, 0 }, { 800, 100, 1 }, { 900, 0, 0 }, { 800, -100, 1 },           /* C2 starts off */
        { 0, 0, 1 },                                                               /* C3 anchor */
        { 1000, 0, 1 }, { 1100, 100, 0 }, { 1200, 100, 0 }, { 1300, 0, 1 },        /* C4 implied mid */
        { 2000, 3000, 0 }, { 2300, 3300, 0 }, { 2600, 3600, 0 }, { 2900, 3900, 1 } /* C5 repeat flags */
    };
    static const uint16_t g2_end[6] = { 3, 7, 11, 12, 16, 20 };
    switch (id) {
        case 0: enc_simple(g, rect, rect_end, 1); break;
        case 1: break;                                                   /* empty */
        case 2: enc_simple(g, g2, g2_end, 6); break;
        case 3: {
            comp_header(g);
            comp(g, 0x0002 | 0x0020, 2, 1000, 500, NULL, 0);
            int32_t sc[1] = { 8192 };                                    /* 0.5 */
            comp(g, 0x0002 | 0x0008, 0, -10, 20, sc, 1);
            break;
        }
        case 4: {
            comp_header(g);
            int32_t xy[2] = { 24576, -16384 };                           /* 1.5, -1 */
            comp(g, 0x0002 | 0x0040 | 0x0800 | 0x0020, 0, 100, 0, xy, 2);
            int32_t rot[4] = { 0, 16384, -16384, 0 };                    /* x'=-y, y'=x */
            comp(g, 0x0002 | 0x0080 | 0x0020, 0, 0, 0, rot, 4);
            int32_t sc[1] = { 8192 };
            comp(g, 0x0002 | 0x0008 | 0x0800 | 0x1000, 0, 100, 100, sc, 1);
            break;
        }
        case 5: comp_header(g); comp(g, 0x0002, 3, 5, 5, NULL, 0); break;
        case 6: comp_header(g); comp(g, 0x0002, 6, 1, 0, NULL, 0); break;   /* self */
        case 7:
            comp_header(g);
            comp(g, 0x0020, 0, 1, 2, NULL, 0);                         /* point matching */
            comp(g, 0x0002, 0, 0, 0, NULL, 0);
            break;
        case 8: case 9: case 10: case 11: case 12:
            comp_header(g); comp(g, 0x0002, (uint32_t)id + 1, 1, 0, NULL, 0); break;
        case 13: comp_header(g); comp(g, 0x0002, 0, 1, 0, NULL, 0); break;
        case 14: comp_header(g); comp(g, 0x0002, 8, 0, 0, NULL, 0); break;  /* 7 levels */
        case 15: case 16:
            comp_header(g);
            for (int i = 0; i < 20; i++) comp(g, 0x0002 | (i < 19 ? 0x0020 : 0), id == 15 ? 16 : 0, i, 0, NULL, 0);
            break;
        case 17: comp_header(g); comp(g, 0x0002, 9999, 0, 0, NULL, 0); break;
        case 18:                                                         /* endPts not increasing */
            b_s16(g, 2); b_zero(g, 8); b_u16(g, 3); b_u16(g, 2); b_u16(g, 0);
            for (int i = 0; i < 4; i++) b_u8(g, 0x37);
            for (int i = 0; i < 8; i++) b_u8(g, 1);
            break;
        case 19:                                                         /* coordinates cut short */
            b_s16(g, 1); b_zero(g, 8); b_u16(g, 3); b_u16(g, 0);
            b_u8(g, 0x01 | 0x08); b_u8(g, 3);                            /* 4 points, word x and y */
            b_s16(g, 10); b_s16(g, 20);
            break;
        case 20: b_s16(g, 0); b_zero(g, 8); break;                      /* zero contours */
        case 21:                                                         /* repeat count overflows */
            b_s16(g, 1); b_zero(g, 8); b_u16(g, 3); b_u16(g, 0);
            b_u8(g, 0x37 | 0x08); b_u8(g, 9);
            for (int i = 0; i < 16; i++) b_u8(g, 1);
            break;
        default: break;
    }
}

typedef struct {
    int loc_long;      /* 0 short loca, 1 long */
    int kern;          /* 0 none, 1 Microsoft, 2 Apple */
    int gpos;          /* include the synthetic GPOS */
    int os2;           /* 0 none, 1 version 1, 4 version 4 with USE_TYPO_METRICS */
    int symbol;        /* (3,0) symbol cmap instead of (3,1) + (1,0) */
} syn_opts_t;

static void build_cmap(buf_t *t, int symbol) {
    b_u16(t, 0);
    if (symbol) {
        b_u16(t, 1);
        b_u16(t, 3); b_u16(t, 0); b_u32(t, 12);
        size_t s = t->n;
        b_u16(t, 4); b_u16(t, 0); b_u16(t, 0); b_u16(t, 4); b_u16(t, 4); b_u16(t, 1); b_u16(t, 0);
        b_u16(t, 0xF043); b_u16(t, 0xFFFF); b_u16(t, 0);
        b_u16(t, 0xF041); b_u16(t, 0xFFFF);
        b_u16(t, (uint32_t)(2 - 0xF041) & 0xFFFF); b_u16(t, 1);
        b_u16(t, 0); b_u16(t, 0);
        b_put16(t, s + 2, (uint32_t)(t->n - s));
        return;
    }
    b_u16(t, 2);
    b_u16(t, 1); b_u16(t, 0); b_u32(t, 20);                 /* Mac format 0: ignored */
    b_u16(t, 3); b_u16(t, 1); b_u32(t, 20 + 262);
    b_u16(t, 0); b_u16(t, 262); b_u16(t, 0);
    for (int i = 0; i < 256; i++) b_u8(t, i == 'A' ? 3 : 0);  /* would map 'A' wrongly */
    size_t s = t->n;
    const int seg = 6;
    b_u16(t, 4); b_u16(t, 0); b_u16(t, 0); b_u16(t, 2 * seg); b_u16(t, 8); b_u16(t, 2); b_u16(t, 4);
    const uint32_t ends[6]   = { 0x20, 0x43, 0x63, 0xE9, 0x20AC, 0xFFFF };
    const uint32_t starts[6] = { 0x20, 0x41, 0x61, 0xE9, 0x20AC, 0xFFFF };
    const int32_t deltas[6]  = { 1 - 0x20, 2 - 0x41, 0, 2, 20 - 0x20AC, 1 };
    const uint32_t ranges[6] = { 0, 0, 2 * (6 - 2) + 0, 2 * (6 - 3) + 2 * 3, 0, 0 };
    for (int i = 0; i < seg; i++) b_u16(t, ends[i]);
    b_u16(t, 0);
    for (int i = 0; i < seg; i++) b_u16(t, starts[i]);
    for (int i = 0; i < seg; i++) b_u16(t, (uint32_t)deltas[i] & 0xFFFF);
    for (int i = 0; i < seg; i++) b_u16(t, ranges[i]);
    b_u16(t, 5); b_u16(t, 0); b_u16(t, 7);                  /* 'a'..'c' */
    b_u16(t, 8);                                              /* U+00E9 -> 8 + 2 */
    b_put16(t, s + 2, (uint32_t)(t->n - s));
}

static void build_kern(buf_t *t, int apple) {
    static const int32_t pairs[3][3] = { { 2, 3, -40 }, { 2, 4, 25 }, { 3, 2, -10 } };
    if (apple) {
        b_u32(t, 0x00010000u); b_u32(t, 1);
        b_u32(t, 8 + 8 + 6 * 3); b_u16(t, 0x0000); b_u16(t, 0);
    } else {
        b_u16(t, 0); b_u16(t, 2);
        /* cross-stream subtable first: must be skipped */
        b_u16(t, 0); b_u16(t, 6 + 8 + 6); b_u16(t, 0x0005);
        b_u16(t, 1); b_u16(t, 6); b_u16(t, 0); b_u16(t, 0);
        b_u16(t, 2); b_u16(t, 3); b_s16(t, 999);
        b_u16(t, 0); b_u16(t, 6 + 8 + 6 * 3); b_u16(t, 0x0001);
    }
    b_u16(t, 3); b_u16(t, 12); b_u16(t, 1); b_u16(t, 6);
    for (int i = 0; i < 3; i++) { b_u16(t, (uint32_t)pairs[i][0]); b_u16(t, (uint32_t)pairs[i][1]); b_s16(t, pairs[i][2]); }
}

/* GPOS: lookups 0 (type 2: format 1 then format 2) and 1 (type 9 ->
 * format 2) belong to two 'kern' features (lookup 1 shared); lookup 2 is
 * only in 'mark' and must be ignored. */
static void build_gpos(buf_t *t) {
    b_u16(t, 1); b_u16(t, 0);
    size_t hdr = t->n; b_u16(t, 0); b_u16(t, 0); b_u16(t, 0);
    /* ScriptList */
    size_t sl = t->n; b_put16(t, hdr, (uint32_t)sl);
    b_u16(t, 1); b_tag(t, "DFLT"); b_u16(t, 8);
    b_u16(t, 4); b_u16(t, 0);                                   /* Script */
    b_u16(t, 0); b_u16(t, 0xFFFF); b_u16(t, 3); b_u16(t, 0); b_u16(t, 1); b_u16(t, 2);
    /* FeatureList */
    size_t fl = t->n; b_put16(t, hdr + 2, (uint32_t)fl);
    b_u16(t, 3);
    size_t fr = t->n;
    b_tag(t, "kern"); b_u16(t, 0); b_tag(t, "kern"); b_u16(t, 0); b_tag(t, "mark"); b_u16(t, 0);
    b_put16(t, fr + 4, (uint32_t)(t->n - fl)); b_u16(t, 0); b_u16(t, 2); b_u16(t, 0); b_u16(t, 1);
    b_put16(t, fr + 10, (uint32_t)(t->n - fl)); b_u16(t, 0); b_u16(t, 1); b_u16(t, 1);
    b_put16(t, fr + 16, (uint32_t)(t->n - fl)); b_u16(t, 0); b_u16(t, 1); b_u16(t, 2);
    /* LookupList */
    size_t ll = t->n; b_put16(t, hdr + 4, (uint32_t)ll);
    b_u16(t, 3); size_t lo = t->n; b_u16(t, 0); b_u16(t, 0); b_u16(t, 0);
    size_t l0 = t->n; b_put16(t, lo, (uint32_t)(l0 - ll));
    b_u16(t, 2); b_u16(t, 0); b_u16(t, 2); size_t l0s = t->n; b_u16(t, 0); b_u16(t, 0);
    size_t l1 = t->n; b_put16(t, lo + 2, (uint32_t)(l1 - ll));
    b_u16(t, 9); b_u16(t, 0); b_u16(t, 1); size_t l1s = t->n; b_u16(t, 0);
    size_t l2 = t->n; b_put16(t, lo + 4, (uint32_t)(l2 - ll));
    b_u16(t, 2); b_u16(t, 0); b_u16(t, 1); size_t l2s = t->n; b_u16(t, 0);

    /* S0: format 1, A(2) -> {V(3): -50, glyph 5: -30} */
    size_t s0 = t->n; b_put16(t, l0s, (uint32_t)(s0 - l0));
    b_u16(t, 1); b_u16(t, 12); b_u16(t, 0x0004); b_u16(t, 0); b_u16(t, 1); b_u16(t, 18);
    b_u16(t, 1); b_u16(t, 1); b_u16(t, 2);                       /* coverage @12 */
    b_u16(t, 2); b_u16(t, 3); b_s16(t, -50); b_u16(t, 5); b_s16(t, -30);   /* pair set @18 */
    /* S1: format 2, value1 = XPlacement + XAdvance, value2 = XAdvance */
    size_t s1 = t->n; b_put16(t, l0s + 2, (uint32_t)(s1 - l0));
    b_u16(t, 2); b_u16(t, 0); b_u16(t, 0x0005); b_u16(t, 0x0004); b_u16(t, 0); b_u16(t, 0);
    b_u16(t, 2); b_u16(t, 2);
    for (int r = 0; r < 3; r++) { b_s16(t, 0); b_s16(t, 0); b_s16(t, 0); }
    b_s16(t, 77); b_s16(t, -20); b_s16(t, 55);
    b_put16(t, s1 + 2, (uint32_t)(t->n - s1)); b_u16(t, 2); b_u16(t, 1); b_u16(t, 2); b_u16(t, 2); b_u16(t, 0);
    b_put16(t, s1 + 8, (uint32_t)(t->n - s1)); b_u16(t, 1); b_u16(t, 2); b_u16(t, 1); b_u16(t, 1);
    b_put16(t, s1 + 10, (uint32_t)(t->n - s1)); b_u16(t, 2); b_u16(t, 1); b_u16(t, 3); b_u16(t, 4); b_u16(t, 1);
    /* E0 -> S2: format 2 through an extension subtable */
    size_t e0 = t->n; b_put16(t, l1s, (uint32_t)(e0 - l1));
    b_u16(t, 1); b_u16(t, 2); b_u32(t, 8);
    size_t s2 = t->n;
    b_u16(t, 2); b_u16(t, 0); b_u16(t, 0x0004); b_u16(t, 0); b_u16(t, 0); b_u16(t, 0); b_u16(t, 3); b_u16(t, 2);
    b_s16(t, 0); b_s16(t, 0); b_s16(t, -3); b_s16(t, -7); b_s16(t, 9); b_s16(t, 0);
    b_put16(t, s2 + 2, (uint32_t)(t->n - s2)); b_u16(t, 1); b_u16(t, 2); b_u16(t, 2); b_u16(t, 3);
    b_put16(t, s2 + 8, (uint32_t)(t->n - s2)); b_u16(t, 2); b_u16(t, 2);
    b_u16(t, 2); b_u16(t, 2); b_u16(t, 1); b_u16(t, 3); b_u16(t, 3); b_u16(t, 2);
    b_put16(t, s2 + 10, (uint32_t)(t->n - s2)); b_u16(t, 1); b_u16(t, 3); b_u16(t, 2); b_u16(t, 1); b_u16(t, 0);
    /* S3: lookup 2 ('mark' only): A V +1000 */
    size_t s3 = t->n; b_put16(t, l2s, (uint32_t)(s3 - l2));
    b_u16(t, 1); b_u16(t, 12); b_u16(t, 0x0004); b_u16(t, 0); b_u16(t, 1); b_u16(t, 18);
    b_u16(t, 1); b_u16(t, 1); b_u16(t, 2);
    b_u16(t, 1); b_u16(t, 3); b_s16(t, 1000);
}

/* Assemble a complete synthetic TrueType font. Caller frees out->data. */
static blob_t build_font(const syn_opts_t *o) {
    buf_t glyf = { 0 }, loca = { 0 };
    uint32_t offs[SYN_GLYPHS + 1];
    for (int g = 0; g < SYN_GLYPHS; g++) {
        offs[g] = (uint32_t)glyf.n;
        build_glyph(&glyf, g);
        b_pad(&glyf, 4);
    }
    offs[SYN_GLYPHS] = (uint32_t)glyf.n;
    for (int g = 0; g <= SYN_GLYPHS; g++) {
        if (o->loc_long) b_u32(&loca, offs[g]); else b_u16(&loca, offs[g] / 2);
    }
    buf_t head = { 0 }, maxp = { 0 }, hhea = { 0 }, hmtx = { 0 }, cmap = { 0 }, os2 = { 0 }, kern = { 0 }, gpos = { 0 };
    b_u32(&head, 0x00010000u); b_u32(&head, 0x00010000u); b_u32(&head, 0); b_u32(&head, 0x5F0F3CF5u);
    b_u16(&head, 0); b_u16(&head, 1000); b_zero(&head, 16);
    b_s16(&head, -1000); b_s16(&head, -1000); b_s16(&head, 4000); b_s16(&head, 4000);
    b_u16(&head, 0); b_u16(&head, 8); b_s16(&head, 2); b_s16(&head, o->loc_long); b_s16(&head, 0);
    b_u32(&maxp, 0x00010000u); b_u16(&maxp, SYN_GLYPHS); b_zero(&maxp, 26);
    b_u32(&hhea, 0x00010000u); b_s16(&hhea, 800); b_s16(&hhea, -200); b_s16(&hhea, 0);
    b_zero(&hhea, 24); b_u16(&hhea, 3);
    b_u16(&hmtx, 500); b_s16(&hmtx, 0); b_u16(&hmtx, 250); b_s16(&hmtx, 0); b_u16(&hmtx, 600); b_s16(&hmtx, 0);
    for (int g = 3; g < SYN_GLYPHS; g++) b_s16(&hmtx, 0);
    build_cmap(&cmap, o->symbol);
    if (o->os2) {
        b_u16(&os2, o->os2 == 4 ? 4 : 1); b_zero(&os2, 60);
        b_u16(&os2, o->os2 == 4 ? 0x00C0 : 0x0040);           /* fsSelection @62 */
        b_u16(&os2, 0x20); b_u16(&os2, 0xFFFF);
        b_s16(&os2, 900); b_s16(&os2, -300); b_s16(&os2, 50);   /* typo @68 */
        b_u16(&os2, 1100); b_u16(&os2, 350);                     /* win @74 */
        b_zero(&os2, 8);
        if (o->os2 == 4) { b_s16(&os2, 480); b_s16(&os2, 650); b_zero(&os2, 6); }
    }
    if (o->kern) build_kern(&kern, o->kern == 2);
    if (o->gpos) build_gpos(&gpos);

    struct { const char *tag; buf_t *b; } tabs[] = {
        { "GPOS", &gpos }, { "OS/2", &os2 }, { "cmap", &cmap }, { "glyf", &glyf }, { "head", &head },
        { "hhea", &hhea }, { "hmtx", &hmtx }, { "kern", &kern }, { "loca", &loca }, { "maxp", &maxp },
    };
    int nt = 0;
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) if (tabs[i].b->n) nt++;
    buf_t f = { 0 };
    b_u32(&f, 0x00010000u); b_u16(&f, (uint32_t)nt); b_u16(&f, 0); b_u16(&f, 0); b_u16(&f, 0);
    size_t dir = f.n;
    b_zero(&f, 16 * (size_t)nt);
    int k = 0;
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
        if (!tabs[i].b->n) continue;
        b_pad(&f, 4);
        size_t at = f.n;
        b_grow(&f, tabs[i].b->n);
        memcpy(f.b + f.n, tabs[i].b->b, tabs[i].b->n);
        f.n += tabs[i].b->n;
        size_t r = dir + 16 * (size_t)k++;
        memcpy(f.b + r, tabs[i].tag, 4);
        b_put32(&f, r + 8, (uint32_t)at);
        b_put32(&f, r + 12, (uint32_t)tabs[i].b->n);
        b_free(tabs[i].b);
    }
    blob_t out;
    out.size = (uint32_t)f.n;
    out.data = malloc(f.n);
    memcpy(out.data, f.b, f.n);
    b_free(&f);
    return out;
}

/* ============ Synthetic font tests ============ */

typedef struct { uint8_t verb; float x0, y0, x1, y1; } exp_seg_t;

/* Compare path verbs/points from (v0, p0) against an expected sequence. */
static int path_matches(const nxc_path_t *p, uint32_t v0, uint32_t p0, const exp_seg_t *e, int n) {
    uint32_t pi = p0;
    for (int i = 0; i < n; i++) {
        if (v0 + (uint32_t)i >= p->nverbs || p->verbs[v0 + i] != e[i].verb) {
            fprintf(stderr, "  verb %d mismatch\n", i);
            return 0;
        }
        int np = e[i].verb == NXC_VERB_QUAD ? 2 : e[i].verb == NXC_VERB_CLOSE ? 0 : 1;
        float want[4] = { e[i].x0, e[i].y0, e[i].x1, e[i].y1 };
        for (int k = 0; k < np; k++) {
            if (fabsf(p->pts[pi + k].x - want[2 * k]) > 1e-3f || fabsf(p->pts[pi + k].y - want[2 * k + 1]) > 1e-3f) {
                fprintf(stderr, "  seg %d pt %d: (%g,%g) want (%g,%g)\n", i, k, p->pts[pi + k].x,
                        p->pts[pi + k].y, want[2 * k], want[2 * k + 1]);
                return 0;
            }
        }
        pi += (uint32_t)np;
    }
    return 1;
}

#define M_(x, y)          { NXC_VERB_MOVE, (x), (y), 0, 0 }
#define L_(x, y)          { NXC_VERB_LINE, (x), (y), 0, 0 }
#define Q_(cx, cy, x, y)  { NXC_VERB_QUAD, (cx), (cy), (x), (y) }
#define Z_                { NXC_VERB_CLOSE, 0, 0, 0, 0 }

/* Device mapping used by the exact checks: X = 10 + fx/2, Y = 2000 - fy/2. */
#define DX(v) (10.0f + (float)(v) * 0.5f)
#define DY(v) (2000.0f - (float)(v) * 0.5f)

static void check_glyph2(const nxc_path_t *p, uint32_t v0, uint32_t p0, float ox, float oy) {
#define X(v) DX((v) + ox)
#define Y(v) DY((v) + oy)
    const exp_seg_t e[] = {
        M_(X(10), Y(10)), L_(X(10), Y(210)), L_(X(210), Y(210)), L_(X(210), Y(10)), Z_,
        M_(X(450), Y(50)), Q_(X(400), Y(100), X(450), Y(150)), Q_(X(500), Y(200), X(550), Y(150)),
        Q_(X(600), Y(100), X(550), Y(50)), Q_(X(500), Y(0), X(450), Y(50)), Z_,
        M_(X(800), Y(-100)), Q_(X(700), Y(0), X(800), Y(100)), Q_(X(900), Y(0), X(800), Y(-100)), Z_,
        M_(X(1000), Y(0)), Q_(X(1100), Y(100), X(1150), Y(100)), Q_(X(1200), Y(100), X(1300), Y(0)), Z_,
        M_(X(2900), Y(3900)), Q_(X(2000), Y(3000), X(2150), Y(3150)), Q_(X(2300), Y(3300), X(2450), Y(3450)),
        Q_(X(2600), Y(3600), X(2900), Y(3900)), Z_,
    };
#undef X
#undef Y
    NXC_CHECK(path_matches(p, v0, p0, e, (int)(sizeof(e) / sizeof(e[0]))));
}

static void check_rect(const nxc_path_t *p, uint32_t v0, uint32_t p0,
                       float a, float b, float c, float d, float ox, float oy) {
    /* rect corners (100,0) (100,700) (500,700) (500,0) under x'=a x + c y + ox, y'=b x + d y + oy */
    const float cx[4] = { 100, 100, 500, 500 }, cy[4] = { 0, 700, 700, 0 };
    exp_seg_t e[5];
    for (int i = 0; i < 4; i++) {
        float X = a * cx[i] + c * cy[i] + ox, Y = b * cx[i] + d * cy[i] + oy;
        exp_seg_t s = { (uint8_t)(i == 0 ? NXC_VERB_MOVE : NXC_VERB_LINE), DX(X), DY(Y), 0, 0 };
        e[i] = s;
    }
    exp_seg_t z = Z_;
    e[4] = z;
    NXC_CHECK(path_matches(p, v0, p0, e, 5));
}

static nxc_status_t syn_outline(const nxc_font_t *f, uint32_t g, nxc_path_t *p) {
    nxc_path_reset(p);
    return nxc__font_glyph_outline(f, g, p, 0.5f, 10.0f, 2000.0f);
}

static void test_synthetic(void) {
    nxc_path_t p;
    path_new(&p);

    /* ---- Variant A: short loca, Microsoft kern, OS/2 v1, (3,1) cmap ---- */
    syn_opts_t oa = { 0, 1, 0, 1, 0 };
    blob_t a = build_font(&oa);
    nxc_font_t *f = NULL;
    NXC_CHECK_OK(nxc_font_load(&f, a.data, a.size));
    if (!f) { free(a.data); return; }

    /* cmap format 4: delta, idRangeOffset, idRangeOffset + delta, misses */
    NXC_CHECK(nxc__font_glyph_index(f, ' ') == 1);
    NXC_CHECK(nxc__font_glyph_index(f, 'A') == 2 && nxc__font_glyph_index(f, 'B') == 3);
    NXC_CHECK(nxc__font_glyph_index(f, 'C') == 4 && nxc__font_glyph_index(f, 'D') == 0);
    NXC_CHECK(nxc__font_glyph_index(f, 'a') == 5 && nxc__font_glyph_index(f, 'b') == 0);
    NXC_CHECK(nxc__font_glyph_index(f, 'c') == 7 && nxc__font_glyph_index(f, 'd') == 0);
    NXC_CHECK(nxc__font_glyph_index(f, 0xE9) == 10);
    NXC_CHECK(nxc__font_glyph_index(f, 0x20AC) == 20 && nxc__font_glyph_index(f, 0x20AB) == 0);
    NXC_CHECK(nxc__font_glyph_index(f, 0x20AD) == 0 && nxc__font_glyph_index(f, 0xFFFF) == 0);
    NXC_CHECK(nxc__font_glyph_index(f, 0x1F600) == 0 && nxc__font_glyph_index(f, 0) == 0);

    /* hhea metrics (no USE_TYPO_METRICS), cap/x height fallbacks */
    int32_t asc, desc, gap, cap, xh;
    NXC_CHECK_OK(nxc__font_vmetrics(f, &asc, &desc, &gap, &cap, &xh));
    NXC_CHECK(asc == 800 && desc == 200 && gap == 0 && cap == 700 && xh == 500);

    /* hmtx: glyphs past numberOfHMetrics take the last advance */
    NXC_CHECK(nxc__font_advance(f, 0) == 500 && nxc__font_advance(f, 1) == 250);
    NXC_CHECK(nxc__font_advance(f, 2) == 600 && nxc__font_advance(f, 21) == 600);

    /* Microsoft kern table (cross-stream subtable skipped) */
    NXC_CHECK(nxc__font_kerning(f, 2, 3) == -40 && nxc__font_kerning(f, 2, 4) == 25);
    NXC_CHECK(nxc__font_kerning(f, 3, 2) == -10 && nxc__font_kerning(f, 3, 3) == 0);
    NXC_CHECK(nxc__font_kerning(f, 0, 2) == 0 && nxc__font_kerning(f, 4, 4) == 0);

    /* Simple glyph decoding, exact device coordinates */
    NXC_CHECK_OK(syn_outline(f, 2, &p));
    check_glyph2(&p, 0, 0, 0, 0);
    path_info_t in = path_info(&p, 0, 0);
    NXC_CHECK(in.moves == 5 && in.closes == 5);
    NXC_CHECK(p.npts <= NXC_FONT_PATH_PTS(21, 6) && p.nverbs <= NXC_FONT_PATH_VERBS(21, 6));
    int16_t x0, y0, x1, y1;
    NXC_CHECK_OK(nxc__font_glyph_bounds(f, 2, &x0, &y0, &x1, &y1));
    NXC_CHECK(x0 == 0 && y0 == -100 && x1 == 2900 && y1 == 3900);
    NXC_CHECK_OK(nxc__font_glyph_bounds(f, 1, &x0, &y0, &x1, &y1));
    NXC_CHECK(x0 == 0 && y0 == 0 && x1 == 0 && y1 == 0);
    NXC_CHECK(nxc__font_glyph_bounds(f, 1, NULL, &y0, &x1, &y1) == NXC_ERR_INVALID_ARG);
    NXC_CHECK_OK(syn_outline(f, 1, &p));
    NXC_CHECK(p.nverbs == 0);
    NXC_CHECK_OK(syn_outline(f, 20, &p));
    NXC_CHECK(p.nverbs == 0);

    /* Composite: glyph 2 at (1000, 500) then glyph 0 scaled 0.5 at (-10, 20) */
    NXC_CHECK_OK(syn_outline(f, 3, &p));
    check_glyph2(&p, 0, 0, 1000, 500);
    check_rect(&p, 24, 30, 0.5f, 0, 0, 0.5f, -10, 20);
    NXC_CHECK(p.nverbs == 29);

    /* X/Y scale with scaled offset, 2x2 rotation, unscaled offset wins */
    NXC_CHECK_OK(syn_outline(f, 4, &p));
    check_rect(&p, 0, 0, 1.5f, 0, 0, -1.0f, 150, 0);
    check_rect(&p, 5, 4, 0, 1.0f, -1.0f, 0, 0, 0);
    check_rect(&p, 10, 8, 0.5f, 0, 0, 0.5f, 100, 100);

    /* Nested composite and the depth limit (6 composite levels allowed) */
    NXC_CHECK_OK(syn_outline(f, 5, &p));
    check_glyph2(&p, 0, 0, 1005, 505);
    NXC_CHECK_OK(syn_outline(f, 8, &p));
    check_rect(&p, 0, 0, 1, 0, 0, 1, 6, 0);
    uint32_t before = nxc_error_count();
    NXC_CHECK(syn_outline(f, 14, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);
    NXC_CHECK(nxc_error_count() == before + 1);
    NXC_CHECK(syn_outline(f, 6, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);     /* cycle */
    NXC_CHECK(syn_outline(f, 15, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);    /* 420 visits */
    NXC_CHECK_OK(syn_outline(f, 16, &p));                                        /* 20 visits */
    NXC_CHECK(path_info(&p, 0, 0).moves == 20);
    NXC_CHECK(syn_outline(f, 17, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);    /* bad glyph id */

    /* Point-matched component skipped, the rest kept */
    NXC_CHECK(syn_outline(f, 7, &p) == NXC_ERR_UNSUPPORTED);
    check_rect(&p, 0, 0, 1, 0, 0, 1, 0, 0);
    NXC_CHECK(p.nverbs == 5);

    /* Malformed simple glyphs, and a failure after content keeps it intact */
    NXC_CHECK(syn_outline(f, 18, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);
    NXC_CHECK(syn_outline(f, 19, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);
    NXC_CHECK(syn_outline(f, 21, &p) == NXC_ERR_BAD_FONT && p.nverbs == 0);
    NXC_CHECK_OK(syn_outline(f, 0, &p));
    NXC_CHECK(nxc__font_glyph_outline(f, 14, &p, 0.5f, 10, 2000) == NXC_ERR_BAD_FONT);
    NXC_CHECK(p.nverbs == 5 && p.npts == 4 && p.error == NXC_OK);
    check_rect(&p, 0, 0, 1, 0, 0, 1, 0, 0);
    nxc_font_destroy(f);
    free(a.data);

    /* ---- Variant B: long loca, Apple kern + GPOS, OS/2 v4, symbol cmap ---- */
    syn_opts_t ob = { 1, 2, 1, 4, 1 };
    blob_t b = build_font(&ob);
    f = NULL;
    NXC_CHECK_OK(nxc_font_load(&f, b.data, b.size));
    if (f) {
        NXC_CHECK(nxc__font_glyph_index(f, 'A') == 2 && nxc__font_glyph_index(f, 0xF042) == 3);
        NXC_CHECK(nxc__font_glyph_index(f, 'C') == 4 && nxc__font_glyph_index(f, 'D') == 0);
        NXC_CHECK_OK(nxc__font_vmetrics(f, &asc, &desc, &gap, &cap, &xh));
        NXC_CHECK(asc == 900 && desc == 300 && gap == 50 && cap == 650 && xh == 480);
        NXC_CHECK(f->kern_nsubs == 3);
        /* lookup 0: format 1 first, else format 2; lookup 1 adds; lookup 2 ignored */
        NXC_CHECK(nxc__font_kerning(f, 2, 3) == -57);
        NXC_CHECK(nxc__font_kerning(f, 2, 4) == -23);
        NXC_CHECK(nxc__font_kerning(f, 2, 5) == -33);
        NXC_CHECK(nxc__font_kerning(f, 2, 2) == -3);
        NXC_CHECK(nxc__font_kerning(f, 3, 4) == 9 && nxc__font_kerning(f, 3, 2) == 9);
        NXC_CHECK(nxc__font_kerning(f, 4, 2) == 0 && nxc__font_kerning(f, 0, 3) == 0);
        NXC_CHECK_OK(syn_outline(f, 3, &p));
        check_glyph2(&p, 0, 0, 1000, 500);
        nxc_font_destroy(f);
    }
    free(b.data);

    /* ---- Variant C: long loca, Apple kern only, no OS/2 ---- */
    syn_opts_t oc = { 1, 2, 0, 0, 0 };
    blob_t c = build_font(&oc);
    f = NULL;
    NXC_CHECK_OK(nxc_font_load(&f, c.data, c.size));
    if (f) {
        NXC_CHECK(nxc__font_kerning(f, 2, 3) == -40 && nxc__font_kerning(f, 3, 2) == -10);
        NXC_CHECK(nxc__font_kerning(f, 2, 2) == 0);
        NXC_CHECK_OK(nxc__font_vmetrics(f, &asc, &desc, &gap, NULL, NULL));
        NXC_CHECK(asc == 800 && desc == 200);
        NXC_CHECK_OK(syn_outline(f, 2, &p));
        check_glyph2(&p, 0, 0, 0, 0);
        nxc_font_destroy(f);
    }
    free(c.data);
}

/* ============ Argument validation and structural rejection ============ */

static void test_load_validation(void) {
    nxc_font_t *f = (nxc_font_t *)1;
    uint8_t tiny[16] = { 0 };
    NXC_CHECK(nxc_font_load(NULL, tiny, 16) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_load(&f, NULL, 16) == NXC_ERR_INVALID_ARG && f == NULL);
    NXC_CHECK(nxc_font_load(&f, tiny, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_load(&f, tiny, 5) == NXC_ERR_BAD_FONT && f == NULL);
    NXC_CHECK(nxc_font_load(&f, tiny, 16) == NXC_ERR_BAD_FONT);
    memcpy(tiny, "OTTO", 4);
    NXC_CHECK(nxc_font_load(&f, tiny, 16) == NXC_ERR_BAD_FONT);
    memcpy(tiny, "ttcf", 4);
    NXC_CHECK(nxc_font_load(&f, tiny, 16) == NXC_ERR_BAD_FONT);
    nxc_font_destroy(NULL);
    NXC_CHECK(nxc__font_glyph_index(NULL, 'A') == 0 && nxc__font_advance(NULL, 0) == 0);
    NXC_CHECK(nxc__font_kerning(NULL, 1, 2) == 0 && nxc__font_id(NULL) == 0);
    NXC_CHECK(nxc__font_vmetrics(NULL, NULL, NULL, NULL, NULL, NULL) == NXC_ERR_INVALID_ARG);

    /* Structural damage to an otherwise valid synthetic font */
    syn_opts_t oa = { 0, 1, 1, 1, 0 };
    blob_t a = build_font(&oa);
    uint8_t *d = malloc(a.size);
    struct { const char *tag; uint32_t at; uint8_t val; } cases[] = {
        { "head", 12, 0x00 },     /* magic */
        { "head", 18, 0x00 },     /* unitsPerEm high byte 0 -> 1000 & 0xFF = 232 is valid, so set low too */
        { "head", 51, 0x07 },     /* indexToLocFormat 7 */
        { "maxp", 4, 0x00 },      /* numGlyphs high byte: 22 stays; see below */
        { "hhea", 35, 0x00 },     /* numberOfHMetrics = 0 */
        { "cmap", 3, 0x40 },      /* numTables = 64: records outside */
        { "loca", 1, 0xFF },      /* first offset beyond the next */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        memcpy(d, a.data, a.size);
        uint32_t nt = (uint32_t)(d[4] << 8 | d[5]);
        for (uint32_t k = 0; k < nt; k++) {
            uint8_t *r = d + 12 + 16 * k;
            if (memcmp(r, cases[i].tag, 4)) continue;
            uint32_t off = (uint32_t)r[8] << 24 | (uint32_t)r[9] << 16 | (uint32_t)r[10] << 8 | r[11];
            d[off + cases[i].at] = cases[i].val;
            if (i == 1) d[off + 19] = 0x08;                  /* unitsPerEm = 8: too small */
            if (i == 3) d[off + 5] = 0x00;                   /* numGlyphs = 0 */
        }
        f = NULL;
        NXC_CHECK(nxc_font_load(&f, d, a.size) == NXC_ERR_BAD_FONT && f == NULL);
    }
    /* Table record pointing past the end of the file */
    memcpy(d, a.data, a.size);
    d[12 + 12] = 0x7F;
    NXC_CHECK(nxc_font_load(&f, d, a.size) == NXC_ERR_BAD_FONT);
    /* Every truncation is rejected cleanly (exact-size copies under ASan). */
    uint32_t rejected = 0;
    for (uint32_t n = 1; n < a.size; n += (n < 256 ? 1 : 7)) {
        uint8_t *t = malloc(n);
        memcpy(t, a.data, n);
        f = NULL;
        nxc_status_t st = nxc_font_load(&f, t, n);
        if (st == NXC_ERR_BAD_FONT && f == NULL) rejected++;
        else { NXC_CHECK(st == NXC_OK); nxc_font_destroy(f); }
        free(t);
    }
    NXC_CHECK(rejected > 100);
    free(d);
    free(a.data);
}

/* ============ Fuzzing ============ */

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

typedef struct { uint32_t off, len; } region_t;

/* Load, then exercise every API on every glyph of a (possibly broken) font. */
static void exercise(const uint8_t *data, uint32_t size, uint32_t *loaded, nxc_path_t *p) {
    nxc_font_t *f = NULL;
    nxc_status_t st = nxc_font_load(&f, data, size);
    if (st != NXC_OK) {
        NXC_CHECK(f == NULL && (st == NXC_ERR_BAD_FONT));
        return;
    }
    (*loaded)++;
    uint32_t ng = nxc__font_num_glyphs(f);
    nxc_font_metrics_t m;
    NXC_CHECK_OK(nxc_font_metrics(f, 16.0f, &m));
    NXC_CHECK(isfinite(m.ascent) && isfinite(m.descent) && m.ascent >= 0 && m.descent >= 0);
    for (uint32_t g = 0; g < ng; g++) {
        nxc_path_reset(p);
        st = nxc__font_glyph_outline(f, g, p, 0.05f, 3.0f, 40.0f);
        NXC_CHECK(st == NXC_OK || st == NXC_ERR_BAD_FONT || st == NXC_ERR_UNSUPPORTED ||
                  st == NXC_ERR_OVERFLOW);
        if (st == NXC_ERR_BAD_FONT || st == NXC_ERR_OVERFLOW) NXC_CHECK(p->nverbs == 0);
        for (uint32_t i = 0; i < p->npts; i++) {
            if (!isfinite(p->pts[i].x) || !isfinite(p->pts[i].y)) { NXC_CHECK(0); break; }
        }
        int16_t a, b, c, d;
        (void)nxc__font_glyph_bounds(f, g, &a, &b, &c, &d);
        (void)nxc__font_advance(f, g);
        (void)nxc__font_kerning(f, g, (g * 7u + 3u) % ng);
        (void)nxc__font_kerning(f, (g * 13u + 1u) % ng, g);
    }
    for (uint32_t cp = 0; cp < 0x3000u; cp += 3) (void)nxc__font_glyph_index(f, cp);
    for (uint32_t k = 0; k < 64; k++) (void)nxc__font_glyph_index(f, rnd() % 0x110000u);
    float adv;
    nxc_path_reset(p);
    (void)nxc_font_glyph_path(f, 'A', 13.0f, 0, 20, p, &adv);
    nxc_font_destroy(f);
}

static void fuzz(const char *name, const blob_t *base, const region_t *hot, int nhot,
                 int iterations, int max_flips) {
    nxc_path_t p;
    path_new(&p);
    uint32_t loaded = 0;
    unsigned errors_before = nxc_error_count();
    g_quiet = 1;
    for (int it = 0; it < iterations; it++) {
        uint8_t *d = malloc(base->size);                  /* exact size: ASan sees overreads */
        memcpy(d, base->data, base->size);
        int flips = 1 + (int)(rnd() % (uint32_t)max_flips);
        for (int k = 0; k < flips; k++) {
            uint32_t pos;
            uint32_t sel = rnd() % 8u;
            if (sel < 6 && nhot > 0) {
                const region_t *r = &hot[rnd() % (uint32_t)nhot];
                pos = r->off + rnd() % (r->len ? r->len : 1);
            } else {
                pos = rnd() % base->size;
            }
            if (pos >= base->size) pos = base->size - 1;
            switch (rnd() % 4u) {
                case 0: d[pos] = (uint8_t)rnd(); break;
                case 1: d[pos] ^= (uint8_t)(1u << (rnd() % 8u)); break;
                case 2: d[pos] = 0xFF; break;
                default: d[pos] = 0x00; break;
            }
        }
        exercise(d, base->size, &loaded, &p);
        free(d);
    }
    g_quiet = 0;
    fprintf(stderr, "fuzz %s: %d iterations, %u loaded, %u errors recorded\n", name, iterations,
            loaded, nxc_error_count() - errors_before);
    NXC_CHECK(loaded > 0);
}

/* Hot regions: the directory and the header-like starts of each table. */
static int hot_regions(const blob_t *b, region_t *out, int cap) {
    int n = 0;
    uint32_t nt = (uint32_t)(b->data[4] << 8 | b->data[5]);
    out[n].off = 0; out[n].len = 12 + 16 * nt; n++;
    for (uint32_t i = 0; i < nt && n < cap; i++) {
        const uint8_t *r = b->data + 12 + 16 * i;
        uint32_t off = (uint32_t)r[8] << 24 | (uint32_t)r[9] << 16 | (uint32_t)r[10] << 8 | r[11];
        uint32_t len = (uint32_t)r[12] << 24 | (uint32_t)r[13] << 16 | (uint32_t)r[14] << 8 | r[15];
        if (!memcmp(r, "glyf", 4) || !memcmp(r, "post", 4) || !memcmp(r, "name", 4)) continue;
        uint32_t take = len < 4096 ? len : 4096;
        out[n].off = off; out[n].len = take; n++;
    }
    return n;
}

static void test_fuzz(void) {
    region_t hot[32];
    syn_opts_t variants[3] = { { 0, 1, 1, 1, 0 }, { 1, 2, 1, 4, 1 }, { 1, 1, 0, 0, 0 } };
    for (int v = 0; v < 3; v++) {
        blob_t s = build_font(&variants[v]);
        char name[32];
        snprintf(name, sizeof(name), "synthetic-%d", v);
        fuzz(name, &s, NULL, 0, 20000, 6);
        free(s.data);
    }
    blob_t b;
    char path[512];
    snprintf(path, sizeof(path), "%sNXSans/NXSans-Text.ttf", FONT_DIR);
    b = read_file(path);
    if (b.data) {
        int n = hot_regions(&b, hot, 32);
        fuzz("NXSans-Text", &b, hot, n, 3000, 12);
        free(b.data);
    }
    snprintf(path, sizeof(path), "%sNXSansMono/NXSans-Mono.ttf", FONT_DIR);
    b = read_file(path);
    if (b.data) {
        int n = hot_regions(&b, hot, 32);
        fuzz("NXSans-Mono", &b, hot, n, 800, 12);
        free(b.data);
    }
}

/* ============ Glyph sheet (visual check) ============ */

/* Host-side reference fill used only when nxc_raster.c is not linked:
 * 16 sub-scanlines per row, exact horizontal coverage, nonzero rule. */
typedef struct { float x0, y0, x1, y1; } edge_t;
static edge_t g_edges[200000];
static uint32_t g_nedges;

static void add_edge(float x0, float y0, float x1, float y1) {
    if (y0 == y1 || g_nedges >= sizeof(g_edges) / sizeof(g_edges[0])) return;
    edge_t e = { x0, y0, x1, y1 };
    g_edges[g_nedges++] = e;
}

static void host_fill(nxc_canvas_t *c, const nxc_path_t *p, nxc_color_t color) {
    g_nedges = 0;
    uint32_t pi = 0;
    nxc_point_t cur = { 0, 0 }, start = { 0, 0 };
    for (uint32_t v = 0; v < p->nverbs; v++) {
        uint8_t vb = p->verbs[v];
        if (vb == NXC_VERB_MOVE) { cur = start = p->pts[pi++]; }
        else if (vb == NXC_VERB_LINE) { add_edge(cur.x, cur.y, p->pts[pi].x, p->pts[pi].y); cur = p->pts[pi++]; }
        else if (vb == NXC_VERB_QUAD) {
            nxc_point_t k = p->pts[pi], e = p->pts[pi + 1];
            float dx = cur.x - 2 * k.x + e.x, dy = cur.y - 2 * k.y + e.y;
            int n = 1 + (int)(sqrtf(sqrtf(dx * dx + dy * dy)) * 3.0f);
            if (n > 64) n = 64;
            nxc_point_t prev = cur;
            for (int i = 1; i <= n; i++) {
                float t = (float)i / n, u = 1 - t;
                nxc_point_t q = { u * u * cur.x + 2 * u * t * k.x + t * t * e.x, u * u * cur.y + 2 * u * t * k.y + t * t * e.y };
                add_edge(prev.x, prev.y, q.x, q.y);
                prev = q;
            }
            cur = e;
            pi += 2;
        } else if (vb == NXC_VERB_CLOSE) { add_edge(cur.x, cur.y, start.x, start.y); cur = start; }
    }
    int w = c->target->width, h = c->target->height;
    static float acc[4096];
    static uint8_t row[4096];
    static struct { float x; int d; } xs[4096];
    for (int y = 0; y < h; y++) {
        memset(acc, 0, sizeof(float) * (size_t)w);
        int any = 0;
        for (int s = 0; s < 16; s++) {
            float sy = (float)y + ((float)s + 0.5f) / 16.0f;
            int n = 0;
            for (uint32_t i = 0; i < g_nedges && n < 4096; i++) {
                const edge_t *e = &g_edges[i];
                float lo = e->y0 < e->y1 ? e->y0 : e->y1, hi = e->y0 < e->y1 ? e->y1 : e->y0;
                if (sy < lo || sy >= hi) continue;
                float t = (sy - e->y0) / (e->y1 - e->y0);
                xs[n].x = e->x0 + t * (e->x1 - e->x0);
                xs[n].d = e->y1 > e->y0 ? 1 : -1;
                n++;
            }
            for (int i = 1; i < n; i++) {                      /* insertion sort */
                float x = xs[i].x; int d = xs[i].d, j = i - 1;
                while (j >= 0 && xs[j].x > x) { xs[j + 1] = xs[j]; j--; }
                xs[j + 1].x = x; xs[j + 1].d = d;
            }
            int wind = 0;
            for (int i = 0; i + 1 < n; i++) {
                wind += xs[i].d;
                if (!wind) continue;
                float a = xs[i].x < 0 ? 0 : xs[i].x, b = xs[i + 1].x > w ? (float)w : xs[i + 1].x;
                if (b <= a) continue;
                any = 1;
                int ia = (int)a, ib = (int)b;
                if (ia == ib) { acc[ia] += (b - a) / 16.0f; continue; }
                acc[ia] += ((float)(ia + 1) - a) / 16.0f;
                for (int k = ia + 1; k < ib; k++) acc[k] += 1.0f / 16.0f;
                if (ib < w) acc[ib] += (b - (float)ib) / 16.0f;
            }
        }
        if (!any) continue;
        for (int x = 0; x < w; x++) {
            float v = acc[x] > 1.0f ? 1.0f : acc[x];
            row[x] = (uint8_t)(v * 255.0f + 0.5f);
        }
        nxc_draw_mask(c, row, w, 1, w, 0, y, color);
    }
}

static void fill(nxc_canvas_t *c, const nxc_path_t *p, nxc_color_t color) {
    if (nxc_fill_path) NXC_CHECK_OK(nxc_fill_path(c, p, NXC_FILL_NONZERO, color));
    else host_fill(c, p, color);
}

/* Lay out ASCII text on the baseline into a path; returns the width. */
static float layout(const nxc_font_t *f, const char *s, float px, float x, float y, int kern, nxc_path_t *p) {
    float scale = px / nxc__font_units_per_em(f), pen = x;
    uint32_t prev = 0;
    for (const char *c = s; *c; c++) {
        uint32_t g = nxc__font_glyph_index(f, (uint8_t)*c);
        if (kern && prev) pen += (float)nxc__font_kerning(f, prev, g) * scale;
        NXC_CHECK_OK(nxc__font_glyph_outline(f, g, p, scale, pen, y));
        pen += (float)nxc__font_advance(f, g) * scale;
        prev = g;
    }
    return pen - x;
}

/* Copy a region scaled up by k with nearest filtering. */
static void upscale(const nxc_surface_t *src, nxc_irect_t r, int k, const char *name) {
    nxc_surface_t up;
    if (nxc_surface_create(&up, r.w * k, r.h * k, NXC_SURFACE_OPAQUE) != NXC_OK) return;
    for (int y = 0; y < r.h * k; y++)
        for (int x = 0; x < r.w * k; x++)
            up.pixels[y * up.stride + x] = src->pixels[(r.y + y / k) * src->stride + r.x + x / k];
    nxc_test_write_png(&up, nxc_test_path(name));
    nxc_surface_destroy(&up);
}

static void test_glyph_sheet(void) {
    blob_t b1, b2, b3;
    nxc_font_t *text = load_font("NXSans/NXSans-Text.ttf", &b1);
    nxc_font_t *ital = load_font("NXSans/NXSans-Italic.ttf", &b2);
    nxc_font_t *mono = load_font("NXSansMono/NXSans-Mono.ttf", &b3);
    if (!text || !ital || !mono) goto done;
    fprintf(stderr, "glyph sheet rasterized by %s\n", nxc_fill_path ? "nxc_fill_path" : "host reference fill");
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 1600, 900, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(250, 250, 248)));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_path_t p;
    path_new(&p);
    const char *sample = "Hamburgefonstiv 0123 AVATAR Wo To";
    const float sizes[5] = { 13, 16, 24, 48, 96 };
    float y = 20;
    for (int i = 0; i < 5; i++) {
        y += sizes[i] * 1.25f;
        nxc_path_reset(&p);
        layout(text, sample, sizes[i], 16, y, 1, &p);
        fill(&c, &p, NXC_RGB(20, 20, 28));
    }
    /* Kerned (top) vs unkerned (bottom), with a guide line at each pen start. */
    const char *pairs = "AVATAR Wo To Ta LT Yo";
    y += 70;
    nxc_path_reset(&p);
    float wk = layout(text, pairs, 48, 16, y, 1, &p);
    fill(&c, &p, NXC_RGB(20, 60, 140));
    y += 60;
    nxc_path_reset(&p);
    float wu = layout(text, pairs, 48, 16, y, 0, &p);
    fill(&c, &p, NXC_RGB(140, 30, 30));
    NXC_CHECK(wk < wu);
    fprintf(stderr, "kerned width %.2f px, unkerned %.2f px\n", wk, wu);
    y += 60;
    nxc_path_reset(&p);
    layout(ital, "Italic AVATAR Wo To 0123", 40, 16, y, 1, &p);
    fill(&c, &p, NXC_RGB(20, 20, 28));
    y += 56;
    nxc_path_reset(&p);
    layout(mono, "Mono {x: 0x1F, y: [i++]} @#&", 32, 16, y, 1, &p);
    fill(&c, &p, NXC_RGB(20, 100, 60));
    /* Composites at display size */
    y += 110;
    nxc_path_reset(&p);
    float pen = 16;
    const uint32_t acc[] = { 0xC1, 0xC5, 0xC7, 0xE9, 0xF1, 0xF6, 0x1E9E, 0x20AC, 0x2116, 0xFFFD };
    for (size_t i = 0; i < sizeof(acc) / sizeof(acc[0]); i++) {
        float adv = 0;
        nxc_status_t st = nxc_font_glyph_path(text, acc[i], 96, pen, y, &p, &adv);
        NXC_CHECK(st == NXC_OK || st == NXC_ERR_NOT_FOUND);
        pen += adv > 0 ? adv : 40;
    }
    fill(&c, &p, NXC_RGB(20, 20, 28));
    NXC_CHECK(nxc_test_write_png(&s, nxc_test_path("font_sheet.png")) == 0);
    upscale(&s, nxc_irect(10, 20, 330, 60), 4, "font_sheet_small_4x.png");
    upscale(&s, nxc_irect(10, 190, 420, 115), 3, "font_sheet_96_3x.png");
    nxc_surface_destroy(&s);
done:
    nxc_font_destroy(text);
    nxc_font_destroy(ital);
    nxc_font_destroy(mono);
    free(b1.data); free(b2.data); free(b3.data);
}

int main(void) {
    /* Before init: loading needs the engine allocator. */
    {
        uint8_t d[64] = { 0, 1, 0, 0 };
        nxc_font_t *f = NULL;
        NXC_CHECK(nxc_font_load(&f, d, sizeof(d)) == NXC_ERR_STATE && f == NULL);
    }
    t_init(1600, 1200);
    test_load_validation();
    for (size_t i = 0; i < sizeof(g_fonts) / sizeof(g_fonts[0]); i++) test_real_font(&g_fonts[i]);
    test_cmap_format4();
    test_synthetic();
    test_glyph_sheet();
    test_fuzz();
    /* Destroy after re-init must not free through the new allocator. */
    {
        syn_opts_t o = { 0, 0, 0, 0, 0 };
        blob_t s = build_font(&o);
        nxc_font_t *f = NULL;
        NXC_CHECK_OK(nxc_font_load(&f, s.data, s.size));
        void *leak = f;
        t_init(64, 64);
        nxc_font_destroy(f);
        free(leak);                            /* the test owns the block now */
        free(s.data);
    }
    nxc_shutdown();
    return nxc_test_finish("font");
}
