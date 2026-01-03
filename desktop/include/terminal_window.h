/*
 * NeolyxOS Terminal Window Widget
 * 
 * Desktop terminal that wraps the existing terminal shell.
 * Renders terminal output into a desktop window.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_TERMINAL_WINDOW_H
#define NEOLYX_TERMINAL_WINDOW_H

#include <stdint.h>
#include "desktop_shell.h"
#include "widgets.h"

/* ============ Terminal Window Configuration ============ */

#define TERM_WIN_COLS       80   /* Characters per line */
#define TERM_WIN_ROWS       24   /* Lines of text */
#define TERM_WIN_CHAR_W     8    /* Character width in pixels */
#define TERM_WIN_CHAR_H     12   /* Character height in pixels */
#define TERM_WIN_PADDING    8    /* Window padding */
#define TERM_WIN_TITLEBAR   24   /* Title bar height */

/* Terminal window colors */
#define TERM_WIN_BG         0xFF151530  /* Dark blue terminal bg */
#define TERM_WIN_FG         0xFFE0E0E0  /* Light gray text */
#define TERM_WIN_PROMPT     0xFF58A6FF  /* Blue prompt */
#define TERM_WIN_ERROR      0xFFFF5555  /* Red errors */
#define TERM_WIN_SUCCESS    0xFF50FA7B  /* Green success */
#define TERM_WIN_CURSOR     0xFFFFFFFF  /* White cursor */

/* ============ Terminal Buffer ============ */

typedef struct {
    char text[TERM_WIN_COLS + 1];  /* Line text */
    uint32_t color;                 /* Line color */
} term_line_t;

typedef struct {
    /* Text buffer */
    term_line_t lines[TERM_WIN_ROWS];
    int cursor_x;       /* Cursor column (0 to COLS-1) */
    int cursor_y;       /* Cursor row (0 to ROWS-1) */
    int scroll_offset;  /* Scroll position */
    
    /* Input buffer */
    char input[256];
    int input_pos;
    
    /* State */
    char prompt[64];
    uint8_t cursor_visible;
    uint8_t running;
    
    /* Associated window */
    uint32_t window_id;
} terminal_window_t;

/* ============ Terminal Window API ============ */

/**
 * terminal_window_create - Create a terminal window
 * 
 * @x, y: Window position
 * 
 * Returns: Terminal window pointer or NULL
 */
terminal_window_t *terminal_window_create(int32_t x, int32_t y);

/**
 * terminal_window_destroy - Destroy terminal window
 */
void terminal_window_destroy(terminal_window_t *term);

/**
 * terminal_window_render - Render terminal content
 * 
 * Called by desktop window compositor.
 */
void terminal_window_render(terminal_window_t *term, void *ctx);

/**
 * terminal_window_handle_key - Handle keyboard input
 * 
 * @scancode: Key scancode
 * 
 * Returns: 1 if handled
 */
int terminal_window_handle_key(terminal_window_t *term, uint8_t scancode);

/**
 * terminal_window_print - Print text to terminal
 */
void terminal_window_print(terminal_window_t *term, const char *text);

/**
 * terminal_window_print_color - Print colored text
 */
void terminal_window_print_color(terminal_window_t *term, const char *text, uint32_t color);

/**
 * terminal_window_clear - Clear terminal
 */
void terminal_window_clear(terminal_window_t *term);

/**
 * terminal_window_set_prompt - Set command prompt
 */
void terminal_window_set_prompt(terminal_window_t *term, const char *prompt);

/**
 * terminal_window_newline - Move to next line
 */
void terminal_window_newline(terminal_window_t *term);

/**
 * terminal_window_get_window_id - Get associated desktop window ID
 */
uint32_t terminal_window_get_window_id(terminal_window_t *term);

#endif /* NEOLYX_TERMINAL_WINDOW_H */
