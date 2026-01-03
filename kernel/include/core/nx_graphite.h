/*
 * NeolyxOS - Graphite2 Complex Script Shaping
 * 
 * Simplified kernel-level text shaping for complex scripts:
 * - Devanagari (Hindi, Sanskrit)
 * - Arabic (RTL support)
 * - Thai
 * - Bengali
 * - Tamil
 * 
 * Based on Graphite2 by SIL International (LGPL)
 * Simplified for kernel use - no C++, no file I/O
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_NX_GRAPHITE_H
#define NEOLYX_NX_GRAPHITE_H

#include <stdint.h>
#include <stddef.h>

/* Forward declaration */
typedef struct nx_font nx_font_t;

/* ============ Script Detection ============ */

/* Unicode script ranges for complex scripts */
typedef enum {
    NX_SCRIPT_LATIN = 0,        /* Basic Latin (no shaping needed) */
    NX_SCRIPT_DEVANAGARI,       /* Hindi, Sanskrit: U+0900-U+097F */
    NX_SCRIPT_ARABIC,           /* Arabic: U+0600-U+06FF (RTL) */
    NX_SCRIPT_THAI,             /* Thai: U+0E00-U+0E7F */
    NX_SCRIPT_BENGALI,          /* Bengali: U+0980-U+09FF */
    NX_SCRIPT_TAMIL,            /* Tamil: U+0B80-U+0BFF */
    NX_SCRIPT_GURMUKHI,         /* Punjabi: U+0A00-U+0A7F */
    NX_SCRIPT_GUJARATI,         /* Gujarati: U+0A80-U+0AFF */
    NX_SCRIPT_KANNADA,          /* Kannada: U+0C80-U+0CFF */
    NX_SCRIPT_MALAYALAM,        /* Malayalam: U+0D00-U+0D7F */
    NX_SCRIPT_TELUGU,           /* Telugu: U+0C00-U+0C7F */
    NX_SCRIPT_HEBREW,           /* Hebrew: U+0590-U+05FF (RTL) */
    NX_SCRIPT_COUNT
} nx_script_t;

/* Text direction */
typedef enum {
    NX_DIR_LTR = 0,             /* Left-to-right */
    NX_DIR_RTL = 1              /* Right-to-left (Arabic, Hebrew) */
} nx_text_direction_t;

/* ============ Shaped Glyph ============ */

/* A single shaped glyph with position offsets */
typedef struct {
    uint16_t glyph_id;          /* Glyph ID in the font */
    uint16_t cluster;           /* Character cluster index */
    float x_offset;             /* Horizontal offset from base position */
    float y_offset;             /* Vertical offset (for diacritics) */
    float x_advance;            /* Horizontal advance to next glyph */
    float y_advance;            /* Vertical advance (usually 0) */
    uint8_t is_base;            /* Is this a base glyph? */
    uint8_t is_mark;            /* Is this a combining mark? */
} nx_shaped_glyph_t;

/* Result of shaping operation */
typedef struct {
    nx_shaped_glyph_t *glyphs;  /* Array of shaped glyphs */
    size_t count;               /* Number of glyphs */
    float total_width;          /* Total advance width */
    float total_height;         /* Total height (for vertical text) */
    nx_script_t script;         /* Detected script */
    nx_text_direction_t dir;    /* Text direction */
} nx_shaping_result_t;

/* ============ Shaping Context ============ */

/* Shaping features (OpenType feature tags) */
typedef struct {
    uint32_t tag;               /* 4-byte feature tag (e.g., 'liga') */
    uint32_t value;             /* Feature value (usually 1 = on) */
} nx_shaping_feature_t;

/* Context for shaping operation */
typedef struct {
    nx_text_direction_t direction;  /* Override text direction */
    uint32_t script_tag;            /* OpenType script tag (e.g., 'deva') */
    uint32_t language_tag;          /* OpenType language tag */
    nx_shaping_feature_t *features; /* Custom features */
    size_t feature_count;           /* Number of features */
} nx_shaping_context_t;

/* ============ Silf Table Structures ============ */

/* Graphite Silf table header (simplified) */
typedef struct {
    uint32_t version;           /* Table version */
    uint32_t num_sub_tables;    /* Number of sub-tables */
    uint32_t reserved;
} nx_silf_header_t;

/* Graphite pass (shaping rule pass) */
typedef struct {
    uint8_t pass_type;          /* 0=linebreak, 1=sub, 2=pos, 3=just */
    uint8_t flags;
    uint16_t max_rule_loop;     /* Max iterations for rules */
    uint16_t num_rules;         /* Number of rules in this pass */
    /* Rule data follows... */
} nx_silf_pass_t;

/* ============ API Functions ============ */

/**
 * Initialize the Graphite shaping subsystem
 * Called once at kernel startup
 */
int nx_graphite_init(void);

/**
 * Shutdown the Graphite subsystem
 */
void nx_graphite_shutdown(void);

/**
 * Detect script from UTF-8 text
 * Returns the primary script detected
 */
nx_script_t nx_detect_script(const char *text, size_t len);

/**
 * Check if a script requires complex shaping
 */
int nx_script_needs_shaping(nx_script_t script);

/**
 * Get default text direction for a script
 */
nx_text_direction_t nx_script_direction(nx_script_t script);

/**
 * Check if font has Graphite tables (Silf)
 * Returns 1 if font supports Graphite, 0 otherwise
 */
int nx_graphite_font_supported(nx_font_t *font);

/**
 * Shape text using Graphite rules
 * 
 * @param font      Font to use for shaping
 * @param text      UTF-8 input text
 * @param len       Length of text in bytes
 * @param ctx       Shaping context (NULL for defaults)
 * @return          Shaping result (caller must free with nx_graphite_free_result)
 */
nx_shaping_result_t* nx_graphite_shape(
    nx_font_t *font,
    const char *text,
    size_t len,
    const nx_shaping_context_t *ctx
);

/**
 * Shape text with basic fallback (no Graphite tables)
 * Uses Unicode character properties for basic shaping
 */
nx_shaping_result_t* nx_graphite_shape_fallback(
    nx_font_t *font,
    const char *text,
    size_t len,
    const nx_shaping_context_t *ctx
);

/**
 * Free shaping result
 */
void nx_graphite_free_result(nx_shaping_result_t *result);

/* ============ OpenType Feature Tags ============ */

/* Common OpenType features for complex scripts */
#define NX_FEATURE_LIGA     0x6C696761  /* 'liga' - Standard ligatures */
#define NX_FEATURE_CLIG     0x636C6967  /* 'clig' - Contextual ligatures */
#define NX_FEATURE_RLIG     0x726C6967  /* 'rlig' - Required ligatures */
#define NX_FEATURE_INIT     0x696E6974  /* 'init' - Initial forms */
#define NX_FEATURE_MEDI     0x6D656469  /* 'medi' - Medial forms */
#define NX_FEATURE_FINA     0x66696E61  /* 'fina' - Final forms */
#define NX_FEATURE_ISOL     0x69736F6C  /* 'isol' - Isolated forms */
#define NX_FEATURE_NUKT     0x6E756B74  /* 'nukt' - Nukta forms (Indic) */
#define NX_FEATURE_AKHN     0x616B686E  /* 'akhn' - Akhand (Indic) */
#define NX_FEATURE_RPHF     0x72706866  /* 'rphf' - Reph forms (Indic) */
#define NX_FEATURE_BLWF     0x626C7766  /* 'blwf' - Below-base forms */
#define NX_FEATURE_HALF     0x68616C66  /* 'half' - Half forms (Indic) */
#define NX_FEATURE_PSTF     0x70737466  /* 'pstf' - Post-base forms */
#define NX_FEATURE_VATU     0x76617475  /* 'vatu' - Vattu variants */
#define NX_FEATURE_CJCT     0x636A6374  /* 'cjct' - Conjunct forms */
#define NX_FEATURE_KERN     0x6B65726E  /* 'kern' - Kerning */
#define NX_FEATURE_MARK     0x6D61726B  /* 'mark' - Mark positioning */
#define NX_FEATURE_MKMK     0x6D6B6D6B  /* 'mkmk' - Mark-to-mark */

/* ============ Script Tags ============ */

/* OpenType script tags */
#define NX_SCRIPT_TAG_DEVA  0x64657661  /* 'deva' - Devanagari */
#define NX_SCRIPT_TAG_ARAB  0x61726162  /* 'arab' - Arabic */
#define NX_SCRIPT_TAG_THAI  0x74686169  /* 'thai' - Thai */
#define NX_SCRIPT_TAG_BENG  0x62656E67  /* 'beng' - Bengali */
#define NX_SCRIPT_TAG_TAML  0x74616D6C  /* 'taml' - Tamil */
#define NX_SCRIPT_TAG_GURU  0x67757275  /* 'guru' - Gurmukhi */
#define NX_SCRIPT_TAG_GUJR  0x67756A72  /* 'gujr' - Gujarati */
#define NX_SCRIPT_TAG_KNDA  0x6B6E6461  /* 'knda' - Kannada */
#define NX_SCRIPT_TAG_MLYM  0x6D6C796D  /* 'mlym' - Malayalam */
#define NX_SCRIPT_TAG_TELU  0x74656C75  /* 'telu' - Telugu */
#define NX_SCRIPT_TAG_HEBR  0x68656272  /* 'hebr' - Hebrew */

#endif /* NEOLYX_NX_GRAPHITE_H */
