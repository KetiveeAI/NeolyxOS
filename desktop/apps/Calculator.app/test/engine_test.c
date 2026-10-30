/*
 * Calculator.app - host-side engine test
 *
 * Links the C generated from src/calculator.rx (with `main` renamed) against
 * NxUI and the host build of nxrender, drives the engine through the same
 * action hooks the buttons use, and checks the display label text.
 *
 * Build and run: make -C desktop/apps/Calculator.app test
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "nxui.h"

/* Symbols from the generated C (reoxc emits external linkage for all fns). */
extern bool build_ui(void);
extern void engine_clear_all(void);
extern void Display_refresh(void);
extern void rx_ui_on_action(int64_t act);
extern void rx_ui_on_key(int64_t key);
extern int64_t calc_main(void);

/* The display handle is a static global in the generated C; NxUI's handle
 * for it is the second label created (expression label is first). */
static nxui_handle_t g_display_handle = 0;

static int g_failures = 0;
static int g_checks = 0;

static const char* display(void) {
    return nxui_label_get_text(g_display_handle);
}

static void press(const char* keys) {
    /* Mirrors Keypad action ids. */
    for (const char* k = keys; *k; k++) {
        switch (*k) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            rx_ui_on_action(*k - '0'); break;
        case '.': rx_ui_on_action(10); break;
        case '+': rx_ui_on_action(11); break;
        case '-': rx_ui_on_action(12); break;
        case '*': rx_ui_on_action(13); break;
        case '/': rx_ui_on_action(14); break;
        case '=': rx_ui_on_action(15); break;
        case 'C': rx_ui_on_action(16); break;
        case 'n': rx_ui_on_action(17); break;   /* negate */
        case '%': rx_ui_on_action(18); break;
        case '<': rx_ui_on_action(19); break;   /* backspace */
        case 'c': rx_ui_on_action(20); break;   /* MC */
        case 'r': rx_ui_on_action(21); break;   /* MR */
        case 'a': rx_ui_on_action(22); break;   /* M+ */
        case 's': rx_ui_on_action(23); break;   /* M- */
        case 'q': rx_ui_on_action(24); break;   /* sqrt */
        case 'S': rx_ui_on_action(25); break;   /* square */
        case 'R': rx_ui_on_action(26); break;   /* reciprocal */
        case ' ': break;
        default:
            fprintf(stderr, "test bug: unknown key '%c'\n", *k);
            g_failures++;
        }
    }
}

static void expect(const char* name, const char* keys, const char* want) {
    g_checks++;
    rx_ui_on_action(16);   /* AC */
    press(keys);
    const char* got = display();
    if (strcmp(got, want) != 0) {
        g_failures++;
        printf("FAIL %-28s keys=%-16s want=%-16s got=%s\n", name, keys, want, got);
    } else {
        printf("ok   %-28s keys=%-16s -> %s\n", name, keys, got);
    }
}

int main(void) {
    engine_clear_all();
    if (!build_ui()) {
        printf("FAIL build_ui\n");
        return 1;
    }
    /* Handles are allocated in creation order: window=1, vstack=2, strip=3,
     * expression label=4, display label=5 (generation 1 in the high word). */
    g_display_handle = (1LL << 32) | 5;
    if (strcmp(nxui_label_get_text(g_display_handle), "0") != 0) {
        printf("FAIL display handle lookup: '%s'\n", nxui_label_get_text(g_display_handle));
        return 1;
    }
    Display_refresh();

    expect("digits",                 "123",        "123");
    expect("leading zero",           "007",        "7");
    expect("decimal entry",          "1.50",       "1.50");
    expect("dot first",              ".5",         "0.5");
    expect("second dot ignored",     "1.2.3",      "1.23");
    expect("add",                    "12+3=",      "15");
    expect("subtract negative",      "3-5=",       "-2");
    expect("multiply decimals",      "1.5*4=",     "6");
    expect("divide",                 "7/2=",       "3.5");
    expect("divide by zero",         "7/0=",       "Error");
    expect("error then digit clears","7/0=4",      "4");
    expect("chained ops",            "2+3*4=",     "20");
    expect("operator replaces",      "2+*3=",      "6");
    expect("equals repeat idempotent","2+3==",     "5");
    expect("negate while typing",    "5n",         "-5");
    expect("negate result",          "5+1=n",      "-6");
    expect("percent",                "50%",        "0.5");
    expect("backspace digit",        "123<",       "12");
    expect("backspace to zero",      "1<",         "0");
    expect("backspace dot",          "1.<",        "1");
    expect("backspace frac",         "1.25<",      "1.2");
    expect("sqrt",                   "2q",         "1.4142135624");
    expect("sqrt exact",             "81q",        "9");
    expect("sqrt negative",          "4nq",        "Error");
    expect("square",                 "12S",        "144");
    expect("reciprocal",             "8R",         "0.125");
    expect("reciprocal zero",        "0R",         "Error");
    /* memory persists across AC, so each memory case starts with MC ('c') */
    expect("memory add recall",      "c5a Cr",     "5");
    expect("memory subtract",        "c5a 2s Cr",  "3");
    expect("memory accumulates",     "c5a 5a Cr",  "10");
    expect("recall empty is no-op",  "c5a c 7r",   "7");
    expect("float noise trimmed",    "0.1+0.2=",   "0.3");
    /* 9999999800000001 is above 2^53: the nearest double is ...000 (IEEE 754 limit) */
    expect("large product (double)", "99999999*99999999=", "9999999800000000");
    expect("very large rounds 5dp",  "99999999*99999999=*99999999=", "1.00000e+24");
    expect("entry limit 15 digits",  "1234567890123456789", "123456789012345");
    expect("key hook enter",         "",           "0");
    rx_ui_on_key('4'); rx_ui_on_key('+'); rx_ui_on_key('4'); rx_ui_on_key('\r');
    g_checks++;
    if (strcmp(display(), "8") != 0) { g_failures++; printf("FAIL key hook: %s\n", display()); }
    else printf("ok   key hook 4+4 enter -> %s\n", display());

    printf("%d checks, %d failures, %lld bridge errors\n", g_checks, g_failures, (long long)nxui_error_count());
    return g_failures == 0 ? 0 : 1;
}
