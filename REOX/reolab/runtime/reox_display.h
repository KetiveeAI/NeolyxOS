/*
 * REOX Display System
 * 
 * Multi-resolution and DPI-aware display management.
 * Supports adaptive scaling for different screen sizes.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef REOX_DISPLAY_H
#define REOX_DISPLAY_H

#include "reox_ui.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Display Resolution Constants
 * ============================================================================ */

/* Standard display resolutions */
typedef enum rx_resolution {
    RX_RES_VGA     = 0,   /* 640x480 */
    RX_RES_SVGA,          /* 800x600 */
    RX_RES_XGA,           /* 1024x768 */
    RX_RES_WXGA,          /* 1280x720 (HD) */
    RX_RES_HD,            /* 1366x768 */
    RX_RES_SXGA,          /* 1280x1024 */
    RX_RES_FHD,           /* 1920x1080 (Full HD) */
    RX_RES_QHD,           /* 2560x1440 (2K) */
    RX_RES_4K,            /* 3840x2160 (4K UHD) */
    RX_RES_5K,            /* 5120x2880 */
    RX_RES_CUSTOM,
    RX_RES_COUNT
} rx_resolution;

/* Display orientation */
typedef enum rx_orientation {
    RX_ORIENT_LANDSCAPE,
    RX_ORIENT_PORTRAIT,
    RX_ORIENT_LANDSCAPE_FLIPPED,
    RX_ORIENT_PORTRAIT_FLIPPED,
} rx_orientation;

/* Device form factors */
typedef enum rx_form_factor {
    RX_FORM_DESKTOP,      /* Standard desktop/laptop */
    RX_FORM_TABLET,       /* Tablet (touch-first) */
    RX_FORM_PHONE,        /* Phone (compact) */
    RX_FORM_TV,           /* Large screen TV */
    RX_FORM_EMBEDDED,     /* Embedded display */
} rx_form_factor;

/* DPI scale factors */
typedef enum rx_scale_factor {
    RX_SCALE_100 = 100,   /* 1x (96 DPI) */
    RX_SCALE_125 = 125,   /* 1.25x (120 DPI) */
    RX_SCALE_150 = 150,   /* 1.5x (144 DPI) */
    RX_SCALE_175 = 175,   /* 1.75x (168 DPI) */
    RX_SCALE_200 = 200,   /* 2x (192 DPI - Retina) */
    RX_SCALE_250 = 250,   /* 2.5x (240 DPI) */
    RX_SCALE_300 = 300,   /* 3x (288 DPI) */
} rx_scale_factor;

/* ============================================================================
 * Display Info Structure
 * ============================================================================ */

typedef struct rx_display_info {
    /* Physical properties */
    int width_px;              /* Width in pixels */
    int height_px;             /* Height in pixels */
    int refresh_rate;          /* Hz (60, 120, 144, etc.) */
    float diagonal_inches;     /* Screen size in inches */
    
    /* Calculated properties */
    int dpi_x;                 /* Horizontal DPI */
    int dpi_y;                 /* Vertical DPI */
    rx_scale_factor scale;     /* UI scale factor */
    rx_orientation orient;     /* Current orientation */
    rx_form_factor form;       /* Device form factor */
    rx_resolution resolution;  /* Standard resolution enum */
    
    /* Display capabilities */
    bool hdr_supported;        /* HDR10/Dolby Vision */
    bool touch_supported;      /* Touch input */
    bool pen_supported;        /* Stylus/pen input */
    int color_depth;           /* Bits per pixel (24, 30, 48) */
    
    /* Safe areas (for notch, rounded corners) */
    rx_edge_insets safe_insets;
} rx_display_info;

/* ============================================================================
 * Responsive Breakpoints (CSS-like)
 * ============================================================================ */

typedef enum rx_breakpoint {
    RX_BP_XS = 0,     /* < 576px (phones) */
    RX_BP_SM,         /* 576-767px (large phones) */
    RX_BP_MD,         /* 768-991px (tablets) */
    RX_BP_LG,         /* 992-1199px (laptops) */
    RX_BP_XL,         /* 1200-1399px (desktops) */
    RX_BP_XXL,        /* >= 1400px (large desktops) */
    RX_BP_COUNT
} rx_breakpoint;

/* Breakpoint thresholds */
extern const int rx_breakpoint_widths[RX_BP_COUNT];

/* ============================================================================
 * Responsive Value (different values per breakpoint)
 * ============================================================================ */

typedef struct rx_responsive_float {
    float values[RX_BP_COUNT];
    bool defined[RX_BP_COUNT];
} rx_responsive_float;

typedef struct rx_responsive_int {
    int values[RX_BP_COUNT];
    bool defined[RX_BP_COUNT];
} rx_responsive_int;

/* Create responsive value */
rx_responsive_float rx_responsive(float base);
rx_responsive_float rx_responsive_at(float base, rx_breakpoint bp, float value);
float rx_responsive_get(rx_responsive_float *resp, rx_breakpoint current);

/* ============================================================================
 * Display Manager
 * ============================================================================ */

typedef struct rx_display_manager {
    rx_display_info primary;
    rx_display_info *displays;
    int display_count;
    int active_display;
    rx_breakpoint current_bp;
    
    /* Callbacks */
    void (*on_resolution_change)(rx_display_info *info, void *ctx);
    void (*on_orientation_change)(rx_orientation orient, void *ctx);
    void (*on_scale_change)(rx_scale_factor scale, void *ctx);
    void *callback_ctx;
} rx_display_manager;

/* Global display manager */
extern rx_display_manager *g_display_manager;

/* Initialize display system */
rx_display_manager *rx_display_init(void);
void rx_display_shutdown(rx_display_manager *mgr);

/* Query display info */
rx_display_info *rx_display_get_primary(void);
rx_display_info *rx_display_get(int index);
int rx_display_count(void);

/* Set display properties */
void rx_display_set_resolution(int width, int height);
void rx_display_set_scale(rx_scale_factor scale);
void rx_display_set_orientation(rx_orientation orient);

/* Detect properties */
rx_scale_factor rx_display_detect_scale(int width, int height, float diagonal);
rx_form_factor rx_display_detect_form_factor(int width, int height, bool touch);
rx_breakpoint rx_display_get_breakpoint(int width);

/* ============================================================================
 * Scaling Utilities
 * ============================================================================ */

/* Scale value by current DPI scale */
float rx_scaled(float value);
int rx_scaled_int(int value);
rx_size rx_scaled_size(rx_size s);
rx_edge_insets rx_scaled_insets(rx_edge_insets insets);

/* Unscale value (convert from pixels to logical units) */
float rx_unscaled(float pixels);

/* Scale by specific factor */
float rx_scale_by(float value, rx_scale_factor scale);

/* ============================================================================
 * Responsive Layout Helpers
 * ============================================================================ */

/* Check if current breakpoint is at least the given size */
bool rx_is_at_least(rx_breakpoint bp);
bool rx_is_at_most(rx_breakpoint bp);
bool rx_is_exactly(rx_breakpoint bp);
bool rx_is_between(rx_breakpoint min, rx_breakpoint max);

/* Get columns for responsive grid */
int rx_grid_columns(void);  /* Returns 1, 2, 3, 4, 6, or 12 based on width */

/* Responsive spacing */
float rx_spacing_xs(void);   /* Extra small spacing */
float rx_spacing_sm(void);   /* Small spacing */
float rx_spacing_md(void);   /* Medium spacing */
float rx_spacing_lg(void);   /* Large spacing */
float rx_spacing_xl(void);   /* Extra large spacing */

/* ============================================================================
 * Typography Scaling
 * ============================================================================ */

typedef enum rx_text_style {
    RX_TEXT_HEADLINE1,    /* 32pt scaled */
    RX_TEXT_HEADLINE2,    /* 28pt scaled */
    RX_TEXT_HEADLINE3,    /* 24pt scaled */
    RX_TEXT_TITLE,        /* 20pt scaled */
    RX_TEXT_SUBTITLE,     /* 18pt scaled */
    RX_TEXT_BODY,         /* 16pt scaled */
    RX_TEXT_BODY_SM,      /* 14pt scaled */
    RX_TEXT_CAPTION,      /* 12pt scaled */
    RX_TEXT_OVERLINE,     /* 10pt scaled */
} rx_text_style;

/* Get scaled font size for text style */
float rx_font_size(rx_text_style style);
float rx_line_height(rx_text_style style);

/* ============================================================================
 * Safe Area Handling
 * ============================================================================ */

/* Get safe area insets (for notches, rounded corners) */
rx_edge_insets rx_safe_area(void);

/* Get content rect within safe area */
rx_rect rx_safe_content_rect(rx_rect bounds);

/* Check if point is in safe area */
bool rx_is_in_safe_area(rx_point pt);

#ifdef __cplusplus
}
#endif

#endif /* REOX_DISPLAY_H */
