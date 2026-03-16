/*
 * IconLay Color Picker - Reox Integration
 * Uses iconlay_widgets for color picker component
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "iconlay_widgets.h"
#include <stdlib.h>
#include <string.h>

/* IconLay color picker wrapper (uses iconlay_color_picker_t from widgets) */

/* Alias for legacy API compatibility */
iconlay_color_picker_t* iconlay_colorpicker_new(iconlay_color_t initial) {
    return iconlay_color_picker_create(initial);
}

void iconlay_colorpicker_free(iconlay_color_picker_t* cp) {
    iconlay_color_picker_destroy(cp);
}

void iconlay_colorpicker_show(iconlay_color_picker_t* cp, float x, float y) {
    if (!cp) return;
    iconlay_color_picker_set_bounds(cp, x, y, 600, 700);
    iconlay_color_picker_set_visible(cp, true);
}

void iconlay_colorpicker_hide(iconlay_color_picker_t* cp) {
    iconlay_color_picker_set_visible(cp, false);
}

/* Default palettes for IconLay effects */
static iconlay_color_t iconlay_effect_palette[] = {
    {255, 255, 255, 255},  /* White */
    {0, 0, 0, 255},        /* Black */
    {59, 130, 246, 255},   /* Blue */
    {139, 92, 246, 255},   /* Purple */
    {236, 72, 153, 255},   /* Pink */
    {239, 68, 68, 255},    /* Red */
    {249, 115, 22, 255},   /* Orange */
    {234, 179, 8, 255},    /* Yellow */
    {34, 197, 94, 255},    /* Green */
    {20, 184, 166, 255},   /* Teal */
};

void iconlay_colorpicker_get_effect_palette(iconlay_color_t* out, int* count) {
    if (out && count) {
        *count = 10;
        memcpy(out, iconlay_effect_palette, sizeof(iconlay_effect_palette));
    }
}