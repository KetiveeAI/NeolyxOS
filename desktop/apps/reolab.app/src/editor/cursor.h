/*
 * Reolab - Editor Cursor
 * NeolyxOS Application
 * 
 * Text cursor with configurable styles and smooth blinking.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_EDITOR_CURSOR_H
#define REOLAB_EDITOR_CURSOR_H

#include "../../include/reolab.h"
#include <stdint.h>
#include <stdbool.h>

/* Cursor styles */
typedef enum {
    CURSOR_BAR,         /* | (default) */
    CURSOR_UNDERSCORE,  /* _ */
    CURSOR_BLOCK        /* █ */
} CursorStyle;

/* Cursor state */
typedef struct {
    /* Position */
    size_t line;
    size_t column;
    
    /* Selection (optional) */
    bool has_selection;
    size_t sel_start_line;
    size_t sel_start_col;
    size_t sel_end_line;
    size_t sel_end_col;
    
    /* Appearance */
    CursorStyle style;
    uint8_t color_r, color_g, color_b;
    
    /* Blinking */
    bool visible;
    uint32_t blink_rate_ms;     /* 530-600ms recommended */
    uint32_t last_blink_time;
    uint32_t last_input_time;   /* Pause blink while typing */
    uint32_t input_pause_ms;    /* How long to pause after typing */
    
    /* Rendering */
    float width;               /* 2px for bar, full char for block */
    float height;              /* Text height */
    float corner_radius;       /* Slight rounding for modern look */
} EditorCursor;

/* Initialize cursor with defaults */
void cursor_init(EditorCursor* cursor);

/* Update cursor (call every frame) */
void cursor_update(EditorCursor* cursor, uint32_t current_time_ms);

/* Notify cursor of input (pauses blinking) */
void cursor_on_input(EditorCursor* cursor, uint32_t current_time_ms);

/* Movement */
void cursor_move_left(EditorCursor* cursor, ReoLabBuffer* buffer);
void cursor_move_right(EditorCursor* cursor, ReoLabBuffer* buffer);
void cursor_move_up(EditorCursor* cursor, ReoLabBuffer* buffer);
void cursor_move_down(EditorCursor* cursor, ReoLabBuffer* buffer);
void cursor_move_to(EditorCursor* cursor, size_t line, size_t col);
void cursor_move_home(EditorCursor* cursor);
void cursor_move_end(EditorCursor* cursor, ReoLabBuffer* buffer);

/* Selection */
void cursor_start_selection(EditorCursor* cursor);
void cursor_clear_selection(EditorCursor* cursor);

/* Get pixel position for rendering */
void cursor_get_pixel_pos(EditorCursor* cursor, float char_width, float line_height,
                          float gutter_width, float* out_x, float* out_y);

/* Style configuration */
void cursor_set_style(EditorCursor* cursor, CursorStyle style);
void cursor_set_color(EditorCursor* cursor, uint8_t r, uint8_t g, uint8_t b);
void cursor_set_blink_rate(EditorCursor* cursor, uint32_t rate_ms);

#endif /* REOLAB_EDITOR_CURSOR_H */
