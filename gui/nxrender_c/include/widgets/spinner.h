/*
 * NeolyxOS - NXRender Spinner Widget
 * Animated loading indicator
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_SPINNER_H
#define NXRENDER_SPINNER_H

#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_spinner nx_spinner_t;

/* Spinner style */
typedef enum {
    NX_SPINNER_CIRCULAR,     /* Rotating arc */
    NX_SPINNER_DOTS,         /* Pulsing dots */
    NX_SPINNER_BAR           /* Sequential bars */
} nx_spinner_style_t;

struct nx_spinner {
    nx_widget_t base;
    nx_spinner_style_t style;
    bool animating;
    float rotation;          /* Current rotation angle */
    float speed;             /* Rotations per second */
    nx_color_t color;
    nx_color_t track_color;
    uint32_t size;
    uint32_t thickness;
};

/* Create/destroy */
nx_spinner_t* nx_spinner_create(void);
void nx_spinner_destroy(nx_spinner_t* sp);

/* Animation */
void nx_spinner_start(nx_spinner_t* sp);
void nx_spinner_stop(nx_spinner_t* sp);
bool nx_spinner_is_animating(nx_spinner_t* sp);
void nx_spinner_animate(nx_spinner_t* sp, float dt);

/* Style */
void nx_spinner_set_style(nx_spinner_t* sp, nx_spinner_style_t style);
void nx_spinner_set_color(nx_spinner_t* sp, nx_color_t color);
void nx_spinner_set_track_color(nx_spinner_t* sp, nx_color_t color);
void nx_spinner_set_size(nx_spinner_t* sp, uint32_t size);
void nx_spinner_set_thickness(nx_spinner_t* sp, uint32_t thickness);
void nx_spinner_set_speed(nx_spinner_t* sp, float speed);

#ifdef __cplusplus
}
#endif
#endif
