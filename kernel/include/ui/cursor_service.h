/*
 * NeolyxOS Universal Cursor Service
 * 
 * Customizable cursor system with theme support, configurable speed,
 * acceleration, and animation. Replaces static hardcoded cursors.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_CURSOR_SERVICE_H
#define NEOLYX_CURSOR_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Cursor Types ============ */

typedef enum {
    NX_CURSOR_ARROW = 0,       /* Default pointer */
    NX_CURSOR_HAND,            /* Clickable link */
    NX_CURSOR_TEXT,            /* Text I-beam */
    NX_CURSOR_CROSSHAIR,       /* Precision select */
    NX_CURSOR_RESIZE_NS,       /* North-South resize */
    NX_CURSOR_RESIZE_EW,       /* East-West resize */
    NX_CURSOR_RESIZE_NESW,     /* Diagonal NE-SW */
    NX_CURSOR_RESIZE_NWSE,     /* Diagonal NW-SE */
    NX_CURSOR_MOVE,            /* Four-way move */
    NX_CURSOR_WAIT,            /* Loading/busy (animated) */
    NX_CURSOR_PROGRESS,        /* Background loading (animated) */
    NX_CURSOR_NOT_ALLOWED,     /* Disabled/forbidden */
    NX_CURSOR_GRAB,            /* Grabbable */
    NX_CURSOR_GRABBING,        /* Grabbing */
    NX_CURSOR_ZOOM_IN,         /* Zoom in */
    NX_CURSOR_ZOOM_OUT,        /* Zoom out */
    NX_CURSOR_COUNT
} nx_cursor_type_t;

/* ============ Cursor Image ============ */

typedef struct {
    uint16_t width;
    uint16_t height;
    int8_t   hotspot_x;
    int8_t   hotspot_y;
    uint32_t *pixels;       /* ARGB pixel data (NULL if using bitmap) */
    const uint32_t *bitmap; /* 1-bit fallback (for kernel mode) */
    bool is_loaded;
} cursor_image_t;

/* ============ Animation Frame ============ */

typedef struct {
    cursor_image_t *frames;
    uint8_t  frame_count;
    uint8_t  current_frame;
    uint16_t frame_delay_ms;    /* Delay between frames */
    uint32_t last_tick;
    bool     is_animated;
} cursor_animation_t;

/* ============ Cursor Configuration ============ */

typedef struct {
    /* Movement */
    uint8_t  speed;             /* 1-10, default 5 */
    uint8_t  acceleration;      /* 0=off, 1=low, 2=medium, 3=high */
    float    multiplier;        /* Computed from speed (0.25 to 4.0) */
    
    /* Appearance */
    uint8_t  size;              /* 0=small (75%), 1=normal (100%), 2=large (150%) */
    float    scale;             /* Computed from size */
    
    /* Behavior */
    uint16_t double_click_ms;   /* Double-click threshold (200-900ms) */
    bool     natural_scrolling; /* Invert scroll direction */
    bool     smooth_tracking;   /* Enable sub-pixel tracking */
    
    /* Theme */
    char     theme_name[32];    /* Active theme name */
} cursor_config_t;

/* ============ Cursor Theme ============ */

typedef struct {
    char name[32];
    char author[64];
    char description[128];
    uint8_t version;
    
    /* Cursor images for each type */
    cursor_image_t   images[NX_CURSOR_COUNT];
    cursor_animation_t animations[NX_CURSOR_COUNT];
    
    /* Theme colors (for monochrome themes) */
    uint32_t primary_color;     /* Main cursor color */
    uint32_t outline_color;     /* Outline for contrast */
    
    bool is_loaded;
} cursor_theme_t;

/* ============ Cursor State ============ */

typedef struct {
    /* Position (sub-pixel for smooth movement) */
    float x;
    float y;
    int32_t screen_x;
    int32_t screen_y;
    
    /* Delta accumulator for acceleration */
    float dx_accum;
    float dy_accum;
    
    /* Current cursor */
    nx_cursor_type_t current_type;
    nx_cursor_type_t requested_type;
    
    /* Button state */
    uint8_t buttons;
    uint32_t last_click_time;
    int32_t last_click_x;
    int32_t last_click_y;
    
    /* Visibility */
    bool visible;
    bool locked;                /* Position locked (for games) */
    
    /* Screen bounds */
    uint16_t screen_width;
    uint16_t screen_height;
} cursor_state_t;

/* ============ Cursor Service API ============ */

/**
 * cursor_service_init - Initialize cursor service
 * @width: Screen width
 * @height: Screen height
 */
void cursor_service_init(uint16_t width, uint16_t height);

/**
 * cursor_service_update - Update cursor from mouse delta
 * @dx: X movement delta (raw from hardware)
 * @dy: Y movement delta (raw from hardware)
 * @buttons: Button state bitmap
 * @timestamp: Current time in milliseconds
 */
void cursor_service_update(int16_t dx, int16_t dy, uint8_t buttons, uint32_t timestamp);

/**
 * cursor_service_animate - Tick animation (call from timer)
 * @timestamp: Current time in milliseconds
 */
void cursor_service_animate(uint32_t timestamp);

/**
 * cursor_draw - Render cursor to framebuffer
 * @fb: Framebuffer pointer
 * @pitch: Bytes per scanline
 * 
 * Returns: Previous rect for damage tracking
 */
void cursor_draw(volatile uint32_t *fb, uint32_t pitch);

/**
 * cursor_hide - Save background and hide cursor
 */
void cursor_hide(void);

/**
 * cursor_show - Restore cursor visibility
 */
void cursor_show(void);

/* ============ Configuration API ============ */

/**
 * cursor_set_config - Apply cursor configuration
 */
void cursor_set_config(const cursor_config_t *config);

/**
 * cursor_get_config - Get current configuration
 */
const cursor_config_t* cursor_get_config(void);

/**
 * cursor_set_speed - Set tracking speed (1-10)
 */
void cursor_set_speed(uint8_t speed);

/**
 * cursor_set_acceleration - Set acceleration curve
 * @level: 0=off, 1=low, 2=medium, 3=high
 */
void cursor_set_acceleration(uint8_t level);

/* ============ Theme API ============ */

/**
 * cursor_load_theme - Load cursor theme from path
 * @path: Path to .nxcursor package or directory
 * 
 * Returns: 0 on success, -1 on error
 */
int cursor_load_theme(const char *path);

/**
 * cursor_set_builtin_theme - Set default built-in theme
 * @dark: Use dark variant (for light backgrounds)
 */
void cursor_set_builtin_theme(bool dark);

/**
 * cursor_get_theme - Get current theme
 */
const cursor_theme_t* cursor_get_theme(void);

/* ============ Cursor Type API ============ */

/**
 * cursor_set_type - Set cursor type
 */
void cursor_set_type(nx_cursor_type_t type);

/**
 * cursor_get_type - Get current cursor type
 */
nx_cursor_type_t cursor_get_type(void);

/* ============ State API ============ */

/**
 * cursor_get_position - Get current cursor position
 */
void cursor_get_position(int32_t *x, int32_t *y);

/**
 * cursor_set_position - Warp cursor to position
 */
void cursor_set_position(int32_t x, int32_t y);

/**
 * cursor_get_state - Get full cursor state
 */
const cursor_state_t* cursor_get_state(void);

/**
 * cursor_lock - Lock cursor position (for games)
 */
void cursor_lock(void);

/**
 * cursor_unlock - Unlock cursor position
 */
void cursor_unlock(void);

/* ============ Built-in Cursor Data (NeolyxOS Design) ============ */

/*
 * Default cursor: Y-shaped spread design
 * Size: 14x16 pixels
 * Colors: Purple gradient (#B0AAE3, #CDBBE6)
 * Hotspot: (0, 0) - top left corner
 */

/* Cursor pixel data - converted from cursor.svg */
/* Format: ARGB, row-major, 14x16 */

#define CURSOR_DEFAULT_WIDTH  14
#define CURSOR_DEFAULT_HEIGHT 16
#define CURSOR_DEFAULT_HOTSPOT_X 0
#define CURSOR_DEFAULT_HOTSPOT_Y 0

/* Colors from the SVG design */
#define CURSOR_COLOR_LEFT  0xFFB0AAE3  /* Left side - darker purple */
#define CURSOR_COLOR_RIGHT 0xFFCDBBE6  /* Right side - lighter purple */
#define CURSOR_COLOR_OUTLINE 0xFF373435 /* Outline - dark gray */

/* ============ Convenience Functions ============ */

/**
 * cursor_is_initialized - Check if cursor service is ready
 * Returns: true if initialized, false otherwise
 */
bool cursor_is_initialized(void);

/**
 * cursor_render - Render cursor to framebuffer with dimensions
 * @fb: Framebuffer pointer
 * @width: Screen width
 * @height: Screen height  
 * @pitch: Bytes per scanline
 */
void cursor_render(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch);

#endif /* NEOLYX_CURSOR_SERVICE_H */
