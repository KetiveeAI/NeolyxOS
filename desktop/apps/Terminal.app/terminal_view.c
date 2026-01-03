/*
 * NeolyxOS Terminal.app - Text Grid View
 * 
 * Renders the terminal text buffer using ReoxUI primitives.
 * Supports text selection, cursor display, and colors.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* ============ Terminal Grid Configuration ============ */

#define GRID_COLS       80
#define GRID_ROWS       24

/* ANSI Colors */
typedef struct {
    uint32_t fg;
    uint32_t bg;
} term_colors_t;

static const uint32_t ansi_colors[16] = {
    0x000000, /* 0: Black */
    0xCC0000, /* 1: Red */
    0x00CC00, /* 2: Green */
    0xCCCC00, /* 3: Yellow */
    0x0000CC, /* 4: Blue */
    0xCC00CC, /* 5: Magenta */
    0x00CCCC, /* 6: Cyan */
    0xCCCCCC, /* 7: White */
    /* Bright versions */
    0x666666, /* 8: Bright Black */
    0xFF0000, /* 9: Bright Red */
    0x00FF00, /* 10: Bright Green */
    0xFFFF00, /* 11: Bright Yellow */
    0x0000FF, /* 12: Bright Blue */
    0xFF00FF, /* 13: Bright Magenta */
    0x00FFFF, /* 14: Bright Cyan */
    0xFFFFFF  /* 15: Bright White */
};

/* ============ Cell Structure ============ */

typedef struct {
    char ch;            /* Character */
    uint8_t fg : 4;     /* Foreground color (0-15) */
    uint8_t bg : 4;     /* Background color (0-15) */
    uint8_t bold : 1;
    uint8_t italic : 1;
    uint8_t underline : 1;
    uint8_t blink : 1;
    uint8_t inverse : 1;
    uint8_t selected : 1;
    uint8_t reserved : 2;
} term_cell_t;

/* ============ Grid Buffer ============ */

typedef struct {
    term_cell_t cells[GRID_ROWS][GRID_COLS];
    int dirty_rows[GRID_ROWS];  /* Which rows need redraw */
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    int cursor_blink_state;
    
    /* Current text attributes */
    uint8_t cur_fg;
    uint8_t cur_bg;
    uint8_t cur_bold;
    uint8_t cur_underline;
} term_grid_t;

/* ============ Function Prototypes ============ */

/* Grid management */
void grid_init(term_grid_t *grid);
void grid_clear(term_grid_t *grid);
void grid_clear_line(term_grid_t *grid, int row);

/* Character output */
void grid_putchar(term_grid_t *grid, char ch);
void grid_puts(term_grid_t *grid, const char *str);
void grid_newline(term_grid_t *grid);
void grid_scroll_up(term_grid_t *grid);

/* Cursor movement */
void grid_cursor_move(term_grid_t *grid, int x, int y);
void grid_cursor_up(term_grid_t *grid, int n);
void grid_cursor_down(term_grid_t *grid, int n);
void grid_cursor_left(term_grid_t *grid, int n);
void grid_cursor_right(term_grid_t *grid, int n);

/* ANSI escape sequence parsing */
void grid_process_ansi(term_grid_t *grid, const char *seq, int len);

/* Selection */
void grid_select_start(term_grid_t *grid, int x, int y);
void grid_select_update(term_grid_t *grid, int x, int y);
void grid_select_end(term_grid_t *grid);
int grid_get_selection(term_grid_t *grid, char *buf, int max_len);

/* Rendering */
void grid_render(term_grid_t *grid, int char_w, int char_h, int offset_x, int offset_y);
void grid_render_cursor(term_grid_t *grid, int char_w, int char_h, int offset_x, int offset_y);

/* ============ Implementation ============ */

void grid_init(term_grid_t *grid) {
    /* Clear all cells */
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            grid->cells[r][c].ch = ' ';
            grid->cells[r][c].fg = 7;  /* White */
            grid->cells[r][c].bg = 0;  /* Black */
            grid->cells[r][c].bold = 0;
            grid->cells[r][c].italic = 0;
            grid->cells[r][c].underline = 0;
            grid->cells[r][c].blink = 0;
            grid->cells[r][c].inverse = 0;
            grid->cells[r][c].selected = 0;
        }
        grid->dirty_rows[r] = 1;
    }
    
    grid->cursor_x = 0;
    grid->cursor_y = 0;
    grid->cursor_visible = 1;
    grid->cursor_blink_state = 1;
    
    grid->cur_fg = 7;
    grid->cur_bg = 0;
    grid->cur_bold = 0;
    grid->cur_underline = 0;
}

void grid_clear(term_grid_t *grid) {
    for (int r = 0; r < GRID_ROWS; r++) {
        grid_clear_line(grid, r);
    }
    grid->cursor_x = 0;
    grid->cursor_y = 0;
}

void grid_clear_line(term_grid_t *grid, int row) {
    if (row < 0 || row >= GRID_ROWS) return;
    
    for (int c = 0; c < GRID_COLS; c++) {
        grid->cells[row][c].ch = ' ';
        grid->cells[row][c].fg = grid->cur_fg;
        grid->cells[row][c].bg = grid->cur_bg;
        grid->cells[row][c].bold = 0;
        grid->cells[row][c].underline = 0;
        grid->cells[row][c].selected = 0;
    }
    grid->dirty_rows[row] = 1;
}

void grid_putchar(term_grid_t *grid, char ch) {
    /* Handle control characters */
    if (ch == '\n') {
        grid_newline(grid);
        return;
    }
    if (ch == '\r') {
        grid->cursor_x = 0;
        return;
    }
    if (ch == '\b') {
        if (grid->cursor_x > 0) grid->cursor_x--;
        return;
    }
    if (ch == '\t') {
        /* Tab to next 8-column boundary */
        grid->cursor_x = (grid->cursor_x + 8) & ~7;
        if (grid->cursor_x >= GRID_COLS) {
            grid->cursor_x = 0;
            grid_newline(grid);
        }
        return;
    }
    
    /* Printable character */
    if (grid->cursor_x >= GRID_COLS) {
        grid->cursor_x = 0;
        grid_newline(grid);
    }
    
    term_cell_t *cell = &grid->cells[grid->cursor_y][grid->cursor_x];
    cell->ch = ch;
    cell->fg = grid->cur_fg;
    cell->bg = grid->cur_bg;
    cell->bold = grid->cur_bold;
    cell->underline = grid->cur_underline;
    
    grid->dirty_rows[grid->cursor_y] = 1;
    grid->cursor_x++;
}

void grid_puts(term_grid_t *grid, const char *str) {
    while (*str) {
        grid_putchar(grid, *str++);
    }
}

void grid_newline(term_grid_t *grid) {
    grid->cursor_x = 0;
    grid->cursor_y++;
    
    if (grid->cursor_y >= GRID_ROWS) {
        grid_scroll_up(grid);
        grid->cursor_y = GRID_ROWS - 1;
    }
}

void grid_scroll_up(term_grid_t *grid) {
    /* Move all rows up by 1 */
    for (int r = 0; r < GRID_ROWS - 1; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            grid->cells[r][c] = grid->cells[r + 1][c];
        }
        grid->dirty_rows[r] = 1;
    }
    
    /* Clear last row */
    grid_clear_line(grid, GRID_ROWS - 1);
}

void grid_cursor_move(term_grid_t *grid, int x, int y) {
    if (x >= 0 && x < GRID_COLS) grid->cursor_x = x;
    if (y >= 0 && y < GRID_ROWS) grid->cursor_y = y;
}

void grid_cursor_up(term_grid_t *grid, int n) {
    grid->cursor_y -= n;
    if (grid->cursor_y < 0) grid->cursor_y = 0;
}

void grid_cursor_down(term_grid_t *grid, int n) {
    grid->cursor_y += n;
    if (grid->cursor_y >= GRID_ROWS) grid->cursor_y = GRID_ROWS - 1;
}

void grid_cursor_left(term_grid_t *grid, int n) {
    grid->cursor_x -= n;
    if (grid->cursor_x < 0) grid->cursor_x = 0;
}

void grid_cursor_right(term_grid_t *grid, int n) {
    grid->cursor_x += n;
    if (grid->cursor_x >= GRID_COLS) grid->cursor_x = GRID_COLS - 1;
}

/* ANSI escape sequence parsing (basic) */
void grid_process_ansi(term_grid_t *grid, const char *seq, int len) {
    if (len < 2 || seq[0] != '\x1b' || seq[1] != '[') return;
    
    /* Parse CSI sequence */
    const char *p = seq + 2;
    int params[8] = {0};
    int param_count = 0;
    
    /* Parse parameters */
    while (*p && param_count < 8) {
        if (*p >= '0' && *p <= '9') {
            params[param_count] = params[param_count] * 10 + (*p - '0');
            p++;
        } else if (*p == ';') {
            param_count++;
            p++;
        } else {
            break;
        }
    }
    param_count++;
    
    /* Handle command */
    char cmd = *p;
    
    switch (cmd) {
        case 'A': /* Cursor up */
            grid_cursor_up(grid, params[0] ? params[0] : 1);
            break;
        case 'B': /* Cursor down */
            grid_cursor_down(grid, params[0] ? params[0] : 1);
            break;
        case 'C': /* Cursor forward */
            grid_cursor_right(grid, params[0] ? params[0] : 1);
            break;
        case 'D': /* Cursor back */
            grid_cursor_left(grid, params[0] ? params[0] : 1);
            break;
        case 'H': /* Cursor position */
        case 'f':
            grid_cursor_move(grid, 
                (param_count > 1 ? params[1] : 1) - 1,
                (params[0] ? params[0] : 1) - 1);
            break;
        case 'J': /* Erase display */
            if (params[0] == 2) grid_clear(grid);
            break;
        case 'K': /* Erase line */
            grid_clear_line(grid, grid->cursor_y);
            break;
        case 'm': /* SGR (colors) */
            for (int i = 0; i < param_count; i++) {
                int code = params[i];
                if (code == 0) {
                    grid->cur_fg = 7;
                    grid->cur_bg = 0;
                    grid->cur_bold = 0;
                    grid->cur_underline = 0;
                } else if (code == 1) {
                    grid->cur_bold = 1;
                } else if (code == 4) {
                    grid->cur_underline = 1;
                } else if (code >= 30 && code <= 37) {
                    grid->cur_fg = code - 30;
                } else if (code >= 40 && code <= 47) {
                    grid->cur_bg = code - 40;
                } else if (code >= 90 && code <= 97) {
                    grid->cur_fg = code - 90 + 8;
                } else if (code >= 100 && code <= 107) {
                    grid->cur_bg = code - 100 + 8;
                }
            }
            break;
    }
}

/* Selection */
static int sel_start_r = -1, sel_start_c = -1;
static int sel_end_r = -1, sel_end_c = -1;

void grid_select_start(term_grid_t *grid, int x, int y) {
    /* Clear previous selection */
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            if (grid->cells[r][c].selected) {
                grid->cells[r][c].selected = 0;
                grid->dirty_rows[r] = 1;
            }
        }
    }
    
    sel_start_r = y;
    sel_start_c = x;
    sel_end_r = y;
    sel_end_c = x;
}

void grid_select_update(term_grid_t *grid, int x, int y) {
    sel_end_r = y;
    sel_end_c = x;
    
    /* Update selection cells */
    int r1 = sel_start_r < sel_end_r ? sel_start_r : sel_end_r;
    int r2 = sel_start_r > sel_end_r ? sel_start_r : sel_end_r;
    
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int in_sel = 0;
            if (r >= r1 && r <= r2) {
                if (r == r1 && r == r2) {
                    int c1 = sel_start_c < sel_end_c ? sel_start_c : sel_end_c;
                    int c2 = sel_start_c > sel_end_c ? sel_start_c : sel_end_c;
                    in_sel = (c >= c1 && c <= c2);
                } else if (r == r1) {
                    in_sel = (c >= sel_start_c);
                } else if (r == r2) {
                    in_sel = (c <= sel_end_c);
                } else {
                    in_sel = 1;
                }
            }
            
            if ((int)grid->cells[r][c].selected != in_sel) {
                grid->cells[r][c].selected = in_sel ? 1 : 0;
                grid->dirty_rows[r] = 1;
            }
        }
    }
}

void grid_select_end(term_grid_t *grid) {
    (void)grid;
    /* Selection complete */
}

int grid_get_selection(term_grid_t *grid, char *buf, int max_len) {
    int pos = 0;
    
    for (int r = 0; r < GRID_ROWS && pos < max_len - 1; r++) {
        int line_start = -1, line_end = -1;
        
        for (int c = 0; c < GRID_COLS; c++) {
            if (grid->cells[r][c].selected) {
                if (line_start < 0) line_start = c;
                line_end = c;
            }
        }
        
        if (line_start >= 0) {
            /* Copy this line's selected text */
            for (int c = line_start; c <= line_end && pos < max_len - 1; c++) {
                buf[pos++] = grid->cells[r][c].ch;
            }
            /* Add newline if not last selected row */
            if (pos < max_len - 1) {
                buf[pos++] = '\n';
            }
        }
    }
    
    buf[pos] = '\0';
    return pos;
}

/* Rendering - placeholder for ReoxUI integration */
void grid_render(term_grid_t *grid, int char_w, int char_h, int offset_x, int offset_y) {
    /* TODO: Render using ReoxUI primitives */
    /* For each dirty row, draw:
     * 1. Background rectangles for each cell
     * 2. Text characters
     * 3. Selection highlights
     * 4. Underlines if needed
     */
    (void)grid;
    (void)char_w;
    (void)char_h;
    (void)offset_x;
    (void)offset_y;
    
    /* Mark all rows clean */
    for (int r = 0; r < GRID_ROWS; r++) {
        grid->dirty_rows[r] = 0;
    }
}

void grid_render_cursor(term_grid_t *grid, int char_w, int char_h, int offset_x, int offset_y) {
    if (!grid->cursor_visible || !grid->cursor_blink_state) return;
    
    /* TODO: Draw cursor at position using ReoxUI */
    int x = offset_x + grid->cursor_x * char_w;
    int y = offset_y + grid->cursor_y * char_h;
    
    (void)x;
    (void)y;
    
    /* Draw as block or line based on mode */
}
