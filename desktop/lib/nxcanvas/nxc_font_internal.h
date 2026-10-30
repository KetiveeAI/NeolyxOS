/*
 * NeolyxOS - NXCanvas font engine internal interface
 *
 * Shared by nxc_font.c (TrueType parsing, cmap, outlines, kerning) and
 * nxc_text.c (layout, glyph cache, rasterization). Nothing outside
 * desktop/lib/nxcanvas/ may include this header.
 *
 * Units and conventions
 *   Glyph ids are font glyph indices (0 = .notdef). Metrics, advances,
 *   kerning and glyph bounds are in FONT UNITS with y pointing UP, as in
 *   the font file; multiply by px_size / units_per_em for pixels. Outlines
 *   are produced in DEVICE space with y pointing DOWN.
 *
 * Lifetime
 *   A font references the caller's data (it never copies it) and owns one
 *   allocation made at load time. Every function below is safe to call
 *   with a NULL or destroyed font: it returns 0 / an error status and
 *   records the failure through nxc__fail. None of them allocate.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXC_FONT_INTERNAL_H
#define NXC_FONT_INTERNAL_H

#include "nxc_internal.h"

/* ============ Limits ============ */

#define NXC_FONT_MAGIC              0x4E58464Eu  /* 'NXFN': live font marker */
#define NXC_FONT_MAX_DEPTH          6            /* composite nesting levels */
#define NXC_FONT_MAX_COMPONENTS     256          /* component visits per outline */
#define NXC_FONT_MAX_PX             16384.0f     /* largest px_size accepted */
#define NXC_FONT_MAX_SCALE          1024.0f      /* largest px per font unit */
#define NXC_FONT_MAX_COORD          1.0e7f       /* |origin| limit in px */
#define NXC_FONT_MAX_KERN_LOOKUPS   64           /* GPOS 'kern' lookups used */
#define NXC_FONT_MAX_KERN_SUBTABLES 4096         /* PairPos subtables used */
#define NXC_FONT_MAX_KERN_ENTRIES   (1u << 20)   /* first-glyph index entries */
#define NXC_FONT_MAX_NAME_RECORDS   1024         /* 'name' records examined */

/*
 * Path capacity a simple glyph outline needs: a contour of k points emits
 * at most 2k + 1 points and k + 2 verbs, so a glyph of n points in c
 * contours needs at most 2n + c points and n + 2c verbs. A composite glyph
 * needs the sum over its components. A path that is too small makes
 * nxc__font_glyph_outline return NXC_ERR_OVERFLOW with the path restored.
 */
#define NXC_FONT_PATH_PTS(n, c)     (2u * (uint32_t)(n) + (uint32_t)(c))
#define NXC_FONT_PATH_VERBS(n, c)   ((uint32_t)(n) + 2u * (uint32_t)(c))

/* ============ Pre-resolved GPOS kerning ============ */

/*
 * One PairPos subtable (GPOS lookup type 2, possibly reached through an
 * extension lookup) of the font's 'kern' feature, validated at load time.
 * All offsets are absolute offsets into the font data.
 */
typedef struct {
    uint32_t off;          /* PairPos subtable */
    uint32_t array;        /* fmt 1: PairSet offset array; fmt 2: Class1Records */
    uint32_t classdef2;    /* fmt 2: ClassDef for the second glyph */
    uint16_t format;       /* 1 = glyph pairs, 2 = class pairs */
    uint16_t lookup;       /* ordinal of the owning lookup (first match wins) */
    uint16_t count1;       /* fmt 1: PairSet count; fmt 2: class1Count */
    uint16_t count2;       /* fmt 2: class2Count */
    uint16_t rec_size;     /* bytes per PairValueRecord / Class2Record */
    uint16_t xadv;         /* byte offset of value1.XAdvance in a record,
                              0xFFFF when value1 has no XAdvance */
} nxc__kern_sub_t;

/* One subtable covering a first glyph. val is the coverage index (fmt 1)
 * or the first glyph's class (fmt 2). */
typedef struct {
    uint16_t sub;          /* index into nxc_font.kern_subs */
    uint16_t val;
} nxc__kern_entry_t;

/* ============ Parsed font ============ */

/*
 * Fields are READ-ONLY outside nxc_font.c; prefer the accessors below.
 * Vertical metrics are positive distances in font units.
 */
struct nxc_font {
    uint32_t       magic;            /* NXC_FONT_MAGIC while alive */
    uint32_t       id;               /* unique per load, never 0 (cache keys) */
    uint32_t       generation;       /* engine generation at load time */
    const uint8_t *data;             /* caller's font file (must outlive font) */
    uint32_t       size;

    /* head / maxp */
    uint16_t units_per_em;           /* 16..16384 */
    uint16_t num_glyphs;             /* >= 1 */
    uint16_t loc_long;               /* 1 = 32-bit loca entries */
    int16_t  bbox[4];                /* font bbox: xMin, yMin, xMax, yMax */

    /* vertical metrics (font units, positive) */
    int32_t  ascent;                 /* above the baseline */
    int32_t  descent;                /* below the baseline */
    int32_t  line_gap;
    int32_t  cap_height;
    int32_t  x_height;

    /* hmtx */
    uint32_t hmtx_off;
    uint16_t num_hmetrics;           /* 1..num_glyphs */

    /* loca / glyf */
    uint32_t loca_off;
    uint32_t glyf_off;
    uint32_t glyf_len;

    /* cmap: chosen subtable (format 4 or 12) */
    uint32_t cmap_sub;               /* absolute offset of the subtable */
    uint32_t cmap_end;               /* end of the cmap table (read limit) */
    uint32_t cmap_count;             /* fmt 4: segCount; fmt 12: numGroups */
    uint16_t cmap_format;
    uint16_t cmap_symbol;            /* 1 = (3,0) symbol: retry at U+F000+c */
    uint16_t ascii_glyph[256];       /* pre-resolved U+0000..U+00FF */

    /* legacy 'kern' table, format 0 (used only without GPOS kerning) */
    uint32_t kern_pairs;             /* absolute offset of the pair array */
    uint32_t kern_npairs;

    /* GPOS 'kern' feature, resolved per first glyph at load */
    uint32_t                 gpos_off, gpos_end;
    const nxc__kern_sub_t   *kern_subs;
    uint32_t                 kern_nsubs;
    const uint32_t          *kern_start;   /* num_glyphs + 1 entry offsets */
    const nxc__kern_entry_t *kern_entries;
    uint32_t                 kern_nentries;

    /* 'name' table (validated header and record array; strings are
     * bounds-checked per read against the storage area) */
    uint32_t name_off;               /* absolute offset of the record array */
    uint32_t name_count;             /* records (<= NXC_FONT_MAX_NAME_RECORDS) */
    uint32_t name_storage;           /* absolute offset of string storage */
    uint32_t name_end;               /* end of the name table */

    /* style data from OS/2, post and head */
    uint16_t weight_class;           /* OS/2 usWeightClass, 400 when absent */
    uint16_t is_italic;              /* OS/2 fsSelection ITALIC or head macStyle */
    uint16_t is_fixed_pitch;         /* post isFixedPitch != 0 */
    uint16_t pad_style;
    uint32_t font_revision;          /* head fontRevision (16.16) */

    /* fallback faces consulted for code points this font does not map
     * (one level: a fallback's own fallbacks are not followed). The id
     * snapshot detects a fallback that was destroyed and reloaded. */
    const struct nxc_font *fallback[NXC_FONT_MAX_FALLBACKS];
    uint32_t               fallback_id[NXC_FONT_MAX_FALLBACKS];
    uint32_t               nfallback;

    /* outline decoding scratch, sized by the largest simple glyph */
    uint32_t max_points;             /* points in the largest simple glyph */
    uint32_t max_contours;           /* contours in the largest simple glyph */
    int32_t *pt_x;
    int32_t *pt_y;
    uint8_t *pt_flags;
};

/* ============ Accessors ============ */

/* 1 when f points to a live font. */
static inline int nxc__font_valid(const nxc_font_t *f) {
    return f && f->magic == NXC_FONT_MAGIC;
}
/* Unique id of a loaded font (0 for an invalid font). */
static inline uint32_t nxc__font_id(const nxc_font_t *f) {
    return nxc__font_valid(f) ? f->id : 0u;
}
/* Font units per em (0 for an invalid font). scale = px / units_per_em. */
static inline float nxc__font_units_per_em(const nxc_font_t *f) {
    return nxc__font_valid(f) ? (float)f->units_per_em : 0.0f;
}
/* Number of glyphs (valid ids are 0 .. n-1). */
static inline uint32_t nxc__font_num_glyphs(const nxc_font_t *f) {
    return nxc__font_valid(f) ? f->num_glyphs : 0u;
}
/* 1 when the font has any kerning data (GPOS 'kern' or 'kern' table). */
static inline int nxc__font_has_kerning(const nxc_font_t *f) {
    return nxc__font_valid(f) && (f->kern_nsubs > 0 || f->kern_npairs > 0);
}

/* Vertical metrics in font units (positive distances). Any output pointer
 * may be NULL. Returns NXC_ERR_INVALID_ARG for an invalid font. */
nxc_status_t nxc__font_vmetrics(const nxc_font_t *f, int32_t *ascent, int32_t *descent,
                                int32_t *line_gap, int32_t *cap_height, int32_t *x_height);

/* ============ Glyph lookup and metrics ============ */

/* Glyph for a Unicode code point through the cmap (0 = .notdef, also for
 * surrogates and code points above U+10FFFF). U+0000..U+00FF are a table
 * read; others a binary search. */
uint32_t nxc__font_glyph_index(const nxc_font_t *f, uint32_t codepoint);

/* Advance width in font units (hmtx; glyphs past numberOfHMetrics use the
 * last advance). 0 and a recorded error for an invalid font or glyph. */
int32_t nxc__font_advance(const nxc_font_t *f, uint32_t glyph);

/* Horizontal kerning between two adjacent glyphs in font units, to add to
 * the left glyph's advance (negative = tighter). From the GPOS 'kern'
 * feature (value1.XAdvance of PairPos lookups, summed over lookups, first
 * matching subtable within a lookup) when the font has one, otherwise
 * from a format 0 'kern' table. 0 when no pair applies. */
int32_t nxc__font_kerning(const nxc_font_t *f, uint32_t left_glyph, uint32_t right_glyph);

/* ============ Outlines ============ */

/*
 * Glyph box from the glyph header, font units, y up (all four outputs are
 * required). An empty glyph (no outline, e.g. space) reports a zero box
 * and NXC_OK. The header box of a hostile font need not contain the
 * outline: callers must still clip. NXC_ERR_BAD_FONT when the glyph data
 * is malformed (box outputs are then zero).
 */
nxc_status_t nxc__font_glyph_bounds(const nxc_font_t *f, uint32_t glyph,
                                    int16_t *xmin, int16_t *ymin,
                                    int16_t *xmax, int16_t *ymax);

/*
 * Append a glyph outline to `out` as move/line/quad/close verbs. A font
 * point (fx, fy) maps to device (x + fx * scale, y - fy * scale), so with
 * scale = px_size / units_per_em, (x, y) is the pen position on the
 * baseline. Composite glyphs are expanded (nesting up to
 * NXC_FONT_MAX_DEPTH, NXC_FONT_MAX_COMPONENTS component visits); hinting
 * instructions are ignored. Empty glyphs append nothing.
 *
 * Returns NXC_OK; NXC_ERR_INVALID_ARG for bad arguments (scale must be in
 * (0, NXC_FONT_MAX_SCALE], |x|,|y| <= NXC_FONT_MAX_COORD); NXC_ERR_BAD_FONT
 * for malformed glyph data or too deep / too many components;
 * NXC_ERR_OVERFLOW when the path is too small. On those errors the path
 * is restored to its previous contents. NXC_ERR_UNSUPPORTED when a
 * point-matched component (ARGS_ARE_XY_VALUES clear) was skipped: the
 * path then holds every other component.
 */
nxc_status_t nxc__font_glyph_outline(const nxc_font_t *f, uint32_t glyph, nxc_path_t *out,
                                     float scale, float x, float y);

/*
 * Same as nxc__font_glyph_outline with separate horizontal and vertical
 * scales (font unit -> px). Used by vertical hinting, which stretches the
 * outline slightly so the x-height lands on a pixel boundary. Both scales
 * must be in (0, NXC_FONT_MAX_SCALE].
 */
nxc_status_t nxc__font_glyph_outline_xy(const nxc_font_t *f, uint32_t glyph, nxc_path_t *out,
                                        float sx, float sy, float x, float y);

/* Fallback i of f when it is still the same live font, else NULL. */
const nxc_font_t *nxc__font_fallback(const nxc_font_t *f, uint32_t i);

#endif /* NXC_FONT_INTERNAL_H */
