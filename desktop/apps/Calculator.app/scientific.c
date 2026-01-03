/*
 * NeolyxOS Calculator.app - Scientific Functions
 * 
 * Trigonometric, logarithmic, and other scientific operations.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "calc.h"

/* ============ Math Constants ============ */

#define PI          3.14159265358979323846
#define E_CONST     2.71828182845904523536

/* ============ Helper Functions ============ */

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

static void double_to_str(char *buf, int buflen, double value) {
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
}

static double deg_to_rad(double deg) {
    return deg * PI / 180.0;
}

static double rad_to_deg(double rad) {
    return rad * 180.0 / PI;
}

static double get_value(calc_engine_t *engine, calc_display_t *display) {
    (void)engine;
    return str_to_double(display->current);
}

static void set_value(calc_engine_t *engine, calc_display_t *display, double value) {
    double_to_str(display->current, CALC_MAX_DIGITS + 1, value);
    display->current_len = 0;
    while (display->current[display->current_len]) display->current_len++;
    engine->new_number = 1;
}

/* ============ Factorial ============ */

static double factorial(int n) {
    if (n < 0) return 0.0;
    if (n <= 1) return 1.0;
    double result = 1.0;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

/* ============ Power and Root ============ */

static double power(double base, double exp) {
    if (exp == 0.0) return 1.0;
    if (base == 0.0) return 0.0;
    
    int int_exp = (int)exp;
    if ((double)int_exp == exp) {
        double result = 1.0;
        int neg = int_exp < 0;
        if (neg) int_exp = -int_exp;
        for (int i = 0; i < int_exp; i++) {
            result *= base;
        }
        return neg ? 1.0 / result : result;
    }
    
    /* Use exp(exp * ln(base)) for non-integer exponents */
    /* Simplified: only handle integer exponents for now */
    return 0.0;
}

static double sqrt_approx(double x) {
    if (x < 0) return 0.0;
    if (x == 0) return 0.0;
    
    double guess = x / 2.0;
    for (int i = 0; i < 20; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

static double cbrt_approx(double x) {
    if (x == 0) return 0.0;
    
    int negative = x < 0;
    if (negative) x = -x;
    
    double guess = x / 3.0;
    for (int i = 0; i < 20; i++) {
        guess = (2.0 * guess + x / (guess * guess)) / 3.0;
    }
    
    return negative ? -guess : guess;
}

/* ============ Trigonometric Functions ============ */

/* Taylor series approximations for sin and cos */

static double sin_rad(double x) {
    /* Normalize to [-PI, PI] */
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    
    double term = x;
    double sum = x;
    double x2 = x * x;
    
    for (int n = 1; n < 15; n++) {
        term *= -x2 / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    
    return sum;
}

static double cos_rad(double x) {
    /* Normalize to [-PI, PI] */
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    
    double term = 1.0;
    double sum = 1.0;
    double x2 = x * x;
    
    for (int n = 1; n < 15; n++) {
        term *= -x2 / ((2 * n - 1) * (2 * n));
        sum += term;
    }
    
    return sum;
}

/* ============ Logarithmic Functions ============ */

static double ln_approx(double x) {
    if (x <= 0) return 0.0;
    
    /* Use ln(x) = 2 * sum((y^(2n+1))/(2n+1)) where y = (x-1)/(x+1) */
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double sum = y;
    double term = y;
    
    for (int n = 1; n < 30; n++) {
        term *= y2;
        sum += term / (2 * n + 1);
    }
    
    return 2.0 * sum;
}

static double log10_approx(double x) {
    return ln_approx(x) / ln_approx(10.0);
}

static double log2_approx(double x) {
    return ln_approx(x) / ln_approx(2.0);
}

/* ============ Exponential Function ============ */

static double exp_approx(double x) {
    /* Taylor series: e^x = sum(x^n / n!) */
    double sum = 1.0;
    double term = 1.0;
    
    for (int n = 1; n < 30; n++) {
        term *= x / n;
        sum += term;
    }
    
    return sum;
}

/* ============ Hyperbolic Functions ============ */

static double sinh_calc(double x) {
    return (exp_approx(x) - exp_approx(-x)) / 2.0;
}

static double cosh_calc(double x) {
    return (exp_approx(x) + exp_approx(-x)) / 2.0;
}

static double tanh_calc(double x) {
    double ex = exp_approx(x);
    double emx = exp_approx(-x);
    return (ex - emx) / (ex + emx);
}

/* ============ Inverse Trigonometric Functions ============ */

static double atan_approx(double x) {
    /* For |x| < 1, use Taylor series */
    if (x > 1.0) return PI / 2 - atan_approx(1.0 / x);
    if (x < -1.0) return -PI / 2 - atan_approx(1.0 / x);
    
    double sum = x;
    double term = x;
    double x2 = x * x;
    
    for (int n = 1; n < 30; n++) {
        term *= -x2;
        sum += term / (2 * n + 1);
    }
    
    return sum;
}

static double asin_approx(double x) {
    if (x < -1.0 || x > 1.0) return 0.0;
    return atan_approx(x / sqrt_approx(1.0 - x * x));
}

static double acos_approx(double x) {
    return PI / 2 - asin_approx(x);
}

/* ============ Random Number Generator ============ */

static unsigned int rand_seed = 12345;

static double random_value(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (double)(rand_seed % 1000000) / 1000000.0;
}

/* ============ Scientific Function Implementations ============ */

void sci_sin(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (engine->angle == ANGLE_DEGREES) x = deg_to_rad(x);
    set_value(engine, display, sin_rad(x));
}

void sci_cos(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (engine->angle == ANGLE_DEGREES) x = deg_to_rad(x);
    set_value(engine, display, cos_rad(x));
}

void sci_tan(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (engine->angle == ANGLE_DEGREES) x = deg_to_rad(x);
    double c = cos_rad(x);
    if (c == 0.0) {
        display_set_error(display, "Error: Undefined");
        return;
    }
    set_value(engine, display, sin_rad(x) / c);
}

void sci_asin(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x < -1.0 || x > 1.0) {
        display_set_error(display, "Error: Domain");
        return;
    }
    double result = asin_approx(x);
    if (engine->angle == ANGLE_DEGREES) result = rad_to_deg(result);
    set_value(engine, display, result);
}

void sci_acos(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x < -1.0 || x > 1.0) {
        display_set_error(display, "Error: Domain");
        return;
    }
    double result = acos_approx(x);
    if (engine->angle == ANGLE_DEGREES) result = rad_to_deg(result);
    set_value(engine, display, result);
}

void sci_atan(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    double result = atan_approx(x);
    if (engine->angle == ANGLE_DEGREES) result = rad_to_deg(result);
    set_value(engine, display, result);
}

void sci_sinh(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, sinh_calc(x));
}

void sci_cosh(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, cosh_calc(x));
}

void sci_tanh(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, tanh_calc(x));
}

void sci_log(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x <= 0) {
        display_set_error(display, "Error: Domain");
        return;
    }
    set_value(engine, display, log10_approx(x));
}

void sci_ln(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x <= 0) {
        display_set_error(display, "Error: Domain");
        return;
    }
    set_value(engine, display, ln_approx(x));
}

void sci_log2(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x <= 0) {
        display_set_error(display, "Error: Domain");
        return;
    }
    set_value(engine, display, log2_approx(x));
}

void sci_exp(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, exp_approx(x));
}

void sci_sqrt(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x < 0) {
        display_set_error(display, "Error: Negative");
        return;
    }
    set_value(engine, display, sqrt_approx(x));
}

void sci_cbrt(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, cbrt_approx(x));
}

void sci_factorial(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    int n = (int)x;
    if (n < 0 || n > 170) {
        display_set_error(display, "Error: Overflow");
        return;
    }
    set_value(engine, display, factorial(n));
}

void sci_reciprocal(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    if (x == 0.0) {
        display_set_error(display, "Error: Division by zero");
        return;
    }
    set_value(engine, display, 1.0 / x);
}

void sci_square(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, x * x);
}

void sci_cube(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, x * x * x);
}

void sci_power_of_10(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, power(10.0, x));
}

void sci_power_of_e(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, exp_approx(x));
}

void sci_pi(calc_engine_t *engine, calc_display_t *display) {
    set_value(engine, display, PI);
}

void sci_e(calc_engine_t *engine, calc_display_t *display) {
    set_value(engine, display, E_CONST);
}

void sci_abs(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, x < 0 ? -x : x);
}

void sci_floor(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    int64_t i = (int64_t)x;
    if (x < 0 && x != (double)i) i--;
    set_value(engine, display, (double)i);
}

void sci_ceil(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    int64_t i = (int64_t)x;
    if (x > 0 && x != (double)i) i++;
    set_value(engine, display, (double)i);
}

void sci_round(calc_engine_t *engine, calc_display_t *display) {
    double x = get_value(engine, display);
    set_value(engine, display, (double)((int64_t)(x + (x >= 0 ? 0.5 : -0.5))));
}

void sci_rand(calc_engine_t *engine, calc_display_t *display) {
    set_value(engine, display, random_value());
}
