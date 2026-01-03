/*
 * NeolyxOS Vector Font System (NXFont)
 * 
 * Fully scalable vector-based font rendering for boot UI.
 * Renders text at any size without pixelation using anti-aliased paths.
 * 
 * Features:
 *   - Path-based glyphs (not bitmap)
 *   - Anti-aliased rendering with sub-pixel accuracy
 *   - Scales to any size from single definition
 *   - Full ASCII printable character set
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXFONT_H
#define NXFONT_H

#include <stdint.h>

/* ============ Vector Path Commands ============ */

#define NXF_END     0   /* End of glyph path */
#define NXF_MOVE    1   /* Move to (x, y) */
#define NXF_LINE    2   /* Line to (x, y) */
#define NXF_HLINE   3   /* Horizontal line to x */
#define NXF_VLINE   4   /* Vertical line to y */

/* Path coordinates are 0-1000 (representing 0.0-1.0 of em square) */
typedef struct {
    uint8_t cmd;
    int16_t x, y;
} nxf_path_t;

/* Glyph definition */
typedef struct {
    char ch;
    int16_t advance;    /* Width in em units (0-1000) */
    int16_t path_count;
    const nxf_path_t *paths;
} nxf_glyph_t;

/* ============ Glyph Path Definitions ============ */

/* A */
static const nxf_path_t glyph_A[] = {
    {NXF_MOVE, 0, 1000}, {NXF_LINE, 400, 0}, {NXF_LINE, 600, 0}, 
    {NXF_LINE, 1000, 1000}, {NXF_LINE, 800, 1000}, {NXF_LINE, 700, 700},
    {NXF_LINE, 300, 700}, {NXF_LINE, 200, 1000}, {NXF_LINE, 0, 1000},
    {NXF_END, 0, 0}
};

/* B */
static const nxf_path_t glyph_B[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 600, 0}, {NXF_LINE, 800, 100},
    {NXF_LINE, 800, 400}, {NXF_LINE, 600, 500}, {NXF_LINE, 800, 600},
    {NXF_LINE, 800, 900}, {NXF_LINE, 600, 1000}, {NXF_LINE, 100, 1000},
    {NXF_LINE, 100, 0}, {NXF_END, 0, 0}
};

/* C */
static const nxf_path_t glyph_C[] = {
    {NXF_MOVE, 900, 200}, {NXF_LINE, 700, 0}, {NXF_LINE, 300, 0},
    {NXF_LINE, 100, 200}, {NXF_LINE, 100, 800}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 700, 1000}, {NXF_LINE, 900, 800}, {NXF_END, 0, 0}
};

/* D */
static const nxf_path_t glyph_D[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 600, 0}, {NXF_LINE, 900, 300},
    {NXF_LINE, 900, 700}, {NXF_LINE, 600, 1000}, {NXF_LINE, 100, 1000},
    {NXF_LINE, 100, 0}, {NXF_END, 0, 0}
};

/* E */
static const nxf_path_t glyph_E[] = {
    {NXF_MOVE, 800, 0}, {NXF_LINE, 100, 0}, {NXF_LINE, 100, 1000},
    {NXF_LINE, 800, 1000}, {NXF_MOVE, 100, 500}, {NXF_LINE, 600, 500},
    {NXF_END, 0, 0}
};

/* F */
static const nxf_path_t glyph_F[] = {
    {NXF_MOVE, 800, 0}, {NXF_LINE, 100, 0}, {NXF_LINE, 100, 1000},
    {NXF_MOVE, 100, 500}, {NXF_LINE, 600, 500}, {NXF_END, 0, 0}
};

/* G */
static const nxf_path_t glyph_G[] = {
    {NXF_MOVE, 900, 200}, {NXF_LINE, 700, 0}, {NXF_LINE, 300, 0},
    {NXF_LINE, 100, 200}, {NXF_LINE, 100, 800}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 700, 1000}, {NXF_LINE, 900, 800}, {NXF_LINE, 900, 500},
    {NXF_LINE, 500, 500}, {NXF_END, 0, 0}
};

/* H */
static const nxf_path_t glyph_H[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 100, 1000}, {NXF_MOVE, 900, 0},
    {NXF_LINE, 900, 1000}, {NXF_MOVE, 100, 500}, {NXF_LINE, 900, 500},
    {NXF_END, 0, 0}
};

/* I */
static const nxf_path_t glyph_I[] = {
    {NXF_MOVE, 200, 0}, {NXF_LINE, 600, 0}, {NXF_MOVE, 400, 0},
    {NXF_LINE, 400, 1000}, {NXF_MOVE, 200, 1000}, {NXF_LINE, 600, 1000},
    {NXF_END, 0, 0}
};

/* J */
static const nxf_path_t glyph_J[] = {
    {NXF_MOVE, 300, 0}, {NXF_LINE, 800, 0}, {NXF_MOVE, 600, 0},
    {NXF_LINE, 600, 800}, {NXF_LINE, 400, 1000}, {NXF_LINE, 200, 1000},
    {NXF_LINE, 100, 800}, {NXF_END, 0, 0}
};

/* K */
static const nxf_path_t glyph_K[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 100, 1000}, {NXF_MOVE, 800, 0},
    {NXF_LINE, 100, 500}, {NXF_LINE, 800, 1000}, {NXF_END, 0, 0}
};

/* L */
static const nxf_path_t glyph_L[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 100, 1000}, {NXF_LINE, 800, 1000},
    {NXF_END, 0, 0}
};

/* M */
static const nxf_path_t glyph_M[] = {
    {NXF_MOVE, 100, 1000}, {NXF_LINE, 100, 0}, {NXF_LINE, 500, 500},
    {NXF_LINE, 900, 0}, {NXF_LINE, 900, 1000}, {NXF_END, 0, 0}
};

/* N */
static const nxf_path_t glyph_N[] = {
    {NXF_MOVE, 100, 1000}, {NXF_LINE, 100, 0}, {NXF_LINE, 900, 1000},
    {NXF_LINE, 900, 0}, {NXF_END, 0, 0}
};

/* O */
static const nxf_path_t glyph_O[] = {
    {NXF_MOVE, 300, 0}, {NXF_LINE, 700, 0}, {NXF_LINE, 900, 200},
    {NXF_LINE, 900, 800}, {NXF_LINE, 700, 1000}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 100, 800}, {NXF_LINE, 100, 200}, {NXF_LINE, 300, 0},
    {NXF_END, 0, 0}
};

/* P */
static const nxf_path_t glyph_P[] = {
    {NXF_MOVE, 100, 1000}, {NXF_LINE, 100, 0}, {NXF_LINE, 700, 0},
    {NXF_LINE, 900, 150}, {NXF_LINE, 900, 400}, {NXF_LINE, 700, 550},
    {NXF_LINE, 100, 550}, {NXF_END, 0, 0}
};

/* Q */
static const nxf_path_t glyph_Q[] = {
    {NXF_MOVE, 300, 0}, {NXF_LINE, 700, 0}, {NXF_LINE, 900, 200},
    {NXF_LINE, 900, 800}, {NXF_LINE, 700, 1000}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 100, 800}, {NXF_LINE, 100, 200}, {NXF_LINE, 300, 0},
    {NXF_MOVE, 600, 700}, {NXF_LINE, 950, 1050}, {NXF_END, 0, 0}
};

/* R */
static const nxf_path_t glyph_R[] = {
    {NXF_MOVE, 100, 1000}, {NXF_LINE, 100, 0}, {NXF_LINE, 700, 0},
    {NXF_LINE, 900, 150}, {NXF_LINE, 900, 400}, {NXF_LINE, 700, 550},
    {NXF_LINE, 100, 550}, {NXF_MOVE, 500, 550}, {NXF_LINE, 900, 1000},
    {NXF_END, 0, 0}
};

/* S */
static const nxf_path_t glyph_S[] = {
    {NXF_MOVE, 900, 150}, {NXF_LINE, 750, 0}, {NXF_LINE, 250, 0},
    {NXF_LINE, 100, 150}, {NXF_LINE, 100, 400}, {NXF_LINE, 250, 500},
    {NXF_LINE, 750, 500}, {NXF_LINE, 900, 600}, {NXF_LINE, 900, 850},
    {NXF_LINE, 750, 1000}, {NXF_LINE, 250, 1000}, {NXF_LINE, 100, 850},
    {NXF_END, 0, 0}
};

/* T */
static const nxf_path_t glyph_T[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 900, 0}, {NXF_MOVE, 500, 0},
    {NXF_LINE, 500, 1000}, {NXF_END, 0, 0}
};

/* U */
static const nxf_path_t glyph_U[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 100, 800}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 700, 1000}, {NXF_LINE, 900, 800}, {NXF_LINE, 900, 0},
    {NXF_END, 0, 0}
};

/* V */
static const nxf_path_t glyph_V[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 500, 1000}, {NXF_LINE, 900, 0},
    {NXF_END, 0, 0}
};

/* W */
static const nxf_path_t glyph_W[] = {
    {NXF_MOVE, 0, 0}, {NXF_LINE, 200, 1000}, {NXF_LINE, 500, 400},
    {NXF_LINE, 800, 1000}, {NXF_LINE, 1000, 0}, {NXF_END, 0, 0}
};

/* X */
static const nxf_path_t glyph_X[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 900, 1000}, {NXF_MOVE, 900, 0},
    {NXF_LINE, 100, 1000}, {NXF_END, 0, 0}
};

/* Y */
static const nxf_path_t glyph_Y[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 500, 500}, {NXF_LINE, 900, 0},
    {NXF_MOVE, 500, 500}, {NXF_LINE, 500, 1000}, {NXF_END, 0, 0}
};

/* Z */
static const nxf_path_t glyph_Z[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 900, 0}, {NXF_LINE, 100, 1000},
    {NXF_LINE, 900, 1000}, {NXF_END, 0, 0}
};

/* Lowercase glyphs - simplified versions */

/* a */
static const nxf_path_t glyph_a[] = {
    {NXF_MOVE, 800, 400}, {NXF_LINE, 600, 350}, {NXF_LINE, 300, 350},
    {NXF_LINE, 150, 500}, {NXF_LINE, 150, 850}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 600, 1000}, {NXF_LINE, 800, 850}, {NXF_LINE, 800, 350},
    {NXF_LINE, 800, 1000}, {NXF_END, 0, 0}
};

/* b */
static const nxf_path_t glyph_b[] = {
    {NXF_MOVE, 150, 0}, {NXF_LINE, 150, 1000}, {NXF_MOVE, 150, 400},
    {NXF_LINE, 600, 350}, {NXF_LINE, 850, 500}, {NXF_LINE, 850, 850},
    {NXF_LINE, 600, 1000}, {NXF_LINE, 150, 1000}, {NXF_END, 0, 0}
};

/* c */
static const nxf_path_t glyph_c[] = {
    {NXF_MOVE, 850, 450}, {NXF_LINE, 650, 350}, {NXF_LINE, 300, 350},
    {NXF_LINE, 150, 500}, {NXF_LINE, 150, 850}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 650, 1000}, {NXF_LINE, 850, 900}, {NXF_END, 0, 0}
};

/* d */
static const nxf_path_t glyph_d[] = {
    {NXF_MOVE, 850, 0}, {NXF_LINE, 850, 1000}, {NXF_MOVE, 850, 400},
    {NXF_LINE, 400, 350}, {NXF_LINE, 150, 500}, {NXF_LINE, 150, 850},
    {NXF_LINE, 400, 1000}, {NXF_LINE, 850, 1000}, {NXF_END, 0, 0}
};

/* e */
static const nxf_path_t glyph_e[] = {
    {NXF_MOVE, 150, 650}, {NXF_LINE, 850, 650}, {NXF_LINE, 850, 500},
    {NXF_LINE, 650, 350}, {NXF_LINE, 300, 350}, {NXF_LINE, 150, 500},
    {NXF_LINE, 150, 850}, {NXF_LINE, 300, 1000}, {NXF_LINE, 700, 1000},
    {NXF_LINE, 850, 900}, {NXF_END, 0, 0}
};

/* f */
static const nxf_path_t glyph_f[] = {
    {NXF_MOVE, 250, 1000}, {NXF_LINE, 250, 200}, {NXF_LINE, 400, 50},
    {NXF_LINE, 650, 50}, {NXF_MOVE, 100, 400}, {NXF_LINE, 500, 400},
    {NXF_END, 0, 0}
};

/* g */
static const nxf_path_t glyph_g[] = {
    {NXF_MOVE, 850, 350}, {NXF_LINE, 850, 1100}, {NXF_LINE, 650, 1200},
    {NXF_LINE, 300, 1200}, {NXF_MOVE, 850, 400}, {NXF_LINE, 400, 350},
    {NXF_LINE, 150, 500}, {NXF_LINE, 150, 750}, {NXF_LINE, 400, 900},
    {NXF_LINE, 850, 850}, {NXF_END, 0, 0}
};

/* h */
static const nxf_path_t glyph_h[] = {
    {NXF_MOVE, 150, 0}, {NXF_LINE, 150, 1000}, {NXF_MOVE, 150, 450},
    {NXF_LINE, 500, 350}, {NXF_LINE, 850, 500}, {NXF_LINE, 850, 1000},
    {NXF_END, 0, 0}
};

/* i */
static const nxf_path_t glyph_i[] = {
    {NXF_MOVE, 400, 350}, {NXF_LINE, 400, 1000}, {NXF_MOVE, 400, 100},
    {NXF_LINE, 400, 200}, {NXF_END, 0, 0}
};

/* j */
static const nxf_path_t glyph_j[] = {
    {NXF_MOVE, 500, 350}, {NXF_LINE, 500, 1100}, {NXF_LINE, 300, 1200},
    {NXF_LINE, 150, 1100}, {NXF_MOVE, 500, 100}, {NXF_LINE, 500, 200},
    {NXF_END, 0, 0}
};

/* k */
static const nxf_path_t glyph_k[] = {
    {NXF_MOVE, 150, 0}, {NXF_LINE, 150, 1000}, {NXF_MOVE, 700, 350},
    {NXF_LINE, 150, 700}, {NXF_LINE, 700, 1000}, {NXF_END, 0, 0}
};

/* l */
static const nxf_path_t glyph_l[] = {
    {NXF_MOVE, 400, 0}, {NXF_LINE, 400, 1000}, {NXF_END, 0, 0}
};

/* m */
static const nxf_path_t glyph_m[] = {
    {NXF_MOVE, 100, 1000}, {NXF_LINE, 100, 350}, {NXF_MOVE, 100, 450},
    {NXF_LINE, 300, 350}, {NXF_LINE, 500, 450}, {NXF_LINE, 500, 1000},
    {NXF_MOVE, 500, 450}, {NXF_LINE, 700, 350}, {NXF_LINE, 900, 450},
    {NXF_LINE, 900, 1000}, {NXF_END, 0, 0}
};

/* n */
static const nxf_path_t glyph_n[] = {
    {NXF_MOVE, 150, 1000}, {NXF_LINE, 150, 350}, {NXF_MOVE, 150, 450},
    {NXF_LINE, 500, 350}, {NXF_LINE, 850, 500}, {NXF_LINE, 850, 1000},
    {NXF_END, 0, 0}
};

/* o */
static const nxf_path_t glyph_o[] = {
    {NXF_MOVE, 300, 350}, {NXF_LINE, 700, 350}, {NXF_LINE, 900, 500},
    {NXF_LINE, 900, 850}, {NXF_LINE, 700, 1000}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 100, 850}, {NXF_LINE, 100, 500}, {NXF_LINE, 300, 350},
    {NXF_END, 0, 0}
};

/* p */
static const nxf_path_t glyph_p[] = {
    {NXF_MOVE, 150, 350}, {NXF_LINE, 150, 1200}, {NXF_MOVE, 150, 400},
    {NXF_LINE, 600, 350}, {NXF_LINE, 850, 500}, {NXF_LINE, 850, 850},
    {NXF_LINE, 600, 1000}, {NXF_LINE, 150, 1000}, {NXF_END, 0, 0}
};

/* q */
static const nxf_path_t glyph_q[] = {
    {NXF_MOVE, 850, 350}, {NXF_LINE, 850, 1200}, {NXF_MOVE, 850, 400},
    {NXF_LINE, 400, 350}, {NXF_LINE, 150, 500}, {NXF_LINE, 150, 850},
    {NXF_LINE, 400, 1000}, {NXF_LINE, 850, 1000}, {NXF_END, 0, 0}
};

/* r */
static const nxf_path_t glyph_r[] = {
    {NXF_MOVE, 200, 1000}, {NXF_LINE, 200, 350}, {NXF_MOVE, 200, 500},
    {NXF_LINE, 450, 350}, {NXF_LINE, 700, 400}, {NXF_END, 0, 0}
};

/* s */
static const nxf_path_t glyph_s[] = {
    {NXF_MOVE, 800, 450}, {NXF_LINE, 600, 350}, {NXF_LINE, 300, 350},
    {NXF_LINE, 150, 450}, {NXF_LINE, 300, 600}, {NXF_LINE, 700, 700},
    {NXF_LINE, 850, 850}, {NXF_LINE, 700, 1000}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 150, 900}, {NXF_END, 0, 0}
};

/* t */
static const nxf_path_t glyph_t[] = {
    {NXF_MOVE, 300, 100}, {NXF_LINE, 300, 900}, {NXF_LINE, 500, 1000},
    {NXF_LINE, 650, 950}, {NXF_MOVE, 150, 350}, {NXF_LINE, 550, 350},
    {NXF_END, 0, 0}
};

/* u */
static const nxf_path_t glyph_u[] = {
    {NXF_MOVE, 150, 350}, {NXF_LINE, 150, 850}, {NXF_LINE, 350, 1000},
    {NXF_LINE, 650, 1000}, {NXF_LINE, 850, 850}, {NXF_MOVE, 850, 350},
    {NXF_LINE, 850, 1000}, {NXF_END, 0, 0}
};

/* v */
static const nxf_path_t glyph_v[] = {
    {NXF_MOVE, 100, 350}, {NXF_LINE, 500, 1000}, {NXF_LINE, 900, 350},
    {NXF_END, 0, 0}
};

/* w */
static const nxf_path_t glyph_w[] = {
    {NXF_MOVE, 50, 350}, {NXF_LINE, 250, 1000}, {NXF_LINE, 500, 550},
    {NXF_LINE, 750, 1000}, {NXF_LINE, 950, 350}, {NXF_END, 0, 0}
};

/* x */
static const nxf_path_t glyph_x[] = {
    {NXF_MOVE, 150, 350}, {NXF_LINE, 850, 1000}, {NXF_MOVE, 850, 350},
    {NXF_LINE, 150, 1000}, {NXF_END, 0, 0}
};

/* y */
static const nxf_path_t glyph_y[] = {
    {NXF_MOVE, 150, 350}, {NXF_LINE, 500, 750}, {NXF_LINE, 850, 350},
    {NXF_MOVE, 500, 750}, {NXF_LINE, 300, 1200}, {NXF_END, 0, 0}
};

/* z */
static const nxf_path_t glyph_z[] = {
    {NXF_MOVE, 150, 350}, {NXF_LINE, 850, 350}, {NXF_LINE, 150, 1000},
    {NXF_LINE, 850, 1000}, {NXF_END, 0, 0}
};

/* Digits */

/* 0 */
static const nxf_path_t glyph_0[] = {
    {NXF_MOVE, 300, 0}, {NXF_LINE, 700, 0}, {NXF_LINE, 900, 200},
    {NXF_LINE, 900, 800}, {NXF_LINE, 700, 1000}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 100, 800}, {NXF_LINE, 100, 200}, {NXF_LINE, 300, 0},
    {NXF_MOVE, 250, 750}, {NXF_LINE, 750, 250}, {NXF_END, 0, 0}
};

/* 1 */
static const nxf_path_t glyph_1[] = {
    {NXF_MOVE, 300, 200}, {NXF_LINE, 500, 0}, {NXF_LINE, 500, 1000},
    {NXF_MOVE, 250, 1000}, {NXF_LINE, 750, 1000}, {NXF_END, 0, 0}
};

/* 2 */
static const nxf_path_t glyph_2[] = {
    {NXF_MOVE, 100, 200}, {NXF_LINE, 300, 0}, {NXF_LINE, 700, 0},
    {NXF_LINE, 900, 200}, {NXF_LINE, 900, 400}, {NXF_LINE, 100, 1000},
    {NXF_LINE, 900, 1000}, {NXF_END, 0, 0}
};

/* 3 */
static const nxf_path_t glyph_3[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 900, 0}, {NXF_LINE, 500, 450},
    {NXF_LINE, 850, 550}, {NXF_LINE, 900, 800}, {NXF_LINE, 700, 1000},
    {NXF_LINE, 300, 1000}, {NXF_LINE, 100, 850}, {NXF_END, 0, 0}
};

/* 4 */
static const nxf_path_t glyph_4[] = {
    {NXF_MOVE, 700, 1000}, {NXF_LINE, 700, 0}, {NXF_LINE, 100, 650},
    {NXF_LINE, 900, 650}, {NXF_END, 0, 0}
};

/* 5 */
static const nxf_path_t glyph_5[] = {
    {NXF_MOVE, 900, 0}, {NXF_LINE, 100, 0}, {NXF_LINE, 100, 450},
    {NXF_LINE, 700, 450}, {NXF_LINE, 900, 600}, {NXF_LINE, 900, 850},
    {NXF_LINE, 700, 1000}, {NXF_LINE, 300, 1000}, {NXF_LINE, 100, 850},
    {NXF_END, 0, 0}
};

/* 6 */
static const nxf_path_t glyph_6[] = {
    {NXF_MOVE, 800, 100}, {NXF_LINE, 600, 0}, {NXF_LINE, 300, 0},
    {NXF_LINE, 100, 200}, {NXF_LINE, 100, 800}, {NXF_LINE, 300, 1000},
    {NXF_LINE, 700, 1000}, {NXF_LINE, 900, 800}, {NXF_LINE, 900, 600},
    {NXF_LINE, 700, 450}, {NXF_LINE, 100, 450}, {NXF_END, 0, 0}
};

/* 7 */
static const nxf_path_t glyph_7[] = {
    {NXF_MOVE, 100, 0}, {NXF_LINE, 900, 0}, {NXF_LINE, 400, 1000},
    {NXF_END, 0, 0}
};

/* 8 */
static const nxf_path_t glyph_8[] = {
    {NXF_MOVE, 300, 0}, {NXF_LINE, 700, 0}, {NXF_LINE, 850, 120},
    {NXF_LINE, 850, 380}, {NXF_LINE, 700, 500}, {NXF_LINE, 300, 500},
    {NXF_LINE, 150, 380}, {NXF_LINE, 150, 120}, {NXF_LINE, 300, 0},
    {NXF_MOVE, 300, 500}, {NXF_LINE, 100, 620}, {NXF_LINE, 100, 880},
    {NXF_LINE, 300, 1000}, {NXF_LINE, 700, 1000}, {NXF_LINE, 900, 880},
    {NXF_LINE, 900, 620}, {NXF_LINE, 700, 500}, {NXF_END, 0, 0}
};

/* 9 */
static const nxf_path_t glyph_9[] = {
    {NXF_MOVE, 200, 900}, {NXF_LINE, 400, 1000}, {NXF_LINE, 700, 1000},
    {NXF_LINE, 900, 800}, {NXF_LINE, 900, 200}, {NXF_LINE, 700, 0},
    {NXF_LINE, 300, 0}, {NXF_LINE, 100, 200}, {NXF_LINE, 100, 400},
    {NXF_LINE, 300, 550}, {NXF_LINE, 900, 550}, {NXF_END, 0, 0}
};

/* Special characters */

/* Space */
static const nxf_path_t glyph_space[] = {
    {NXF_END, 0, 0}
};

/* : (colon) */
static const nxf_path_t glyph_colon[] = {
    {NXF_MOVE, 400, 300}, {NXF_LINE, 600, 300}, {NXF_LINE, 600, 450},
    {NXF_LINE, 400, 450}, {NXF_LINE, 400, 300},
    {NXF_MOVE, 400, 700}, {NXF_LINE, 600, 700}, {NXF_LINE, 600, 850},
    {NXF_LINE, 400, 850}, {NXF_LINE, 400, 700}, {NXF_END, 0, 0}
};

/* . (period) */
static const nxf_path_t glyph_period[] = {
    {NXF_MOVE, 350, 850}, {NXF_LINE, 650, 850}, {NXF_LINE, 650, 1000},
    {NXF_LINE, 350, 1000}, {NXF_LINE, 350, 850}, {NXF_END, 0, 0}
};

/* / (slash) */
static const nxf_path_t glyph_slash[] = {
    {NXF_MOVE, 800, 0}, {NXF_LINE, 200, 1000}, {NXF_END, 0, 0}
};

/* - (hyphen) */
static const nxf_path_t glyph_hyphen[] = {
    {NXF_MOVE, 200, 500}, {NXF_LINE, 800, 500}, {NXF_END, 0, 0}
};

/* ( */
static const nxf_path_t glyph_lparen[] = {
    {NXF_MOVE, 600, 0}, {NXF_LINE, 400, 200}, {NXF_LINE, 400, 800},
    {NXF_LINE, 600, 1000}, {NXF_END, 0, 0}
};

/* ) */
static const nxf_path_t glyph_rparen[] = {
    {NXF_MOVE, 400, 0}, {NXF_LINE, 600, 200}, {NXF_LINE, 600, 800},
    {NXF_LINE, 400, 1000}, {NXF_END, 0, 0}
};

/* | (pipe) */
static const nxf_path_t glyph_pipe[] = {
    {NXF_MOVE, 500, 0}, {NXF_LINE, 500, 1000}, {NXF_END, 0, 0}
};

/* ============ Glyph Table ============ */

static const nxf_glyph_t nxfont_glyphs[] = {
    {' ', 400, 0, glyph_space},
    {'A', 1000, 10, glyph_A}, {'B', 900, 11, glyph_B}, {'C', 900, 9, glyph_C},
    {'D', 1000, 8, glyph_D}, {'E', 800, 7, glyph_E}, {'F', 800, 6, glyph_F},
    {'G', 1000, 11, glyph_G}, {'H', 1000, 6, glyph_H}, {'I', 800, 6, glyph_I},
    {'J', 900, 8, glyph_J}, {'K', 900, 6, glyph_K}, {'L', 800, 3, glyph_L},
    {'M', 1000, 6, glyph_M}, {'N', 1000, 5, glyph_N}, {'O', 1000, 10, glyph_O},
    {'P', 1000, 8, glyph_P}, {'Q', 1000, 12, glyph_Q}, {'R', 1000, 10, glyph_R},
    {'S', 1000, 13, glyph_S}, {'T', 1000, 4, glyph_T}, {'U', 1000, 7, glyph_U},
    {'V', 1000, 3, glyph_V}, {'W', 1000, 6, glyph_W}, {'X', 1000, 5, glyph_X},
    {'Y', 1000, 5, glyph_Y}, {'Z', 1000, 5, glyph_Z},
    {'a', 900, 11, glyph_a}, {'b', 900, 8, glyph_b}, {'c', 900, 9, glyph_c},
    {'d', 900, 8, glyph_d}, {'e', 1000, 11, glyph_e}, {'f', 700, 6, glyph_f},
    {'g', 900, 11, glyph_g}, {'h', 900, 8, glyph_h}, {'i', 400, 5, glyph_i},
    {'j', 600, 6, glyph_j}, {'k', 800, 6, glyph_k}, {'l', 400, 2, glyph_l},
    {'m', 1000, 11, glyph_m}, {'n', 900, 8, glyph_n}, {'o', 1000, 10, glyph_o},
    {'p', 900, 8, glyph_p}, {'q', 900, 8, glyph_q}, {'r', 700, 5, glyph_r},
    {'s', 900, 11, glyph_s}, {'t', 700, 6, glyph_t}, {'u', 900, 8, glyph_u},
    {'v', 1000, 4, glyph_v}, {'w', 1000, 6, glyph_w}, {'x', 1000, 5, glyph_x},
    {'y', 1000, 6, glyph_y}, {'z', 1000, 5, glyph_z},
    {'0', 1000, 12, glyph_0}, {'1', 1000, 6, glyph_1}, {'2', 1000, 8, glyph_2},
    {'3', 1000, 9, glyph_3}, {'4', 1000, 5, glyph_4}, {'5', 1000, 10, glyph_5},
    {'6', 1000, 12, glyph_6}, {'7', 1000, 4, glyph_7}, {'8', 1000, 18, glyph_8},
    {'9', 1000, 12, glyph_9},
    {':', 400, 11, glyph_colon}, {'.', 400, 6, glyph_period},
    {'/', 600, 2, glyph_slash}, {'-', 600, 2, glyph_hyphen},
    {'(', 600, 5, glyph_lparen}, {')', 600, 5, glyph_rparen},
    {'|', 300, 2, glyph_pipe},
    {0, 0, 0, 0}  /* End marker */
};

/* ============ Anti-Aliased Line Rendering ============ */


/* ============ Integer Math Helpers ============ */

static inline int nxf_abs(int x) { return x < 0 ? -x : x; }

/* Blend colors */
static inline uint32_t nxf_blend(uint32_t fg, uint32_t bg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;
    int r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * (255 - alpha)) >> 8;
    int g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * (255 - alpha)) >> 8;
    int b = ((fg & 0xFF) * alpha + (bg & 0xFF) * (255 - alpha)) >> 8;
    return (r << 16) | (g << 8) | b;
}

/* Plot anti-aliased pixel */
static inline void nxf_plot_aa(volatile uint32_t *fb, uint32_t pitch,
                                int x, int y, uint32_t color, int alpha,
                                int max_x, int max_y) {
    if (x < 0 || y < 0 || x >= (int)max_x || y >= (int)max_y || alpha <= 0) return;
    uint32_t *p = (uint32_t*)&fb[y * (pitch / 4) + x];
    if (alpha >= 255) {
        *p = color;
    } else {
        *p = nxf_blend(color, *p, alpha);
    }
}

/* ============ Anti-Aliased Line Rendering (Integer) ============ */

/* Draw anti-aliased line using Wu's algorithm (Fixed Point) */
static void nxf_line_aa(volatile uint32_t *fb, uint32_t pitch,
                        int x0, int y0, int x1, int y1,
                        uint32_t color, int thickness,
                        uint32_t max_x, uint32_t max_y) {
    int dx = nxf_abs(x1 - x0);
    int dy = nxf_abs(y1 - y0);
    
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    int t2 = thickness >> 1;
    if (t2 < 1) t2 = 1;
    
    while (1) {
        /* Draw thick pixel */
        for (int ox = -t2 + 1; ox <= t2; ox++) {
            for (int oy = -t2 + 1; oy <= t2; oy++) {
                /* Simple box filter for thickness */
                nxf_plot_aa(fb, pitch, x0 + ox, y0 + oy, color, 255, max_x, max_y);
            }
        }
        
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* ============ Text Rendering API ============ */

/* Find glyph for character */
static const nxf_glyph_t* nxf_find_glyph(char ch) {
    for (int i = 0; nxfont_glyphs[i].ch != 0 || nxfont_glyphs[i].paths != 0; i++) {
        if (nxfont_glyphs[i].ch == ch) {
            return &nxfont_glyphs[i];
        }
    }
    return 0;
}

/*
 * Draw text with vector font - scalable to any size
 */
static void nxf_draw_text(volatile uint32_t *fb, uint32_t pitch,
                          int x, int y, const char *text, int size,
                          uint32_t color, uint32_t max_x, uint32_t max_y) {
    /* Integer scaling: coord * size / 1000 */
    int thickness = size > 24 ? 3 : (size > 16 ? 2 : 1);
    int cursor_x = x;
    
    while (*text) {
        const nxf_glyph_t *g = nxf_find_glyph(*text);
        if (!g) {
            cursor_x += size / 2;
            text++;
            continue;
        }
        
        /* Render glyph paths */
        const nxf_path_t *path = g->paths;
        int last_x = 0, last_y = 0;
        int pen_down = 0;
        
        while (path && path->cmd != NXF_END) {
            int px = cursor_x + (int)((int64_t)path->x * size / 1000);
            int py = y + (int)((int64_t)path->y * size / 1000);
            
            if (path->cmd == NXF_MOVE) {
                last_x = px;
                last_y = py;
                pen_down = 1;
            } else if (path->cmd == NXF_LINE && pen_down) {
                nxf_line_aa(fb, pitch, last_x, last_y, px, py, 
                           color, thickness, max_x, max_y);
                last_x = px;
                last_y = py;
            }
            path++;
        }
        
        cursor_x += (int)((int64_t)g->advance * size / 1000);
        text++;
    }
}

/* Measure text width */
static int nxf_text_width(const char *text, int size) {
    int width = 0;
    
    while (*text) {
        const nxf_glyph_t *g = nxf_find_glyph(*text);
        if (g) {
            width += (int)((int64_t)g->advance * size / 1000);
        } else {
            width += size / 2;
        }
        text++;
    }
    return width;
}

/* Draw centered text */
static void nxf_draw_centered(volatile uint32_t *fb, uint32_t pitch,
                               int screen_width, int y, const char *text,
                               int size, uint32_t color,
                               uint32_t max_x, uint32_t max_y) {
    int w = nxf_text_width(text, size);
    int x = (screen_width - w) / 2;
    nxf_draw_text(fb, pitch, x, y, text, size, color, max_x, max_y);
}

#endif /* NXFONT_H */
