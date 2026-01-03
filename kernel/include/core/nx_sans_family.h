/*
 * NeolyxOS - NX Sans Font Family Configuration
 * 
 * This header defines the NX Sans font family and provides
 * identifiers for loading system fonts from the kernel font system.
 * 
 * The NX Sans family is based on Source Sans 3, organized as:
 *   - NX Sans Text     : Body text, documents, articles (Regular)
 *   - NX Sans UI       : Interface elements, labels, menus (Medium)
 *   - NX Sans Display  : Headlines, titles, large text (SemiBold)
 *   - NX Sans Bold     : Emphasis in body text (Bold)
 *   - NX Sans Italic   : Emphasis, quotes (Italic)
 *   - NX Sans Mono     : Code, terminal, technical content (CascadiaMono)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_NX_SANS_FAMILY_H
#define NEOLYX_NX_SANS_FAMILY_H

/* NX Sans font file paths (relative to /System/Library/Fonts) */
#define NX_SANS_TEXT_PATH       "/System/Library/Fonts/NXSans/NXSans-Text.ttf"
#define NX_SANS_UI_PATH         "/System/Library/Fonts/NXSans/NXSans-UI.ttf"
#define NX_SANS_DISPLAY_PATH    "/System/Library/Fonts/NXSans/NXSans-Display.ttf"
#define NX_SANS_BOLD_PATH       "/System/Library/Fonts/NXSans/NXSans-Bold.ttf"
#define NX_SANS_ITALIC_PATH     "/System/Library/Fonts/NXSans/NXSans-Italic.ttf"
#define NX_SANS_MONO_PATH       "/System/Library/Fonts/NXSansMono/NXSans-Mono.ttf"

/* Font family identifiers */
typedef enum {
    NX_FONT_TEXT = 0,        /* Body text, default for documents */
    NX_FONT_UI,              /* UI elements (labels, buttons, menus) */
    NX_FONT_DISPLAY,         /* Headlines, large titles */
    NX_FONT_BOLD,            /* Bold emphasis in body */
    NX_FONT_ITALIC,          /* Italic emphasis */
    NX_FONT_MONO,            /* Monospace: code, terminal */
    NX_FONT_COUNT
} nx_font_role_t;

/* Recommended font sizes for consistent UI */
typedef enum {
    NX_SIZE_CAPTION = 10,    /* Small labels, annotations */
    NX_SIZE_FOOTNOTE = 11,   /* Footnotes, secondary info */
    NX_SIZE_SMALL = 12,      /* Small body text */
    NX_SIZE_BODY = 14,       /* Default body text */
    NX_SIZE_CALLOUT = 15,    /* Highlighted text */
    NX_SIZE_SUBTITLE = 16,   /* Subtitles, section headers */
    NX_SIZE_TITLE = 20,      /* Titles */
    NX_SIZE_HEADLINE = 24,   /* Headlines */
    NX_SIZE_DISPLAY = 32,    /* Large display text */
    NX_SIZE_HERO = 48        /* Hero text, splash screens */
} nx_font_size_t;

/* Font weights for NX Sans */
typedef enum {
    NX_WEIGHT_LIGHT = 300,
    NX_WEIGHT_REGULAR = 400,
    NX_WEIGHT_MEDIUM = 500,
    NX_WEIGHT_SEMIBOLD = 600,
    NX_WEIGHT_BOLD = 700,
    NX_WEIGHT_BLACK = 900
} nx_font_weight_t;

/* Font usage context for automatic selection */
typedef struct {
    nx_font_role_t role;       /* Which font variant to use */
    nx_font_size_t size;       /* Pixel size */
    uint32_t color;            /* Text color (ARGB) */
} nx_font_style_t;

/* Predefined styles for common UI elements */
#define NX_STYLE_BODY           (nx_font_style_t){ NX_FONT_TEXT, NX_SIZE_BODY, 0xFF000000 }
#define NX_STYLE_TITLE          (nx_font_style_t){ NX_FONT_DISPLAY, NX_SIZE_TITLE, 0xFF000000 }
#define NX_STYLE_MENUBAR        (nx_font_style_t){ NX_FONT_UI, NX_SIZE_BODY, 0xFFFFFFFF }
#define NX_STYLE_BUTTON         (nx_font_style_t){ NX_FONT_UI, NX_SIZE_BODY, 0xFF000000 }
#define NX_STYLE_CODE           (nx_font_style_t){ NX_FONT_MONO, NX_SIZE_SMALL, 0xFF000000 }
#define NX_STYLE_TERMINAL       (nx_font_style_t){ NX_FONT_MONO, NX_SIZE_BODY, 0xFFE0E0E0 }
#define NX_STYLE_LABEL          (nx_font_style_t){ NX_FONT_UI, NX_SIZE_SMALL, 0xFF666666 }

/* Font family info for FontManager.app */
static const char* NX_SANS_FAMILY_NAME = "NX Sans";
static const char* NX_SANS_FAMILY_DESC = "The NeolyxOS system font family. Based on Source Sans 3.";
static const char* NX_SANS_COPYRIGHT = "Copyright (c) 2025 KetiveeAI. Font base: Source Sans 3 (Adobe).";

/* Font variant names */
static const char* nx_font_role_names[] = {
    "Text",
    "UI",
    "Display",
    "Bold",
    "Italic",
    "Mono"
};

/* Get font path for a role */
static inline const char* nx_font_get_path(nx_font_role_t role) {
    switch (role) {
        case NX_FONT_TEXT:    return NX_SANS_TEXT_PATH;
        case NX_FONT_UI:      return NX_SANS_UI_PATH;
        case NX_FONT_DISPLAY: return NX_SANS_DISPLAY_PATH;
        case NX_FONT_BOLD:    return NX_SANS_BOLD_PATH;
        case NX_FONT_ITALIC:  return NX_SANS_ITALIC_PATH;
        case NX_FONT_MONO:    return NX_SANS_MONO_PATH;
        default:              return NX_SANS_TEXT_PATH;
    }
}

#endif /* NEOLYX_NX_SANS_FAMILY_H */
