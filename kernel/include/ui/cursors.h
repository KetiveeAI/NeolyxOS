/*
 * NeolyxOS Cursor Bitmaps
 * 
 * Kernel-compatible cursor icons converted from NeolyxOS SVG assets.
 * 
 * Available cursors:
 *   - cursor_arrow: Default arrow cursor (12x16)
 *   - cursor_hand: Hand/pointer cursor (16x24)
 *   - cursor_text: Text/I-beam cursor (8x16)
 *   - cursor_crosshair: Crosshair cursor (16x16)
 *   - cursor_resize_ns: North-South resize (12x16)
 *   - cursor_resize_ew: East-West resize (16x12)
 *   - cursor_move: Move/drag cursor (16x16)
 *   - cursor_wait: Loading/wait cursor (16x16)
 * 
 * Usage:
 *   cursor_set(CURSOR_HAND);
 *   cursor_draw(mouse_x, mouse_y);
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_CURSORS_H
#define NEOLYX_CURSORS_H

#include <stdint.h>

/* ============ Cursor Types ============ */

typedef enum {
    CURSOR_ARROW = 0,
    CURSOR_HAND,
    CURSOR_TEXT,
    CURSOR_CROSSHAIR,
    CURSOR_RESIZE_NS,
    CURSOR_RESIZE_EW,
    CURSOR_MOVE,
    CURSOR_WAIT,
    CURSOR_COUNT
} cursor_type_t;

/* ============ Cursor Structure ============ */

typedef struct {
    uint16_t width;
    uint16_t height;
    int8_t   hotspot_x;     /* Click position offset from top-left */
    int8_t   hotspot_y;
    const uint32_t *bitmap; /* 1-bit mask packed into uint32_t rows */
} cursor_def_t;

/* ============ Arrow Cursor (12x16) ============ */
/* Default pointer - diagonal arrow pointing top-left */

static const uint32_t cursor_arrow_data[16] = {
    0x80000000, /* 1000 0000 0000 */
    0xC0000000, /* 1100 0000 0000 */
    0xE0000000, /* 1110 0000 0000 */
    0xF0000000, /* 1111 0000 0000 */
    0xF8000000, /* 1111 1000 0000 */
    0xFC000000, /* 1111 1100 0000 */
    0xFE000000, /* 1111 1110 0000 */
    0xFF000000, /* 1111 1111 0000 */
    0xF8000000, /* 1111 1000 0000 */
    0xD8000000, /* 1101 1000 0000 */
    0x8C000000, /* 1000 1100 0000 */
    0x0C000000, /* 0000 1100 0000 */
    0x06000000, /* 0000 0110 0000 */
    0x06000000, /* 0000 0110 0000 */
    0x03000000, /* 0000 0011 0000 */
    0x00000000
};

static const cursor_def_t cursor_arrow = {
    .width = 12,
    .height = 16,
    .hotspot_x = 0,
    .hotspot_y = 0,
    .bitmap = cursor_arrow_data
};

/* ============ Hand Cursor (16x24) ============ */
/* Pointer hand with index finger - for clickable elements */
/* Converted from hand.svg */

static const uint32_t cursor_hand_data[24] = {
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01B00000, /*       11 11    */
    0x01B60000, /*       11 11 11 */
    0x01B60000, /*       11 11 11 */
    0x0DB60000, /*    11 11 11 11 */
    0x0DB60000, /*    11 11 11 11 */
    0x0FF60000, /*    1111 11 11  */
    0x0FFE0000, /*    111111111   */
    0x0FFE0000, /*    111111111   */
    0x07FE0000, /*     11111111   */
    0x07FE0000, /*     11111111   */
    0x03FC0000, /*      111111    */
    0x03FC0000, /*      111111    */
    0x03FC0000, /*      111111    */
    0x03FC0000, /*      111111    */
    0x01F80000, /*       11111    */
    0x01F80000, /*       11111    */
    0x01F80000, /*       11111    */
    0x00F00000, /*        1111    */
    0x00000000
};

static const cursor_def_t cursor_hand = {
    .width = 16,
    .height = 24,
    .hotspot_x = 6,  /* Index finger tip */
    .hotspot_y = 0,
    .bitmap = cursor_hand_data
};

/* ============ Text/I-Beam Cursor (8x16) ============ */
/* For text input fields */

static const uint32_t cursor_text_data[16] = {
    0xE7000000, /* 111  111 */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0x18000000, /*    11    */
    0xE7000000  /* 111  111 */
};

static const cursor_def_t cursor_text = {
    .width = 8,
    .height = 16,
    .hotspot_x = 4,  /* Center */
    .hotspot_y = 8,
    .bitmap = cursor_text_data
};

/* ============ Crosshair Cursor (16x16) ============ */
/* For precision selection */

static const uint32_t cursor_crosshair_data[16] = {
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x00000000, /*                */
    0xFE7F0000, /* 11111  1111111 */
    0xFE7F0000, /* 11111  1111111 */
    0x00000000, /*                */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0x01800000  /*       11       */
};

static const cursor_def_t cursor_crosshair = {
    .width = 16,
    .height = 16,
    .hotspot_x = 8,  /* Center */
    .hotspot_y = 8,
    .bitmap = cursor_crosshair_data
};

/* ============ Move Cursor (16x16) ============ */
/* Four-directional arrows for moving/dragging */

static const uint32_t cursor_move_data[16] = {
    0x01800000, /*       11       */
    0x03C00000, /*      1111      */
    0x07E00000, /*     111111     */
    0x01800000, /*       11       */
    0x41820000, /*  1     11     1*/
    0x61860000, /*  11    11    11*/
    0xF18F0000, /* 1111   11  1111*/
    0x01800000, /*       11       */
    0x01800000, /*       11       */
    0xF18F0000, /* 1111   11  1111*/
    0x61860000, /*  11    11    11*/
    0x41820000, /*  1     11     1*/
    0x01800000, /*       11       */
    0x07E00000, /*     111111     */
    0x03C00000, /*      1111      */
    0x01800000  /*       11       */
};

static const cursor_def_t cursor_move = {
    .width = 16,
    .height = 16,
    .hotspot_x = 8,
    .hotspot_y = 8,
    .bitmap = cursor_move_data
};

/* ============ Resize N-S Cursor (12x16) ============ */
/* Vertical resize arrows */

static const uint32_t cursor_resize_ns_data[16] = {
    0x06000000, /*     11     */
    0x0F000000, /*    1111    */
    0x1F800000, /*   111111   */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x06000000, /*     11     */
    0x1F800000, /*   111111   */
    0x0F000000, /*    1111    */
    0x06000000  /*     11     */
};

static const cursor_def_t cursor_resize_ns = {
    .width = 12,
    .height = 16,
    .hotspot_x = 6,
    .hotspot_y = 8,
    .bitmap = cursor_resize_ns_data
};

/* ============ Resize E-W Cursor (16x12) ============ */
/* Horizontal resize arrows */

static const uint32_t cursor_resize_ew_data[12] = {
    0x00000000,
    0x00000000,
    0x10080000, /*    1       1    */
    0x30180000, /*   11      11    */
    0x70380000, /*  111     111    */
    0xFFFF0000, /* 1111111111111111*/
    0xFFFF0000, /* 1111111111111111*/
    0x70380000, /*  111     111    */
    0x30180000, /*   11      11    */
    0x10080000, /*    1       1    */
    0x00000000,
    0x00000000
};

static const cursor_def_t cursor_resize_ew = {
    .width = 16,
    .height = 12,
    .hotspot_x = 8,
    .hotspot_y = 6,
    .bitmap = cursor_resize_ew_data
};

/* ============ Wait/Loading Cursor (16x16) ============ */
/* Hourglass/spinner */

static const uint32_t cursor_wait_data[16] = {
    0xFFFF0000, /* 1111111111111111 */
    0x80010000, /* 1              1 */
    0x40020000, /*  1            1  */
    0x20040000, /*   1          1   */
    0x10080000, /*    1        1    */
    0x08100000, /*     1      1     */
    0x04200000, /*      1    1      */
    0x02400000, /*       1  1       */
    0x02400000, /*       1  1       */
    0x04200000, /*      1    1      */
    0x08100000, /*     1      1     */
    0x10080000, /*    1        1    */
    0x20040000, /*   1          1   */
    0x40020000, /*  1            1  */
    0x80010000, /* 1              1 */
    0xFFFF0000  /* 1111111111111111 */
};

static const cursor_def_t cursor_wait = {
    .width = 16,
    .height = 16,
    .hotspot_x = 8,
    .hotspot_y = 8,
    .bitmap = cursor_wait_data
};

/* ============ Cursor Table ============ */

static const cursor_def_t *cursors[CURSOR_COUNT] = {
    &cursor_arrow,
    &cursor_hand,
    &cursor_text,
    &cursor_crosshair,
    &cursor_resize_ns,
    &cursor_resize_ew,
    &cursor_move,
    &cursor_wait
};

/* ============ Cursor API ============ */

/* Current cursor state */
static cursor_type_t current_cursor = CURSOR_ARROW;

/**
 * cursor_set - Set the current cursor type
 */
static inline void cursor_set(cursor_type_t type) {
    if (type < CURSOR_COUNT) {
        current_cursor = type;
    }
}

/**
 * cursor_get - Get current cursor definition
 */
static inline const cursor_def_t* cursor_get(void) {
    return cursors[current_cursor];
}

/**
 * cursor_get_hotspot - Get click position offset
 */
static inline void cursor_get_hotspot(int8_t *hx, int8_t *hy) {
    const cursor_def_t *c = cursors[current_cursor];
    *hx = c->hotspot_x;
    *hy = c->hotspot_y;
}

#endif /* NEOLYX_CURSORS_H */
