/*
 * REOX Additional Widgets
 * 
 * Extended widget set for richer UI components.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef REOX_WIDGETS_EXT_H
#define REOX_WIDGETS_EXT_H

#include "reox_ui.h"
#include "reox_animation.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Slider Widget
 * ============================================================================ */

typedef void (*rx_slider_callback)(float value, void *user_data);

typedef struct rx_slider_view {
    rx_view base;
    float value;              /* Current value (0.0 to 1.0) */
    float min_value;          /* Minimum value */
    float max_value;          /* Maximum value */
    float step;               /* Step increment (0 = continuous) */
    bool vertical;            /* Vertical slider */
    bool show_value;          /* Show current value label */
    rx_slider_callback on_change;
    void *callback_data;
    rx_color track_color;
    rx_color fill_color;
    rx_color thumb_color;
    float thumb_size;
    bool dragging;
} rx_slider_view;

extern rx_slider_view *slider_view_new(float min, float max, float initial);
extern void slider_view_set_value(rx_slider_view *view, float value);
extern float slider_view_get_value(rx_slider_view *view);
extern void slider_view_set_callback(rx_slider_view *view, rx_slider_callback cb, void *data);
extern void slider_view_set_step(rx_slider_view *view, float step);

/* ============================================================================
 * Stepper Widget (Number +/-)
 * ============================================================================ */

typedef void (*rx_stepper_callback)(int value, void *user_data);

typedef struct rx_stepper_view {
    rx_view base;
    int value;
    int min_value;
    int max_value;
    int step;
    rx_stepper_callback on_change;
    void *callback_data;
    bool wrap_around;         /* Wrap from max to min */
    const char *format;       /* Printf format for value */
} rx_stepper_view;

extern rx_stepper_view *stepper_view_new(int min, int max, int initial);
extern void stepper_view_set_value(rx_stepper_view *view, int value);
extern int stepper_view_get_value(rx_stepper_view *view);
extern void stepper_view_increment(rx_stepper_view *view);
extern void stepper_view_decrement(rx_stepper_view *view);

/* ============================================================================
 * Segmented Control
 * ============================================================================ */

typedef void (*rx_segment_callback)(int index, void *user_data);

typedef struct rx_segment_view {
    rx_view base;
    char **segments;          /* Array of segment titles */
    int segment_count;
    int selected_index;
    rx_segment_callback on_change;
    void *callback_data;
    rx_color selected_color;
    rx_color unselected_color;
    float segment_padding;
} rx_segment_view;

extern rx_segment_view *segment_view_new(const char **titles, int count);
extern void segment_view_set_selected(rx_segment_view *view, int index);
extern int segment_view_get_selected(rx_segment_view *view);
extern void segment_view_set_callback(rx_segment_view *view, rx_segment_callback cb, void *data);

/* ============================================================================
 * Color Picker
 * ============================================================================ */

typedef void (*rx_color_picker_callback)(rx_color color, void *user_data);

typedef struct rx_color_picker_view {
    rx_view base;
    rx_color value;
    float hue;                /* 0-360 */
    float saturation;         /* 0-1 */
    float brightness;         /* 0-1 */
    float alpha;              /* 0-1 */
    rx_color_picker_callback on_change;
    void *callback_data;
    bool show_alpha;
    bool show_hex_input;
    rx_color *preset_colors;
    int preset_count;
} rx_color_picker_view;

extern rx_color_picker_view *color_picker_view_new(rx_color initial);
extern void color_picker_view_set_color(rx_color_picker_view *view, rx_color color);
extern rx_color color_picker_view_get_color(rx_color_picker_view *view);
extern void color_picker_view_set_presets(rx_color_picker_view *view, rx_color *colors, int count);

/* ============================================================================
 * Date Picker
 * ============================================================================ */

typedef struct rx_date {
    int year;
    int month;   /* 1-12 */
    int day;     /* 1-31 */
} rx_date;

typedef void (*rx_date_callback)(rx_date date, void *user_data);

typedef struct rx_date_picker_view {
    rx_view base;
    rx_date value;
    rx_date min_date;
    rx_date max_date;
    rx_date_callback on_change;
    void *callback_data;
    bool show_year_picker;
    bool show_month_picker;
    int first_day_of_week;    /* 0=Sunday, 1=Monday */
    const char **month_names;
    const char **day_names;
} rx_date_picker_view;

extern rx_date_picker_view *date_picker_view_new(rx_date initial);
extern void date_picker_view_set_date(rx_date_picker_view *view, rx_date date);
extern rx_date date_picker_view_get_date(rx_date_picker_view *view);
extern void date_picker_view_set_range(rx_date_picker_view *view, rx_date min, rx_date max);

/* ============================================================================
 * Time Picker
 * ============================================================================ */

typedef struct rx_time {
    int hour;    /* 0-23 */
    int minute;  /* 0-59 */
    int second;  /* 0-59 */
} rx_time;

typedef void (*rx_time_callback)(rx_time time, void *user_data);

typedef struct rx_time_picker_view {
    rx_view base;
    rx_time value;
    rx_time_callback on_change;
    void *callback_data;
    bool use_24h;
    bool show_seconds;
    int minute_interval;      /* 1, 5, 15, 30 */
} rx_time_picker_view;

extern rx_time_picker_view *time_picker_view_new(rx_time initial);
extern void time_picker_view_set_time(rx_time_picker_view *view, rx_time time);
extern rx_time time_picker_view_get_time(rx_time_picker_view *view);

/* ============================================================================
 * Popover
 * ============================================================================ */

typedef enum rx_popover_arrow {
    RX_ARROW_TOP,
    RX_ARROW_BOTTOM,
    RX_ARROW_LEFT,
    RX_ARROW_RIGHT,
    RX_ARROW_NONE,
} rx_popover_arrow;

typedef struct rx_popover_view {
    rx_view base;
    rx_view *content;
    rx_view *anchor;          /* View to attach to */
    rx_popover_arrow arrow_direction;
    rx_point arrow_offset;
    bool dismiss_on_tap_outside;
    bool animate;
} rx_popover_view;

extern rx_popover_view *popover_view_new(rx_view *content);
extern void popover_view_show(rx_popover_view *view, rx_view *anchor, rx_popover_arrow arrow);
extern void popover_view_dismiss(rx_popover_view *view);

/* ============================================================================
 * Toast / Snackbar
 * ============================================================================ */

typedef enum rx_toast_position {
    RX_TOAST_TOP,
    RX_TOAST_BOTTOM,
    RX_TOAST_TOP_LEFT,
    RX_TOAST_TOP_RIGHT,
    RX_TOAST_BOTTOM_LEFT,
    RX_TOAST_BOTTOM_RIGHT,
} rx_toast_position;

typedef enum rx_toast_type {
    RX_TOAST_INFO,
    RX_TOAST_SUCCESS,
    RX_TOAST_WARNING,
    RX_TOAST_ERROR,
} rx_toast_type;

typedef struct rx_toast_view {
    rx_view base;
    char *message;
    rx_toast_type type;
    rx_toast_position position;
    float duration_ms;        /* 0 = until dismissed */
    const char *action_label;
    rx_button_callback action_callback;
    void *action_data;
} rx_toast_view;

extern void rx_toast_show(const char *message, rx_toast_type type, float duration_ms);
extern void rx_toast_show_with_action(const char *message, rx_toast_type type, 
                                       const char *action, rx_button_callback cb, void *data);
extern void rx_toast_dismiss_all(void);

/* ============================================================================
 * Badge
 * ============================================================================ */

typedef struct rx_badge_view {
    rx_view base;
    char *text;               /* Badge text (e.g., "3", "New") */
    rx_color background_color;
    rx_color text_color;
    bool dot_only;            /* Show just a dot, no text */
    float offset_x;
    float offset_y;
} rx_badge_view;

extern rx_badge_view *badge_view_new(const char *text);
extern void badge_view_set_text(rx_badge_view *view, const char *text);
extern void badge_view_set_dot(rx_badge_view *view, bool dot_only);

/* ============================================================================
 * Avatar / Profile Image
 * ============================================================================ */

typedef struct rx_avatar_view {
    rx_view base;
    const char *image_source;
    char *initials;           /* Fallback if no image */
    rx_color background_color;
    float size;
    bool circular;
    bool show_status;         /* Online/offline indicator */
    rx_color status_color;
} rx_avatar_view;

extern rx_avatar_view *avatar_view_new(float size);
extern void avatar_view_set_image(rx_avatar_view *view, const char *source);
extern void avatar_view_set_initials(rx_avatar_view *view, const char *initials);
extern void avatar_view_set_status(rx_avatar_view *view, bool online);

/* ============================================================================
 * Chip / Tag
 * ============================================================================ */

typedef void (*rx_chip_callback)(int index, void *user_data);

typedef struct rx_chip_view {
    rx_view base;
    char *label;
    const char *icon_name;    /* Optional leading icon */
    bool deletable;           /* Show delete button */
    bool selected;
    rx_chip_callback on_delete;
    rx_chip_callback on_tap;
    void *callback_data;
    rx_color selected_color;
    rx_color unselected_color;
} rx_chip_view;

extern rx_chip_view *chip_view_new(const char *label);
extern void chip_view_set_selected(rx_chip_view *view, bool selected);
extern void chip_view_set_deletable(rx_chip_view *view, bool deletable);

/* ============================================================================
 * Skeleton Loader (Loading Placeholder)
 * ============================================================================ */

typedef struct rx_skeleton_view {
    rx_view base;
    bool animated;            /* Shimmer animation */
    rx_corner_radii radii;
    rx_color base_color;
    rx_color shimmer_color;
} rx_skeleton_view;

extern rx_skeleton_view *skeleton_view_new(float width, float height);
extern rx_skeleton_view *skeleton_text_view_new(int lines, float line_height);
extern rx_skeleton_view *skeleton_circle_view_new(float diameter);

#ifdef __cplusplus
}
#endif

#endif /* REOX_WIDGETS_EXT_H */
