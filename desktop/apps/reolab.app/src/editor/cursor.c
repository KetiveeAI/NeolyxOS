/*
 * Reolab - Editor Cursor Implementation
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "cursor.h"
#include "buffer.h"
#include <string.h>

/* Default values - premium feel */
#define DEFAULT_BLINK_RATE_MS   530
#define DEFAULT_INPUT_PAUSE_MS  600
#define DEFAULT_CURSOR_WIDTH    2.0f
#define DEFAULT_CORNER_RADIUS   1.0f

void cursor_init(EditorCursor* cursor) {
    memset(cursor, 0, sizeof(EditorCursor));
    
    cursor->line = 0;
    cursor->column = 0;
    cursor->has_selection = false;
    
    /* Default style: | bar */
    cursor->style = CURSOR_BAR;
    
    /* Accent color (adapts to theme) */
    cursor->color_r = 86;   /* Slightly brighter than text */
    cursor->color_g = 156;
    cursor->color_b = 214;  /* Blue accent */
    
    /* Blinking */
    cursor->visible = true;
    cursor->blink_rate_ms = DEFAULT_BLINK_RATE_MS;
    cursor->last_blink_time = 0;
    cursor->last_input_time = 0;
    cursor->input_pause_ms = DEFAULT_INPUT_PAUSE_MS;
    
    /* Rendering */
    cursor->width = DEFAULT_CURSOR_WIDTH;
    cursor->height = 0;  /* Set from line height */
    cursor->corner_radius = DEFAULT_CORNER_RADIUS;
}

void cursor_update(EditorCursor* cursor, uint32_t current_time_ms) {
    /* Check if we should pause blinking (recent input) */
    uint32_t since_input = current_time_ms - cursor->last_input_time;
    
    if (since_input < cursor->input_pause_ms) {
        /* Keep cursor visible while typing */
        cursor->visible = true;
        cursor->last_blink_time = current_time_ms;
        return;
    }
    
    /* Blink logic */
    uint32_t since_blink = current_time_ms - cursor->last_blink_time;
    
    if (since_blink >= cursor->blink_rate_ms) {
        cursor->visible = !cursor->visible;
        cursor->last_blink_time = current_time_ms;
    }
}

void cursor_on_input(EditorCursor* cursor, uint32_t current_time_ms) {
    cursor->last_input_time = current_time_ms;
    cursor->visible = true;  /* Always visible when typing */
}

void cursor_move_left(EditorCursor* cursor, ReoLabBuffer* buffer) {
    (void)buffer;
    if (cursor->column > 0) {
        cursor->column--;
    } else if (cursor->line > 0) {
        cursor->line--;
        cursor->column = buffer_line_length(buffer, cursor->line);
    }
    cursor_clear_selection(cursor);
}

void cursor_move_right(EditorCursor* cursor, ReoLabBuffer* buffer) {
    size_t line_len = buffer_line_length(buffer, cursor->line);
    
    if (cursor->column < line_len) {
        cursor->column++;
    } else if (cursor->line < buffer->line_count - 1) {
        cursor->line++;
        cursor->column = 0;
    }
    cursor_clear_selection(cursor);
}

void cursor_move_up(EditorCursor* cursor, ReoLabBuffer* buffer) {
    if (cursor->line > 0) {
        cursor->line--;
        size_t line_len = buffer_line_length(buffer, cursor->line);
        if (cursor->column > line_len) {
            cursor->column = line_len;
        }
    }
    cursor_clear_selection(cursor);
}

void cursor_move_down(EditorCursor* cursor, ReoLabBuffer* buffer) {
    if (cursor->line < buffer->line_count - 1) {
        cursor->line++;
        size_t line_len = buffer_line_length(buffer, cursor->line);
        if (cursor->column > line_len) {
            cursor->column = line_len;
        }
    }
    cursor_clear_selection(cursor);
}

void cursor_move_to(EditorCursor* cursor, size_t line, size_t col) {
    cursor->line = line;
    cursor->column = col;
    cursor_clear_selection(cursor);
}

void cursor_move_home(EditorCursor* cursor) {
    cursor->column = 0;
    cursor_clear_selection(cursor);
}

void cursor_move_end(EditorCursor* cursor, ReoLabBuffer* buffer) {
    cursor->column = buffer_line_length(buffer, cursor->line);
    cursor_clear_selection(cursor);
}

void cursor_start_selection(EditorCursor* cursor) {
    if (!cursor->has_selection) {
        cursor->has_selection = true;
        cursor->sel_start_line = cursor->line;
        cursor->sel_start_col = cursor->column;
    }
    cursor->sel_end_line = cursor->line;
    cursor->sel_end_col = cursor->column;
}

void cursor_clear_selection(EditorCursor* cursor) {
    cursor->has_selection = false;
}

void cursor_get_pixel_pos(EditorCursor* cursor, float char_width, float line_height,
                          float gutter_width, float* out_x, float* out_y) {
    if (out_x) *out_x = gutter_width + (cursor->column * char_width);
    if (out_y) *out_y = cursor->line * line_height;
}

void cursor_set_style(EditorCursor* cursor, CursorStyle style) {
    cursor->style = style;
    
    /* Adjust width based on style */
    switch (style) {
        case CURSOR_BAR:
            cursor->width = 2.0f;
            break;
        case CURSOR_UNDERSCORE:
            cursor->width = 8.0f;  /* Char width-ish */
            break;
        case CURSOR_BLOCK:
            cursor->width = 8.0f;  /* Full char width */
            break;
    }
}

void cursor_set_color(EditorCursor* cursor, uint8_t r, uint8_t g, uint8_t b) {
    cursor->color_r = r;
    cursor->color_g = g;
    cursor->color_b = b;
}

void cursor_set_blink_rate(EditorCursor* cursor, uint32_t rate_ms) {
    cursor->blink_rate_ms = rate_ms;
}
