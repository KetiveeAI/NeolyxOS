/*
 * NeolyxOS Font Service API
 * 
 * Public API for apps to access fonts (read-only).
 * Only FontManager.app can install/remove fonts.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: LicenseRef-KetiveeAI-Proprietary
 */

#ifndef FONT_SERVICE_H
#define FONT_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Font Info ============ */

typedef struct {
    char family[64];        /* Font family name (e.g., "Inter") */
    char style[32];         /* Style (e.g., "Regular", "Bold") */
    char path[256];         /* Full path to font file */
    uint32_t font_id;       /* Unique font ID */
    bool is_system;         /* System font (read-only) */
    bool is_user;           /* User-installed font */
    uint16_t weight;        /* Font weight (100-900) */
    bool is_italic;         /* Italic variant */
    bool is_monospace;      /* Monospace font */
} nx_font_info_t;

/* ============ Font Query (Read-Only for Apps) ============ */

/* Get list of all available fonts */
int nx_fonts_list(nx_font_info_t *fonts, int max_count);

/* Get font by family name */
int nx_fonts_find_family(const char *family, nx_font_info_t *fonts, int max_count);

/* Get font by ID */
int nx_fonts_get_by_id(uint32_t font_id, nx_font_info_t *info);

/* Get specific style from family */
int nx_fonts_get_style(const char *family, const char *style, nx_font_info_t *info);

/* Check if font family exists */
bool nx_fonts_has_family(const char *family);

/* Get system default fonts */
int nx_fonts_get_default_serif(nx_font_info_t *info);
int nx_fonts_get_default_sans(nx_font_info_t *info);
int nx_fonts_get_default_mono(nx_font_info_t *info);
int nx_fonts_get_default_ui(nx_font_info_t *info);

/* ============ Font Install/Remove (FontManager.app ONLY) ============ */

/* These functions are restricted to FontManager.app */
/* Other apps will receive permission denied errors */

/* Install font from file path */
int nx_fonts_install(const char *font_path, bool as_system);

/* Remove user-installed font */
int nx_fonts_remove(uint32_t font_id);

/* Import font from external source (downloads, USB, etc.) */
int nx_fonts_import(const char *source_path);

/* Validate font file before install */
int nx_fonts_validate(const char *font_path);

/* ============ Font Rendering (For Text Engines) ============ */

/* Load font for rendering */
void* nx_font_load(uint32_t font_id, int size);

/* Unload font */
void nx_font_unload(void *font);

/* Measure text width */
int nx_font_measure_text(void *font, const char *text);

/* Get font metrics */
typedef struct {
    int ascent;
    int descent;
    int line_height;
    int x_height;
    int cap_height;
} nx_font_metrics_t;

int nx_font_get_metrics(void *font, nx_font_metrics_t *metrics);

/* ============ Error Codes ============ */

#define NX_FONT_OK              0
#define NX_FONT_NOT_FOUND      -1
#define NX_FONT_INVALID        -2
#define NX_FONT_ACCESS_DENIED  -3
#define NX_FONT_ALREADY_EXISTS -4
#define NX_FONT_CORRUPTED      -5
#define NX_FONT_NO_MEMORY      -6

#endif /* FONT_SERVICE_H */
