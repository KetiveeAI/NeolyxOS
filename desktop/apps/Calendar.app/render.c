/*
 * NeolyxOS Calendar - Smooth Rendering Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "render.h"
#include <stdlib.h>
#include <string.h>

/* External drawing functions */
extern void fill_circle(int cx, int cy, int r, uint32_t color);
extern void draw_circle(int cx, int cy, int r, uint32_t color);
extern void fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
extern void desktop_draw_text(int x, int y, const char *text, uint32_t color);
extern uint64_t pit_get_ticks(void);

/* Initialize render context */
cal_render_ctx_t *cal_render_init(calendar_ctx_t *cal) {
    cal_render_ctx_t *ctx = (cal_render_ctx_t *)malloc(sizeof(cal_render_ctx_t));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(cal_render_ctx_t));
    ctx->cal = cal;
    ctx->animator = cal_animator_create();
    
    /* Initialize with full opacity */
    ctx->clock_state.opacity = 1.0f;
    ctx->clock_state.scale = 1.0f;
    ctx->calendar_state.opacity = 1.0f;
    ctx->calendar_state.scale = 1.0f;
    ctx->moon_state.opacity = 1.0f;
    ctx->moon_state.scale = 1.0f;
    
    ctx->needs_redraw = true;
    ctx->dirty_count = 0;
    ctx->last_render = 0;
    
    return ctx;
}

/* Destroy render context */
void cal_render_destroy(cal_render_ctx_t *ctx) {
    if (!ctx) return;
    
    if (ctx->animator) {
        cal_animator_destroy(ctx->animator);
    }
    free(ctx);
}

/* Smooth step function */
float cal_smooth_step(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* Linear interpolation */
float cal_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Mark region as dirty */
void cal_mark_dirty(cal_render_ctx_t *ctx, int x, int y, int w, int h) {
    if (!ctx || ctx->dirty_count >= 10) return;
    
    ctx->dirty_regions[ctx->dirty_count][0] = x;
    ctx->dirty_regions[ctx->dirty_count][1] = y;
    ctx->dirty_regions[ctx->dirty_count][2] = w;
    ctx->dirty_regions[ctx->dirty_count][3] = h;
    ctx->dirty_count++;
    ctx->needs_redraw = true;
}

/* Clear dirty regions */
void cal_clear_dirty(cal_render_ctx_t *ctx) {
    if (!ctx) return;
    ctx->dirty_count = 0;
    ctx->needs_redraw = false;
}

/* Check if needs redraw */
bool cal_is_dirty(cal_render_ctx_t *ctx) {
    return ctx && ctx->needs_redraw;
}

/* Render clock with animations */
void cal_render_clock_animated(cal_render_ctx_t *ctx, int x, int y, int size) {
    if (!ctx || !ctx->cal) return;
    
    const theme_colors_t *theme = calendar_get_theme(ctx->cal->current_theme);
    render_state_t *state = &ctx->clock_state;
    
    /* Apply animation state */
    int ax = x + (int)state->x_offset;
    int ay = y + (int)state->y_offset;
    int asize = (int)(size * state->scale);
    
    /* Apply opacity to colors */
    uint32_t bg_color = cal_color_lerp(0x00000000, theme->background, state->opacity);
    uint32_t border_color = cal_color_lerp(0x00000000, theme->border, state->opacity);
    uint32_t text_color = cal_color_lerp(0x00000000, theme->text_primary, state->opacity);
    uint32_t accent_color = cal_color_lerp(0x00000000, theme->accent, state->opacity);
    
    /* Background */
    fill_rounded_rect(ax, ay, asize, asize, 12, bg_color);
    
    /* Center point */
    int cx = ax + asize / 2;
    int cy = ay + asize / 2;
    int r = asize / 2 - 10;
    
    /* Clock face based on current face */
    switch (ctx->cal->current_face) {
        case CLOCK_FACE_ANALOG:
        case CLOCK_FACE_MINIMAL: {
            /* Outer ring */
            draw_circle(cx, cy, r, border_color);
            
            /* Hour markers (12 positions) */
            if (ctx->cal->current_face != CLOCK_FACE_MINIMAL) {
                for (int h = 0; h < 12; h++) {
                    /* Simplified - just show dots at 12, 3, 6, 9 */
                    if (h % 3 == 0) {
                        int mx = cx;
                        int my = cy - r + 8;
                        fill_circle(mx, my, 3, text_color);
                    }
                }
            }
            
            /* Hands would be drawn here with proper angle math */
            /* For now, just center dot */
            fill_circle(cx, cy, 4, accent_color);
            break;
        }
        
        case CLOCK_FACE_DIGITAL:
        default: {
            /* Digital time display */
            char time_str[9];
            int hour = ctx->cal->current_time.hour;
            int min = ctx->cal->current_time.minute;
            int sec = ctx->cal->current_time.second;
            
            time_str[0] = '0' + (hour / 10);
            time_str[1] = '0' + (hour % 10);
            time_str[2] = ':';
            time_str[3] = '0' + (min / 10);
            time_str[4] = '0' + (min % 10);
            time_str[5] = ':';
            time_str[6] = '0' + (sec / 10);
            time_str[7] = '0' + (sec % 10);
            time_str[8] = '\0';
            
            desktop_draw_text(ax + asize / 4, ay + asize / 2 - 5, time_str, text_color);
            break;
        }
    }
}

/* Render calendar with animations */
void cal_render_calendar_animated(cal_render_ctx_t *ctx, int x, int y, int w, int h) {
    if (!ctx || !ctx->cal) return;
    
    render_state_t *state = &ctx->calendar_state;
    
    /* Apply animation offset for slide transitions */
    int ax = x + (int)state->x_offset;
    int ay = y + (int)state->y_offset;
    
    /* Delegate to main calendar render with animated position */
    calendar_render_month_view(ctx->cal, NULL, ax, ay, w, h);
}

/* Render moon phase with animations */
void cal_render_moon_animated(cal_render_ctx_t *ctx, int x, int y, int size) {
    if (!ctx) return;
    
    render_state_t *state = &ctx->moon_state;
    
    /* Pulsing glow effect for full moon */
    moon_phase_t phase = (moon_phase_t)ctx->cal->current_moon_phase;
    
    int ax = x + (int)state->x_offset;
    int ay = y + (int)state->y_offset;
    int asize = (int)(size * state->scale);
    
    /* Render moon with glow if full */
    if (phase == MOON_FULL) {
        /* Glow rings */
        uint32_t glow = 0x20FFFFCC;
        fill_circle(ax + asize/2, ay + asize/2, asize/2 + 8, glow);
        fill_circle(ax + asize/2, ay + asize/2, asize/2 + 4, glow);
    }
    
    /* Render actual moon */
    calendar_render_moon_phase(NULL, ax, ay, asize, phase);
}

/* Theme transition */
void cal_transition_theme(cal_render_ctx_t *ctx, calendar_theme_t new_theme, float duration_ms) {
    if (!ctx || !ctx->animator) return;
    
    /* Fade out */
    cal_animation_t *fade_out = cal_anim_create(ANIM_PROP_OPACITY, 1.0f, 0.0f, duration_ms / 2, EASE_OUT);
    fade_out->callback_ctx = ctx;
    
    cal_animator_add(ctx->animator, fade_out);
    cal_anim_start(fade_out);
    
    /* Set new theme after fade */
    calendar_set_theme(ctx->cal, new_theme);
    
    /* Fade in */
    cal_animation_t *fade_in = cal_anim_create(ANIM_PROP_OPACITY, 0.0f, 1.0f, duration_ms / 2, EASE_IN);
    cal_animator_add(ctx->animator, fade_in);
    cal_anim_start(fade_in);
}

/* Clock face transition */
void cal_transition_clock_face(cal_render_ctx_t *ctx, clock_face_t new_face, float duration_ms) {
    if (!ctx || !ctx->animator) return;
    
    /* Scale down, switch, scale up */
    cal_animation_t *scale = cal_anim_create(ANIM_PROP_SCALE, 1.0f, 0.8f, duration_ms / 2, EASE_IN_BACK);
    cal_animator_add(ctx->animator, scale);
    cal_anim_start(scale);
    
    calendar_set_clock_face(ctx->cal, new_face);
    
    cal_animation_t *scale_up = cal_anim_create(ANIM_PROP_SCALE, 0.8f, 1.0f, duration_ms / 2, EASE_OUT_BACK);
    cal_animator_add(ctx->animator, scale_up);
    cal_anim_start(scale_up);
    
    ctx->needs_redraw = true;
}

/* Month transition with slide */
void cal_transition_month(cal_render_ctx_t *ctx, int new_month, int new_year, int direction) {
    if (!ctx || !ctx->animator) return;
    
    float distance = (direction > 0) ? 300.0f : -300.0f;
    
    /* Slide out old month */
    cal_animation_t *slide_out = cal_anim_create(ANIM_PROP_X, 0.0f, -distance, 200.0f, EASE_IN);
    cal_animator_add(ctx->animator, slide_out);
    cal_anim_start(slide_out);
    
    /* Update month */
    ctx->cal->view_month = new_month;
    ctx->cal->view_year = new_year;
    
    /* Slide in new month */
    cal_animation_t *slide_in = cal_anim_create(ANIM_PROP_X, distance, 0.0f, 200.0f, EASE_OUT);
    cal_animator_add(ctx->animator, slide_in);
    cal_anim_start(slide_in);
    
    ctx->needs_redraw = true;
}

/* Main render frame */
void cal_render_frame(cal_render_ctx_t *ctx, uint32_t *fb, int width, int height, int pitch) {
    if (!ctx) return;
    
    (void)fb; (void)pitch;  /* Direct FB rendering handled by external functions */
    
    uint64_t now = pit_get_ticks();
    
    /* Update animations */
    bool animating = cal_animator_update(ctx->animator, now);
    if (animating) {
        ctx->needs_redraw = true;
    }
    
    /* Skip if nothing to redraw */
    if (!ctx->needs_redraw) return;
    
    /* Render clock (centered top) */
    cal_render_clock_animated(ctx, width / 2 - 100, 50, 200);
    
    /* Render calendar (left side) */
    cal_render_calendar_animated(ctx, 30, height / 2 - 150, 280, 300);
    
    /* Render moon phase (right side) */
    if (ctx->cal->show_moon_phase) {
        cal_render_moon_animated(ctx, width - 100, 50, 60);
    }
    
    ctx->last_render = now;
    cal_clear_dirty(ctx);
}
