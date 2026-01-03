/*
 * NXTouch - Touch Screen and Gesture Driver
 * 
 * Multi-touch gesture recognition for laptops and tablets:
 * - 2 fingers: Scroll (up/down/left/right)
 * - 3 fingers: Volume, Brightness, Media control
 * - 4 fingers: App switching, Desktop gestures
 * - Edge swipe: App switcher, Control Center
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXTOUCH_H
#define NXTOUCH_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Touch Device Types
 * ============================================================================ */

typedef enum {
    NXTOUCH_TYPE_TOUCHSCREEN = 0,   /* Built-in touch screen */
    NXTOUCH_TYPE_TOUCHPAD,          /* Laptop touchpad */
    NXTOUCH_TYPE_EXTERNAL,          /* External touch device */
    NXTOUCH_TYPE_STYLUS,            /* Pen/stylus input */
} nxtouch_type_t;

/* ============================================================================
 * Touch Device Capabilities
 * ============================================================================ */

typedef struct {
    char name[64];
    nxtouch_type_t type;
    
    /* Resolution */
    uint32_t width;             /* Touch area width (units) */
    uint32_t height;            /* Touch area height (units) */
    uint32_t dpi;               /* Touch resolution */
    
    /* Capabilities */
    uint8_t max_touch_points;   /* Max simultaneous touches (1-10+) */
    bool pressure_supported;    /* Pressure sensitivity */
    uint16_t max_pressure;      /* Max pressure value */
    bool palm_rejection;        /* Hardware palm rejection */
    bool stylus_supported;      /* Supports stylus input */
    bool hover_supported;       /* Detects hover (no contact) */
    
    /* State */
    uint8_t current_touches;    /* Current number of touch points */
    bool active;
} nxtouch_device_t;

/* ============================================================================
 * Touch Point Data
 * ============================================================================ */

typedef struct {
    uint8_t id;                 /* Touch point ID (0-9) */
    int32_t x;                  /* X position */
    int32_t y;                  /* Y position */
    uint16_t pressure;          /* Pressure (0-max_pressure) */
    uint16_t major_axis;        /* Touch ellipse major axis */
    uint16_t minor_axis;        /* Touch ellipse minor axis */
    uint8_t type;               /* 0=finger, 1=stylus, 2=palm */
} nxtouch_point_t;

/* ============================================================================
 * Gesture Types - Complete Set (34+ gestures)
 * Supports: Mobile, Tablet, Laptop Trackpad, Desktop Touchscreen
 * ============================================================================ */

typedef enum {
    /* ======== Single Finger Gestures ======== */
    NXGESTURE_TAP = 0,              /* Single tap (click) */
    NXGESTURE_DOUBLE_TAP,           /* Double tap (double-click, zoom) */
    NXGESTURE_TRIPLE_TAP,           /* Triple tap (select paragraph) */
    NXGESTURE_LONG_PRESS,           /* Hold (context menu) */
    NXGESTURE_TOUCH_HOLD,           /* Touch and hold (drag start) */
    
    /* Swipe/Flick (velocity-based) */
    NXGESTURE_SWIPE,                /* General swipe */
    NXGESTURE_SWIPE_UP,             /* Swipe up */
    NXGESTURE_SWIPE_DOWN,           /* Swipe down */
    NXGESTURE_SWIPE_LEFT,           /* Swipe left */
    NXGESTURE_SWIPE_RIGHT,          /* Swipe right */
    NXGESTURE_FLICK_UP,             /* Quick flick up */
    NXGESTURE_FLICK_DOWN,           /* Quick flick down */
    NXGESTURE_FLICK_LEFT,           /* Quick flick left */
    NXGESTURE_FLICK_RIGHT,          /* Quick flick right */
    
    /* Drag (position-based) */
    NXGESTURE_DRAG,                 /* Drag (hold + move) */
    NXGESTURE_DRAG_VERTICAL,        /* Vertical drag only */
    NXGESTURE_DRAG_HORIZONTAL,      /* Horizontal drag only */
    
    /* ======== Two Finger Gestures ======== */
    NXGESTURE_TWO_TAP,              /* 2-finger tap (right-click) */
    NXGESTURE_TWO_DOUBLE_TAP,       /* 2-finger double tap */
    NXGESTURE_SCROLL,               /* 2-finger scroll vertical */
    NXGESTURE_SCROLL_HORIZONTAL,    /* 2-finger scroll horizontal */
    NXGESTURE_PINCH,                /* Pinch zoom in/out */
    NXGESTURE_PINCH_HORIZONTAL,     /* Horizontal pinch */
    NXGESTURE_SPREAD,               /* Spread apart (zoom in) */
    NXGESTURE_SPREAD_HORIZONTAL,    /* Horizontal spread */
    NXGESTURE_ROTATE,               /* 2-finger rotate */
    
    /* ======== Three Finger Gestures ======== */
    NXGESTURE_THREE_TAP,            /* 3-finger tap (middle-click) */
    NXGESTURE_THREE_SWIPE,          /* 3-finger swipe */
    NXGESTURE_THREE_SWIPE_UP,       /* Volume/brightness up */
    NXGESTURE_THREE_SWIPE_DOWN,     /* Volume/brightness down */
    NXGESTURE_THREE_SWIPE_LEFT,     /* Previous */
    NXGESTURE_THREE_SWIPE_RIGHT,    /* Next */
    NXGESTURE_THREE_FLICK,          /* 3-finger flick (quick) */
    
    /* ======== Four Finger Gestures ======== */
    NXGESTURE_FOUR_TAP,             /* 4-finger tap (show desktop) */
    NXGESTURE_FOUR_SWIPE,           /* 4-finger swipe (app switch) */
    NXGESTURE_FOUR_SWIPE_UP,        /* Mission Control */
    NXGESTURE_FOUR_SWIPE_DOWN,      /* App Exposé */
    NXGESTURE_FOUR_SWIPE_LEFT,      /* Previous app */
    NXGESTURE_FOUR_SWIPE_RIGHT,     /* Next app */
    
    /* ======== Five Finger Gestures ======== */
    NXGESTURE_FIVE_TAP,             /* 5-finger tap */
    NXGESTURE_FIVE_PINCH,           /* 5-finger pinch (show desktop) */
    NXGESTURE_FIVE_SPREAD,          /* 5-finger spread (hide desktop) */
    
    /* ======== Edge Gestures ======== */
    NXGESTURE_EDGE_LEFT,            /* Left edge swipe (back) */
    NXGESTURE_EDGE_RIGHT,           /* Right edge swipe (forward) */
    NXGESTURE_EDGE_TOP,             /* Top edge swipe (notifications) */
    NXGESTURE_EDGE_BOTTOM,          /* Bottom edge swipe (app switcher) */
    NXGESTURE_EDGE_CORNER_TL,       /* Top-left corner */
    NXGESTURE_EDGE_CORNER_TR,       /* Top-right corner */
    NXGESTURE_EDGE_CORNER_BL,       /* Bottom-left corner */
    NXGESTURE_EDGE_CORNER_BR,       /* Bottom-right corner */
    
    /* ======== Device Motion Gestures ======== */
    NXGESTURE_SHAKE,                /* Shake device (undo) */
    NXGESTURE_BUMP_LEFT,            /* Bump device left */
    NXGESTURE_BUMP_RIGHT,           /* Bump device right */
    NXGESTURE_TILT,                 /* Tilt device */
    
    /* ======== Browser-Specific Gestures ======== */
    NXGESTURE_BROWSER_NEW_TAB,      /* Swipe left-to-right (new tab) */
    NXGESTURE_BROWSER_CLOSE_TAB,    /* Swipe tab away */
    NXGESTURE_BROWSER_BACK,         /* Swipe right (back page) */
    NXGESTURE_BROWSER_FORWARD,      /* Swipe left (forward page) */
    NXGESTURE_BROWSER_REFRESH,      /* Draw circle (refresh) */
    NXGESTURE_BROWSER_BOOKMARK,     /* Long press URL */
    NXGESTURE_BROWSER_ADDRESS_BAR,  /* Swipe across address bar */
    
    /* ======== Drawing Gestures (Symbol Recognition) ======== */
    NXGESTURE_DRAW_CIRCLE,          /* Draw circle */
    NXGESTURE_DRAW_CHECK,           /* Draw checkmark (approve) */
    NXGESTURE_DRAW_X,               /* Draw X (cancel/close) */
    NXGESTURE_DRAW_LINE,            /* Draw straight line */
    NXGESTURE_DRAW_ARROW,           /* Draw arrow (navigate) */
    NXGESTURE_DRAW_HOUSE,           /* Draw house roof (home) */
    
    /* ======== Palm/Special ======== */
    NXGESTURE_PALM,                 /* Palm touch (rejected) */
    NXGESTURE_FIST,                 /* Fist (screenshot) */
    NXGESTURE_THUMB_UP,             /* Thumb up (like) */
    NXGESTURE_THUMB_DOWN,           /* Thumb down (dislike) */
    
    /* Count */
    NXGESTURE_COUNT
} nxgesture_type_t;

/* Gesture directions */
typedef enum {
    NXDIR_NONE = 0,
    NXDIR_UP,
    NXDIR_DOWN,
    NXDIR_LEFT,
    NXDIR_RIGHT,
    NXDIR_IN,                   /* Pinch inward */
    NXDIR_OUT,                  /* Pinch outward */
    NXDIR_CLOCKWISE,
    NXDIR_COUNTER_CLOCKWISE,
} nxgesture_dir_t;

/* ============================================================================
 * Gesture Event
 * ============================================================================ */

typedef struct {
    nxgesture_type_t type;
    nxgesture_dir_t direction;
    
    /* Position */
    int32_t x;                  /* Center X position */
    int32_t y;                  /* Center Y position */
    
    /* Velocity (for swipes) */
    float velocity_x;
    float velocity_y;
    
    /* Magnitude (for pinch/scroll) */
    float magnitude;            /* 1.0 = normal, >1 = zoom in, <1 = zoom out */
    float delta;                /* Change from last event */
    
    /* Rotation (for rotate gesture) */
    float angle;                /* Rotation angle in degrees */
    
    /* Finger count */
    uint8_t finger_count;
    
    /* State */
    uint8_t state;              /* 0=begin, 1=update, 2=end, 3=cancel */
    
    /* Timestamp */
    uint64_t timestamp_ms;
} nxgesture_event_t;

/* Gesture states */
#define NXGESTURE_BEGIN     0
#define NXGESTURE_UPDATE    1
#define NXGESTURE_END       2
#define NXGESTURE_CANCEL    3

/* ============================================================================
 * Gesture Actions (Configurable)
 * ============================================================================ */

typedef enum {
    NXACTION_NONE = 0,
    
    /* Scroll actions */
    NXACTION_SCROLL_VERTICAL,
    NXACTION_SCROLL_HORIZONTAL,
    NXACTION_NATURAL_SCROLL,        /* Inverted scroll direction */
    
    /* Media/System */
    NXACTION_VOLUME_UP,
    NXACTION_VOLUME_DOWN,
    NXACTION_BRIGHTNESS_UP,
    NXACTION_BRIGHTNESS_DOWN,
    NXACTION_MUTE,
    NXACTION_PLAY_PAUSE,
    NXACTION_NEXT_TRACK,
    NXACTION_PREV_TRACK,
    
    /* Window/App */
    NXACTION_SWITCH_APP,            /* App switcher */
    NXACTION_SHOW_DESKTOP,
    NXACTION_SHOW_DOCK,
    NXACTION_MISSION_CONTROL,       /* All windows overview */
    NXACTION_APP_EXPOSE,            /* Current app windows */
    NXACTION_CLOSE_APP,
    NXACTION_MINIMIZE,
    NXACTION_MAXIMIZE,
    NXACTION_FLOAT_WINDOW,          /* Pop-out floating window */
    
    /* Edge-specific */
    NXACTION_CONTROL_CENTER,        /* Right edge -> Control Center */
    NXACTION_NOTIFICATION_CENTER,   /* Top edge -> Notifications */
    NXACTION_APP_DRAWER,            /* Bottom edge -> App drawer */
    NXACTION_BACK,                  /* Left edge -> Back navigation */
    
    /* Zoom */
    NXACTION_ZOOM,
    NXACTION_ZOOM_IN,
    NXACTION_ZOOM_OUT,
    
    /* Custom */
    NXACTION_CUSTOM,
} nxtouch_action_t;

/* ============================================================================
 * Gesture Configuration
 * ============================================================================ */

typedef struct {
    /* 2-finger gestures */
    nxtouch_action_t two_finger_scroll;     /* Default: SCROLL_VERTICAL */
    nxtouch_action_t two_finger_tap;        /* Default: Right-click */
    nxtouch_action_t two_finger_pinch;      /* Default: ZOOM */
    
    /* 3-finger gestures */
    nxtouch_action_t three_finger_swipe_up;      /* Default: VOLUME_UP */
    nxtouch_action_t three_finger_swipe_down;    /* Default: VOLUME_DOWN */
    nxtouch_action_t three_finger_swipe_left;    /* Default: BRIGHTNESS_DOWN */
    nxtouch_action_t three_finger_swipe_right;   /* Default: BRIGHTNESS_UP */
    nxtouch_action_t three_finger_tap;           /* Default: PLAY_PAUSE */
    
    /* 4-finger gestures */
    nxtouch_action_t four_finger_swipe_up;       /* Default: MISSION_CONTROL */
    nxtouch_action_t four_finger_swipe_down;     /* Default: APP_EXPOSE */
    nxtouch_action_t four_finger_swipe_left;     /* Default: SWITCH_APP (prev) */
    nxtouch_action_t four_finger_swipe_right;    /* Default: SWITCH_APP (next) */
    nxtouch_action_t four_finger_tap;            /* Default: SHOW_DESKTOP */
    
    /* Edge swipes (touch screen only) */
    nxtouch_action_t edge_left;              /* Default: BACK */
    nxtouch_action_t edge_right;             /* Default: CONTROL_CENTER */
    nxtouch_action_t edge_top;               /* Default: NOTIFICATION_CENTER */
    nxtouch_action_t edge_bottom;            /* Default: APP_DRAWER */
    
    /* Settings */
    bool natural_scroll;                     /* Invert scroll direction */
    uint8_t scroll_speed;                    /* 1-10 */
    uint8_t gesture_sensitivity;             /* 1-10 */
    uint16_t edge_zone_pixels;               /* Edge detection zone width */
    bool tap_to_click;                       /* Tap = click */
    bool palm_rejection;                     /* Ignore palm touches */
    
    /* App-specific overrides */
    bool enable_in_games;                    /* Gestures work in games */
    bool enable_in_fullscreen;               /* Gestures work in fullscreen */
} nxtouch_config_t;

/* ============================================================================
 * Driver API
 * ============================================================================ */

/**
 * Initialize touch subsystem
 */
int nxtouch_init(void);
void nxtouch_shutdown(void);

/**
 * Check if touch screen is available
 */
int nxtouch_available(void);
int nxtouch_is_touchscreen(void);  /* Has full touch screen */
int nxtouch_is_touchpad(void);     /* Has touchpad only */

/**
 * Get touch device info
 */
int nxtouch_get_device(int index, nxtouch_device_t *dev);
int nxtouch_get_device_count(void);

/**
 * Get current touch points
 */
int nxtouch_get_points(nxtouch_point_t *points, int max_points);

/* ============================================================================
 * Gesture Recognition
 * ============================================================================ */

/**
 * Poll for gesture events
 * Returns number of events (0 if none)
 */
int nxtouch_poll_gesture(nxgesture_event_t *event);

/**
 * Register gesture callback
 */
typedef void (*nxgesture_callback_t)(const nxgesture_event_t *event, void *userdata);
int nxtouch_register_gesture_handler(nxgesture_callback_t callback, void *userdata);
void nxtouch_unregister_gesture_handler(void);

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * Get/set gesture configuration
 */
int nxtouch_get_config(nxtouch_config_t *config);
int nxtouch_set_config(const nxtouch_config_t *config);

/**
 * Reset to defaults
 */
int nxtouch_reset_config(void);

/**
 * Get default configuration
 */
void nxtouch_get_default_config(nxtouch_config_t *config);

/* ============================================================================
 * Edge Detection (Touch Screen)
 * ============================================================================ */

/**
 * Configure edge zones for swipe gestures
 * zone_width: pixels from screen edge that trigger edge gesture
 */
int nxtouch_set_edge_zone(uint16_t zone_width);

/**
 * Enable/disable specific edge
 */
int nxtouch_edge_enable(nxgesture_dir_t edge, bool enabled);

/* ============================================================================
 * App Switcher Integration
 * 
 * Single-finger edge swipe for app switching (like Android/iOS)
 * ============================================================================ */

typedef struct {
    bool enabled;
    uint16_t edge_width;            /* Pixels from bottom */
    uint8_t swipe_threshold;        /* Min swipe distance to trigger */
    
    /* Swipe behaviors */
    bool swipe_up_close_app;        /* Swipe up on app = close */
    bool swipe_left_right_switch;   /* Swipe left/right = switch apps */
    bool hold_for_floating;         /* Hold swipe = floating window */
} nxtouch_appswitcher_t;

int nxtouch_appswitcher_enable(const nxtouch_appswitcher_t *config);
int nxtouch_appswitcher_disable(void);

/* ============================================================================
 * Stylus / Pen Support
 * 
 * Full support for active stylus (Wacom, Surface Pen, Apple Pencil style):
 * - Pressure sensitivity (4096 levels typical)
 * - Tilt detection (X and Y axis)
 * - Button support (1-2 buttons + eraser end)
 * - Hover detection (in-range before contact)
 * ============================================================================ */

typedef struct {
    bool detected;              /* Stylus in range */
    int32_t x;                  /* X coordinate */
    int32_t y;                  /* Y coordinate */
    uint16_t pressure;          /* Pressure level (0-4095) */
    int16_t tilt_x;             /* Tilt angle X (-90 to 90 degrees) */
    int16_t tilt_y;             /* Tilt angle Y (-90 to 90 degrees) */
    float rotation;             /* Barrel rotation (0-360 degrees) */
    bool button1;               /* Primary button (right-click) */
    bool button2;               /* Secondary button (eraser mode) */
    bool eraser_end;            /* Using eraser end of pen */
    bool in_range;              /* Stylus hovering (not touching) */
    bool touching;              /* Stylus touching surface */
    uint8_t battery_percent;    /* Stylus battery (if wireless) */
} nxstylus_state_t;

typedef struct {
    char name[64];              /* Stylus model name */
    char vendor[32];            /* Manufacturer */
    uint16_t max_pressure;      /* Maximum pressure levels */
    bool has_tilt;              /* Supports tilt */
    bool has_rotation;          /* Supports barrel rotation */
    bool has_eraser;            /* Has eraser end */
    uint8_t button_count;       /* Number of buttons */
    bool battery_powered;       /* Has internal battery */
} nxstylus_info_t;

int nxtouch_stylus_available(void);
int nxtouch_get_stylus(nxstylus_state_t *state);
int nxtouch_get_stylus_info(nxstylus_info_t *info);

/* ============================================================================
 * Palm Rejection
 * 
 * Automatic rejection of palm/hand touches when:
 * - Stylus is in range (active stylus takes priority)
 * - Large touch area detected (palm vs finger)
 * - Multi-touch pattern matches palm
 * ============================================================================ */

typedef struct {
    bool enabled;               /* Master enable */
    bool stylus_priority;       /* Reject touch when stylus in range */
    bool size_detection;        /* Detect by touch size */
    uint16_t min_palm_size;     /* Minimum major axis for palm (pixels) */
    bool pattern_detection;     /* Use pattern matching */
    uint8_t sensitivity;        /* 1-10 (higher = more aggressive) */
} nxpalm_config_t;

int nxtouch_palm_rejection_enable(bool enabled);
int nxtouch_palm_get_config(nxpalm_config_t *config);
int nxtouch_palm_set_config(const nxpalm_config_t *config);

/**
 * Get default palm rejection config
 * Enabled by default when stylus is detected
 */
void nxtouch_palm_get_default(nxpalm_config_t *config);

/* ============================================================================
 * Force Touch / 3D Touch (Pressure-sensitive displays)
 * ============================================================================ */

typedef struct {
    bool available;             /* Hardware supports force touch */
    bool enabled;               /* Currently enabled */
    uint16_t max_force;         /* Maximum force level */
    uint16_t light_threshold;   /* Light press threshold */
    uint16_t deep_threshold;    /* Deep press threshold */
} nxforce_touch_t;

int nxtouch_force_available(void);
int nxtouch_force_get_config(nxforce_touch_t *config);
int nxtouch_force_enable(bool enabled);

#endif /* NXTOUCH_H */
