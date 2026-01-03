/*
 * NeolyxOS Calculator.app - History Management
 * 
 * Calculation history tracking and recall.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "calc.h"

/* ============ String Helpers ============ */

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ History Implementation ============ */

void history_init(calc_app_t *app) {
    app->history_count = 0;
    app->history_index = -1;
}

void history_add(calc_app_t *app, const char *expression, const char *result) {
    if (!expression || !expression[0]) return;
    if (!result || !result[0]) return;
    
    /* Shift entries if at max capacity */
    if (app->history_count >= CALC_MAX_HISTORY) {
        for (int i = 0; i < CALC_MAX_HISTORY - 1; i++) {
            app->history[i] = app->history[i + 1];
        }
        app->history_count = CALC_MAX_HISTORY - 1;
    }
    
    /* Add new entry */
    calc_history_entry_t *entry = &app->history[app->history_count];
    str_copy(entry->expression, expression, 128);
    str_copy(entry->result, result, 64);
    entry->mode = app->mode;
    
    app->history_count++;
    app->history_index = app->history_count;
}

const char* history_get_previous(calc_app_t *app) {
    if (app->history_count == 0) return NULL;
    
    if (app->history_index > 0) {
        app->history_index--;
    }
    
    if (app->history_index >= 0 && app->history_index < app->history_count) {
        return app->history[app->history_index].result;
    }
    
    return NULL;
}

const char* history_get_next(calc_app_t *app) {
    if (app->history_count == 0) return NULL;
    
    if (app->history_index < app->history_count - 1) {
        app->history_index++;
    }
    
    if (app->history_index >= 0 && app->history_index < app->history_count) {
        return app->history[app->history_index].result;
    }
    
    return NULL;
}

void history_clear(calc_app_t *app) {
    app->history_count = 0;
    app->history_index = -1;
}
