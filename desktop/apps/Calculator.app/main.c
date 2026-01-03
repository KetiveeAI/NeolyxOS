/*
 * NeolyxOS Calculator.app - Main Entry Point
 * 
 * Desktop calculator with basic, scientific, and programmer modes.
 * Uses NXRender for native NeolyxOS look.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"
#include "calc.h"
#include "nxaudio_feedback.h"

/* ============ Color Palette ============ */

#define COLOR_BG_DARK       0x1C1C1E
#define COLOR_BG_DISPLAY    0x000000
#define COLOR_TEXT          0xFFFFFF
#define COLOR_TEXT_DIM      0x8E8E93
#define COLOR_BTN_NUMBER    0x333333
#define COLOR_BTN_OP        0xFF9F0A
#define COLOR_BTN_FUNC      0x505050
#define COLOR_BTN_HOVER     0x444444
#define COLOR_BTN_PRESSED   0x555555

/* ============ Layout Constants ============ */

#define WINDOW_WIDTH_BASIC      280
#define WINDOW_WIDTH_SCIENTIFIC 520
#define WINDOW_WIDTH_PROGRAMMER 520
#define WINDOW_HEIGHT           480

#define DISPLAY_HEIGHT          100
#define BUTTON_GAP              4
#define BUTTON_MARGIN           12

/* ============ Button Actions ============ */

typedef enum {
    BTN_0 = 0, BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8, BTN_9,
    BTN_DOT,
    BTN_ADD, BTN_SUB, BTN_MUL, BTN_DIV,
    BTN_EQUALS, BTN_CLEAR, BTN_CLEAR_ENTRY, BTN_BACKSPACE,
    BTN_NEGATE, BTN_PERCENT,
    BTN_MEM_CLEAR, BTN_MEM_RECALL, BTN_MEM_ADD, BTN_MEM_SUB, BTN_MEM_STORE,
    BTN_SIN, BTN_COS, BTN_TAN, BTN_ASIN, BTN_ACOS, BTN_ATAN,
    BTN_SINH, BTN_COSH, BTN_TANH,
    BTN_LOG, BTN_LN, BTN_LOG2,
    BTN_EXP, BTN_SQRT, BTN_CBRT,
    BTN_FACTORIAL, BTN_RECIPROCAL,
    BTN_SQUARE, BTN_CUBE,
    BTN_POWER, BTN_ROOT,
    BTN_POW_10, BTN_POW_E,
    BTN_PI, BTN_E_CONST,
    BTN_ABS, BTN_FLOOR, BTN_CEIL, BTN_ROUND,
    BTN_MOD, BTN_RAND,
    BTN_AND, BTN_OR, BTN_XOR, BTN_NOT,
    BTN_LSHIFT, BTN_RSHIFT,
    BTN_HEX, BTN_DEC, BTN_OCT, BTN_BIN,
    BTN_A, BTN_B, BTN_C_HEX, BTN_D, BTN_E_HEX, BTN_F,
    BTN_PAREN_OPEN, BTN_PAREN_CLOSE
} button_action_t;

/* ============ Global Application State ============ */

static calc_app_t g_app;

/* ============ String Helpers ============ */

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Application Functions ============ */

static int calc_app_init(calc_app_t *app) {
    /* Zero initialize */
    for (int i = 0; i < (int)sizeof(*app); i++) {
        ((char *)app)[i] = 0;
    }
    
    /* Set defaults */
    app->mode = CALC_MODE_BASIC;
    app->window_width = WINDOW_WIDTH_BASIC;
    app->window_height = WINDOW_HEIGHT;
    app->running = 1;
    app->needs_redraw = 1;
    
    /* Initialize components */
    display_init(&app->display);
    engine_init(&app->engine);
    history_init(app);
    
    /* Initialize buttons for basic mode */
    int btn_area_y = DISPLAY_HEIGHT + BUTTON_MARGIN;
    int btn_area_h = app->window_height - btn_area_y - BUTTON_MARGIN;
    int btn_w = (app->window_width - BUTTON_MARGIN * 2 - BUTTON_GAP * 3) / 4;
    int btn_h = (btn_area_h - BUTTON_GAP * 4) / 5;
    
    buttons_init_basic(app->buttons, &app->button_count,
                       BUTTON_MARGIN, btn_area_y,
                       btn_w, btn_h, BUTTON_GAP);
    
    return 0;
}

static void calc_set_mode(calc_app_t *app, calc_mode_t mode) {
    if (app->mode == mode) return;
    
    app->mode = mode;
    
    /* Update window size and buttons based on mode */
    int btn_area_y = DISPLAY_HEIGHT + BUTTON_MARGIN;
    int btn_w, btn_h;
    
    switch (mode) {
        case CALC_MODE_BASIC:
            app->window_width = WINDOW_WIDTH_BASIC;
            btn_w = (app->window_width - BUTTON_MARGIN * 2 - BUTTON_GAP * 3) / 4;
            btn_h = (app->window_height - btn_area_y - BUTTON_MARGIN - BUTTON_GAP * 4) / 5;
            buttons_init_basic(app->buttons, &app->button_count,
                               BUTTON_MARGIN, btn_area_y, btn_w, btn_h, BUTTON_GAP);
            break;
            
        case CALC_MODE_SCIENTIFIC:
            app->window_width = WINDOW_WIDTH_SCIENTIFIC;
            btn_w = (app->window_width - BUTTON_MARGIN * 2 - BUTTON_GAP * 9) / 10;
            btn_h = (app->window_height - btn_area_y - BUTTON_MARGIN - BUTTON_GAP * 5) / 6;
            buttons_init_scientific(app->buttons, &app->button_count,
                                    BUTTON_MARGIN, btn_area_y, btn_w, btn_h, BUTTON_GAP);
            break;
            
        case CALC_MODE_PROGRAMMER:
            app->window_width = WINDOW_WIDTH_PROGRAMMER;
            btn_w = (app->window_width - BUTTON_MARGIN * 2 - BUTTON_GAP * 7) / 8;
            btn_h = (app->window_height - btn_area_y - BUTTON_MARGIN - BUTTON_GAP * 6) / 7;
            buttons_init_programmer(app->buttons, &app->button_count,
                                    BUTTON_MARGIN, btn_area_y, btn_w, btn_h, BUTTON_GAP);
            break;
    }
    
    app->needs_redraw = 1;
}

static void calc_on_button(calc_app_t *app, int action) {
    calc_engine_t *eng = &app->engine;
    calc_display_t *disp = &app->display;
    
    /* Play audio feedback based on button type */
    if (action >= BTN_0 && action <= BTN_DOT) {
        audio_play(AUDIO_CLICK_NUMBER);
        haptic_trigger(HAPTIC_LIGHT);
    } else if (action >= BTN_ADD && action <= BTN_DIV) {
        audio_play(AUDIO_CLICK_OPERATOR);
        haptic_trigger(HAPTIC_MEDIUM);
    } else if (action == BTN_EQUALS) {
        audio_play(AUDIO_RESULT);
        haptic_trigger(HAPTIC_SUCCESS);
    } else if (action == BTN_CLEAR) {
        audio_play(AUDIO_CLEAR);
        haptic_trigger(HAPTIC_MEDIUM);
    } else if (action >= BTN_MEM_CLEAR && action <= BTN_MEM_STORE) {
        audio_play(AUDIO_MEMORY);
        haptic_trigger(HAPTIC_LIGHT);
    } else {
        audio_play(AUDIO_CLICK_FUNCTION);
        haptic_trigger(HAPTIC_LIGHT);
    }
    
    switch (action) {
        /* Digits */
        case BTN_0: engine_input_digit(eng, disp, 0); break;
        case BTN_1: engine_input_digit(eng, disp, 1); break;
        case BTN_2: engine_input_digit(eng, disp, 2); break;
        case BTN_3: engine_input_digit(eng, disp, 3); break;
        case BTN_4: engine_input_digit(eng, disp, 4); break;
        case BTN_5: engine_input_digit(eng, disp, 5); break;
        case BTN_6: engine_input_digit(eng, disp, 6); break;
        case BTN_7: engine_input_digit(eng, disp, 7); break;
        case BTN_8: engine_input_digit(eng, disp, 8); break;
        case BTN_9: engine_input_digit(eng, disp, 9); break;
        case BTN_DOT: engine_input_decimal(eng, disp); break;
        
        /* Basic operators */
        case BTN_ADD: engine_input_operator(eng, disp, OP_ADD); break;
        case BTN_SUB: engine_input_operator(eng, disp, OP_SUBTRACT); break;
        case BTN_MUL: engine_input_operator(eng, disp, OP_MULTIPLY); break;
        case BTN_DIV: engine_input_operator(eng, disp, OP_DIVIDE); break;
        case BTN_EQUALS:
            engine_calculate(eng, disp);
            if (disp->has_error) {
                audio_play(AUDIO_ERROR);
                haptic_trigger(HAPTIC_ERROR);
            }
            history_add(app, disp->expression, disp->current);
            break;
        
        /* Clear operations */
        case BTN_CLEAR: engine_clear(eng, disp); break;
        case BTN_CLEAR_ENTRY: engine_clear_entry(disp); break;
        case BTN_BACKSPACE: engine_backspace(disp); break;
        
        /* Unary operations */
        case BTN_NEGATE: engine_negate(eng, disp); break;
        case BTN_PERCENT: engine_percent(eng, disp); break;
        
        /* Memory operations */
        case BTN_MEM_CLEAR: engine_memory_clear(eng); break;
        case BTN_MEM_RECALL: engine_memory_recall(eng, disp); break;
        case BTN_MEM_ADD: engine_memory_add(eng, disp); break;
        case BTN_MEM_SUB: engine_memory_subtract(eng, disp); break;
        case BTN_MEM_STORE: engine_memory_store(eng, disp); break;
        
        /* Scientific functions */
        case BTN_SIN: sci_sin(eng, disp); break;
        case BTN_COS: sci_cos(eng, disp); break;
        case BTN_TAN: sci_tan(eng, disp); break;
        case BTN_ASIN: sci_asin(eng, disp); break;
        case BTN_ACOS: sci_acos(eng, disp); break;
        case BTN_ATAN: sci_atan(eng, disp); break;
        case BTN_SINH: sci_sinh(eng, disp); break;
        case BTN_COSH: sci_cosh(eng, disp); break;
        case BTN_TANH: sci_tanh(eng, disp); break;
        case BTN_LOG: sci_log(eng, disp); break;
        case BTN_LN: sci_ln(eng, disp); break;
        case BTN_LOG2: sci_log2(eng, disp); break;
        case BTN_EXP: sci_exp(eng, disp); break;
        case BTN_SQRT: sci_sqrt(eng, disp); break;
        case BTN_CBRT: sci_cbrt(eng, disp); break;
        case BTN_FACTORIAL: sci_factorial(eng, disp); break;
        case BTN_RECIPROCAL: sci_reciprocal(eng, disp); break;
        case BTN_SQUARE: sci_square(eng, disp); break;
        case BTN_CUBE: sci_cube(eng, disp); break;
        case BTN_POW_10: sci_power_of_10(eng, disp); break;
        case BTN_POW_E: sci_power_of_e(eng, disp); break;
        case BTN_PI: sci_pi(eng, disp); break;
        case BTN_E_CONST: sci_e(eng, disp); break;
        case BTN_ABS: sci_abs(eng, disp); break;
        case BTN_FLOOR: sci_floor(eng, disp); break;
        case BTN_CEIL: sci_ceil(eng, disp); break;
        case BTN_ROUND: sci_round(eng, disp); break;
        case BTN_RAND: sci_rand(eng, disp); break;
        
        /* Programmer operations */
        case BTN_POWER: engine_input_operator(eng, disp, OP_POWER); break;
        case BTN_MOD: engine_input_operator(eng, disp, OP_MODULO); break;
        case BTN_AND: engine_input_operator(eng, disp, OP_AND); break;
        case BTN_OR: engine_input_operator(eng, disp, OP_OR); break;
        case BTN_XOR: engine_input_operator(eng, disp, OP_XOR); break;
        case BTN_LSHIFT: engine_input_operator(eng, disp, OP_LSHIFT); break;
        case BTN_RSHIFT: engine_input_operator(eng, disp, OP_RSHIFT); break;
        
        /* Base changes */
        case BTN_HEX: eng->base = BASE_HEXADECIMAL; display_set_value(disp, eng->accumulator, eng->base); break;
        case BTN_DEC: eng->base = BASE_DECIMAL; display_set_value(disp, eng->accumulator, eng->base); break;
        case BTN_OCT: eng->base = BASE_OCTAL; display_set_value(disp, eng->accumulator, eng->base); break;
        case BTN_BIN: eng->base = BASE_BINARY; display_set_value(disp, eng->accumulator, eng->base); break;
        
        /* Hex digits */
        case BTN_A: engine_input_digit(eng, disp, 10); break;
        case BTN_B: engine_input_digit(eng, disp, 11); break;
        case BTN_C_HEX: engine_input_digit(eng, disp, 12); break;
        case BTN_D: engine_input_digit(eng, disp, 13); break;
        case BTN_E_HEX: engine_input_digit(eng, disp, 14); break;
        case BTN_F: engine_input_digit(eng, disp, 15); break;
    }
    
    app->needs_redraw = 1;
}

static void calc_on_key(calc_app_t *app, uint16_t keycode, uint16_t mods) {
    (void)mods;
    
    switch (keycode) {
        case NX_KEY_0: calc_on_button(app, BTN_0); break;
        case NX_KEY_1: calc_on_button(app, BTN_1); break;
        case NX_KEY_2: calc_on_button(app, BTN_2); break;
        case NX_KEY_3: calc_on_button(app, BTN_3); break;
        case NX_KEY_4: calc_on_button(app, BTN_4); break;
        case NX_KEY_5: calc_on_button(app, BTN_5); break;
        case NX_KEY_6: calc_on_button(app, BTN_6); break;
        case NX_KEY_7: calc_on_button(app, BTN_7); break;
        case NX_KEY_8: calc_on_button(app, BTN_8); break;
        case NX_KEY_9: calc_on_button(app, BTN_9); break;
        case NX_KEY_DOT: calc_on_button(app, BTN_DOT); break;
        case NX_KEY_PLUS: calc_on_button(app, BTN_ADD); break;
        case NX_KEY_MINUS: calc_on_button(app, BTN_SUB); break;
        case NX_KEY_STAR: calc_on_button(app, BTN_MUL); break;
        case NX_KEY_SLASH: calc_on_button(app, BTN_DIV); break;
        case NX_KEY_ENTER: calc_on_button(app, BTN_EQUALS); break;
        case NX_KEY_ESCAPE: calc_on_button(app, BTN_CLEAR); break;
        case NX_KEY_BACKSPACE: calc_on_button(app, BTN_BACKSPACE); break;
        case NX_KEY_PERCENT: calc_on_button(app, BTN_PERCENT); break;
    }
}

static void calc_on_mouse(calc_app_t *app, int x, int y, int button, int action) {
    if (button != 0) return;
    
    if (action == 1) {  /* Mouse down */
        int hit = buttons_hit_test(app->buttons, app->button_count, x, y);
        if (hit >= 0 && hit < app->button_count) {
            buttons_on_press(app->buttons, app->button_count, hit, 1);
            app->needs_redraw = 1;
        }
    } else if (action == 0) {  /* Mouse up */
        int hit = buttons_hit_test(app->buttons, app->button_count, x, y);
        for (int i = 0; i < app->button_count; i++) {
            if (app->buttons[i].pressed) {
                app->buttons[i].pressed = 0;
                if (i == hit) {
                    calc_on_button(app, app->buttons[i].action);
                }
                app->needs_redraw = 1;
            }
        }
    }
}

static void calc_on_mouse_move(calc_app_t *app, int x, int y) {
    int hit = buttons_hit_test(app->buttons, app->button_count, x, y);
    int changed = 0;
    
    for (int i = 0; i < app->button_count; i++) {
        int should_hover = (i == hit);
        if (app->buttons[i].hover != should_hover) {
            app->buttons[i].hover = should_hover;
            changed = 1;
        }
    }
    
    if (changed) {
        app->needs_redraw = 1;
    }
}

static void calc_draw(calc_app_t *app) {
    void *ctx = nx_window_context(app->window);
    
    /* Draw background */
    nx_fill_rect(ctx, 0, 0, app->window_width, app->window_height, COLOR_BG_DARK);
    
    /* Draw display */
    display_draw(ctx, &app->display, 0, 0, app->window_width, DISPLAY_HEIGHT);
    
    /* Draw buttons */
    buttons_draw(ctx, app->buttons, app->button_count);
}

static void calc_app_run(calc_app_t *app) {
    while (app->running) {
        /* Process events */
        nx_event_t event;
        while (nx_poll_event(app->window, &event)) {
            switch (event.type) {
                case NX_EVENT_KEY_DOWN:
                    calc_on_key(app, event.key.keycode, event.key.mods);
                    break;
                case NX_EVENT_MOUSE_MOVE:
                    calc_on_mouse_move(app, event.mouse.x, event.mouse.y);
                    break;
                case NX_EVENT_MOUSE_DOWN:
                    calc_on_mouse(app, event.mouse.x, event.mouse.y, event.mouse.button, 1);
                    break;
                case NX_EVENT_MOUSE_UP:
                    calc_on_mouse(app, event.mouse.x, event.mouse.y, event.mouse.button, 0);
                    break;
                case NX_EVENT_CLOSE:
                    app->running = 0;
                    break;
                case NX_EVENT_RESIZE:
                    app->window_width = event.resize.width;
                    app->window_height = event.resize.height;
                    app->needs_redraw = 1;
                    break;
                default:
                    break;
            }
        }
        
        /* Redraw if needed */
        if (app->needs_redraw) {
            calc_draw(app);
            nx_present(app->window);
            app->needs_redraw = 0;
        }
        
        /* Yield CPU */
        nx_sleep(16);
    }
}

static void calc_app_shutdown(calc_app_t *app) {
    if (app->window) {
        nx_window_destroy(app->window);
        app->window = 0;
    }
}

/* ============ Entry Point ============ */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Initialize audio feedback system */
    audio_init();
    
    if (calc_app_init(&g_app) != 0) {
        audio_shutdown();
        return 1;
    }
    
    calc_app_run(&g_app);
    calc_app_shutdown(&g_app);
    
    /* Shutdown audio */
    audio_shutdown();
    
    return 0;
}
