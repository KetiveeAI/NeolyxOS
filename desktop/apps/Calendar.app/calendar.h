/*
 * NeolyxOS Calendar Application
 * 
 * Full calendar with time widget, themes, and clock faces.
 * Launchable from navigation bar clock click.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef CALENDAR_H
#define CALENDAR_H

#include <stdint.h>
#include <stdbool.h>

/* Theme IDs */
typedef enum {
    THEME_MIDNIGHT = 0,      /* Dark blue/purple - default */
    THEME_ARCTIC,            /* White/light blue */
    THEME_SUNSET,            /* Orange/red gradient */
    THEME_FOREST,            /* Green tones */
    THEME_OCEAN,             /* Teal/aqua */
    THEME_SAKURA,            /* Pink/cherry blossom */
    THEME_MONOCHROME,        /* Black and white */
    THEME_COUNT
} calendar_theme_t;

/* Clock face styles */
typedef enum {
    CLOCK_FACE_DIGITAL = 0,  /* Digital HH:MM:SS */
    CLOCK_FACE_ANALOG,       /* Traditional analog */
    CLOCK_FACE_MINIMAL,      /* Just hands, no numbers */
    CLOCK_FACE_ROMAN,        /* Roman numerals */
    CLOCK_FACE_BINARY,       /* Binary clock */
    CLOCK_FACE_FLIP,         /* Flip clock style */
    CLOCK_FACE_COUNT
} clock_face_t;

/* Theme color scheme */
typedef struct {
    uint32_t background;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t accent;
    uint32_t highlight;
    uint32_t border;
    const char *name;
} theme_colors_t;

/* Time structure */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  weekday;  /* 0=Sunday */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} calendar_time_t;

/* Calendar context */
typedef struct {
    calendar_time_t current_time;
    calendar_theme_t current_theme;
    clock_face_t current_face;
    int view_month;
    int view_year;
    int current_region;      /* calendar_region_t from regions.h */
    int current_moon_phase;  /* moon_phase_t from regions.h */
    int active_festival;     /* festival_type_t from regions.h */
    bool running;
    bool show_seconds;
    bool use_24h;
    bool show_moon_phase;
    bool show_festivals;
} calendar_ctx_t;

/* Theme functions */
const theme_colors_t *calendar_get_theme(calendar_theme_t theme);
void calendar_set_theme(calendar_ctx_t *ctx, calendar_theme_t theme);
void calendar_next_theme(calendar_ctx_t *ctx);

/* Clock face functions */
void calendar_set_clock_face(calendar_ctx_t *ctx, clock_face_t face);
void calendar_next_clock_face(calendar_ctx_t *ctx);

/* Calendar functions */
void calendar_init(calendar_ctx_t *ctx);
void calendar_update_time(calendar_ctx_t *ctx);
void calendar_render(calendar_ctx_t *ctx, uint32_t *fb, uint32_t w, uint32_t h, uint32_t pitch);
void calendar_render_clock_widget(calendar_ctx_t *ctx, uint32_t *fb, int x, int y, int size);
void calendar_render_month_view(calendar_ctx_t *ctx, uint32_t *fb, int x, int y, int w, int h);
void calendar_render_live_icon(calendar_ctx_t *ctx, uint32_t *fb, int x, int y, int size);

/* Day names and month names */
extern const char *day_names_short[7];
extern const char *day_names_full[7];
extern const char *month_names_short[12];
extern const char *month_names_full[12];

/* Utilities */
int calendar_days_in_month(int year, int month);
int calendar_day_of_week(int year, int month, int day);
bool calendar_is_leap_year(int year);

#endif /* CALENDAR_H */
