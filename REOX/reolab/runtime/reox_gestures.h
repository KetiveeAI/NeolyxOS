/*
 * REOX Gesture Recognition System
 * Touch and mouse gesture handling for NeolyxOS UI
 * 
 * Features:
 * - Multi-touch gestures
 * - Custom gesture recognition
 * - Gesture priority and conflict resolution
 * - Haptic feedback integration
 */

#ifndef REOX_GESTURES_H
#define REOX_GESTURES_H

#include "reox_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Gesture Types
 * ============================================================================ */

typedef enum rx_gesture_type {
    RX_GESTURE_TAP,
    RX_GESTURE_DOUBLE_TAP,
    RX_GESTURE_LONG_PRESS,
    RX_GESTURE_PAN,
    RX_GESTURE_SWIPE,
    RX_GESTURE_PINCH,
    RX_GESTURE_ROTATE,
    RX_GESTURE_SCROLL,
    RX_GESTURE_EDGE_SWIPE,
    RX_GESTURE_FORCE_TOUCH,
    RX_GESTURE_HOVER,
    RX_GESTURE_CUSTOM,
} rx_gesture_type;

typedef enum rx_gesture_state {
    RX_GESTURE_POSSIBLE,      /* Might be recognized */
    RX_GESTURE_BEGAN,         /* Gesture started */
    RX_GESTURE_CHANGED,       /* Gesture in progress */
    RX_GESTURE_ENDED,         /* Gesture completed */
    RX_GESTURE_CANCELLED,     /* Gesture was cancelled */
    RX_GESTURE_FAILED,        /* Recognition failed */
} rx_gesture_state;

typedef enum rx_swipe_direction {
    RX_SWIPE_LEFT   = 1 << 0,
    RX_SWIPE_RIGHT  = 1 << 1,
    RX_SWIPE_UP     = 1 << 2,
    RX_SWIPE_DOWN   = 1 << 3,
} rx_swipe_direction;

typedef enum rx_edge_type {
    RX_EDGE_LEFT,
    RX_EDGE_RIGHT,
    RX_EDGE_TOP,
    RX_EDGE_BOTTOM,
} rx_edge_type;

/* ============================================================================
 * Touch Point
 * ============================================================================ */

typedef struct rx_touch {
    uint32_t id;              /* Unique touch identifier */
    rx_point position;        /* Current position */
    rx_point previous;        /* Previous position */
    rx_point initial;         /* Starting position */
    float pressure;           /* 0.0 - 1.0 */
    float radius;             /* Touch radius */
    float timestamp;          /* Touch time */
    bool is_primary;          /* Primary touch in multi-touch */
} rx_touch;

/* ============================================================================
 * Gesture Events
 * ============================================================================ */

typedef struct rx_tap_event {
    rx_point location;
    int tap_count;
    uint32_t button;          /* Mouse button if applicable */
} rx_tap_event;

typedef struct rx_pan_event {
    rx_point location;
    rx_point translation;     /* Total movement */
    rx_point velocity;        /* Pixels per second */
    rx_gesture_state state;
} rx_pan_event;

typedef struct rx_swipe_event {
    rx_point location;
    rx_swipe_direction direction;
    rx_point velocity;
} rx_swipe_event;

typedef struct rx_pinch_event {
    rx_point center;
    float scale;              /* Current scale factor */
    float velocity;           /* Scale change per second */
    rx_gesture_state state;
} rx_pinch_event;

typedef struct rx_rotate_event {
    rx_point center;
    float rotation;           /* Radians from start */
    float velocity;           /* Radians per second */
    rx_gesture_state state;
} rx_rotate_event;

typedef struct rx_long_press_event {
    rx_point location;
    float duration;
    rx_gesture_state state;
} rx_long_press_event;

typedef struct rx_scroll_event {
    rx_point location;
    float delta_x;
    float delta_y;
    bool is_momentum;         /* Inertia scrolling */
    bool is_precise;          /* Trackpad vs mouse wheel */
} rx_scroll_event;

typedef struct rx_edge_swipe_event {
    rx_edge_type edge;
    float progress;           /* 0.0 - 1.0 */
    rx_point translation;
    rx_gesture_state state;
} rx_edge_swipe_event;

typedef struct rx_hover_event {
    rx_point location;
    bool entered;
    bool exited;
} rx_hover_event;

/* ============================================================================
 * Gesture Recognizer Base
 * ============================================================================ */

typedef struct rx_gesture_recognizer rx_gesture_recognizer;

typedef void (*rx_gesture_callback)(rx_gesture_recognizer* recognizer, void* event, void* user_data);

struct rx_gesture_recognizer {
    rx_gesture_type type;
    rx_gesture_state state;
    bool enabled;
    rx_view* view;            /* Attached view */
    
    /* Configuration */
    int required_touches;     /* Number of touches required */
    bool cancel_touches_in_view;
    bool delay_touches_began;
    
    /* Callback */
    rx_gesture_callback callback;
    void* user_data;
    
    /* Priority and conflict */
    int priority;
    rx_gesture_recognizer** require_failure;  /* Wait for these to fail */
    size_t require_failure_count;
    
    /* Internal */
    struct rx_gesture_recognizer* next;
};

/* ============================================================================
 * Specific Gesture Recognizers
 * ============================================================================ */

/* Tap Recognizer */
typedef struct rx_tap_recognizer {
    rx_gesture_recognizer base;
    int number_of_taps;       /* Required taps (1, 2, 3...) */
    int number_of_touches;    /* Required simultaneous touches */
    float max_duration;       /* Max time between taps */
    float max_distance;       /* Max movement allowed */
} rx_tap_recognizer;

extern rx_tap_recognizer* rx_tap_recognizer_create(int taps, rx_gesture_callback cb, void* data);

/* Pan Recognizer */
typedef struct rx_pan_recognizer {
    rx_gesture_recognizer base;
    int minimum_touches;
    int maximum_touches;
    float minimum_distance;   /* Distance before recognition */
    rx_point translation;
    rx_point velocity;
} rx_pan_recognizer;

extern rx_pan_recognizer* rx_pan_recognizer_create(rx_gesture_callback cb, void* data);

/* Swipe Recognizer */
typedef struct rx_swipe_recognizer {
    rx_gesture_recognizer base;
    rx_swipe_direction allowed_directions;
    float minimum_velocity;
    float minimum_distance;
} rx_swipe_recognizer;

extern rx_swipe_recognizer* rx_swipe_recognizer_create(rx_swipe_direction dir, rx_gesture_callback cb, void* data);

/* Pinch Recognizer */
typedef struct rx_pinch_recognizer {
    rx_gesture_recognizer base;
    float scale;
    float initial_distance;
} rx_pinch_recognizer;

extern rx_pinch_recognizer* rx_pinch_recognizer_create(rx_gesture_callback cb, void* data);

/* Rotation Recognizer */
typedef struct rx_rotation_recognizer {
    rx_gesture_recognizer base;
    float rotation;
    float initial_angle;
} rx_rotation_recognizer;

extern rx_rotation_recognizer* rx_rotation_recognizer_create(rx_gesture_callback cb, void* data);

/* Long Press Recognizer */
typedef struct rx_long_press_recognizer {
    rx_gesture_recognizer base;
    float minimum_duration;
    float allowed_movement;
} rx_long_press_recognizer;

extern rx_long_press_recognizer* rx_long_press_recognizer_create(float duration, rx_gesture_callback cb, void* data);

/* Edge Swipe Recognizer */
typedef struct rx_edge_swipe_recognizer {
    rx_gesture_recognizer base;
    rx_edge_type edges;
    float edge_width;         /* Detection zone size */
    float minimum_distance;
} rx_edge_swipe_recognizer;

extern rx_edge_swipe_recognizer* rx_edge_swipe_recognizer_create(rx_edge_type edge, rx_gesture_callback cb, void* data);

/* ============================================================================
 * Gesture Recognizer Control
 * ============================================================================ */

extern void rx_gesture_attach(rx_gesture_recognizer* recognizer, rx_view* view);
extern void rx_gesture_detach(rx_gesture_recognizer* recognizer);
extern void rx_gesture_set_enabled(rx_gesture_recognizer* recognizer, bool enabled);
extern void rx_gesture_require_failure(rx_gesture_recognizer* recognizer, rx_gesture_recognizer* other);
extern void rx_gesture_reset(rx_gesture_recognizer* recognizer);
extern void rx_gesture_destroy(rx_gesture_recognizer* recognizer);

/* ============================================================================
 * Custom Gesture Recognizer
 * ============================================================================ */

typedef struct rx_custom_gesture_config {
    const char* name;
    int required_points;
    float tolerance;          /* Path matching tolerance */
    
    /* Path template (normalized 0-1 coordinates) */
    rx_point* path;
    size_t path_length;
} rx_custom_gesture_config;

typedef struct rx_custom_gesture_recognizer {
    rx_gesture_recognizer base;
    rx_custom_gesture_config config;
    rx_point* recorded_path;
    size_t recorded_length;
    float match_score;
} rx_custom_gesture_recognizer;

extern rx_custom_gesture_recognizer* rx_custom_gesture_create(rx_custom_gesture_config config, rx_gesture_callback cb, void* data);
extern bool rx_custom_gesture_match(rx_custom_gesture_recognizer* recognizer, rx_point* path, size_t length);

/* Preset custom gestures */
extern rx_custom_gesture_config rx_gesture_circle(void);
extern rx_custom_gesture_config rx_gesture_check(void);
extern rx_custom_gesture_config rx_gesture_cross(void);
extern rx_custom_gesture_config rx_gesture_triangle(void);
extern rx_custom_gesture_config rx_gesture_square(void);

/* ============================================================================
 * Gesture Event Dispatch
 * ============================================================================ */

typedef struct rx_gesture_system {
    rx_gesture_recognizer* recognizers;
    size_t recognizer_count;
    rx_touch* active_touches;
    size_t touch_count;
    bool processing;
} rx_gesture_system;

extern rx_gesture_system* rx_gesture_system_create(void);
extern void rx_gesture_system_process_touch_began(rx_gesture_system* sys, rx_touch* touches, size_t count);
extern void rx_gesture_system_process_touch_moved(rx_gesture_system* sys, rx_touch* touches, size_t count);
extern void rx_gesture_system_process_touch_ended(rx_gesture_system* sys, rx_touch* touches, size_t count);
extern void rx_gesture_system_process_touch_cancelled(rx_gesture_system* sys, rx_touch* touches, size_t count);
extern void rx_gesture_system_destroy(rx_gesture_system* sys);

/* Global gesture system */
extern rx_gesture_system* rx_gesture_system_shared(void);

/* ============================================================================
 * Haptic Feedback
 * ============================================================================ */

typedef enum rx_haptic_type {
    RX_HAPTIC_SELECTION,      /* Light selection feedback */
    RX_HAPTIC_IMPACT_LIGHT,
    RX_HAPTIC_IMPACT_MEDIUM,
    RX_HAPTIC_IMPACT_HEAVY,
    RX_HAPTIC_SUCCESS,
    RX_HAPTIC_WARNING,
    RX_HAPTIC_ERROR,
    RX_HAPTIC_CLICK,
    RX_HAPTIC_DOUBLE_CLICK,
    RX_HAPTIC_PATTERN,        /* Custom pattern */
} rx_haptic_type;

typedef struct rx_haptic_pattern {
    float* intensities;       /* 0.0 - 1.0 */
    float* durations;         /* Seconds */
    size_t count;
} rx_haptic_pattern;

extern void rx_haptic_generate(rx_haptic_type type);
extern void rx_haptic_impact(float intensity);
extern void rx_haptic_pattern_play(rx_haptic_pattern* pattern);
extern rx_haptic_pattern* rx_haptic_pattern_create(float* intensities, float* durations, size_t count);
extern void rx_haptic_pattern_destroy(rx_haptic_pattern* pattern);

/* Haptic integration with gestures */
extern void rx_gesture_set_haptic(rx_gesture_recognizer* recognizer, rx_haptic_type type);

#ifdef __cplusplus
}
#endif

#endif /* REOX_GESTURES_H */
