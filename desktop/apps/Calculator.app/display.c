/*
 * NeolyxOS Calculator.app - Display Rendering
 * 
 * Renders the calculator display area.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"
#include "calc.h"

/* ============ Colors ============ */

#define COLOR_DISPLAY_BG    0x000000
#define COLOR_DISPLAY_TEXT  0xFFFFFF
#define COLOR_DISPLAY_DIM   0x8E8E93
#define COLOR_ERROR         0xFF3B30

/* ============ Display Implementation ============ */

void display_init(calc_display_t *display) {
    display->current[0] = '0';
    display->current[1] = '\0';
    display->current_len = 1;
    display->expression[0] = '\0';
    display->has_decimal = 0;
    display->has_error = 0;
    display->error_msg[0] = '\0';
}

void display_draw(void *ctx, calc_display_t *display, int x, int y, int width, int height) {
    /* Draw background */
    nx_fill_rect(ctx, x, y, width, height, COLOR_DISPLAY_BG);
    
    /* Draw expression (secondary line) */
    if (display->expression[0]) {
        nx_draw_text(ctx, display->expression, x + 12, y + 20, COLOR_DISPLAY_DIM);
    }
    
    /* Draw current value (main line) */
    uint32_t text_color = display->has_error ? COLOR_ERROR : COLOR_DISPLAY_TEXT;
    const char *text = display->has_error ? display->error_msg : display->current;
    
    /* Right-align the text */
    int text_len = 0;
    while (text[text_len]) text_len++;
    
    int char_width = 24;  /* Approximate width of large font character */
    int text_width = text_len * char_width;
    int text_x = x + width - text_width - 12;
    
    nx_draw_text_large(ctx, text, text_x, y + height - 30, text_color, 48);
}

void display_set_value(calc_display_t *display, double value, calc_base_t base) {
    display_format_number(display->current, CALC_MAX_DIGITS + 1, value, base);
    display->current_len = 0;
    while (display->current[display->current_len]) display->current_len++;
    display->has_error = 0;
}

void display_set_error(calc_display_t *display, const char *msg) {
    int i = 0;
    while (msg[i] && i < 63) {
        display->error_msg[i] = msg[i];
        i++;
    }
    display->error_msg[i] = '\0';
    display->has_error = 1;
}

void display_format_number(char *buf, int buflen, double value, calc_base_t base) {
    if (base == BASE_DECIMAL) {
        /* Decimal formatting */
        int idx = 0;
        
        if (value < 0) {
            buf[idx++] = '-';
            value = -value;
        }
        
        int64_t int_part = (int64_t)value;
        double frac_part = value - (double)int_part;
        
        char int_buf[32];
        int int_idx = 0;
        
        if (int_part == 0) {
            int_buf[int_idx++] = '0';
        } else {
            while (int_part > 0 && int_idx < 30) {
                int_buf[int_idx++] = '0' + (int_part % 10);
                int_part /= 10;
            }
        }
        
        for (int i = int_idx - 1; i >= 0 && idx < buflen - 1; i--) {
            buf[idx++] = int_buf[i];
        }
        
        if (frac_part > 0.0000001) {
            buf[idx++] = '.';
            int max_decimals = 10;
            while (frac_part > 0.0000001 && max_decimals > 0 && idx < buflen - 1) {
                frac_part *= 10.0;
                int digit = (int)frac_part;
                buf[idx++] = '0' + digit;
                frac_part -= digit;
                max_decimals--;
            }
            while (idx > 1 && buf[idx - 1] == '0' && buf[idx - 2] != '.') {
                idx--;
            }
        }
        
        buf[idx] = '\0';
    } else {
        /* Integer bases */
        int64_t int_value = (int64_t)value;
        int idx = 0;
        int negative = 0;
        
        if (int_value < 0) {
            negative = 1;
            int_value = -int_value;
        }
        
        char digits[64];
        int dlen = 0;
        
        if (int_value == 0) {
            digits[dlen++] = '0';
        } else {
            while (int_value > 0 && dlen < 63) {
                int digit = int_value % base;
                if (digit < 10) {
                    digits[dlen++] = '0' + digit;
                } else {
                    digits[dlen++] = 'A' + (digit - 10);
                }
                int_value /= base;
            }
        }
        
        /* Add prefix */
        if (base == BASE_HEXADECIMAL) {
            buf[idx++] = '0';
            buf[idx++] = 'x';
        } else if (base == BASE_OCTAL) {
            buf[idx++] = '0';
            buf[idx++] = 'o';
        } else if (base == BASE_BINARY) {
            buf[idx++] = '0';
            buf[idx++] = 'b';
        }
        
        if (negative) {
            buf[idx++] = '-';
        }
        
        /* Reverse digits */
        for (int i = dlen - 1; i >= 0 && idx < buflen - 1; i--) {
            buf[idx++] = digits[i];
        }
        
        buf[idx] = '\0';
    }
}
