/*
 * NeolyxOS Calculator.app - Calculation Engine
 * 
 * Core calculation logic for all operations.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "calc.h"

/* ============ String to Number ============ */

static double str_to_double(const char *s) {
    double result = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;
    int negative = 0;
    int decimal = 0;
    
    if (*s == '-') {
        negative = 1;
        s++;
    }
    
    while (*s) {
        if (*s == '.') {
            decimal = 1;
        } else if (*s >= '0' && *s <= '9') {
            if (decimal) {
                divisor *= 10.0;
                fraction += (*s - '0') / divisor;
            } else {
                result = result * 10.0 + (*s - '0');
            }
        }
        s++;
    }
    
    result += fraction;
    return negative ? -result : result;
}

/* ============ Number to String ============ */

static void double_to_str(char *buf, int buflen, double value) {
    if (buflen < 2) return;
    
    int idx = 0;
    
    /* Handle negative */
    if (value < 0) {
        buf[idx++] = '-';
        value = -value;
    }
    
    /* Integer part */
    int64_t int_part = (int64_t)value;
    double frac_part = value - (double)int_part;
    
    /* Convert integer part */
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
    
    /* Reverse integer digits */
    for (int i = int_idx - 1; i >= 0 && idx < buflen - 1; i--) {
        buf[idx++] = int_buf[i];
    }
    
    /* Fractional part */
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
        
        /* Remove trailing zeros */
        while (idx > 1 && buf[idx - 1] == '0' && buf[idx - 2] != '.') {
            idx--;
        }
    }
    
    buf[idx] = '\0';
}

/* ============ Engine Implementation ============ */

void engine_init(calc_engine_t *engine) {
    engine->accumulator = 0.0;
    engine->operand = 0.0;
    engine->memory = 0.0;
    engine->pending_op = OP_NONE;
    engine->new_number = 1;
    engine->has_memory = 0;
    engine->base = BASE_DECIMAL;
    engine->angle = ANGLE_DEGREES;
}

void engine_input_digit(calc_engine_t *engine, calc_display_t *display, int digit) {
    if (display->has_error) {
        display->has_error = 0;
        display->current[0] = '\0';
        display->current_len = 0;
    }
    
    if (engine->new_number) {
        display->current[0] = '\0';
        display->current_len = 0;
        display->has_decimal = 0;
        engine->new_number = 0;
    }
    
    if (display->current_len >= CALC_MAX_DIGITS) return;
    
    /* Convert digit to character */
    char c;
    if (digit < 10) {
        c = '0' + digit;
    } else {
        c = 'A' + (digit - 10);
    }
    
    /* Handle leading zero */
    if (display->current_len == 1 && display->current[0] == '0' && !display->has_decimal) {
        display->current[0] = c;
        return;
    }
    
    display->current[display->current_len++] = c;
    display->current[display->current_len] = '\0';
}

void engine_input_decimal(calc_engine_t *engine, calc_display_t *display) {
    if (display->has_decimal) return;
    
    if (engine->new_number) {
        display->current[0] = '0';
        display->current[1] = '.';
        display->current[2] = '\0';
        display->current_len = 2;
        engine->new_number = 0;
    } else {
        if (display->current_len >= CALC_MAX_DIGITS) return;
        display->current[display->current_len++] = '.';
        display->current[display->current_len] = '\0';
    }
    
    display->has_decimal = 1;
}

void engine_input_operator(calc_engine_t *engine, calc_display_t *display, calc_op_t op) {
    if (engine->pending_op != OP_NONE && !engine->new_number) {
        engine_calculate(engine, display);
    }
    
    engine->accumulator = str_to_double(display->current);
    engine->pending_op = op;
    engine->new_number = 1;
}

void engine_calculate(calc_engine_t *engine, calc_display_t *display) {
    if (engine->pending_op == OP_NONE) return;
    
    double operand = str_to_double(display->current);
    double result = engine->accumulator;
    int error = 0;
    
    switch (engine->pending_op) {
        case OP_ADD:
            result = engine->accumulator + operand;
            break;
        case OP_SUBTRACT:
            result = engine->accumulator - operand;
            break;
        case OP_MULTIPLY:
            result = engine->accumulator * operand;
            break;
        case OP_DIVIDE:
            if (operand == 0.0) {
                display_set_error(display, "Error: Division by zero");
                error = 1;
            } else {
                result = engine->accumulator / operand;
            }
            break;
        case OP_MODULO:
            if (operand == 0.0) {
                display_set_error(display, "Error: Division by zero");
                error = 1;
            } else {
                int64_t a = (int64_t)engine->accumulator;
                int64_t b = (int64_t)operand;
                result = (double)(a % b);
            }
            break;
        case OP_POWER: {
            /* Simple integer power for now */
            double base = engine->accumulator;
            int exp = (int)operand;
            result = 1.0;
            if (exp >= 0) {
                for (int i = 0; i < exp; i++) {
                    result *= base;
                }
            } else {
                for (int i = 0; i < -exp; i++) {
                    result /= base;
                }
            }
            break;
        }
        case OP_AND: {
            int64_t a = (int64_t)engine->accumulator;
            int64_t b = (int64_t)operand;
            result = (double)(a & b);
            break;
        }
        case OP_OR: {
            int64_t a = (int64_t)engine->accumulator;
            int64_t b = (int64_t)operand;
            result = (double)(a | b);
            break;
        }
        case OP_XOR: {
            int64_t a = (int64_t)engine->accumulator;
            int64_t b = (int64_t)operand;
            result = (double)(a ^ b);
            break;
        }
        case OP_LSHIFT: {
            int64_t a = (int64_t)engine->accumulator;
            int shift = (int)operand;
            result = (double)(a << shift);
            break;
        }
        case OP_RSHIFT: {
            int64_t a = (int64_t)engine->accumulator;
            int shift = (int)operand;
            result = (double)(a >> shift);
            break;
        }
        default:
            break;
    }
    
    if (!error) {
        double_to_str(display->current, CALC_MAX_DIGITS + 1, result);
        display->current_len = 0;
        while (display->current[display->current_len]) display->current_len++;
        engine->accumulator = result;
    }
    
    engine->pending_op = OP_NONE;
    engine->new_number = 1;
}

void engine_clear(calc_engine_t *engine, calc_display_t *display) {
    engine->accumulator = 0.0;
    engine->operand = 0.0;
    engine->pending_op = OP_NONE;
    engine->new_number = 1;
    
    display->current[0] = '0';
    display->current[1] = '\0';
    display->current_len = 1;
    display->has_decimal = 0;
    display->has_error = 0;
    display->expression[0] = '\0';
}

void engine_clear_entry(calc_display_t *display) {
    display->current[0] = '0';
    display->current[1] = '\0';
    display->current_len = 1;
    display->has_decimal = 0;
    display->has_error = 0;
}

void engine_backspace(calc_display_t *display) {
    if (display->has_error) {
        display->has_error = 0;
        display->current[0] = '0';
        display->current[1] = '\0';
        display->current_len = 1;
        return;
    }
    
    if (display->current_len > 0) {
        display->current_len--;
        if (display->current[display->current_len] == '.') {
            display->has_decimal = 0;
        }
        display->current[display->current_len] = '\0';
    }
    
    if (display->current_len == 0) {
        display->current[0] = '0';
        display->current[1] = '\0';
        display->current_len = 1;
    }
}

void engine_negate(calc_engine_t *engine, calc_display_t *display) {
    (void)engine;
    
    if (display->has_error) return;
    if (display->current[0] == '0' && display->current_len == 1) return;
    
    if (display->current[0] == '-') {
        /* Remove negative sign */
        for (int i = 0; i < display->current_len; i++) {
            display->current[i] = display->current[i + 1];
        }
        display->current_len--;
    } else {
        /* Add negative sign */
        if (display->current_len >= CALC_MAX_DIGITS) return;
        for (int i = display->current_len; i >= 0; i--) {
            display->current[i + 1] = display->current[i];
        }
        display->current[0] = '-';
        display->current_len++;
    }
}

void engine_percent(calc_engine_t *engine, calc_display_t *display) {
    double value = str_to_double(display->current);
    
    if (engine->pending_op == OP_ADD || engine->pending_op == OP_SUBTRACT) {
        /* Percentage of accumulator */
        value = engine->accumulator * (value / 100.0);
    } else {
        /* Simple percentage */
        value /= 100.0;
    }
    
    double_to_str(display->current, CALC_MAX_DIGITS + 1, value);
    display->current_len = 0;
    while (display->current[display->current_len]) display->current_len++;
}

void engine_memory_store(calc_engine_t *engine, calc_display_t *display) {
    engine->memory = str_to_double(display->current);
    engine->has_memory = 1;
}

void engine_memory_recall(calc_engine_t *engine, calc_display_t *display) {
    if (!engine->has_memory) return;
    
    double_to_str(display->current, CALC_MAX_DIGITS + 1, engine->memory);
    display->current_len = 0;
    while (display->current[display->current_len]) display->current_len++;
    engine->new_number = 1;
}

void engine_memory_clear(calc_engine_t *engine) {
    engine->memory = 0.0;
    engine->has_memory = 0;
}

void engine_memory_add(calc_engine_t *engine, calc_display_t *display) {
    engine->memory += str_to_double(display->current);
    engine->has_memory = 1;
}

void engine_memory_subtract(calc_engine_t *engine, calc_display_t *display) {
    engine->memory -= str_to_double(display->current);
    engine->has_memory = 1;
}

double engine_get_result(calc_engine_t *engine, calc_display_t *display) {
    (void)engine;
    return str_to_double(display->current);
}
