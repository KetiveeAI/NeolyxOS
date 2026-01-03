/*
 * NeolyxOS Terminal Emulator Header
 * 
 * VT100-compatible terminal with escape sequence support.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_TERMINAL_H
#define NEOLYX_TERMINAL_H

#include <stdint.h>

/* ============ Terminal Constants ============ */

#define TERM_DEFAULT_WIDTH      80
#define TERM_DEFAULT_HEIGHT     25
#define TERM_MAX_WIDTH          240
#define TERM_MAX_HEIGHT         67
#define TERM_TAB_SIZE           8

/* Terminal colors (VT100 compatible) */
typedef enum {
    TERM_COLOR_BLACK = 0,
    TERM_COLOR_RED,
    TERM_COLOR_GREEN,
    TERM_COLOR_YELLOW,
    TERM_COLOR_BLUE,
    TERM_COLOR_MAGENTA,
    TERM_COLOR_CYAN,
    TERM_COLOR_WHITE,
    TERM_COLOR_DEFAULT = 9,
} term_color_t;

/* Terminal attributes */
#define TERM_ATTR_NORMAL    0x00
#define TERM_ATTR_BOLD      0x01
#define TERM_ATTR_DIM       0x02
#define TERM_ATTR_UNDERLINE 0x04
#define TERM_ATTR_BLINK     0x08
#define TERM_ATTR_REVERSE   0x10
#define TERM_ATTR_HIDDEN    0x20

/* Escape sequence states */
typedef enum {
    TERM_STATE_NORMAL = 0,
    TERM_STATE_ESC,         /* Received ESC */
    TERM_STATE_CSI,         /* Received ESC [ */
    TERM_STATE_OSC,         /* Received ESC ] */
    TERM_STATE_CHARSET,     /* Received ESC ( or ESC ) */
} term_state_t;

/* ============ Terminal Cell ============ */

typedef struct {
    uint32_t codepoint;     /* Unicode character */
    uint8_t  fg_color;      /* Foreground color */
    uint8_t  bg_color;      /* Background color */
    uint8_t  attrs;         /* Attributes (bold, etc.) */
    uint8_t  dirty;         /* Needs redraw */
} term_cell_t;

/* ============ Terminal Structure ============ */

typedef struct {
    /* Display buffer */
    term_cell_t *buffer;        /* Cell buffer */
    uint32_t     width;         /* Terminal width in chars */
    uint32_t     height;        /* Terminal height in chars */
    
    /* Cursor */
    uint32_t cursor_x;
    uint32_t cursor_y;
    int      cursor_visible;
    int      cursor_blink;
    
    /* Current attributes */
    uint8_t current_fg;
    uint8_t current_bg;
    uint8_t current_attrs;
    
    /* Escape sequence parsing */
    term_state_t state;
    char         esc_buf[64];
    int          esc_idx;
    int          esc_params[16];
    int          esc_param_count;
    
    /* Scroll region */
    uint32_t scroll_top;
    uint32_t scroll_bottom;
    
    /* Saved cursor position */
    uint32_t saved_x;
    uint32_t saved_y;
    
    /* Modes */
    int mode_insert;        /* Insert mode */
    int mode_autowrap;      /* Auto-wrap at end of line */
    int mode_origin;        /* Origin mode */
    int mode_linefeed;      /* LF/NL mode */
    int mode_cursor_keys;   /* Cursor key mode */
    
    /* Output callback */
    void (*output_char)(uint32_t x, uint32_t y, term_cell_t *cell);
    void (*output_cursor)(uint32_t x, uint32_t y, int visible);
    void (*output_scroll)(int lines);
} terminal_t;

/* ============ Terminal API ============ */

/**
 * Create a new terminal.
 * 
 * @param width  Terminal width
 * @param height Terminal height
 * @return Terminal instance or NULL
 */
terminal_t *terminal_create(uint32_t width, uint32_t height);

/**
 * Destroy terminal and free resources.
 */
void terminal_destroy(terminal_t *term);

/**
 * Write data to terminal.
 * 
 * @param term Terminal instance
 * @param data Data to write
 * @param len  Length of data
 */
void terminal_write(terminal_t *term, const char *data, uint32_t len);

/**
 * Write a single character to terminal.
 */
void terminal_putc(terminal_t *term, char c);

/**
 * Write a string to terminal.
 */
void terminal_puts(terminal_t *term, const char *str);

/**
 * Clear the terminal screen.
 */
void terminal_clear(terminal_t *term);

/**
 * Set cursor position.
 */
void terminal_set_cursor(terminal_t *term, uint32_t x, uint32_t y);

/**
 * Get cursor position.
 */
void terminal_get_cursor(terminal_t *term, uint32_t *x, uint32_t *y);

/**
 * Set foreground color.
 */
void terminal_set_fg(terminal_t *term, term_color_t color);

/**
 * Set background color.
 */
void terminal_set_bg(terminal_t *term, term_color_t color);

/**
 * Set text attributes.
 */
void terminal_set_attrs(terminal_t *term, uint8_t attrs);

/**
 * Reset terminal to default state.
 */
void terminal_reset(terminal_t *term);

/**
 * Scroll the terminal by n lines.
 * Positive = up, negative = down.
 */
void terminal_scroll(terminal_t *term, int lines);

/**
 * Refresh the entire terminal display.
 */
void terminal_refresh(terminal_t *term);

/**
 * Set output callbacks for rendering.
 */
void terminal_set_callbacks(terminal_t *term,
    void (*char_cb)(uint32_t x, uint32_t y, term_cell_t *cell),
    void (*cursor_cb)(uint32_t x, uint32_t y, int visible),
    void (*scroll_cb)(int lines));

/* ============ Global Terminal ============ */

/**
 * Initialize the kernel terminal.
 */
void term_init(void);

/**
 * Get the kernel terminal instance.
 */
terminal_t *term_get(void);

/**
 * Write to kernel terminal.
 */
void term_write(const char *data, uint32_t len);

/**
 * Print formatted string to terminal.
 */
void term_printf(const char *fmt, ...);

#endif /* NEOLYX_TERMINAL_H */
