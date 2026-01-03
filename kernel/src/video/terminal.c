/*
 * NeolyxOS Terminal Emulator Implementation
 * 
 * VT100-compatible terminal with escape sequence support.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "video/terminal.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void *memset(void *s, int c, uint64_t n);
extern void *memcpy(void *dest, const void *src, uint64_t n);

/* ============ Helpers ============ */

static void term_memset(void *s, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

/* ============ Cell Operations ============ */

static void term_clear_cell(terminal_t *term, uint32_t x, uint32_t y) {
    if (x >= term->width || y >= term->height) return;
    
    term_cell_t *cell = &term->buffer[y * term->width + x];
    cell->codepoint = ' ';
    cell->fg_color = term->current_fg;
    cell->bg_color = term->current_bg;
    cell->attrs = 0;
    cell->dirty = 1;
}

static void term_set_cell(terminal_t *term, uint32_t x, uint32_t y, uint32_t ch) {
    if (x >= term->width || y >= term->height) return;
    
    term_cell_t *cell = &term->buffer[y * term->width + x];
    cell->codepoint = ch;
    cell->fg_color = term->current_fg;
    cell->bg_color = term->current_bg;
    cell->attrs = term->current_attrs;
    cell->dirty = 1;
    
    if (term->output_char) {
        term->output_char(x, y, cell);
    }
}

/* ============ Scrolling ============ */

void terminal_scroll(terminal_t *term, int lines) {
    if (lines == 0) return;
    
    uint32_t top = term->scroll_top;
    uint32_t bottom = term->scroll_bottom;
    uint32_t height = bottom - top + 1;
    
    if (lines > 0) {
        /* Scroll up */
        if ((uint32_t)lines >= height) {
            /* Clear entire scroll region */
            for (uint32_t y = top; y <= bottom; y++) {
                for (uint32_t x = 0; x < term->width; x++) {
                    term_clear_cell(term, x, y);
                }
            }
        } else {
            /* Move lines up */
            for (uint32_t y = top; y <= bottom - lines; y++) {
                memcpy(&term->buffer[y * term->width],
                       &term->buffer[(y + lines) * term->width],
                       term->width * sizeof(term_cell_t));
            }
            /* Clear bottom lines */
            for (uint32_t y = bottom - lines + 1; y <= bottom; y++) {
                for (uint32_t x = 0; x < term->width; x++) {
                    term_clear_cell(term, x, y);
                }
            }
        }
    } else {
        /* Scroll down */
        lines = -lines;
        if ((uint32_t)lines >= height) {
            for (uint32_t y = top; y <= bottom; y++) {
                for (uint32_t x = 0; x < term->width; x++) {
                    term_clear_cell(term, x, y);
                }
            }
        } else {
            for (uint32_t y = bottom; y >= top + lines; y--) {
                memcpy(&term->buffer[y * term->width],
                       &term->buffer[(y - lines) * term->width],
                       term->width * sizeof(term_cell_t));
            }
            for (uint32_t y = top; y < top + lines; y++) {
                for (uint32_t x = 0; x < term->width; x++) {
                    term_clear_cell(term, x, y);
                }
            }
        }
    }
    
    if (term->output_scroll) {
        term->output_scroll(lines);
    }
}

/* ============ Cursor Movement ============ */

static void term_advance_cursor(terminal_t *term) {
    term->cursor_x++;
    if (term->cursor_x >= term->width) {
        if (term->mode_autowrap) {
            term->cursor_x = 0;
            term->cursor_y++;
            if (term->cursor_y > term->scroll_bottom) {
                term->cursor_y = term->scroll_bottom;
                terminal_scroll(term, 1);
            }
        } else {
            term->cursor_x = term->width - 1;
        }
    }
}

static void term_newline(terminal_t *term) {
    term->cursor_x = 0;
    term->cursor_y++;
    if (term->cursor_y > term->scroll_bottom) {
        term->cursor_y = term->scroll_bottom;
        terminal_scroll(term, 1);
    }
}

static void term_carriage_return(terminal_t *term) {
    term->cursor_x = 0;
}

static void term_backspace(terminal_t *term) {
    if (term->cursor_x > 0) {
        term->cursor_x--;
    }
}

static void term_tab(terminal_t *term) {
    uint32_t next_tab = (term->cursor_x / TERM_TAB_SIZE + 1) * TERM_TAB_SIZE;
    if (next_tab >= term->width) {
        next_tab = term->width - 1;
    }
    term->cursor_x = next_tab;
}

/* ============ CSI Sequence Handling ============ */

static int term_parse_param(terminal_t *term, int idx, int default_val) {
    if (idx >= term->esc_param_count || term->esc_params[idx] < 0) {
        return default_val;
    }
    return term->esc_params[idx];
}

static void term_handle_sgr(terminal_t *term) {
    /* Select Graphic Rendition */
    for (int i = 0; i < term->esc_param_count; i++) {
        int param = term_parse_param(term, i, 0);
        
        switch (param) {
        case 0:  /* Reset */
            term->current_fg = TERM_COLOR_WHITE;
            term->current_bg = TERM_COLOR_BLACK;
            term->current_attrs = 0;
            break;
        case 1:  /* Bold */
            term->current_attrs |= TERM_ATTR_BOLD;
            break;
        case 2:  /* Dim */
            term->current_attrs |= TERM_ATTR_DIM;
            break;
        case 4:  /* Underline */
            term->current_attrs |= TERM_ATTR_UNDERLINE;
            break;
        case 5:  /* Blink */
            term->current_attrs |= TERM_ATTR_BLINK;
            break;
        case 7:  /* Reverse */
            term->current_attrs |= TERM_ATTR_REVERSE;
            break;
        case 8:  /* Hidden */
            term->current_attrs |= TERM_ATTR_HIDDEN;
            break;
        case 22: /* Normal intensity */
            term->current_attrs &= ~(TERM_ATTR_BOLD | TERM_ATTR_DIM);
            break;
        case 24: /* Underline off */
            term->current_attrs &= ~TERM_ATTR_UNDERLINE;
            break;
        case 25: /* Blink off */
            term->current_attrs &= ~TERM_ATTR_BLINK;
            break;
        case 27: /* Reverse off */
            term->current_attrs &= ~TERM_ATTR_REVERSE;
            break;
        case 28: /* Hidden off */
            term->current_attrs &= ~TERM_ATTR_HIDDEN;
            break;
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            term->current_fg = param - 30;
            break;
        case 39: /* Default foreground */
            term->current_fg = TERM_COLOR_WHITE;
            break;
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            term->current_bg = param - 40;
            break;
        case 49: /* Default background */
            term->current_bg = TERM_COLOR_BLACK;
            break;
        }
    }
}

static void term_handle_csi(terminal_t *term, char final) {
    int n, m;
    
    switch (final) {
    case 'A':  /* Cursor Up */
        n = term_parse_param(term, 0, 1);
        if (term->cursor_y > (uint32_t)n) {
            term->cursor_y -= n;
        } else {
            term->cursor_y = 0;
        }
        break;
        
    case 'B':  /* Cursor Down */
        n = term_parse_param(term, 0, 1);
        term->cursor_y += n;
        if (term->cursor_y > term->scroll_bottom) {
            term->cursor_y = term->scroll_bottom;
        }
        break;
        
    case 'C':  /* Cursor Forward */
        n = term_parse_param(term, 0, 1);
        term->cursor_x += n;
        if (term->cursor_x >= term->width) {
            term->cursor_x = term->width - 1;
        }
        break;
        
    case 'D':  /* Cursor Back */
        n = term_parse_param(term, 0, 1);
        if (term->cursor_x > (uint32_t)n) {
            term->cursor_x -= n;
        } else {
            term->cursor_x = 0;
        }
        break;
        
    case 'H':  /* Cursor Position */
    case 'f':
        m = term_parse_param(term, 0, 1);
        n = term_parse_param(term, 1, 1);
        term->cursor_y = (m > 0) ? m - 1 : 0;
        term->cursor_x = (n > 0) ? n - 1 : 0;
        if (term->cursor_x >= term->width) term->cursor_x = term->width - 1;
        if (term->cursor_y >= term->height) term->cursor_y = term->height - 1;
        break;
        
    case 'J':  /* Erase in Display */
        n = term_parse_param(term, 0, 0);
        if (n == 0) {
            /* Clear from cursor to end */
            for (uint32_t x = term->cursor_x; x < term->width; x++) {
                term_clear_cell(term, x, term->cursor_y);
            }
            for (uint32_t y = term->cursor_y + 1; y < term->height; y++) {
                for (uint32_t x = 0; x < term->width; x++) {
                    term_clear_cell(term, x, y);
                }
            }
        } else if (n == 1) {
            /* Clear from start to cursor */
            for (uint32_t y = 0; y < term->cursor_y; y++) {
                for (uint32_t x = 0; x < term->width; x++) {
                    term_clear_cell(term, x, y);
                }
            }
            for (uint32_t x = 0; x <= term->cursor_x; x++) {
                term_clear_cell(term, x, term->cursor_y);
            }
        } else if (n == 2) {
            /* Clear entire screen */
            terminal_clear(term);
        }
        break;
        
    case 'K':  /* Erase in Line */
        n = term_parse_param(term, 0, 0);
        if (n == 0) {
            for (uint32_t x = term->cursor_x; x < term->width; x++) {
                term_clear_cell(term, x, term->cursor_y);
            }
        } else if (n == 1) {
            for (uint32_t x = 0; x <= term->cursor_x; x++) {
                term_clear_cell(term, x, term->cursor_y);
            }
        } else if (n == 2) {
            for (uint32_t x = 0; x < term->width; x++) {
                term_clear_cell(term, x, term->cursor_y);
            }
        }
        break;
        
    case 'm':  /* Select Graphic Rendition */
        term_handle_sgr(term);
        break;
        
    case 's':  /* Save cursor position */
        term->saved_x = term->cursor_x;
        term->saved_y = term->cursor_y;
        break;
        
    case 'u':  /* Restore cursor position */
        term->cursor_x = term->saved_x;
        term->cursor_y = term->saved_y;
        break;
        
    case 'r':  /* Set scroll region */
        n = term_parse_param(term, 0, 1);
        m = term_parse_param(term, 1, term->height);
        term->scroll_top = (n > 0) ? n - 1 : 0;
        term->scroll_bottom = (m > 0) ? m - 1 : term->height - 1;
        if (term->scroll_top >= term->height) term->scroll_top = term->height - 1;
        if (term->scroll_bottom >= term->height) term->scroll_bottom = term->height - 1;
        if (term->scroll_top > term->scroll_bottom) {
            uint32_t tmp = term->scroll_top;
            term->scroll_top = term->scroll_bottom;
            term->scroll_bottom = tmp;
        }
        term->cursor_x = 0;
        term->cursor_y = term->scroll_top;
        break;
    }
}

/* ============ Escape Sequence Parser ============ */

static void term_process_escape(terminal_t *term, char c) {
    switch (term->state) {
    case TERM_STATE_NORMAL:
        if (c == '\033') {
            term->state = TERM_STATE_ESC;
            term->esc_idx = 0;
            term->esc_param_count = 0;
        }
        break;
        
    case TERM_STATE_ESC:
        if (c == '[') {
            term->state = TERM_STATE_CSI;
            term->esc_param_count = 0;
            for (int i = 0; i < 16; i++) term->esc_params[i] = -1;
        } else if (c == ']') {
            term->state = TERM_STATE_OSC;
        } else if (c == '(' || c == ')') {
            term->state = TERM_STATE_CHARSET;
        } else {
            term->state = TERM_STATE_NORMAL;
        }
        break;
        
    case TERM_STATE_CSI:
        if (c >= '0' && c <= '9') {
            if (term->esc_param_count == 0) term->esc_param_count = 1;
            if (term->esc_params[term->esc_param_count - 1] < 0) {
                term->esc_params[term->esc_param_count - 1] = 0;
            }
            term->esc_params[term->esc_param_count - 1] *= 10;
            term->esc_params[term->esc_param_count - 1] += (c - '0');
        } else if (c == ';') {
            if (term->esc_param_count < 16) term->esc_param_count++;
        } else {
            term_handle_csi(term, c);
            term->state = TERM_STATE_NORMAL;
        }
        break;
        
    case TERM_STATE_OSC:
        if (c == '\007' || c == '\033') {
            term->state = TERM_STATE_NORMAL;
        }
        break;
        
    case TERM_STATE_CHARSET:
        term->state = TERM_STATE_NORMAL;
        break;
    }
}

/* ============ Public API ============ */

terminal_t *terminal_create(uint32_t width, uint32_t height) {
    terminal_t *term = (terminal_t *)kzalloc(sizeof(terminal_t));
    if (!term) return 0;
    
    term->width = width;
    term->height = height;
    term->buffer = (term_cell_t *)kzalloc(width * height * sizeof(term_cell_t));
    if (!term->buffer) {
        kfree(term);
        return 0;
    }
    
    terminal_reset(term);
    serial_puts("[TERM] Terminal created\n");
    
    return term;
}

void terminal_destroy(terminal_t *term) {
    if (!term) return;
    if (term->buffer) kfree(term->buffer);
    kfree(term);
}

void terminal_reset(terminal_t *term) {
    if (!term) return;
    
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->cursor_visible = 1;
    term->cursor_blink = 1;
    
    term->current_fg = TERM_COLOR_WHITE;
    term->current_bg = TERM_COLOR_BLACK;
    term->current_attrs = 0;
    
    term->state = TERM_STATE_NORMAL;
    term->esc_idx = 0;
    term->esc_param_count = 0;
    
    term->scroll_top = 0;
    term->scroll_bottom = term->height - 1;
    
    term->saved_x = 0;
    term->saved_y = 0;
    
    term->mode_insert = 0;
    term->mode_autowrap = 1;
    term->mode_origin = 0;
    term->mode_linefeed = 0;
    term->mode_cursor_keys = 0;
    
    terminal_clear(term);
}

void terminal_clear(terminal_t *term) {
    if (!term) return;
    
    for (uint32_t y = 0; y < term->height; y++) {
        for (uint32_t x = 0; x < term->width; x++) {
            term_clear_cell(term, x, y);
        }
    }
    term->cursor_x = 0;
    term->cursor_y = 0;
}

void terminal_putc(terminal_t *term, char c) {
    if (!term) return;
    
    if (term->state != TERM_STATE_NORMAL || c == '\033') {
        term_process_escape(term, c);
        return;
    }
    
    switch (c) {
    case '\n':
        term_newline(term);
        break;
    case '\r':
        term_carriage_return(term);
        break;
    case '\b':
        term_backspace(term);
        break;
    case '\t':
        term_tab(term);
        break;
    case '\a':
        /* Bell - could trigger sound */
        break;
    default:
        if (c >= 32 && c < 127) {
            term_set_cell(term, term->cursor_x, term->cursor_y, c);
            term_advance_cursor(term);
        }
        break;
    }
}

void terminal_write(terminal_t *term, const char *data, uint32_t len) {
    if (!term || !data) return;
    
    for (uint32_t i = 0; i < len; i++) {
        terminal_putc(term, data[i]);
    }
}

void terminal_puts(terminal_t *term, const char *str) {
    if (!term || !str) return;
    while (*str) {
        terminal_putc(term, *str++);
    }
}

void terminal_set_cursor(terminal_t *term, uint32_t x, uint32_t y) {
    if (!term) return;
    term->cursor_x = (x < term->width) ? x : term->width - 1;
    term->cursor_y = (y < term->height) ? y : term->height - 1;
}

void terminal_get_cursor(terminal_t *term, uint32_t *x, uint32_t *y) {
    if (!term) return;
    if (x) *x = term->cursor_x;
    if (y) *y = term->cursor_y;
}

void terminal_set_fg(terminal_t *term, term_color_t color) {
    if (term) term->current_fg = color;
}

void terminal_set_bg(terminal_t *term, term_color_t color) {
    if (term) term->current_bg = color;
}

void terminal_set_attrs(terminal_t *term, uint8_t attrs) {
    if (term) term->current_attrs = attrs;
}

void terminal_set_callbacks(terminal_t *term,
    void (*char_cb)(uint32_t x, uint32_t y, term_cell_t *cell),
    void (*cursor_cb)(uint32_t x, uint32_t y, int visible),
    void (*scroll_cb)(int lines))
{
    if (!term) return;
    term->output_char = char_cb;
    term->output_cursor = cursor_cb;
    term->output_scroll = scroll_cb;
}

void terminal_refresh(terminal_t *term) {
    if (!term || !term->output_char) return;
    
    for (uint32_t y = 0; y < term->height; y++) {
        for (uint32_t x = 0; x < term->width; x++) {
            term_cell_t *cell = &term->buffer[y * term->width + x];
            if (cell->dirty) {
                term->output_char(x, y, cell);
                cell->dirty = 0;
            }
        }
    }
}

/* ============ Global Kernel Terminal ============ */

static terminal_t *kernel_term = 0;

void term_init(void) {
    kernel_term = terminal_create(TERM_DEFAULT_WIDTH, TERM_DEFAULT_HEIGHT);
    if (kernel_term) {
        serial_puts("[TERM] Kernel terminal initialized (80x25)\n");
    }
}

terminal_t *term_get(void) {
    return kernel_term;
}

void term_write(const char *data, uint32_t len) {
    if (kernel_term) {
        terminal_write(kernel_term, data, len);
    }
}

/* Simple printf without varargs (basic implementation) */
void term_printf(const char *fmt, ...) {
    /* For now, just print the format string */
    if (kernel_term && fmt) {
        terminal_puts(kernel_term, fmt);
    }
}
