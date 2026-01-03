/*
 * NeolyxOS - Graphite2 Complex Script Shaping
 * 
 * Simplified kernel-level text shaping implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "core/nx_graphite.h"
#include "core/font_system.h"

/* ============ Unicode Script Ranges ============ */

typedef struct {
    uint32_t start;
    uint32_t end;
    nx_script_t script;
} script_range_t;

static const script_range_t g_script_ranges[] = {
    { 0x0900, 0x097F, NX_SCRIPT_DEVANAGARI },   /* Hindi, Sanskrit */
    { 0x0980, 0x09FF, NX_SCRIPT_BENGALI },      /* Bengali */
    { 0x0A00, 0x0A7F, NX_SCRIPT_GURMUKHI },     /* Punjabi */
    { 0x0A80, 0x0AFF, NX_SCRIPT_GUJARATI },     /* Gujarati */
    { 0x0B80, 0x0BFF, NX_SCRIPT_TAMIL },        /* Tamil */
    { 0x0C00, 0x0C7F, NX_SCRIPT_TELUGU },       /* Telugu */
    { 0x0C80, 0x0CFF, NX_SCRIPT_KANNADA },      /* Kannada */
    { 0x0D00, 0x0D7F, NX_SCRIPT_MALAYALAM },    /* Malayalam */
    { 0x0E00, 0x0E7F, NX_SCRIPT_THAI },         /* Thai */
    { 0x0590, 0x05FF, NX_SCRIPT_HEBREW },       /* Hebrew */
    { 0x0600, 0x06FF, NX_SCRIPT_ARABIC },       /* Arabic */
    { 0x0750, 0x077F, NX_SCRIPT_ARABIC },       /* Arabic Supplement */
    { 0x08A0, 0x08FF, NX_SCRIPT_ARABIC },       /* Arabic Extended-A */
    { 0xFB50, 0xFDFF, NX_SCRIPT_ARABIC },       /* Arabic Presentation Forms */
    { 0xFE70, 0xFEFF, NX_SCRIPT_ARABIC },       /* Arabic Presentation Forms-B */
    { 0, 0, NX_SCRIPT_LATIN }                   /* Sentinel */
};

/* RTL scripts */
static const nx_script_t g_rtl_scripts[] = {
    NX_SCRIPT_ARABIC,
    NX_SCRIPT_HEBREW
};

/* ============ UTF-8 Decoder ============ */

static uint32_t utf8_decode(const char **ptr, const char *end) {
    const uint8_t *p = (const uint8_t *)*ptr;
    uint32_t cp;
    int len;
    
    if (p >= (const uint8_t *)end) return 0;
    
    if (*p < 0x80) {
        cp = *p++;
        len = 1;
    } else if ((*p & 0xE0) == 0xC0) {
        cp = *p++ & 0x1F;
        len = 2;
    } else if ((*p & 0xF0) == 0xE0) {
        cp = *p++ & 0x0F;
        len = 3;
    } else if ((*p & 0xF8) == 0xF0) {
        cp = *p++ & 0x07;
        len = 4;
    } else {
        *ptr = (const char *)(p + 1);
        return 0xFFFD; /* Replacement char */
    }
    
    for (int i = 1; i < len; i++) {
        if (p >= (const uint8_t *)end || (*p & 0xC0) != 0x80) {
            *ptr = (const char *)p;
            return 0xFFFD;
        }
        cp = (cp << 6) | (*p++ & 0x3F);
    }
    
    *ptr = (const char *)p;
    return cp;
}

/* ============ Script Detection ============ */

static nx_script_t codepoint_script(uint32_t cp) {
    for (int i = 0; g_script_ranges[i].start != 0; i++) {
        if (cp >= g_script_ranges[i].start && cp <= g_script_ranges[i].end) {
            return g_script_ranges[i].script;
        }
    }
    return NX_SCRIPT_LATIN;
}

int nx_graphite_init(void) {
    /* Nothing to initialize for now */
    return 0;
}

void nx_graphite_shutdown(void) {
    /* Nothing to clean up */
}

nx_script_t nx_detect_script(const char *text, size_t len) {
    if (!text || len == 0) return NX_SCRIPT_LATIN;
    
    const char *ptr = text;
    const char *end = text + len;
    nx_script_t detected = NX_SCRIPT_LATIN;
    int counts[NX_SCRIPT_COUNT] = {0};
    
    while (ptr < end) {
        uint32_t cp = utf8_decode(&ptr, end);
        if (cp == 0) break;
        nx_script_t s = codepoint_script(cp);
        counts[s]++;
    }
    
    /* Find most common non-Latin script */
    int max_count = 0;
    for (int i = 1; i < NX_SCRIPT_COUNT; i++) {
        if (counts[i] > max_count) {
            max_count = counts[i];
            detected = (nx_script_t)i;
        }
    }
    
    return max_count > 0 ? detected : NX_SCRIPT_LATIN;
}

int nx_script_needs_shaping(nx_script_t script) {
    return script != NX_SCRIPT_LATIN;
}

nx_text_direction_t nx_script_direction(nx_script_t script) {
    for (size_t i = 0; i < sizeof(g_rtl_scripts) / sizeof(g_rtl_scripts[0]); i++) {
        if (g_rtl_scripts[i] == script) return NX_DIR_RTL;
    }
    return NX_DIR_LTR;
}

/* ============ Memory Helpers ============ */

/* Simple kernel allocation (replace with actual kernel malloc) */
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

static void* graphite_alloc(size_t size) {
    return kmalloc(size);
}

static void graphite_free(void* ptr) {
    if (ptr) kfree(ptr);
}

/* ============ Font Table Access ============ */

/* Check for Silf (Graphite) table */
int nx_graphite_font_supported(nx_font_t *font) {
    if (!font) return 0;
    /* Check if font has Silf table - simplified check */
    /* In a full implementation, we'd parse the font's table directory */
    /* For now, return 0 to use fallback shaping */
    return 0;
}

/* ============ Devanagari Shaping ============ */

/* Devanagari character classes */
typedef enum {
    DEVA_OTHER = 0,
    DEVA_CONSONANT,     /* क, ख, ग, ... */
    DEVA_VOWEL,         /* अ, आ, इ, ... */
    DEVA_MATRA,         /* ा, ि, ी, ... (vowel signs) */
    DEVA_VIRAMA,        /* ् (halant) */
    DEVA_NUKTA,         /* ़ */
    DEVA_ANUSVARA,      /* ं */
    DEVA_VISARGA,       /* ः */
    DEVA_CHANDRABINDU   /* ँ */
} deva_class_t;

static deva_class_t devanagari_class(uint32_t cp) {
    if (cp >= 0x0915 && cp <= 0x0939) return DEVA_CONSONANT; /* क-ह */
    if (cp >= 0x0958 && cp <= 0x095F) return DEVA_CONSONANT; /* Nukta consonants */
    if (cp >= 0x0904 && cp <= 0x0914) return DEVA_VOWEL;     /* अ-औ */
    if (cp >= 0x093E && cp <= 0x094C) return DEVA_MATRA;     /* Vowel signs */
    if (cp == 0x094D) return DEVA_VIRAMA;                     /* Halant */
    if (cp == 0x093C) return DEVA_NUKTA;                      /* Nukta */
    if (cp == 0x0902) return DEVA_ANUSVARA;
    if (cp == 0x0903) return DEVA_VISARGA;
    if (cp == 0x0901) return DEVA_CHANDRABINDU;
    return DEVA_OTHER;
}

/* ============ Arabic Shaping ============ */

/* Arabic joining types */
typedef enum {
    ARAB_NON_JOINING = 0,
    ARAB_RIGHT_JOINING,     /* Joins to the right only (Alef, Dal, etc.) */
    ARAB_DUAL_JOINING,      /* Joins both sides (most letters) */
    ARAB_TRANSPARENT        /* Marks, don't affect joining */
} arab_join_t;

/* Arabic positional forms */
typedef enum {
    ARAB_ISOLATED = 0,
    ARAB_INITIAL,
    ARAB_MEDIAL,
    ARAB_FINAL
} arab_form_t;

static arab_join_t arabic_joining_type(uint32_t cp) {
    /* Right-joining letters */
    if (cp == 0x0627 || /* Alef */
        cp == 0x0622 || /* Alef with madda */
        cp == 0x0623 || /* Alef with hamza */
        cp == 0x0625 || /* Alef with hamza below */
        cp == 0x062F || /* Dal */
        cp == 0x0630 || /* Thal */
        cp == 0x0631 || /* Ra */
        cp == 0x0632 || /* Zain */
        cp == 0x0648 || /* Waw */
        cp == 0x0629)   /* Ta Marbuta */
        return ARAB_RIGHT_JOINING;
    
    /* Dual-joining letters (most Arabic letters) */
    if (cp >= 0x0628 && cp <= 0x064A) return ARAB_DUAL_JOINING;
    
    /* Transparent marks */
    if (cp >= 0x064B && cp <= 0x0652) return ARAB_TRANSPARENT;
    
    return ARAB_NON_JOINING;
}

/* ============ Fallback Shaping ============ */

nx_shaping_result_t* nx_graphite_shape_fallback(
    nx_font_t *font,
    const char *text,
    size_t len,
    const nx_shaping_context_t *ctx
) {
    if (!font || !text || len == 0) return 0;
    
    /* Count codepoints first */
    const char *ptr = text;
    const char *end = text + len;
    size_t count = 0;
    while (ptr < end) {
        uint32_t cp = utf8_decode(&ptr, end);
        if (cp == 0) break;
        count++;
    }
    
    if (count == 0) return 0;
    
    /* Allocate result */
    nx_shaping_result_t *result = graphite_alloc(sizeof(nx_shaping_result_t));
    if (!result) return 0;
    
    result->glyphs = graphite_alloc(sizeof(nx_shaped_glyph_t) * count);
    if (!result->glyphs) {
        graphite_free(result);
        return 0;
    }
    
    /* Detect script and direction */
    nx_script_t script = nx_detect_script(text, len);
    nx_text_direction_t dir = ctx ? ctx->direction : nx_script_direction(script);
    
    result->script = script;
    result->dir = dir;
    result->count = 0;
    result->total_width = 0;
    result->total_height = 0;
    
    /* Shape based on script */
    ptr = text;
    size_t cluster = 0;
    
    while (ptr < end && result->count < count) {
        uint32_t cp = utf8_decode(&ptr, end);
        if (cp == 0) break;
        
        /* Get glyph ID from cmap */
        nx_glyph_info_t ginfo;
        if (nx_font_get_glyph_info(font, cp, 16, &ginfo) != 0) {
            /* Fallback to .notdef */
            ginfo.advance = 8;
        }
        
        nx_shaped_glyph_t *g = &result->glyphs[result->count];
        g->glyph_id = cp; /* Use codepoint as temp glyph ID */
        g->cluster = cluster;
        g->x_offset = 0;
        g->y_offset = 0;
        g->x_advance = (float)ginfo.advance;
        g->y_advance = 0;
        g->is_base = 1;
        g->is_mark = 0;
        
        /* Script-specific adjustments */
        if (script == NX_SCRIPT_DEVANAGARI) {
            deva_class_t cls = devanagari_class(cp);
            if (cls == DEVA_MATRA || cls == DEVA_VIRAMA || 
                cls == DEVA_ANUSVARA || cls == DEVA_VISARGA ||
                cls == DEVA_CHANDRABINDU) {
                g->is_base = 0;
                g->is_mark = 1;
                /* Position marks relative to previous base */
                if (result->count > 0) {
                    g->x_offset = -(float)result->glyphs[result->count - 1].x_advance / 2;
                    g->x_advance = 0; /* Marks don't advance */
                }
            }
        } else if (script == NX_SCRIPT_ARABIC) {
            arab_join_t jt = arabic_joining_type(cp);
            if (jt == ARAB_TRANSPARENT) {
                g->is_base = 0;
                g->is_mark = 1;
                g->x_advance = 0;
            }
            /* TODO: Determine positional form (init/medi/fina/isol) */
            /* and substitute appropriate glyph from font */
        }
        
        result->total_width += g->x_advance;
        result->count++;
        cluster++;
    }
    
    /* Handle RTL: reverse glyph order */
    if (dir == NX_DIR_RTL && result->count > 1) {
        for (size_t i = 0; i < result->count / 2; i++) {
            size_t j = result->count - 1 - i;
            nx_shaped_glyph_t temp = result->glyphs[i];
            result->glyphs[i] = result->glyphs[j];
            result->glyphs[j] = temp;
        }
    }
    
    return result;
}

nx_shaping_result_t* nx_graphite_shape(
    nx_font_t *font,
    const char *text,
    size_t len,
    const nx_shaping_context_t *ctx
) {
    if (!font || !text || len == 0) return 0;
    
    /* Check if font has Graphite tables */
    if (nx_graphite_font_supported(font)) {
        /* Full Graphite shaping would go here */
        /* For now, fall through to fallback */
    }
    
    /* Use fallback shaping */
    return nx_graphite_shape_fallback(font, text, len, ctx);
}

void nx_graphite_free_result(nx_shaping_result_t *result) {
    if (result) {
        if (result->glyphs) {
            graphite_free(result->glyphs);
        }
        graphite_free(result);
    }
}
