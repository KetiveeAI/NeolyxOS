/*
 * NeolyxOS - NXCanvas TrueType font engine
 *
 * Parses TrueType (glyf) fonts following the Apple TrueType Reference
 * Manual and the OpenType specification: table directory, head, maxp,
 * hhea/hmtx, loca/glyf, cmap (formats 4 and 12), OS/2 vertical metrics,
 * the legacy 'kern' table (format 0) and the GPOS 'kern' feature (pair
 * adjustment lookups, directly or through extension lookups).
 *
 * Font data is untrusted. Every read goes through the bounds-checked
 * reader below, every offset is validated against the table it belongs
 * to, and malformed data yields NXC_ERR_BAD_FONT instead of an
 * out-of-bounds access. All loops are bounded by validated counts or by
 * explicit work budgets, and composite recursion is depth-limited.
 *
 * Loading makes exactly one allocation (font record, pre-resolved GPOS
 * kerning index and the outline decoding buffers); lookups and outline
 * decoding never allocate, so they are safe in drawing paths.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_font_internal.h"

#define NXC_TAG(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* Simple glyph point flags. */
#define PT_ON_CURVE      0x01u
#define PT_X_SHORT       0x02u
#define PT_Y_SHORT       0x04u
#define PT_REPEAT        0x08u
#define PT_X_SAME_POS    0x10u
#define PT_Y_SAME_POS    0x20u

/* Composite glyph component flags. */
#define CG_ARG_WORDS     0x0001u
#define CG_ARGS_XY       0x0002u
#define CG_SCALE         0x0008u
#define CG_MORE          0x0020u
#define CG_XY_SCALE      0x0040u
#define CG_TWO_BY_TWO    0x0080u
#define CG_SCALED_OFS    0x0800u
#define CG_UNSCALED_OFS  0x1000u

/* Work units allowed while resolving GPOS kerning at load time. */
#define GPOS_WORK_BUDGET (2u * NXC_FONT_MAX_KERN_ENTRIES)

static uint32_t g_font_last_id;     /* source of unique font ids */

/* ============ Checked reader ============ */

/*
 * A read window [lo, hi) over the font data. Reads outside the window
 * return 0 and set `bad`; callers test it once per structure, so one
 * check covers a whole group of reads. Offsets are 64-bit so that
 * base + offset arithmetic on hostile values cannot wrap.
 */
typedef struct {
    const uint8_t *d;
    uint32_t       lo, hi;
    int            bad;
} rd_t;

static inline rd_t rd_make(const uint8_t *d, uint32_t lo, uint32_t hi) {
    rd_t r;
    r.d = d;
    r.lo = lo;
    r.hi = hi;
    r.bad = 0;
    return r;
}

/* 1 when [off, off + n) lies inside the window. */
static inline int rd_has(const rd_t *r, uint64_t off, uint64_t n) {
    return off >= r->lo && off <= r->hi && n <= (uint64_t)r->hi - off;
}

static inline uint32_t rd_u8(rd_t *r, uint64_t off) {
    if (!rd_has(r, off, 1)) { r->bad = 1; return 0; }
    return r->d[off];
}

static inline uint32_t rd_u16(rd_t *r, uint64_t off) {
    if (!rd_has(r, off, 2)) { r->bad = 1; return 0; }
    const uint8_t *p = r->d + off;
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

static inline int32_t rd_s16(rd_t *r, uint64_t off) {
    uint32_t v = rd_u16(r, off);
    return (v & 0x8000u) ? (int32_t)v - 65536 : (int32_t)v;
}

static inline uint32_t rd_u32(rd_t *r, uint64_t off) {
    if (!rd_has(r, off, 4)) { r->bad = 1; return 0; }
    const uint8_t *p = r->d + off;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ============ Diagnostics ============ */

/* Append src to dst[pos..cap) and return the new position. */
static uint32_t str_append(char *dst, uint32_t pos, uint32_t cap, const char *src) {
    while (src && *src && pos + 1 < cap) dst[pos++] = *src++;
    dst[pos] = '\0';
    return pos;
}

/* Append the decimal form of v. */
static uint32_t str_append_u32(char *dst, uint32_t pos, uint32_t cap, uint32_t v) {
    char tmp[11];
    uint32_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < sizeof(tmp));
    while (n > 0 && pos + 1 < cap) dst[pos++] = tmp[--n];
    dst[pos] = '\0';
    return pos;
}

/* Log why a font was rejected and record NXC_ERR_BAD_FONT. */
static nxc_status_t font_reject(const char *why) {
    char line[128];
    uint32_t n = str_append(line, 0, sizeof(line), "[NXC] font rejected: ");
    n = str_append(line, n, sizeof(line), why);
    (void)str_append(line, n, sizeof(line), "\n");
    nxc__log(line);
    return nxc__fail(NXC_ERR_BAD_FONT, "nxc_font_load");
}

/* ============ Table directory ============ */

typedef struct { uint32_t off, len; } tab_t;

typedef struct {
    tab_t head, maxp, hhea, hmtx, loca, glyf, cmap, os2, kern, gpos;
} tables_t;

/*
 * Validate the sfnt header and every table record (each must lie inside
 * the file) and locate the tables the engine uses. The first record of a
 * duplicated tag wins. Absent optional tables keep len 0.
 */
static nxc_status_t parse_directory(const uint8_t *data, uint32_t size, tables_t *t) {
    rd_t r = rd_make(data, 0, size);
    uint32_t version = rd_u32(&r, 0);
    uint32_t ntables = rd_u16(&r, 4);
    if (r.bad) return font_reject("truncated header");
    if (version == NXC_TAG('O', 'T', 'T', 'O')) return font_reject("CFF outlines are not supported");
    if (version == NXC_TAG('t', 't', 'c', 'f')) return font_reject("font collections are not supported");
    if (version != 0x00010000u && version != NXC_TAG('t', 'r', 'u', 'e')) {
        return font_reject("not a TrueType font");
    }
    if (ntables == 0 || !rd_has(&r, 12, (uint64_t)ntables * 16u)) {
        return font_reject("bad table directory");
    }
    nxc__zero(t, sizeof(*t));
    for (uint32_t i = 0; i < ntables; i++) {
        uint64_t rec = 12u + (uint64_t)i * 16u;
        uint32_t tag = rd_u32(&r, rec);
        uint32_t off = rd_u32(&r, rec + 8);
        uint32_t len = rd_u32(&r, rec + 12);
        if (r.bad || !rd_has(&r, off, len)) return font_reject("table outside the file");
        tab_t *dst = 0;
        switch (tag) {
            case NXC_TAG('h', 'e', 'a', 'd'): dst = &t->head; break;
            case NXC_TAG('m', 'a', 'x', 'p'): dst = &t->maxp; break;
            case NXC_TAG('h', 'h', 'e', 'a'): dst = &t->hhea; break;
            case NXC_TAG('h', 'm', 't', 'x'): dst = &t->hmtx; break;
            case NXC_TAG('l', 'o', 'c', 'a'): dst = &t->loca; break;
            case NXC_TAG('g', 'l', 'y', 'f'): dst = &t->glyf; break;
            case NXC_TAG('c', 'm', 'a', 'p'): dst = &t->cmap; break;
            case NXC_TAG('O', 'S', '/', '2'): dst = &t->os2;  break;
            case NXC_TAG('k', 'e', 'r', 'n'): dst = &t->kern; break;
            case NXC_TAG('G', 'P', 'O', 'S'): dst = &t->gpos; break;
            default: break;
        }
        if (dst && dst->len == 0 && len > 0) { dst->off = off; dst->len = len; }
    }
    if (!t->head.len || !t->maxp.len || !t->hhea.len || !t->hmtx.len ||
        !t->loca.len || !t->cmap.len) {
        return font_reject("missing required table");
    }
    /* glyf may be empty only if every glyph is empty; loca decides. */
    if (!t->glyf.len) t->glyf.off = 0;
    return NXC_OK;
}

/* ============ head / maxp / hhea / hmtx ============ */

static nxc_status_t parse_head_maxp(struct nxc_font *f, const tables_t *t) {
    rd_t r = rd_make(f->data, t->head.off, t->head.off + t->head.len);
    uint32_t magic = rd_u32(&r, (uint64_t)t->head.off + 12);
    uint32_t upem = rd_u16(&r, (uint64_t)t->head.off + 18);
    int32_t xmin = rd_s16(&r, (uint64_t)t->head.off + 36);
    int32_t ymin = rd_s16(&r, (uint64_t)t->head.off + 38);
    int32_t xmax = rd_s16(&r, (uint64_t)t->head.off + 40);
    int32_t ymax = rd_s16(&r, (uint64_t)t->head.off + 42);
    int32_t loc_format = rd_s16(&r, (uint64_t)t->head.off + 50);
    int32_t glyph_format = rd_s16(&r, (uint64_t)t->head.off + 52);
    if (r.bad || magic != 0x5F0F3CF5u) return font_reject("bad head table");
    if (upem < 16 || upem > 16384) return font_reject("unitsPerEm out of range");
    if ((loc_format != 0 && loc_format != 1) || glyph_format != 0) {
        return font_reject("unknown loca/glyph data format");
    }
    f->units_per_em = (uint16_t)upem;
    f->loc_long = (uint16_t)loc_format;
    f->bbox[0] = (int16_t)xmin; f->bbox[1] = (int16_t)ymin;
    f->bbox[2] = (int16_t)xmax; f->bbox[3] = (int16_t)ymax;

    rd_t m = rd_make(f->data, t->maxp.off, t->maxp.off + t->maxp.len);
    uint32_t mver = rd_u32(&m, t->maxp.off);
    uint32_t ng = rd_u16(&m, (uint64_t)t->maxp.off + 4);
    if (m.bad || (mver != 0x00010000u && mver != 0x00005000u)) return font_reject("bad maxp table");
    if (ng == 0) return font_reject("font has no glyphs");
    f->num_glyphs = (uint16_t)ng;
    return NXC_OK;
}

static nxc_status_t parse_hhea_hmtx(struct nxc_font *f, const tables_t *t, int32_t hhea_metrics[3]) {
    rd_t r = rd_make(f->data, t->hhea.off, t->hhea.off + t->hhea.len);
    hhea_metrics[0] = rd_s16(&r, (uint64_t)t->hhea.off + 4);    /* ascender */
    hhea_metrics[1] = rd_s16(&r, (uint64_t)t->hhea.off + 6);    /* descender */
    hhea_metrics[2] = rd_s16(&r, (uint64_t)t->hhea.off + 8);    /* lineGap */
    uint32_t nhm = rd_u16(&r, (uint64_t)t->hhea.off + 34);
    if (r.bad || nhm == 0) return font_reject("bad hhea table");
    /* numberOfHMetrics may not exceed numGlyphs; extra metrics are unused. */
    if (nhm > f->num_glyphs) nhm = f->num_glyphs;
    if ((uint64_t)nhm * 4u > t->hmtx.len) return font_reject("hmtx shorter than numberOfHMetrics");
    f->hmtx_off = t->hmtx.off;
    f->num_hmetrics = (uint16_t)nhm;
    return NXC_OK;
}

/* ============ loca / glyf ============ */

/* Read glyph g's [off, off + len) inside glyf from loca. 0 when malformed. */
static int glyph_range(const struct nxc_font *f, uint32_t g, uint32_t *off, uint32_t *len) {
    rd_t r = rd_make(f->data, 0, f->size);
    uint64_t a, b;
    if (f->loc_long) {
        a = rd_u32(&r, (uint64_t)f->loca_off + (uint64_t)g * 4u);
        b = rd_u32(&r, (uint64_t)f->loca_off + (uint64_t)g * 4u + 4u);
    } else {
        a = 2u * (uint64_t)rd_u16(&r, (uint64_t)f->loca_off + (uint64_t)g * 2u);
        b = 2u * (uint64_t)rd_u16(&r, (uint64_t)f->loca_off + (uint64_t)g * 2u + 2u);
    }
    if (r.bad || a > b || b > f->glyf_len) return 0;
    *off = f->glyf_off + (uint32_t)a;
    *len = (uint32_t)(b - a);
    return 1;
}

/*
 * Validate loca for every glyph (in order, inside glyf, each non-empty
 * glyph at least a full header) and find the largest simple glyph, which
 * sizes the outline decoding buffers. O(numGlyphs) reads.
 */
static nxc_status_t scan_glyphs(struct nxc_font *f, const tables_t *t) {
    uint64_t entry = f->loc_long ? 4u : 2u;
    if (((uint64_t)f->num_glyphs + 1u) * entry > t->loca.len) return font_reject("loca too short");
    f->loca_off = t->loca.off;
    f->glyf_off = t->glyf.off;
    f->glyf_len = t->glyf.len;
    f->max_points = 0;
    f->max_contours = 0;
    rd_t r = rd_make(f->data, f->glyf_off, f->glyf_off + f->glyf_len);
    for (uint32_t g = 0; g < f->num_glyphs; g++) {
        uint32_t off = 0, len = 0;
        if (!glyph_range(f, g, &off, &len)) return font_reject("bad loca entry");
        if (len == 0) continue;
        if (len < 10) return font_reject("truncated glyph header");
        int32_t nc = rd_s16(&r, off);
        if (nc <= 0) continue;
        if ((uint64_t)10u + 2u * (uint64_t)nc > len) return font_reject("truncated contour list");
        uint32_t last = rd_u16(&r, (uint64_t)off + 10u + 2u * (uint64_t)(nc - 1));
        if (r.bad) return font_reject("truncated contour list");
        if (last + 1u > f->max_points) f->max_points = last + 1u;
        if ((uint32_t)nc > f->max_contours) f->max_contours = (uint32_t)nc;
    }
    return NXC_OK;
}

/* ============ cmap ============ */

/* Raw cmap lookup in the chosen subtable (no ASCII table). */
static uint32_t cmap_lookup_raw(const struct nxc_font *f, uint32_t cp) {
    rd_t r = rd_make(f->data, f->cmap_sub, f->cmap_end);
    uint64_t base = f->cmap_sub;
    uint32_t gid = 0;
    if (f->cmap_format == 4) {
        if (cp > 0xFFFFu) return 0;
        uint32_t seg = f->cmap_count;
        uint64_t ends = base + 14u;
        uint64_t starts = ends + 2u * (uint64_t)seg + 2u;
        uint64_t deltas = starts + 2u * (uint64_t)seg;
        uint64_t ranges = deltas + 2u * (uint64_t)seg;
        /* First segment whose endCode >= cp. */
        uint32_t lo = 0, hi = seg;
        while (lo < hi) {
            uint32_t mid = lo + ((hi - lo) >> 1);
            if (rd_u16(&r, ends + 2u * (uint64_t)mid) < cp) lo = mid + 1; else hi = mid;
        }
        if (lo >= seg) return 0;
        uint32_t start = rd_u16(&r, starts + 2u * (uint64_t)lo);
        if (cp < start) return 0;
        uint32_t delta = rd_u16(&r, deltas + 2u * (uint64_t)lo);
        uint64_t ro_at = ranges + 2u * (uint64_t)lo;
        uint32_t ro = rd_u16(&r, ro_at);
        if (ro == 0) {
            gid = (cp + delta) & 0xFFFFu;
        } else {
            /* idRangeOffset is relative to its own location. */
            gid = rd_u16(&r, ro_at + ro + 2u * (uint64_t)(cp - start));
            if (gid != 0) gid = (gid + delta) & 0xFFFFu;
        }
    } else {
        uint32_t lo = 0, hi = f->cmap_count;
        uint64_t groups = base + 16u;
        while (lo < hi) {
            uint32_t mid = lo + ((hi - lo) >> 1);
            uint64_t g = groups + 12u * (uint64_t)mid;
            uint32_t s = rd_u32(&r, g), e = rd_u32(&r, g + 4);
            if (r.bad) return 0;
            if (cp < s) hi = mid;
            else if (cp > e) lo = mid + 1;
            else {
                uint64_t v = (uint64_t)rd_u32(&r, g + 8) + (cp - s);
                gid = (v < 0x10000u) ? (uint32_t)v : 0u;
                break;
            }
        }
    }
    if (r.bad || gid >= f->num_glyphs) return 0;
    return gid;
}

/* cmap lookup including the symbol-font remap to the U+F000 page. */
static uint32_t cmap_lookup(const struct nxc_font *f, uint32_t cp) {
    uint32_t g = cmap_lookup_raw(f, cp);
    if (g == 0 && f->cmap_symbol && cp < 0x100u) g = cmap_lookup_raw(f, 0xF000u + cp);
    return g;
}

/* Preference of a cmap subtable (0 = unusable): Windows full Unicode
 * format 12, Windows BMP format 4, then Unicode-platform tables, and
 * Windows symbol last. */
static uint32_t cmap_score(uint32_t platform, uint32_t encoding, uint32_t format) {
    if (format != 4 && format != 12) return 0;
    if (platform == 3 && encoding == 10 && format == 12) return 6;
    if (platform == 3 && encoding == 1) return 5;
    if (platform == 0 && format == 12) return 4;
    if (platform == 0) return 3;
    if (platform == 3 && encoding == 0 && format == 4) return 1;
    return 0;
}

/* Choose and validate the cmap subtable. */
static nxc_status_t parse_cmap(struct nxc_font *f, const tables_t *t) {
    uint32_t end = t->cmap.off + t->cmap.len;
    rd_t r = rd_make(f->data, t->cmap.off, end);
    uint32_t n = rd_u16(&r, (uint64_t)t->cmap.off + 2);
    if (r.bad || !rd_has(&r, (uint64_t)t->cmap.off + 4, (uint64_t)n * 8u)) {
        return font_reject("bad cmap header");
    }
    uint32_t best = 0, best_off = 0, best_fmt = 0, best_plat = 0, best_enc = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t rec = (uint64_t)t->cmap.off + 4u + 8u * (uint64_t)i;
        uint32_t plat = rd_u16(&r, rec);
        uint32_t enc = rd_u16(&r, rec + 2);
        uint64_t sub = (uint64_t)t->cmap.off + rd_u32(&r, rec + 4);
        uint32_t fmt = rd_u16(&r, sub);
        if (r.bad) return font_reject("cmap subtable outside the table");
        uint32_t s = cmap_score(plat, enc, fmt);
        if (s > best) {
            best = s; best_off = (uint32_t)sub; best_fmt = fmt;
            best_plat = plat; best_enc = enc;
        }
    }
    if (best == 0) return font_reject("no Unicode cmap subtable");
    f->cmap_sub = best_off;
    f->cmap_end = end;
    f->cmap_format = (uint16_t)best_fmt;
    f->cmap_symbol = (uint16_t)(best_plat == 3 && best_enc == 0);
    if (best_fmt == 4) {
        uint32_t seg2 = rd_u16(&r, (uint64_t)best_off + 6);
        if (r.bad || seg2 == 0 || (seg2 & 1u)) return font_reject("bad cmap format 4 header");
        uint32_t seg = seg2 >> 1;
        /* endCode, pad, startCode, idDelta, idRangeOffset; glyphIdArray
         * reads are checked individually against the table end. */
        if (!rd_has(&r, (uint64_t)best_off + 14u, 8u * (uint64_t)seg + 2u)) {
            return font_reject("cmap format 4 segments outside the table");
        }
        f->cmap_count = seg;
    } else {
        uint32_t groups = rd_u32(&r, (uint64_t)best_off + 12);
        if (r.bad || !rd_has(&r, (uint64_t)best_off + 16u, 12u * (uint64_t)groups)) {
            return font_reject("cmap format 12 groups outside the table");
        }
        f->cmap_count = groups;
    }
    return NXC_OK;
}

/* ============ Vertical metrics (OS/2, hhea) ============ */

/*
 * Ascent/descent/line gap: OS/2 typo metrics when USE_TYPO_METRICS is
 * set, else hhea, else typo, else win metrics, else the font bbox, else
 * 0.8 / 0.2 em. Cap and x height come from OS/2 v2+ here; glyph-based
 * fallbacks are applied once the font is usable (finish_metrics).
 */
static void parse_metrics(struct nxc_font *f, const tables_t *t, const int32_t hhea[3]) {
    int32_t typo[3] = { 0, 0, 0 }, win[2] = { 0, 0 };
    int has_typo = 0, use_typo = 0;
    f->cap_height = 0;
    f->x_height = 0;
    if (t->os2.len >= 78) {
        rd_t r = rd_make(f->data, t->os2.off, t->os2.off + t->os2.len);
        uint64_t o = t->os2.off;
        uint32_t version = rd_u16(&r, o);
        uint32_t fs_selection = rd_u16(&r, o + 62);
        typo[0] = rd_s16(&r, o + 68);
        typo[1] = rd_s16(&r, o + 70);
        typo[2] = rd_s16(&r, o + 72);
        win[0] = (int32_t)rd_u16(&r, o + 74);
        win[1] = (int32_t)rd_u16(&r, o + 76);
        if (version >= 2 && t->os2.len >= 90) {
            f->x_height = rd_s16(&r, o + 86);
            f->cap_height = rd_s16(&r, o + 88);
        }
        if (!r.bad) {
            has_typo = 1;
            use_typo = (fs_selection & 0x80u) != 0;
        } else {
            f->x_height = 0;
            f->cap_height = 0;
        }
    }
    int32_t asc, desc, gap;
    if (has_typo && use_typo) {
        asc = typo[0]; desc = typo[1]; gap = typo[2];
    } else if (hhea[0] != 0 || hhea[1] != 0) {
        asc = hhea[0]; desc = hhea[1]; gap = hhea[2];
    } else if (has_typo && (typo[0] != 0 || typo[1] != 0)) {
        asc = typo[0]; desc = typo[1]; gap = typo[2];
    } else if (has_typo && (win[0] != 0 || win[1] != 0)) {
        asc = win[0]; desc = -win[1]; gap = 0;
    } else {
        asc = f->bbox[3]; desc = f->bbox[1]; gap = 0;
    }
    /* Descenders are negative (below the baseline); some fonts store the
     * magnitude instead, so use the absolute value. */
    if (desc < 0) desc = -desc;
    if (asc < 0) asc = 0;
    if (gap < 0) gap = 0;
    if (asc == 0 && desc == 0) {
        asc = (int32_t)f->units_per_em * 4 / 5;
        desc = (int32_t)f->units_per_em / 5;
    }
    f->ascent = asc;
    f->descent = desc;
    f->line_gap = gap;
}

/* ============ Legacy 'kern' table ============ */

/*
 * Locate the first horizontal format 0 subtable of a Microsoft (version
 * 0) or Apple (version 1.0) 'kern' table. Other subtable formats are
 * skipped. The pair array must lie inside the table.
 */
static nxc_status_t parse_kern(struct nxc_font *f, const tables_t *t) {
    f->kern_pairs = 0;
    f->kern_npairs = 0;
    if (t->kern.len == 0) return NXC_OK;
    uint32_t end = t->kern.off + t->kern.len;
    rd_t r = rd_make(f->data, t->kern.off, end);
    uint64_t o = t->kern.off;
    uint32_t v16 = rd_u16(&r, o);
    uint32_t v32 = rd_u32(&r, o);
    if (r.bad) return font_reject("truncated kern table");
    int apple = 0;
    uint64_t p;
    uint32_t nt;
    if (v16 == 0) {
        nt = rd_u16(&r, o + 2);
        p = o + 4;
    } else if (v32 == 0x00010000u) {
        apple = 1;
        nt = rd_u32(&r, o + 4);
        p = o + 8;
    } else {
        nxc__log("[NXC] font: unknown kern table version ignored\n");
        return NXC_OK;
    }
    /* Each subtable header is at least 6 bytes, so the loop runs at most
     * table_length / 6 times whatever nTables claims. */
    for (uint32_t i = 0; i < nt && p < end; i++) {
        uint32_t len, cov, fmt;
        int usable;
        uint64_t body;
        if (apple) {
            len = rd_u32(&r, p);
            cov = rd_u16(&r, p + 4);
            fmt = cov & 0xFFu;
            usable = (cov & 0xE000u) == 0;           /* not vertical/cross/variation */
            body = p + 8;
        } else {
            len = rd_u16(&r, p + 2);
            cov = rd_u16(&r, p + 4);
            fmt = cov >> 8;
            usable = (cov & 0x0007u) == 0x0001u;     /* horizontal, not min/cross */
            body = p + 6;
        }
        if (r.bad) return font_reject("truncated kern subtable");
        if (fmt == 0 && usable) {
            uint32_t npairs = rd_u16(&r, body);
            uint64_t pairs = body + 8u;
            if (r.bad || !rd_has(&r, pairs, 6u * (uint64_t)npairs)) {
                return font_reject("kern pairs outside the table");
            }
            f->kern_pairs = (uint32_t)pairs;
            f->kern_npairs = npairs;
            return NXC_OK;
        }
        if (len < (apple ? 8u : 6u)) return font_reject("bad kern subtable length");
        p += len;
    }
    return NXC_OK;
}

/* Binary search of the format 0 pair array (sorted by left << 16 | right). */
static int32_t kern_table_value(const struct nxc_font *f, uint32_t left, uint32_t right) {
    rd_t r = rd_make(f->data, 0, f->size);
    uint32_t key = (left << 16) | right;
    uint32_t lo = 0, hi = f->kern_npairs;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        uint64_t rec = (uint64_t)f->kern_pairs + 6u * (uint64_t)mid;
        uint32_t k = rd_u32(&r, rec);
        if (r.bad) return 0;
        if (k == key) return rd_s16(&r, rec + 4);
        if (k < key) lo = mid + 1; else hi = mid;
    }
    return 0;
}

/* ============ GPOS: coverage and class definitions ============ */

/* Iterator over a Coverage table, yielding (glyph, coverage index) for
 * glyphs below num_glyphs. */
typedef struct {
    uint64_t off;
    uint32_t format, count, num_glyphs;
    uint32_t i;              /* format 1: next index; format 2: current range */
    uint32_t g, gend, ci;    /* format 2: next glyph, last glyph, its index */
    int      in_range;
} cov_it_t;

/* Validate a Coverage table header and array. 0 when malformed. */
static int cov_begin(rd_t *r, uint64_t off, uint32_t num_glyphs, cov_it_t *it) {
    it->off = off;
    it->format = rd_u16(r, off);
    it->count = rd_u16(r, off + 2);
    it->num_glyphs = num_glyphs;
    it->i = 0;
    it->in_range = 0;
    if (r->bad) return 0;
    if (it->format == 1) return rd_has(r, off + 4, 2u * (uint64_t)it->count);
    if (it->format == 2) return rd_has(r, off + 4, 6u * (uint64_t)it->count);
    return 0;
}

/* Next covered glyph. Returns 1 with the glyph and its index set, 0 at the end,
 * -1 for a malformed range. Each call does O(1) work. */
static int cov_next(rd_t *r, cov_it_t *it, uint32_t *glyph, uint32_t *index) {
    if (it->format == 1) {
        while (it->i < it->count) {
            uint32_t i = it->i++;
            uint32_t g = rd_u16(r, it->off + 4u + 2u * (uint64_t)i);
            if (r->bad) return -1;
            if (g < it->num_glyphs) { *glyph = g; *index = i; return 1; }
        }
        return 0;
    }
    for (;;) {
        if (it->in_range && it->g <= it->gend) {
            *glyph = it->g++;
            *index = it->ci++;
            return 1;
        }
        it->in_range = 0;
        if (it->i >= it->count) return 0;
        uint64_t rec = it->off + 4u + 6u * (uint64_t)it->i++;
        uint32_t s = rd_u16(r, rec), e = rd_u16(r, rec + 2), ci = rd_u16(r, rec + 4);
        if (r->bad || s > e) return -1;
        if (s >= it->num_glyphs) continue;
        if (e >= it->num_glyphs) e = it->num_glyphs - 1u;
        it->g = s;
        it->gend = e;
        it->ci = ci;
        it->in_range = 1;
    }
}

/* Validate a ClassDef table. 0 when malformed. */
static int classdef_ok(rd_t *r, uint64_t off) {
    uint32_t format = rd_u16(r, off);
    if (r->bad) return 0;
    if (format == 1) {
        uint32_t count = rd_u16(r, off + 4);
        return !r->bad && rd_has(r, off + 6, 2u * (uint64_t)count);
    }
    if (format == 2) {
        uint32_t count = rd_u16(r, off + 2);
        return !r->bad && rd_has(r, off + 4, 6u * (uint64_t)count);
    }
    return 0;
}

/* Class of glyph g (0 when unlisted). Format 2 ranges are binary searched. */
static uint32_t classdef_get(rd_t *r, uint64_t off, uint32_t g) {
    uint32_t format = rd_u16(r, off);
    if (format == 1) {
        uint32_t start = rd_u16(r, off + 2), count = rd_u16(r, off + 4);
        if (g < start || g - start >= count) return 0;
        return rd_u16(r, off + 6u + 2u * (uint64_t)(g - start));
    }
    if (format == 2) {
        uint32_t lo = 0, hi = rd_u16(r, off + 2);
        while (lo < hi) {
            uint32_t mid = lo + ((hi - lo) >> 1);
            uint64_t rec = off + 4u + 6u * (uint64_t)mid;
            uint32_t s = rd_u16(r, rec), e = rd_u16(r, rec + 2);
            if (r->bad) return 0;
            if (g < s) hi = mid;
            else if (g > e) lo = mid + 1;
            else return rd_u16(r, rec + 4);
        }
    }
    return 0;
}

/* Bytes in a ValueRecord with format vf (2 per set bit of the low byte). */
static uint32_t value_size(uint32_t vf) {
    uint32_t n = 0;
    for (uint32_t b = 0; b < 8; b++) n += (vf >> b) & 1u;
    return 2u * n;
}

/* ============ GPOS: 'kern' feature resolution (load time) ============ */

typedef struct {
    rd_t     r;                                   /* window over GPOS */
    uint64_t lookup_list;
    uint32_t nlookups;
    uint16_t lookups[NXC_FONT_MAX_KERN_LOOKUPS];  /* sorted, unique */
    uint32_t work;                                /* budget consumed */
    int      over_budget;
} gpos_scan_t;

/* Spend n work units; returns 0 once the budget is exhausted. */
static int gpos_spend(gpos_scan_t *gs, uint32_t n) {
    if (gs->work > GPOS_WORK_BUDGET - n) { gs->over_budget = 1; return 0; }
    gs->work += n;
    return 1;
}

/*
 * Collect the lookup indices of every 'kern' feature (union over the
 * FeatureList, so all scripts' kerning is available without itemizing
 * runs by script). Lookups are applied in LookupList order.
 */
static nxc_status_t gpos_collect(const struct nxc_font *f, const tables_t *t, gpos_scan_t *gs) {
    nxc__zero(gs, sizeof(*gs));
    gs->r = rd_make(f->data, t->gpos.off, t->gpos.off + t->gpos.len);
    rd_t *r = &gs->r;
    uint64_t g = t->gpos.off;
    uint32_t major = rd_u16(r, g);
    uint32_t feat_off = rd_u16(r, g + 6);
    uint32_t look_off = rd_u16(r, g + 8);
    if (r->bad) return font_reject("truncated GPOS header");
    if (major != 1) {
        nxc__log("[NXC] font: unknown GPOS version, kerning from GPOS disabled\n");
        return NXC_OK;
    }
    if (feat_off == 0 || look_off == 0) return NXC_OK;
    uint64_t fl = g + feat_off;
    gs->lookup_list = g + look_off;
    uint32_t nfeat = rd_u16(r, fl);
    uint32_t nlook = rd_u16(r, gs->lookup_list);
    if (r->bad || !rd_has(r, fl + 2, 6u * (uint64_t)nfeat) ||
        !rd_has(r, gs->lookup_list + 2, 2u * (uint64_t)nlook)) {
        return font_reject("bad GPOS feature/lookup list");
    }
    int dropped = 0;
    for (uint32_t i = 0; i < nfeat; i++) {
        uint64_t rec = fl + 2u + 6u * (uint64_t)i;
        if (rd_u32(r, rec) != NXC_TAG('k', 'e', 'r', 'n')) continue;
        uint64_t feat = fl + rd_u16(r, rec + 4);
        uint32_t n = rd_u16(r, feat + 2);
        if (r->bad || !rd_has(r, feat + 4, 2u * (uint64_t)n)) return font_reject("bad GPOS kern feature");
        if (!gpos_spend(gs, n + 1u)) return NXC_OK;
        for (uint32_t j = 0; j < n; j++) {
            uint32_t idx = rd_u16(r, feat + 4u + 2u * (uint64_t)j);
            if (idx >= nlook) return font_reject("GPOS lookup index out of range");
            /* Sorted insert without duplicates. */
            uint32_t k = 0;
            while (k < gs->nlookups && gs->lookups[k] < idx) k++;
            if (k < gs->nlookups && gs->lookups[k] == idx) continue;
            if (gs->nlookups == NXC_FONT_MAX_KERN_LOOKUPS) { dropped = 1; continue; }
            for (uint32_t m = gs->nlookups; m > k; m--) gs->lookups[m] = gs->lookups[m - 1];
            gs->lookups[k] = (uint16_t)idx;
            gs->nlookups++;
        }
    }
    if (dropped) nxc__log("[NXC] font: too many GPOS kern lookups, extra lookups ignored\n");
    return NXC_OK;
}

/*
 * Parse and fully validate one PairPos subtable at absolute offset so.
 * Returns 1 when usable, 0 for a format the engine skips, -1 when
 * malformed or the work budget ran out (gs->over_budget tells which).
 */
static int pairpos_parse(gpos_scan_t *gs, uint64_t so, uint32_t lookup_ord,
                         uint32_t num_glyphs, nxc__kern_sub_t *ks, uint64_t *coverage,
                         uint64_t *classdef1) {
    rd_t *r = &gs->r;
    uint32_t format = rd_u16(r, so);
    uint32_t cov_off = rd_u16(r, so + 2);
    uint32_t vf1 = rd_u16(r, so + 4);
    uint32_t vf2 = rd_u16(r, so + 6);
    if (r->bad) return -1;
    if (format != 1 && format != 2) return 0;
    cov_it_t it;
    if (cov_off == 0 || !cov_begin(r, so + cov_off, num_glyphs, &it)) return -1;
    uint32_t s1 = value_size(vf1), s2 = value_size(vf2);
    uint32_t xadv_in_v1 = 2u * ((vf1 & 1u) + ((vf1 >> 1) & 1u));
    nxc__zero(ks, sizeof(*ks));
    ks->off = (uint32_t)so;
    ks->format = (uint16_t)format;
    ks->lookup = (uint16_t)lookup_ord;
    *coverage = so + cov_off;
    *classdef1 = 0;
    if (format == 1) {
        uint32_t nsets = rd_u16(r, so + 8);
        uint64_t array = so + 10u;
        uint32_t rec = 2u + s1 + s2;
        if (r->bad || !rd_has(r, array, 2u * (uint64_t)nsets)) return -1;
        if (!gpos_spend(gs, nsets + 1u)) return -1;
        for (uint32_t i = 0; i < nsets; i++) {
            uint32_t ps_off = rd_u16(r, array + 2u * (uint64_t)i);
            uint64_t ps = so + ps_off;
            uint32_t n = rd_u16(r, ps);
            if (r->bad || ps_off == 0 || !rd_has(r, ps + 2u, (uint64_t)n * rec)) return -1;
        }
        ks->array = (uint32_t)array;
        ks->count1 = (uint16_t)nsets;
        ks->rec_size = (uint16_t)rec;
        ks->xadv = (vf1 & 0x0004u) ? (uint16_t)(2u + xadv_in_v1) : 0xFFFFu;
    } else {
        uint32_t cd1 = rd_u16(r, so + 8), cd2 = rd_u16(r, so + 10);
        uint32_t c1 = rd_u16(r, so + 12), c2 = rd_u16(r, so + 14);
        uint64_t array = so + 16u;
        uint32_t rec = s1 + s2;
        if (r->bad || cd1 == 0 || cd2 == 0 || c1 == 0 || c2 == 0) return -1;
        if (!classdef_ok(r, so + cd1) || !classdef_ok(r, so + cd2)) return -1;
        if (!rd_has(r, array, (uint64_t)c1 * (uint64_t)c2 * rec)) return -1;
        ks->array = (uint32_t)array;
        ks->classdef2 = (uint32_t)(so + cd2);
        ks->count1 = (uint16_t)c1;
        ks->count2 = (uint16_t)c2;
        ks->rec_size = (uint16_t)rec;
        ks->xadv = (vf1 & 0x0004u) ? (uint16_t)xadv_in_v1 : 0xFFFFu;
        *classdef1 = so + cd1;
    }
    return 1;
}

/* Entry value for first glyph g at coverage index ci, or 0 when the
 * subtable can never match g (index or class beyond the record arrays). */
static int kern_entry_value(rd_t *r, const nxc__kern_sub_t *ks, uint64_t classdef1,
                            uint32_t g, uint32_t ci, uint16_t *val) {
    if (ks->format == 1) {
        if (ci >= ks->count1) return 0;
        *val = (uint16_t)ci;
        return 1;
    }
    uint32_t c = classdef_get(r, classdef1, g);
    if (r->bad || c >= ks->count1) return 0;
    *val = (uint16_t)c;
    return 1;
}

/*
 * Visit every usable PairPos subtable of the collected lookups in
 * application order. With subs == NULL this only counts subtables and
 * first-glyph entries (pass A, before allocation); otherwise it stores
 * the subtables (pass B). Returns NXC_OK (check gs->over_budget) or
 * NXC_ERR_BAD_FONT.
 */
static nxc_status_t gpos_walk(const struct nxc_font *f, gpos_scan_t *gs, nxc__kern_sub_t *subs,
                              uint64_t *covs, uint64_t *cds, uint32_t *nsubs_out,
                              uint32_t *nentries_out) {
    rd_t *r = &gs->r;
    uint32_t nsubs = 0, nentries = 0;
    for (uint32_t li = 0; li < gs->nlookups; li++) {
        uint64_t lo = gs->lookup_list + rd_u16(r, gs->lookup_list + 2u + 2u * (uint64_t)gs->lookups[li]);
        uint32_t type = rd_u16(r, lo);
        uint32_t nsub = rd_u16(r, lo + 4);
        if (r->bad || !rd_has(r, lo + 6, 2u * (uint64_t)nsub)) return font_reject("bad GPOS lookup");
        if (type != 2 && type != 9) continue;
        for (uint32_t s = 0; s < nsub; s++) {
            if (!gpos_spend(gs, 1)) return NXC_OK;
            uint64_t so = lo + rd_u16(r, lo + 6u + 2u * (uint64_t)s);
            uint32_t st = type;
            if (type == 9) {
                uint32_t ext_format = rd_u16(r, so);
                st = rd_u16(r, so + 2);
                so += rd_u32(r, so + 4);
                if (r->bad || ext_format != 1) return font_reject("bad GPOS extension subtable");
            }
            if (st != 2) continue;
            nxc__kern_sub_t ks;
            uint64_t cov = 0, cd1 = 0;
            int rc = pairpos_parse(gs, so, li, f->num_glyphs, &ks, &cov, &cd1);
            if (rc < 0) {
                if (gs->over_budget) return NXC_OK;
                return font_reject("bad GPOS pair adjustment subtable");
            }
            if (rc == 0) continue;
            if (nsubs == NXC_FONT_MAX_KERN_SUBTABLES) { gs->over_budget = 1; return NXC_OK; }
            if (!subs) {
                /* Count the entries this subtable contributes. */
                cov_it_t it;
                uint32_t g = 0, ci = 0;
                uint16_t v = 0;
                int more;
                (void)cov_begin(r, cov, f->num_glyphs, &it);
                while ((more = cov_next(r, &it, &g, &ci)) > 0) {
                    if (!gpos_spend(gs, 1)) return NXC_OK;
                    if (kern_entry_value(r, &ks, cd1, g, ci, &v)) nentries++;
                }
                if (more < 0) return font_reject("bad GPOS coverage range");
            } else {
                subs[nsubs] = ks;
                covs[nsubs] = cov;
                cds[nsubs] = cd1;
            }
            nsubs++;
        }
    }
    *nsubs_out = nsubs;
    *nentries_out = nentries;
    return NXC_OK;
}

/*
 * Pass C: build the per-first-glyph index (CSR layout). start[g] ..
 * start[g + 1] are the entries of glyph g, in subtable order. Returns 0
 * when the entry count differs from pass A (internal inconsistency).
 */
static int gpos_index(const struct nxc_font *f, gpos_scan_t *gs, const nxc__kern_sub_t *subs,
                      const uint64_t *covs, const uint64_t *cds, uint32_t nsubs,
                      uint32_t *start, nxc__kern_entry_t *ent, uint32_t nent) {
    rd_t *r = &gs->r;
    uint32_t ng = f->num_glyphs;
    for (uint32_t g = 0; g <= ng; g++) start[g] = 0;
    /* Count per glyph into start[g + 1]. */
    for (uint32_t s = 0; s < nsubs; s++) {
        cov_it_t it;
        uint32_t g = 0, ci = 0;
        uint16_t v = 0;
        (void)cov_begin(r, covs[s], ng, &it);
        while (cov_next(r, &it, &g, &ci) > 0) {
            if (kern_entry_value(r, &subs[s], cds[s], g, ci, &v)) start[g + 1]++;
        }
    }
    for (uint32_t g = 0; g < ng; g++) start[g + 1] += start[g];
    if (start[ng] != nent) return 0;
    /* Fill, using start[g] as the cursor of glyph g. */
    for (uint32_t s = 0; s < nsubs; s++) {
        cov_it_t it;
        uint32_t g = 0, ci = 0;
        uint16_t v = 0;
        (void)cov_begin(r, covs[s], ng, &it);
        while (cov_next(r, &it, &g, &ci) > 0) {
            if (!kern_entry_value(r, &subs[s], cds[s], g, ci, &v)) continue;
            uint32_t pos = start[g]++;
            if (pos >= nent) return 0;
            ent[pos].sub = (uint16_t)s;
            ent[pos].val = v;
        }
    }
    /* Each cursor now holds the next glyph's start: shift back by one. */
    for (uint32_t g = ng; g > 0; g--) start[g] = start[g - 1];
    start[0] = 0;
    return !r->bad;
}

/* Kerning value of one subtable for (entry value, right glyph). Returns
 * 1 when the subtable applies (its value is stored, possibly 0). */
static int kern_sub_value(rd_t *r, const nxc__kern_sub_t *ks, uint32_t val, uint32_t right,
                          int32_t *out) {
    if (ks->format == 1) {
        if (val >= ks->count1) return 0;
        uint64_t ps = (uint64_t)ks->off + rd_u16(r, (uint64_t)ks->array + 2u * (uint64_t)val);
        uint32_t lo = 0, hi = rd_u16(r, ps);
        if (r->bad) return 0;
        while (lo < hi) {
            uint32_t mid = lo + ((hi - lo) >> 1);
            uint64_t rec = ps + 2u + (uint64_t)mid * ks->rec_size;
            uint32_t second = rd_u16(r, rec);
            if (r->bad) return 0;
            if (second == right) {
                *out = (ks->xadv == 0xFFFFu) ? 0 : rd_s16(r, rec + ks->xadv);
                return !r->bad;
            }
            if (second < right) lo = mid + 1; else hi = mid;
        }
        return 0;
    }
    uint32_t c2 = classdef_get(r, ks->classdef2, right);
    if (r->bad || val >= ks->count1 || c2 >= ks->count2) return 0;
    uint64_t rec = (uint64_t)ks->array +
                   ((uint64_t)val * ks->count2 + c2) * (uint64_t)ks->rec_size;
    *out = (ks->xadv == 0xFFFFu) ? 0 : rd_s16(r, rec + ks->xadv);
    return !r->bad;
}

/* ============ Load / destroy ============ */

static inline uint64_t align16(uint64_t v) { return (v + 15u) & ~(uint64_t)15u; }

/* Cap height and x height fallbacks from the 'H' and 'x' glyph boxes. */
static void finish_metrics(struct nxc_font *f) {
    int16_t x0, y0, x1, y1;
    if (f->cap_height <= 0) {
        uint32_t g = nxc__font_glyph_index(f, 'H');
        if (g && nxc__font_glyph_bounds(f, g, &x0, &y0, &x1, &y1) == NXC_OK && y1 > 0) f->cap_height = y1;
    }
    if (f->x_height <= 0) {
        uint32_t g = nxc__font_glyph_index(f, 'x');
        if (g && nxc__font_glyph_bounds(f, g, &x0, &y0, &x1, &y1) == NXC_OK && y1 > 0) f->x_height = y1;
    }
    if (f->cap_height <= 0) f->cap_height = (int32_t)f->units_per_em * 7 / 10;
    if (f->x_height <= 0) f->x_height = (int32_t)f->units_per_em / 2;
}

/* Log a one-line summary of a loaded font. */
static void log_loaded(const struct nxc_font *f) {
    char line[160];
    uint32_t n = str_append(line, 0, sizeof(line), "[NXC] font loaded: id ");
    n = str_append_u32(line, n, sizeof(line), f->id);
    n = str_append(line, n, sizeof(line), ", ");
    n = str_append_u32(line, n, sizeof(line), f->num_glyphs);
    n = str_append(line, n, sizeof(line), " glyphs, ");
    n = str_append_u32(line, n, sizeof(line), f->units_per_em);
    n = str_append(line, n, sizeof(line), " upem, cmap format ");
    n = str_append_u32(line, n, sizeof(line), f->cmap_format);
    n = str_append(line, n, sizeof(line), ", kerning ");
    n = str_append(line, n, sizeof(line),
                   f->kern_nsubs ? "GPOS" : (f->kern_npairs ? "kern table" : "none"));
    (void)str_append(line, n, sizeof(line), "\n");
    nxc__log(line);
}

nxc_status_t nxc_font_load(nxc_font_t **out, const uint8_t *data, uint32_t size) {
    if (!out) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_font_load");
    *out = 0;
    if (!data || size == 0) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_font_load");
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, "nxc_font_load");

    struct nxc_font f;
    tables_t t;
    int32_t hhea[3];
    nxc__zero(&f, sizeof(f));
    f.data = data;
    f.size = size;

    nxc_status_t st = parse_directory(data, size, &t);
    if (st == NXC_OK) st = parse_head_maxp(&f, &t);
    if (st == NXC_OK) st = parse_hhea_hmtx(&f, &t, hhea);
    if (st == NXC_OK) st = scan_glyphs(&f, &t);
    if (st == NXC_OK) st = parse_cmap(&f, &t);
    if (st == NXC_OK) st = parse_kern(&f, &t);
    if (st != NXC_OK) return st;
    parse_metrics(&f, &t, hhea);

    /* GPOS pass A: count subtables and entries. */
    gpos_scan_t gs;
    uint32_t nsubs = 0, nent = 0;
    nxc__zero(&gs, sizeof(gs));
    if (t.gpos.len) {
        st = gpos_collect(&f, &t, &gs);
        if (st == NXC_OK && gs.nlookups && !gs.over_budget) {
            st = gpos_walk(&f, &gs, 0, 0, 0, &nsubs, &nent);
        }
        if (st != NXC_OK) return st;
        if (gs.over_budget) {
            nxc__log("[NXC] font: GPOS kerning exceeds the engine budget, disabled\n");
            nsubs = 0;
            nent = 0;
        }
    }

    /* One allocation: record, GPOS tables, outline buffers. */
    uint32_t npts = f.max_points ? f.max_points : 1u;
    uint64_t sz_font = align16(sizeof(struct nxc_font));
    uint64_t sz_subs = align16((uint64_t)nsubs * sizeof(nxc__kern_sub_t));
    uint64_t sz_aux = align16((uint64_t)nsubs * sizeof(uint64_t));
    uint64_t sz_start = nsubs ? align16(((uint64_t)f.num_glyphs + 1u) * sizeof(uint32_t)) : 0;
    uint64_t sz_ent = align16((uint64_t)nent * sizeof(nxc__kern_entry_t));
    uint64_t sz_pt = align16((uint64_t)npts * sizeof(int32_t));
    uint64_t sz_fl = align16(npts);
    /* The coverage/classdef offsets (2 * sz_aux) are needed only while
     * building the index; they live at the end of the block. */
    uint64_t total = sz_font + sz_subs + sz_start + sz_ent + 2u * sz_pt + sz_fl + 2u * sz_aux;
    uint8_t *block = (uint8_t *)nxc__alloc(total);
    if (!block) return nxc__fail(NXC_ERR_NO_MEMORY, "nxc_font_load");
    nxc__zero(block, total);

    struct nxc_font *font = (struct nxc_font *)block;
    *font = f;
    uint8_t *p = block + sz_font;
    nxc__kern_sub_t *subs = (nxc__kern_sub_t *)p;       p += sz_subs;
    uint32_t *start = sz_start ? (uint32_t *)p : 0;     p += sz_start;
    nxc__kern_entry_t *ent = (nxc__kern_entry_t *)p;    p += sz_ent;
    font->pt_x = (int32_t *)p;                          p += sz_pt;
    font->pt_y = (int32_t *)p;                          p += sz_pt;
    font->pt_flags = p;                                 p += sz_fl;
    uint64_t *covs = (uint64_t *)p;                     p += sz_aux;
    uint64_t *cds = (uint64_t *)p;

    if (nsubs) {
        /* GPOS passes B and C on the same validated data (pass B does
         * less work than pass A, so a fresh budget always suffices). */
        uint32_t n2 = 0, e2 = 0;
        gs.work = 0;
        gs.over_budget = 0;
        st = gpos_walk(font, &gs, subs, covs, cds, &n2, &e2);
        if (st != NXC_OK || gs.over_budget || n2 != nsubs ||
            !gpos_index(font, &gs, subs, covs, cds, nsubs, start, ent, nent)) {
            nxc__free(block);
            if (st != NXC_OK) return st;
            return nxc__fail(NXC_ERR_STATE, "nxc_font_load");
        }
        font->gpos_off = t.gpos.off;
        font->gpos_end = t.gpos.off + t.gpos.len;
        font->kern_subs = subs;
        font->kern_nsubs = nsubs;
        font->kern_start = start;
        font->kern_entries = ent;
        font->kern_nentries = nent;
    }

    font->generation = nxc__generation();
    font->id = ++g_font_last_id;
    if (font->id == 0) font->id = ++g_font_last_id;
    font->magic = NXC_FONT_MAGIC;
    for (uint32_t c = 0; c < 256; c++) font->ascii_glyph[c] = (uint16_t)cmap_lookup(font, c);
    finish_metrics(font);
    log_loaded(font);
    *out = font;
    return NXC_OK;
}

void nxc_font_destroy(nxc_font_t *font) {
    if (!font) return;
    if (font->magic != NXC_FONT_MAGIC) {
        (void)nxc__fail(NXC_ERR_INVALID_ARG, "nxc_font_destroy");
        return;
    }
    font->magic = 0;
    font->id = 0;
    if (font->generation != nxc__generation()) {
        /* The allocator that made this block may be gone: do not free. */
        nxc__log("[NXC] font destroyed after engine re-init: memory not released\n");
        return;
    }
    nxc__free(font);
}

/* ============ Metrics and lookups ============ */

nxc_status_t nxc_font_metrics(const nxc_font_t *font, float px_size, nxc_font_metrics_t *out) {
    if (!nxc__font_valid(font) || !out || !nxc__finitef(px_size) || !(px_size > 0.0f) ||
        px_size > NXC_FONT_MAX_PX) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_font_metrics");
    }
    float s = px_size / (float)font->units_per_em;
    out->units_per_em = (float)font->units_per_em;
    out->ascent = (float)font->ascent * s;
    out->descent = (float)font->descent * s;
    out->line_gap = (float)font->line_gap * s;
    out->cap_height = (float)font->cap_height * s;
    out->x_height = (float)font->x_height * s;
    return NXC_OK;
}

nxc_status_t nxc__font_vmetrics(const nxc_font_t *f, int32_t *ascent, int32_t *descent,
                                int32_t *line_gap, int32_t *cap_height, int32_t *x_height) {
    if (!nxc__font_valid(f)) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__font_vmetrics");
    if (ascent) *ascent = f->ascent;
    if (descent) *descent = f->descent;
    if (line_gap) *line_gap = f->line_gap;
    if (cap_height) *cap_height = f->cap_height;
    if (x_height) *x_height = f->x_height;
    return NXC_OK;
}

uint32_t nxc__font_glyph_index(const nxc_font_t *f, uint32_t codepoint) {
    if (!nxc__font_valid(f)) {
        (void)nxc__fail(NXC_ERR_INVALID_ARG, "nxc__font_glyph_index");
        return 0;
    }
    if (codepoint < 256u) return f->ascii_glyph[codepoint];
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) return 0;
    return cmap_lookup(f, codepoint);
}

int32_t nxc__font_advance(const nxc_font_t *f, uint32_t glyph) {
    if (!nxc__font_valid(f) || glyph >= f->num_glyphs) {
        (void)nxc__fail(NXC_ERR_INVALID_ARG, "nxc__font_advance");
        return 0;
    }
    rd_t r = rd_make(f->data, f->hmtx_off, f->hmtx_off + 4u * (uint32_t)f->num_hmetrics);
    uint32_t i = glyph < f->num_hmetrics ? glyph : (uint32_t)f->num_hmetrics - 1u;
    uint32_t adv = rd_u16(&r, (uint64_t)f->hmtx_off + 4u * (uint64_t)i);
    if (r.bad) {
        (void)nxc__fail(NXC_ERR_BAD_FONT, "nxc__font_advance");
        return 0;
    }
    return (int32_t)adv;
}

int32_t nxc__font_kerning(const nxc_font_t *f, uint32_t left_glyph, uint32_t right_glyph) {
    if (!nxc__font_valid(f) || left_glyph >= f->num_glyphs || right_glyph >= f->num_glyphs) {
        (void)nxc__fail(NXC_ERR_INVALID_ARG, "nxc__font_kerning");
        return 0;
    }
    if (f->kern_nsubs) {
        rd_t r = rd_make(f->data, f->gpos_off, f->gpos_end);
        uint32_t a = f->kern_start[left_glyph], b = f->kern_start[left_glyph + 1u];
        uint32_t matched = 0xFFFFFFFFu;     /* lookup that already applied */
        int32_t total = 0;
        if (b > f->kern_nentries) b = f->kern_nentries;
        for (uint32_t i = a; i < b; i++) {
            const nxc__kern_entry_t *e = &f->kern_entries[i];
            if (e->sub >= f->kern_nsubs) break;
            const nxc__kern_sub_t *ks = &f->kern_subs[e->sub];
            if (ks->lookup == matched) continue;
            int32_t v = 0;
            if (kern_sub_value(&r, ks, e->val, right_glyph, &v)) {
                total += v;
                matched = ks->lookup;
            }
        }
        return total;
    }
    if (f->kern_npairs) return kern_table_value(f, left_glyph, right_glyph);
    return 0;
}

nxc_status_t nxc__font_glyph_bounds(const nxc_font_t *f, uint32_t glyph,
                                    int16_t *xmin, int16_t *ymin,
                                    int16_t *xmax, int16_t *ymax) {
    if (!nxc__font_valid(f) || glyph >= f->num_glyphs || !xmin || !ymin || !xmax || !ymax) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__font_glyph_bounds");
    }
    *xmin = *ymin = *xmax = *ymax = 0;
    uint32_t off = 0, len = 0;
    if (!glyph_range(f, glyph, &off, &len)) return nxc__fail(NXC_ERR_BAD_FONT, "nxc__font_glyph_bounds");
    if (len == 0) return NXC_OK;
    rd_t r = rd_make(f->data, off, off + len);
    int32_t x0 = rd_s16(&r, (uint64_t)off + 2), y0 = rd_s16(&r, (uint64_t)off + 4);
    int32_t x1 = rd_s16(&r, (uint64_t)off + 6), y1 = rd_s16(&r, (uint64_t)off + 8);
    if (r.bad || x0 > x1 || y0 > y1) return nxc__fail(NXC_ERR_BAD_FONT, "nxc__font_glyph_bounds");
    *xmin = (int16_t)x0; *ymin = (int16_t)y0;
    *xmax = (int16_t)x1; *ymax = (int16_t)y1;
    return NXC_OK;
}

/* ============ Outlines ============ */

/* Affine map from font units to device space:
 * X = xx * fx + xy * fy + dx,  Y = yx * fx + yy * fy + dy. */
typedef struct { float xx, xy, yx, yy, dx, dy; } xform_t;

typedef struct {
    const struct nxc_font *f;
    nxc_path_t            *out;
    uint32_t               visits;     /* component visits so far */
    uint32_t               skipped;    /* point-matched components skipped */
} outline_t;

static inline nxc_point_t xf_apply(const xform_t *t, int32_t fx, int32_t fy) {
    nxc_point_t p;
    p.x = t->xx * (float)fx + t->xy * (float)fy + t->dx;
    p.y = t->yx * (float)fx + t->yy * (float)fy + t->dy;
    return p;
}

static inline nxc_point_t mid_point(nxc_point_t a, nxc_point_t b) {
    nxc_point_t m;
    m.x = 0.5f * (a.x + b.x);
    m.y = 0.5f * (a.y + b.y);
    return m;
}

/*
 * Emit one closed contour of n >= 2 decoded points starting at index s.
 * Consecutive off-curve points imply an on-curve midpoint; a contour may
 * start off-curve (it then starts at the last point if that is on-curve,
 * else at the midpoint of the last and first points).
 */
static nxc_status_t emit_contour(nxc_path_t *out, const struct nxc_font *f, const xform_t *t,
                                 uint32_t s, uint32_t n) {
    const int32_t *X = f->pt_x + s;
    const int32_t *Y = f->pt_y + s;
    const uint8_t *F = f->pt_flags + s;
    nxc_point_t start, ctrl;
    uint32_t first, count;
    if (F[0] & PT_ON_CURVE) {
        start = xf_apply(t, X[0], Y[0]);
        first = 1; count = n - 1;
    } else if (F[n - 1] & PT_ON_CURVE) {
        start = xf_apply(t, X[n - 1], Y[n - 1]);
        first = 0; count = n - 1;
    } else {
        start = mid_point(xf_apply(t, X[n - 1], Y[n - 1]), xf_apply(t, X[0], Y[0]));
        first = 0; count = n;
    }
    nxc_status_t st = nxc_path_move_to(out, start.x, start.y);
    if (st != NXC_OK) return st;
    int pending = 0;
    ctrl = start;
    for (uint32_t k = 0; k < count; k++) {
        uint32_t i = first + k;
        nxc_point_t q = xf_apply(t, X[i], Y[i]);
        if (F[i] & PT_ON_CURVE) {
            st = pending ? nxc_path_quad_to(out, ctrl.x, ctrl.y, q.x, q.y)
                         : nxc_path_line_to(out, q.x, q.y);
            pending = 0;
        } else if (pending) {
            nxc_point_t m = mid_point(ctrl, q);
            st = nxc_path_quad_to(out, ctrl.x, ctrl.y, m.x, m.y);
            ctrl = q;
        } else {
            ctrl = q;
            pending = 1;
        }
        if (st != NXC_OK) return st;
    }
    if (pending) {
        st = nxc_path_quad_to(out, ctrl.x, ctrl.y, start.x, start.y);
        if (st != NXC_OK) return st;
    }
    return nxc_path_close(out);
}

/*
 * Decode a simple glyph (nc > 0 contours) at [off, off + len) into the
 * font's point buffers and emit its contours. Flags may repeat; x/y are
 * deltas that are short (1 byte + sign flag), "same" (0) or 16-bit.
 */
static nxc_status_t outline_simple(outline_t *o, uint32_t off, uint32_t len, uint32_t nc,
                                   const xform_t *t) {
    const struct nxc_font *f = o->f;
    rd_t r = rd_make(f->data, off, off + len);
    uint64_t p = (uint64_t)off + 10u;
    uint32_t last = 0;
    for (uint32_t c = 0; c < nc; c++) {
        uint32_t e = rd_u16(&r, p + 2u * c);
        if (c > 0 && e <= last) return NXC_ERR_BAD_FONT;
        last = e;
    }
    if (r.bad) return NXC_ERR_BAD_FONT;
    uint32_t npts = last + 1u;
    if (npts > f->max_points) return NXC_ERR_BAD_FONT;
    p += 2u * (uint64_t)nc;
    p += 2u + (uint64_t)rd_u16(&r, p);          /* skip hinting instructions */

    uint8_t *F = f->pt_flags;
    for (uint32_t i = 0; i < npts;) {
        uint32_t fl = rd_u8(&r, p++);
        if (r.bad) return NXC_ERR_BAD_FONT;
        F[i++] = (uint8_t)fl;
        if (fl & PT_REPEAT) {
            uint32_t rep = rd_u8(&r, p++);
            if (r.bad || rep > npts - i) return NXC_ERR_BAD_FONT;
            while (rep--) F[i++] = (uint8_t)fl;
        }
    }
    int32_t v = 0;
    for (uint32_t i = 0; i < npts; i++) {
        uint32_t fl = F[i];
        if (fl & PT_X_SHORT) {
            int32_t d = (int32_t)rd_u8(&r, p++);
            v += (fl & PT_X_SAME_POS) ? d : -d;
        } else if (!(fl & PT_X_SAME_POS)) {
            v += rd_s16(&r, p);
            p += 2;
        }
        f->pt_x[i] = v;
    }
    v = 0;
    for (uint32_t i = 0; i < npts; i++) {
        uint32_t fl = F[i];
        if (fl & PT_Y_SHORT) {
            int32_t d = (int32_t)rd_u8(&r, p++);
            v += (fl & PT_Y_SAME_POS) ? d : -d;
        } else if (!(fl & PT_Y_SAME_POS)) {
            v += rd_s16(&r, p);
            p += 2;
        }
        f->pt_y[i] = v;
    }
    if (r.bad) return NXC_ERR_BAD_FONT;

    uint32_t s = 0;
    for (uint32_t c = 0; c < nc; c++) {
        uint32_t e = rd_u16(&r, (uint64_t)off + 10u + 2u * c);
        if (r.bad) return NXC_ERR_BAD_FONT;
        if (e >= s + 1u) {                      /* 1-point contours have no area */
            nxc_status_t st = emit_contour(o->out, f, t, s, e - s + 1u);
            if (st != NXC_OK) return st;
        }
        s = e + 1u;
    }
    return NXC_OK;
}

/* F2DOT14 fixed point to float. */
static inline float f2dot14(int32_t v) { return (float)v * (1.0f / 16384.0f); }

/*
 * Emit glyph `glyph` under transform t. Composite glyphs recurse into
 * their components with the combined transform, at most
 * NXC_FONT_MAX_DEPTH levels and NXC_FONT_MAX_COMPONENTS visits in total.
 */
static nxc_status_t outline_glyph(outline_t *o, uint32_t glyph, const xform_t *t, uint32_t depth) {
    const struct nxc_font *f = o->f;
    uint32_t off = 0, len = 0;
    if (glyph >= f->num_glyphs || !glyph_range(f, glyph, &off, &len)) return NXC_ERR_BAD_FONT;
    if (len == 0) return NXC_OK;
    rd_t r = rd_make(f->data, off, off + len);
    int32_t nc = rd_s16(&r, off);
    if (r.bad) return NXC_ERR_BAD_FONT;
    if (nc >= 0) return nc ? outline_simple(o, off, len, (uint32_t)nc, t) : NXC_OK;

    if (depth >= NXC_FONT_MAX_DEPTH) return NXC_ERR_BAD_FONT;
    uint64_t p = (uint64_t)off + 10u;
    uint32_t flags;
    do {
        flags = rd_u16(&r, p);
        uint32_t gi = rd_u16(&r, p + 2);
        int32_t a1, a2;
        p += 4;
        if (flags & CG_ARG_WORDS) {
            a1 = rd_s16(&r, p); a2 = rd_s16(&r, p + 2);
            if (!(flags & CG_ARGS_XY)) { a1 &= 0xFFFF; a2 &= 0xFFFF; }
            p += 4;
        } else {
            a1 = (int32_t)rd_u8(&r, p); a2 = (int32_t)rd_u8(&r, p + 1);
            if (flags & CG_ARGS_XY) {
                if (a1 > 127) a1 -= 256;
                if (a2 > 127) a2 -= 256;
            }
            p += 2;
        }
        /* Component matrix: x' = a x + c y, y' = b x + d y. */
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
        if (flags & CG_SCALE) {
            a = d = f2dot14(rd_s16(&r, p));
            p += 2;
        } else if (flags & CG_XY_SCALE) {
            a = f2dot14(rd_s16(&r, p));
            d = f2dot14(rd_s16(&r, p + 2));
            p += 4;
        } else if (flags & CG_TWO_BY_TWO) {
            a = f2dot14(rd_s16(&r, p));
            b = f2dot14(rd_s16(&r, p + 2));
            c = f2dot14(rd_s16(&r, p + 4));
            d = f2dot14(rd_s16(&r, p + 6));
            p += 8;
        }
        if (r.bad || gi >= f->num_glyphs) return NXC_ERR_BAD_FONT;
        if (++o->visits > NXC_FONT_MAX_COMPONENTS) return NXC_ERR_BAD_FONT;
        if (!(flags & CG_ARGS_XY)) {
            /* Point-matched placement needs the parent's points: skip. */
            o->skipped++;
            continue;
        }
        float ox = (float)a1, oy = (float)a2;
        if ((flags & CG_SCALED_OFS) && !(flags & CG_UNSCALED_OFS)) {
            ox = a * (float)a1 + c * (float)a2;
            oy = b * (float)a1 + d * (float)a2;
        }
        xform_t ct;
        ct.xx = t->xx * a + t->xy * b;
        ct.xy = t->xx * c + t->xy * d;
        ct.yx = t->yx * a + t->yy * b;
        ct.yy = t->yx * c + t->yy * d;
        ct.dx = t->xx * ox + t->xy * oy + t->dx;
        ct.dy = t->yx * ox + t->yy * oy + t->dy;
        nxc_status_t st = outline_glyph(o, gi, &ct, depth + 1u);
        if (st != NXC_OK) return st;
    } while (flags & CG_MORE);
    return NXC_OK;
}

nxc_status_t nxc__font_glyph_outline(const nxc_font_t *f, uint32_t glyph, nxc_path_t *out,
                                     float scale, float x, float y) {
    if (!nxc__font_valid(f) || !out || glyph >= f->num_glyphs ||
        !nxc__finitef(scale) || !(scale > 0.0f) || scale > NXC_FONT_MAX_SCALE ||
        !nxc__finitef(x) || !nxc__finitef(y) ||
        nxc__absf(x) > NXC_FONT_MAX_COORD || nxc__absf(y) > NXC_FONT_MAX_COORD) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__font_glyph_outline");
    }
    if (out->error != NXC_OK) return nxc__fail(out->error, "nxc__font_glyph_outline");
    uint32_t npts = out->npts, nverbs = out->nverbs;
    xform_t t;
    t.xx = scale; t.xy = 0.0f; t.dx = x;
    t.yx = 0.0f;  t.yy = -scale; t.dy = y;
    outline_t o;
    o.f = f;
    o.out = out;
    o.visits = 0;
    o.skipped = 0;
    nxc_status_t st = outline_glyph(&o, glyph, &t, 0);
    if (st != NXC_OK) {
        /* Atomic append: drop whatever this call added. */
        out->npts = npts;
        out->nverbs = nverbs;
        out->error = NXC_OK;
        return nxc__fail(st, "nxc__font_glyph_outline");
    }
    if (o.skipped) return nxc__fail(NXC_ERR_UNSUPPORTED, "nxc__font_glyph_outline");
    return NXC_OK;
}

nxc_status_t nxc_font_glyph_path(const nxc_font_t *font, uint32_t codepoint,
                                 float px_size, float x, float baseline_y,
                                 nxc_path_t *out, float *advance_out) {
    if (advance_out) *advance_out = 0.0f;
    if (!nxc__font_valid(font) || !out || !nxc__finitef(px_size) || !(px_size > 0.0f) ||
        px_size > NXC_FONT_MAX_PX || !nxc__finitef(x) || !nxc__finitef(baseline_y) ||
        nxc__absf(x) > NXC_FONT_MAX_COORD || nxc__absf(baseline_y) > NXC_FONT_MAX_COORD) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_font_glyph_path");
    }
    uint32_t g = nxc__font_glyph_index(font, codepoint);
    if (g == 0) return nxc__fail(NXC_ERR_NOT_FOUND, "nxc_font_glyph_path");
    float scale = px_size / (float)font->units_per_em;
    nxc_status_t st = nxc__font_glyph_outline(font, g, out, scale, x, baseline_y);
    if (st != NXC_OK && st != NXC_ERR_UNSUPPORTED) return st;
    if (advance_out) *advance_out = (float)nxc__font_advance(font, g) * scale;
    return st;
}
