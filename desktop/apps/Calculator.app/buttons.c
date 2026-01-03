/*
 * NeolyxOS Calculator.app - Button Layouts
 * 
 * Button initialization and rendering for all calculator modes.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"
#include "calc.h"

/* ============ Colors ============ */

#define COLOR_BTN_NUM       0x333333
#define COLOR_BTN_OP        0xFF9F0A
#define COLOR_BTN_FUNC      0x505050
#define COLOR_BTN_NUM_HOVER 0x444444
#define COLOR_BTN_OP_HOVER  0xFFAA33
#define COLOR_BTN_FUNC_HOVER 0x606060
#define COLOR_BTN_PRESSED   0x666666
#define COLOR_TEXT_WHITE    0xFFFFFF
#define COLOR_TEXT_BLACK    0x000000

/* ============ Button Actions (matching main.c) ============ */

#define BTN_0           0
#define BTN_1           1
#define BTN_2           2
#define BTN_3           3
#define BTN_4           4
#define BTN_5           5
#define BTN_6           6
#define BTN_7           7
#define BTN_8           8
#define BTN_9           9
#define BTN_DOT         10
#define BTN_ADD         11
#define BTN_SUB         12
#define BTN_MUL         13
#define BTN_DIV         14
#define BTN_EQUALS      15
#define BTN_CLEAR       16
#define BTN_CLEAR_ENTRY 17
#define BTN_BACKSPACE   18
#define BTN_NEGATE      19
#define BTN_PERCENT     20
#define BTN_MEM_CLEAR   21
#define BTN_MEM_RECALL  22
#define BTN_MEM_ADD     23
#define BTN_MEM_SUB     24
#define BTN_MEM_STORE   25
#define BTN_SIN         26
#define BTN_COS         27
#define BTN_TAN         28
#define BTN_LOG         39
#define BTN_LN          40
#define BTN_SQRT        43
#define BTN_SQUARE      47
#define BTN_POWER       49
#define BTN_PI          51
#define BTN_E_CONST     52
#define BTN_PAREN_OPEN  60
#define BTN_PAREN_CLOSE 61

/* ============ Helper Functions ============ */

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void add_button(calc_button_t *buttons, int *count, 
                       const char *label, int x, int y, int w, int h,
                       int action, uint32_t bg, uint32_t fg) {
    calc_button_t *btn = &buttons[*count];
    str_copy(btn->label, label, 8);
    btn->x = x;
    btn->y = y;
    btn->width = w;
    btn->height = h;
    btn->action = action;
    btn->hover = 0;
    btn->pressed = 0;
    btn->bg_color = bg;
    btn->fg_color = fg;
    (*count)++;
}

/* ============ Basic Mode Layout ============ */

void buttons_init_basic(calc_button_t *buttons, int *count,
                        int start_x, int start_y, int btn_w, int btn_h, int gap) {
    *count = 0;
    int x, y;
    
    /* Row 1: AC, +/-, %, / */
    y = start_y;
    x = start_x;
    add_button(buttons, count, "AC", x, y, btn_w, btn_h, BTN_CLEAR, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "+/-", x, y, btn_w, btn_h, BTN_NEGATE, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "%", x, y, btn_w, btn_h, BTN_PERCENT, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "/", x, y, btn_w, btn_h, BTN_DIV, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 2: 7, 8, 9, x */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "7", x, y, btn_w, btn_h, BTN_7, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "8", x, y, btn_w, btn_h, BTN_8, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "9", x, y, btn_w, btn_h, BTN_9, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "x", x, y, btn_w, btn_h, BTN_MUL, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 3: 4, 5, 6, - */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "4", x, y, btn_w, btn_h, BTN_4, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "5", x, y, btn_w, btn_h, BTN_5, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "6", x, y, btn_w, btn_h, BTN_6, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "-", x, y, btn_w, btn_h, BTN_SUB, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 4: 1, 2, 3, + */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "1", x, y, btn_w, btn_h, BTN_1, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "2", x, y, btn_w, btn_h, BTN_2, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "3", x, y, btn_w, btn_h, BTN_3, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "+", x, y, btn_w, btn_h, BTN_ADD, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 5: 0 (double width), ., = */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "0", x, y, btn_w * 2 + gap, btn_h, BTN_0, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w * 2 + gap * 2;
    add_button(buttons, count, ".", x, y, btn_w, btn_h, BTN_DOT, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "=", x, y, btn_w, btn_h, BTN_EQUALS, COLOR_BTN_OP, COLOR_TEXT_WHITE);
}

/* ============ Scientific Mode Layout ============ */

void buttons_init_scientific(calc_button_t *buttons, int *count,
                             int start_x, int start_y, int btn_w, int btn_h, int gap) {
    *count = 0;
    int x, y;
    
    /* Row 1: (, ), mc, m+, m-, mr, AC, +/-, %, / */
    y = start_y;
    x = start_x;
    add_button(buttons, count, "(", x, y, btn_w, btn_h, BTN_PAREN_OPEN, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, ")", x, y, btn_w, btn_h, BTN_PAREN_CLOSE, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "mc", x, y, btn_w, btn_h, BTN_MEM_CLEAR, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "m+", x, y, btn_w, btn_h, BTN_MEM_ADD, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "m-", x, y, btn_w, btn_h, BTN_MEM_SUB, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "mr", x, y, btn_w, btn_h, BTN_MEM_RECALL, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "AC", x, y, btn_w, btn_h, BTN_CLEAR, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "+/-", x, y, btn_w, btn_h, BTN_NEGATE, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "%", x, y, btn_w, btn_h, BTN_PERCENT, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "/", x, y, btn_w, btn_h, BTN_DIV, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 2: x^2, x^3, x^y, e^x, 10^x, 7, 8, 9, x */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "x2", x, y, btn_w, btn_h, BTN_SQUARE, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "x3", x, y, btn_w, btn_h, 48, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_CUBE */
    x += btn_w + gap;
    add_button(buttons, count, "x^y", x, y, btn_w, btn_h, BTN_POWER, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "e^x", x, y, btn_w, btn_h, 45, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_POW_E */
    x += btn_w + gap;
    add_button(buttons, count, "10x", x, y, btn_w, btn_h, 44, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_POW_10 */
    x += btn_w + gap;
    x += btn_w + gap;  /* Skip one */
    add_button(buttons, count, "7", x, y, btn_w, btn_h, BTN_7, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "8", x, y, btn_w, btn_h, BTN_8, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "9", x, y, btn_w, btn_h, BTN_9, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "x", x, y, btn_w, btn_h, BTN_MUL, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 3: 1/x, sqrt, cbrt, ln, log, 4, 5, 6, - */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "1/x", x, y, btn_w, btn_h, 41, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_RECIPROCAL */
    x += btn_w + gap;
    add_button(buttons, count, "V", x, y, btn_w, btn_h, BTN_SQRT, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "3V", x, y, btn_w, btn_h, 42, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_CBRT */
    x += btn_w + gap;
    add_button(buttons, count, "ln", x, y, btn_w, btn_h, BTN_LN, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "log", x, y, btn_w, btn_h, BTN_LOG, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    x += btn_w + gap;  /* Skip one */
    add_button(buttons, count, "4", x, y, btn_w, btn_h, BTN_4, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "5", x, y, btn_w, btn_h, BTN_5, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "6", x, y, btn_w, btn_h, BTN_6, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "-", x, y, btn_w, btn_h, BTN_SUB, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 4: sin, cos, tan, e, pi, 1, 2, 3, + */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "sin", x, y, btn_w, btn_h, BTN_SIN, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "cos", x, y, btn_w, btn_h, BTN_COS, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "tan", x, y, btn_w, btn_h, BTN_TAN, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "e", x, y, btn_w, btn_h, BTN_E_CONST, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "pi", x, y, btn_w, btn_h, BTN_PI, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    x += btn_w + gap;  /* Skip one */
    add_button(buttons, count, "1", x, y, btn_w, btn_h, BTN_1, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "2", x, y, btn_w, btn_h, BTN_2, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "3", x, y, btn_w, btn_h, BTN_3, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "+", x, y, btn_w, btn_h, BTN_ADD, COLOR_BTN_OP, COLOR_TEXT_WHITE);
    
    /* Row 5: Rand, n!, DEL, 0, ., = */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "Rnd", x, y, btn_w, btn_h, 54, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_RAND */
    x += btn_w + gap;
    add_button(buttons, count, "n!", x, y, btn_w, btn_h, 40, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_FACTORIAL */
    x += btn_w + gap;
    add_button(buttons, count, "DEL", x, y, btn_w, btn_h, BTN_BACKSPACE, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    x += btn_w + gap;
    x += btn_w + gap;
    x += btn_w + gap;  /* Skip to align with basic mode */
    add_button(buttons, count, "0", x, y, btn_w * 2 + gap, btn_h, BTN_0, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w * 2 + gap * 2;
    add_button(buttons, count, ".", x, y, btn_w, btn_h, BTN_DOT, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "=", x, y, btn_w, btn_h, BTN_EQUALS, COLOR_BTN_OP, COLOR_TEXT_WHITE);
}

/* ============ Programmer Mode Layout ============ */

void buttons_init_programmer(calc_button_t *buttons, int *count,
                             int start_x, int start_y, int btn_w, int btn_h, int gap) {
    *count = 0;
    int x, y;
    
    /* Row 1: AND, OR, XOR, NOT, <<, >>, AC, DEL */
    y = start_y;
    x = start_x;
    add_button(buttons, count, "AND", x, y, btn_w, btn_h, 55, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_AND */
    x += btn_w + gap;
    add_button(buttons, count, "OR", x, y, btn_w, btn_h, 56, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);   /* BTN_OR */
    x += btn_w + gap;
    add_button(buttons, count, "XOR", x, y, btn_w, btn_h, 57, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_XOR */
    x += btn_w + gap;
    add_button(buttons, count, "NOT", x, y, btn_w, btn_h, 58, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_NOT */
    x += btn_w + gap;
    add_button(buttons, count, "<<", x, y, btn_w, btn_h, 59, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);   /* BTN_LSHIFT */
    x += btn_w + gap;
    add_button(buttons, count, ">>", x, y, btn_w, btn_h, 60, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);   /* BTN_RSHIFT */
    x += btn_w + gap;
    add_button(buttons, count, "AC", x, y, btn_w, btn_h, BTN_CLEAR, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "DEL", x, y, btn_w, btn_h, BTN_BACKSPACE, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    
    /* Row 2: HEX, DEC, OCT, BIN - base selection */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "HEX", x, y, btn_w, btn_h, 62, 0x0066CC, COLOR_TEXT_WHITE);  /* BTN_HEX */
    x += btn_w + gap;
    add_button(buttons, count, "DEC", x, y, btn_w, btn_h, 63, 0x00AA55, COLOR_TEXT_WHITE);  /* BTN_DEC */
    x += btn_w + gap;
    add_button(buttons, count, "OCT", x, y, btn_w, btn_h, 64, 0xCC6600, COLOR_TEXT_WHITE);  /* BTN_OCT */
    x += btn_w + gap;
    add_button(buttons, count, "BIN", x, y, btn_w, btn_h, 65, 0x990099, COLOR_TEXT_WHITE);  /* BTN_BIN */
    x += btn_w + gap;
    x += btn_w + gap;  /* Gap */
    add_button(buttons, count, "D", x, y, btn_w, btn_h, 69, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "E", x, y, btn_w, btn_h, 70, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "F", x, y, btn_w, btn_h, 71, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    
    /* Row 3: MOD, A, B, C, 7, 8, 9, / */
    y += btn_h + gap;
    x = start_x;
    add_button(buttons, count, "MOD", x, y, btn_w, btn_h, 53, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);  /* BTN_MOD */
    x += btn_w + gap;
    add_button(buttons, count, "A", x, y, btn_w, btn_h, 66, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "B", x, y, btn_w, btn_h, 67, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "C", x, y, btn_w, btn_h, 68, COLOR_BTN_FUNC, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    x += btn_w + gap;  /* Gap */
    add_button(buttons, count, "7", x, y, btn_w, btn_h, BTN_7, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "8", x, y, btn_w, btn_h, BTN_8, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "9", x, y, btn_w, btn_h, BTN_9, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    
    /* Row 4: 4, 5, 6, x */
    y += btn_h + gap;
    x = start_x;
    x += (btn_w + gap) * 5;  /* Skip to numpad */
    add_button(buttons, count, "4", x, y, btn_w, btn_h, BTN_4, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "5", x, y, btn_w, btn_h, BTN_5, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "6", x, y, btn_w, btn_h, BTN_6, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    
    /* Row 5: 1, 2, 3, - */
    y += btn_h + gap;
    x = start_x + (btn_w + gap) * 5;
    add_button(buttons, count, "1", x, y, btn_w, btn_h, BTN_1, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "2", x, y, btn_w, btn_h, BTN_2, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w + gap;
    add_button(buttons, count, "3", x, y, btn_w, btn_h, BTN_3, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    
    /* Row 6: 0, =, +, - */
    y += btn_h + gap;
    x = start_x + (btn_w + gap) * 5;
    add_button(buttons, count, "0", x, y, btn_w * 2 + gap, btn_h, BTN_0, COLOR_BTN_NUM, COLOR_TEXT_WHITE);
    x += btn_w * 2 + gap * 2;
    add_button(buttons, count, "=", x, y, btn_w, btn_h, BTN_EQUALS, COLOR_BTN_OP, COLOR_TEXT_WHITE);
}

/* ============ Drawing ============ */

void buttons_draw(void *ctx, calc_button_t *buttons, int count) {
    for (int i = 0; i < count; i++) {
        calc_button_t *btn = &buttons[i];
        
        uint32_t bg_color = btn->bg_color;
        if (btn->pressed) {
            bg_color = COLOR_BTN_PRESSED;
        } else if (btn->hover) {
            /* Lighten by 10% */
            uint8_t r = (bg_color >> 16) & 0xFF;
            uint8_t g = (bg_color >> 8) & 0xFF;
            uint8_t b = bg_color & 0xFF;
            r = r + (255 - r) / 5;
            g = g + (255 - g) / 5;
            b = b + (255 - b) / 5;
            bg_color = (r << 16) | (g << 8) | b;
        }
        
        /* Draw button background (rounded rectangle) */
        nx_fill_rounded(ctx, btn->x, btn->y, btn->width, btn->height, 8, bg_color);
        
        /* Draw label centered */
        int label_len = 0;
        while (btn->label[label_len]) label_len++;
        int text_x = btn->x + (btn->width - label_len * 10) / 2;
        int text_y = btn->y + (btn->height - 16) / 2;
        
        nx_draw_text(ctx, btn->label, text_x, text_y, btn->fg_color);
    }
}

/* ============ Hit Testing ============ */

int buttons_hit_test(calc_button_t *buttons, int count, int x, int y) {
    for (int i = 0; i < count; i++) {
        calc_button_t *btn = &buttons[i];
        if (x >= btn->x && x < btn->x + btn->width &&
            y >= btn->y && y < btn->y + btn->height) {
            return i;
        }
    }
    return -1;
}

void buttons_on_hover(calc_button_t *buttons, int count, int index) {
    for (int i = 0; i < count; i++) {
        buttons[i].hover = (i == index);
    }
}

void buttons_on_press(calc_button_t *buttons, int count, int index, int pressed) {
    (void)count;
    if (index >= 0) {
        buttons[index].pressed = pressed;
    }
}
