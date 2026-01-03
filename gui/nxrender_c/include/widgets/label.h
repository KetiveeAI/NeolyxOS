/*
 * NeolyxOS - NXRender Label Widget
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_LABEL_H
#define NXRENDER_LABEL_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NX_TEXT_ALIGN_LEFT,
    NX_TEXT_ALIGN_CENTER,
    NX_TEXT_ALIGN_RIGHT
} nx_text_align_t;

typedef struct {
    nx_color_t text_color;
    uint32_t font_size;
    nx_text_align_t align;
} nx_label_style_t;

typedef struct {
    nx_widget_t base;
    char* text;
    nx_label_style_t style;
} nx_label_t;

/* Default label style */
nx_label_style_t nx_label_default_style(void);

/* Create label */
nx_label_t* nx_label_create(const char* text);

/* Set text */
void nx_label_set_text(nx_label_t* label, const char* text);

/* Set style */
void nx_label_set_style(nx_label_t* label, nx_label_style_t style);

#ifdef __cplusplus
}
#endif
#endif
