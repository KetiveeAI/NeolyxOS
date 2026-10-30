/*
 * NeolyxOS Calculator.app - Common Definitions
 * 
 * Shared types and definitions for the calculator application.
 * Supports basic, scientific, and programmer modes.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#ifndef CALC_H
#define CALC_H

#include <stdint.h>
#include <stddef.h>

/* ============ Configuration ============ */

#define CALC_MAX_DIGITS     32
#define CALC_MAX_HISTORY    100
#define CALC_PRECISION      15

/* ============ Calculator Modes ============ */

typedef enum {
    CALC_MODE_BASIC,
    CALC_MODE_SCIENTIFIC,
    CALC_MODE_PROGRAMMER
} calc_mode_t;

/* ============ Operations ============ */

typedef enum {
    OP_NONE = 0,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_POWER,
    OP_ROOT,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_NOT,
    OP_LSHIFT,
    OP_RSHIFT
} calc_op_t;

/* ============ Number Bases (Programmer Mode) ============ */

typedef enum {
    BASE_DECIMAL = 10,
    BASE_HEXADECIMAL = 16,
    BASE_OCTAL = 8,
    BASE_BINARY = 2
} calc_base_t;

/* ============ Angle Units (Scientific Mode) ============ */

typedef enum {
    ANGLE_DEGREES,
    ANGLE_RADIANS,
    ANGLE_GRADIANS
} calc_angle_t;

/* ============ Button Definition ============ */

typedef struct calc_button {
    char label[8];
    int x;
    int y;
    int width;
    int height;
    int action;
    int hover;
    int pressed;
    uint32_t bg_color;
    uint32_t fg_color;
} calc_button_t;

/* ============ History Entry ============ */

typedef struct calc_history_entry {
    char expression[128];
    char result[64];
    calc_mode_t mode;
} calc_history_entry_t;

/* ============ Display State ============ */

typedef struct calc_display {
    char current[CALC_MAX_DIGITS + 1];
    char expression[128];
    int current_len;
    int has_decimal;
    int has_error;
    char error_msg[64];
} calc_display_t;

/* ============ Calculator Engine ============ */

typedef struct calc_engine {
    double accumulator;
    double operand;
    double memory;
    calc_op_t pending_op;
    int new_number;
    int has_memory;
    calc_base_t base;
    calc_angle_t angle;
} calc_engine_t;

/* ============ Calculator Application ============ */

typedef struct calc_app {
    /* Window */
    void *window;
    int window_width;
    int window_height;
    
    /* Mode */
    calc_mode_t mode;
    
    /* Display */
    calc_display_t display;
    
    /* Engine */
    calc_engine_t engine;
    
    /* Buttons */
    calc_button_t buttons[64];
    int button_count;
    
    /* History */
    calc_history_entry_t history[CALC_MAX_HISTORY];
    int history_count;
    int history_index;
    
    /* State */
    int running;
    int needs_redraw;
} calc_app_t;

/* ============ Function Prototypes ============ */

/* Engine functions (engine.c) */
void engine_init(calc_engine_t *engine);
void engine_input_digit(calc_engine_t *engine, calc_display_t *display, int digit);
void engine_input_decimal(calc_engine_t *engine, calc_display_t *display);
void engine_input_operator(calc_engine_t *engine, calc_display_t *display, calc_op_t op);
void engine_calculate(calc_engine_t *engine, calc_display_t *display);
void engine_clear(calc_engine_t *engine, calc_display_t *display);
void engine_clear_entry(calc_display_t *display);
void engine_backspace(calc_display_t *display);
void engine_negate(calc_engine_t *engine, calc_display_t *display);
void engine_percent(calc_engine_t *engine, calc_display_t *display);
void engine_memory_store(calc_engine_t *engine, calc_display_t *display);
void engine_memory_recall(calc_engine_t *engine, calc_display_t *display);
void engine_memory_clear(calc_engine_t *engine);
void engine_memory_add(calc_engine_t *engine, calc_display_t *display);
void engine_memory_subtract(calc_engine_t *engine, calc_display_t *display);
double engine_get_result(calc_engine_t *engine, calc_display_t *display);

/* Scientific functions (scientific.c) */
void sci_sin(calc_engine_t *engine, calc_display_t *display);
void sci_cos(calc_engine_t *engine, calc_display_t *display);
void sci_tan(calc_engine_t *engine, calc_display_t *display);
void sci_asin(calc_engine_t *engine, calc_display_t *display);
void sci_acos(calc_engine_t *engine, calc_display_t *display);
void sci_atan(calc_engine_t *engine, calc_display_t *display);
void sci_sinh(calc_engine_t *engine, calc_display_t *display);
void sci_cosh(calc_engine_t *engine, calc_display_t *display);
void sci_tanh(calc_engine_t *engine, calc_display_t *display);
void sci_log(calc_engine_t *engine, calc_display_t *display);
void sci_ln(calc_engine_t *engine, calc_display_t *display);
void sci_log2(calc_engine_t *engine, calc_display_t *display);
void sci_exp(calc_engine_t *engine, calc_display_t *display);
void sci_sqrt(calc_engine_t *engine, calc_display_t *display);
void sci_cbrt(calc_engine_t *engine, calc_display_t *display);
void sci_factorial(calc_engine_t *engine, calc_display_t *display);
void sci_reciprocal(calc_engine_t *engine, calc_display_t *display);
void sci_square(calc_engine_t *engine, calc_display_t *display);
void sci_cube(calc_engine_t *engine, calc_display_t *display);
void sci_power_of_10(calc_engine_t *engine, calc_display_t *display);
void sci_power_of_e(calc_engine_t *engine, calc_display_t *display);
void sci_pi(calc_engine_t *engine, calc_display_t *display);
void sci_e(calc_engine_t *engine, calc_display_t *display);
void sci_abs(calc_engine_t *engine, calc_display_t *display);
void sci_floor(calc_engine_t *engine, calc_display_t *display);
void sci_ceil(calc_engine_t *engine, calc_display_t *display);
void sci_round(calc_engine_t *engine, calc_display_t *display);
void sci_rand(calc_engine_t *engine, calc_display_t *display);

/* Display functions (display.c) */
void display_init(calc_display_t *display);
void display_draw(void *ctx, calc_display_t *display, int x, int y, int width, int height);
void display_set_value(calc_display_t *display, double value, calc_base_t base);
void display_set_error(calc_display_t *display, const char *msg);
void display_format_number(char *buf, int buflen, double value, calc_base_t base);

/* Button functions (buttons.c) */
void buttons_init_basic(calc_button_t *buttons, int *count, int start_x, int start_y, int btn_w, int btn_h, int gap);
void buttons_init_scientific(calc_button_t *buttons, int *count, int start_x, int start_y, int btn_w, int btn_h, int gap);
void buttons_init_programmer(calc_button_t *buttons, int *count, int start_x, int start_y, int btn_w, int btn_h, int gap);
void buttons_draw(void *ctx, calc_button_t *buttons, int count);
int buttons_hit_test(calc_button_t *buttons, int count, int x, int y);
void buttons_on_hover(calc_button_t *buttons, int count, int index);
void buttons_on_press(calc_button_t *buttons, int count, int index, int pressed);

/* History functions (history.c) */
void history_init(calc_app_t *app);
void history_add(calc_app_t *app, const char *expression, const char *result);
const char* history_get_previous(calc_app_t *app);
const char* history_get_next(calc_app_t *app);
void history_clear(calc_app_t *app);

#endif /* CALC_H */
