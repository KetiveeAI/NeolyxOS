/*
 * NeolyxOS - NXRender ProgressBar Widget
 * Linear progress indicator
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_PROGRESSBAR_H
#define NXRENDER_PROGRESSBAR_H

#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_progressbar nx_progressbar_t;

/* Progress bar style */
typedef enum {
    NX_PROGRESS_LINEAR,
    NX_PROGRESS_INDETERMINATE
} nx_progress_style_t;

struct nx_progressbar {
    nx_widget_t base;
    float progress;           /* 0.0 to 1.0 */
    nx_progress_style_t style;
    nx_color_t track_color;
    nx_color_t fill_color;
    uint32_t height;
    uint32_t corner_radius;
    float indeterminate_offset;  /* For animation */
};

/* Create/destroy */
nx_progressbar_t* nx_progressbar_create(void);
void nx_progressbar_destroy(nx_progressbar_t* pb);

/* Value */
void nx_progressbar_set_progress(nx_progressbar_t* pb, float progress);
float nx_progressbar_get_progress(nx_progressbar_t* pb);

/* Style */
void nx_progressbar_set_style(nx_progressbar_t* pb, nx_progress_style_t style);
void nx_progressbar_set_track_color(nx_progressbar_t* pb, nx_color_t color);
void nx_progressbar_set_fill_color(nx_progressbar_t* pb, nx_color_t color);
void nx_progressbar_set_height(nx_progressbar_t* pb, uint32_t height);
void nx_progressbar_set_corner_radius(nx_progressbar_t* pb, uint32_t radius);

/* Animation (for indeterminate) */
void nx_progressbar_animate(nx_progressbar_t* pb, float dt);

#ifdef __cplusplus
}
#endif
#endif
