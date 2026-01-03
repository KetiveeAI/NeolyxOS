/*
 * NeolyxOS Calendar - Smooth Rendering
 * 
 * GPU-friendly rendering with animation support.
 * 60 FPS target using damage tracking.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef CALENDAR_RENDER_H
#define CALENDAR_RENDER_H

#include "calendar.h"
#include "animation.h"
#include "regions.h"
#include <stdint.h>
#include <stdbool.h>

/* Render state for animated elements */
typedef struct {
    float opacity;
    float scale;
    float x_offset;
    float y_offset;
    float rotation;
    uint32_t tint_color;
} render_state_t;

/* Animated widget types */
typedef enum {
    WIDGET_CLOCK,
    WIDGET_CALENDAR,
    WIDGET_MOON,
    WIDGET_FESTIVAL,
    WIDGET_THEME_PICKER,
    WIDGET_FACE_PICKER
} widget_type_t;

/* Calendar render context */
typedef struct {
    calendar_ctx_t *cal;
    cal_animator_t *animator;
    render_state_t clock_state;
    render_state_t calendar_state;
    render_state_t moon_state;
    uint64_t last_render;
    bool needs_redraw;
    int dirty_regions[10][4];  /* x, y, w, h */
    int dirty_count;
} cal_render_ctx_t;

/* Initialize render context */
cal_render_ctx_t *cal_render_init(calendar_ctx_t *cal);
void cal_render_destroy(cal_render_ctx_t *ctx);

/* Rendering functions */
void cal_render_frame(cal_render_ctx_t *ctx, uint32_t *fb, int width, int height, int pitch);
void cal_render_clock_animated(cal_render_ctx_t *ctx, int x, int y, int size);
void cal_render_calendar_animated(cal_render_ctx_t *ctx, int x, int y, int w, int h);
void cal_render_moon_animated(cal_render_ctx_t *ctx, int x, int y, int size);
void cal_render_festival_animated(cal_render_ctx_t *ctx, int x, int y, int size);

/* Transitions */
void cal_transition_theme(cal_render_ctx_t *ctx, calendar_theme_t new_theme, float duration_ms);
void cal_transition_clock_face(cal_render_ctx_t *ctx, clock_face_t new_face, float duration_ms);
void cal_transition_month(cal_render_ctx_t *ctx, int new_month, int new_year, int direction);

/* Damage tracking */
void cal_mark_dirty(cal_render_ctx_t *ctx, int x, int y, int w, int h);
void cal_clear_dirty(cal_render_ctx_t *ctx);
bool cal_is_dirty(cal_render_ctx_t *ctx);

/* Smooth value transitions */
float cal_smooth_step(float edge0, float edge1, float x);
float cal_lerp(float a, float b, float t);

#endif /* CALENDAR_RENDER_H */
