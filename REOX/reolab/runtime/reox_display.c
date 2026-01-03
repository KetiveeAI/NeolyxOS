/*
 * REOX Display System Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "reox_display.h"
#include <stdlib.h>
#include <string.h>

/* Breakpoint width thresholds */
const int rx_breakpoint_widths[RX_BP_COUNT] = {
    0,      /* XS: 0-575 */
    576,    /* SM: 576-767 */
    768,    /* MD: 768-991 */
    992,    /* LG: 992-1199 */
    1200,   /* XL: 1200-1399 */
    1400,   /* XXL: 1400+ */
};

/* Base font sizes for text styles (in logical points) */
static const float base_font_sizes[] = {
    32.0f,  /* HEADLINE1 */
    28.0f,  /* HEADLINE2 */
    24.0f,  /* HEADLINE3 */
    20.0f,  /* TITLE */
    18.0f,  /* SUBTITLE */
    16.0f,  /* BODY */
    14.0f,  /* BODY_SM */
    12.0f,  /* CAPTION */
    10.0f,  /* OVERLINE */
};

/* Line height multipliers */
static const float line_height_multipliers[] = {
    1.2f,   /* HEADLINE1 */
    1.2f,   /* HEADLINE2 */
    1.3f,   /* HEADLINE3 */
    1.4f,   /* TITLE */
    1.4f,   /* SUBTITLE */
    1.5f,   /* BODY */
    1.5f,   /* BODY_SM */
    1.4f,   /* CAPTION */
    1.3f,   /* OVERLINE */
};

/* Global display manager */
rx_display_manager *g_display_manager = NULL;

/* Initialize display system */
rx_display_manager *rx_display_init(void) {
    if (g_display_manager) return g_display_manager;
    
    g_display_manager = (rx_display_manager *)malloc(sizeof(rx_display_manager));
    if (!g_display_manager) return NULL;
    
    memset(g_display_manager, 0, sizeof(rx_display_manager));
    
    /* Default primary display - 1024x768 */
    g_display_manager->primary.width_px = 1024;
    g_display_manager->primary.height_px = 768;
    g_display_manager->primary.refresh_rate = 60;
    g_display_manager->primary.diagonal_inches = 15.6f;
    g_display_manager->primary.dpi_x = 96;
    g_display_manager->primary.dpi_y = 96;
    g_display_manager->primary.scale = RX_SCALE_100;
    g_display_manager->primary.orient = RX_ORIENT_LANDSCAPE;
    g_display_manager->primary.form = RX_FORM_DESKTOP;
    g_display_manager->primary.resolution = RX_RES_XGA;
    g_display_manager->primary.color_depth = 32;
    g_display_manager->primary.touch_supported = false;
    g_display_manager->primary.hdr_supported = false;
    
    g_display_manager->displays = &g_display_manager->primary;
    g_display_manager->display_count = 1;
    g_display_manager->active_display = 0;
    g_display_manager->current_bp = RX_BP_LG;
    
    return g_display_manager;
}

/* Shutdown display system */
void rx_display_shutdown(rx_display_manager *mgr) {
    if (!mgr) return;
    
    if (mgr->displays != &mgr->primary) {
        free(mgr->displays);
    }
    
    free(mgr);
    if (mgr == g_display_manager) {
        g_display_manager = NULL;
    }
}

/* Get primary display */
rx_display_info *rx_display_get_primary(void) {
    if (!g_display_manager) rx_display_init();
    return &g_display_manager->primary;
}

/* Get display by index */
rx_display_info *rx_display_get(int index) {
    if (!g_display_manager) rx_display_init();
    if (index < 0 || index >= g_display_manager->display_count) return NULL;
    return &g_display_manager->displays[index];
}

/* Display count */
int rx_display_count(void) {
    if (!g_display_manager) rx_display_init();
    return g_display_manager->display_count;
}

/* Set display resolution */
void rx_display_set_resolution(int width, int height) {
    if (!g_display_manager) rx_display_init();
    
    rx_display_info *disp = &g_display_manager->primary;
    disp->width_px = width;
    disp->height_px = height;
    
    /* Update breakpoint */
    g_display_manager->current_bp = rx_display_get_breakpoint(width);
    
    /* Detect resolution enum */
    if (width <= 640) disp->resolution = RX_RES_VGA;
    else if (width <= 800) disp->resolution = RX_RES_SVGA;
    else if (width <= 1024) disp->resolution = RX_RES_XGA;
    else if (width <= 1366) disp->resolution = RX_RES_HD;
    else if (width <= 1920) disp->resolution = RX_RES_FHD;
    else if (width <= 2560) disp->resolution = RX_RES_QHD;
    else if (width <= 3840) disp->resolution = RX_RES_4K;
    else disp->resolution = RX_RES_5K;
    
    /* Detect orientation */
    disp->orient = (width >= height) ? RX_ORIENT_LANDSCAPE : RX_ORIENT_PORTRAIT;
    
    /* Callback */
    if (g_display_manager->on_resolution_change) {
        g_display_manager->on_resolution_change(disp, g_display_manager->callback_ctx);
    }
}

/* Set scale factor */
void rx_display_set_scale(rx_scale_factor scale) {
    if (!g_display_manager) rx_display_init();
    
    g_display_manager->primary.scale = scale;
    
    if (g_display_manager->on_scale_change) {
        g_display_manager->on_scale_change(scale, g_display_manager->callback_ctx);
    }
}

/* Set orientation */
void rx_display_set_orientation(rx_orientation orient) {
    if (!g_display_manager) rx_display_init();
    
    rx_display_info *disp = &g_display_manager->primary;
    
    /* Swap width/height if changing between landscape/portrait */
    bool was_landscape = (disp->orient == RX_ORIENT_LANDSCAPE || 
                          disp->orient == RX_ORIENT_LANDSCAPE_FLIPPED);
    bool is_landscape = (orient == RX_ORIENT_LANDSCAPE || 
                         orient == RX_ORIENT_LANDSCAPE_FLIPPED);
    
    if (was_landscape != is_landscape) {
        int temp = disp->width_px;
        disp->width_px = disp->height_px;
        disp->height_px = temp;
        
        g_display_manager->current_bp = rx_display_get_breakpoint(disp->width_px);
    }
    
    disp->orient = orient;
    
    if (g_display_manager->on_orientation_change) {
        g_display_manager->on_orientation_change(orient, g_display_manager->callback_ctx);
    }
}

/* Detect scale factor from physical dimensions */
rx_scale_factor rx_display_detect_scale(int width, int height, float diagonal) {
    if (diagonal <= 0) return RX_SCALE_100;
    
    /* Calculate DPI */
    float diagonal_px = sqrtf((float)(width * width + height * height));
    float dpi = diagonal_px / diagonal;
    
    /* Map DPI to scale */
    if (dpi < 108) return RX_SCALE_100;
    if (dpi < 132) return RX_SCALE_125;
    if (dpi < 156) return RX_SCALE_150;
    if (dpi < 180) return RX_SCALE_175;
    if (dpi < 216) return RX_SCALE_200;
    if (dpi < 264) return RX_SCALE_250;
    return RX_SCALE_300;
}

/* Detect form factor */
rx_form_factor rx_display_detect_form_factor(int width, int height, bool touch) {
    int smaller = (width < height) ? width : height;
    int larger = (width > height) ? width : height;
    
    if (larger < 800) return RX_FORM_PHONE;
    if (larger < 1200 && touch) return RX_FORM_TABLET;
    if (larger >= 2560 && !touch) return RX_FORM_TV;
    return RX_FORM_DESKTOP;
}

/* Get breakpoint for width */
rx_breakpoint rx_display_get_breakpoint(int width) {
    for (int i = RX_BP_COUNT - 1; i >= 0; i--) {
        if (width >= rx_breakpoint_widths[i]) {
            return (rx_breakpoint)i;
        }
    }
    return RX_BP_XS;
}

/* Scale value by current DPI */
float rx_scaled(float value) {
    if (!g_display_manager) rx_display_init();
    return value * (float)g_display_manager->primary.scale / 100.0f;
}

int rx_scaled_int(int value) {
    return (int)rx_scaled((float)value);
}

rx_size rx_scaled_size(rx_size s) {
    return (rx_size){ rx_scaled(s.width), rx_scaled(s.height) };
}

rx_edge_insets rx_scaled_insets(rx_edge_insets insets) {
    return (rx_edge_insets){
        rx_scaled(insets.top),
        rx_scaled(insets.right),
        rx_scaled(insets.bottom),
        rx_scaled(insets.left)
    };
}

float rx_unscaled(float pixels) {
    if (!g_display_manager) rx_display_init();
    return pixels * 100.0f / (float)g_display_manager->primary.scale;
}

float rx_scale_by(float value, rx_scale_factor scale) {
    return value * (float)scale / 100.0f;
}

/* Breakpoint checks */
bool rx_is_at_least(rx_breakpoint bp) {
    if (!g_display_manager) rx_display_init();
    return g_display_manager->current_bp >= bp;
}

bool rx_is_at_most(rx_breakpoint bp) {
    if (!g_display_manager) rx_display_init();
    return g_display_manager->current_bp <= bp;
}

bool rx_is_exactly(rx_breakpoint bp) {
    if (!g_display_manager) rx_display_init();
    return g_display_manager->current_bp == bp;
}

bool rx_is_between(rx_breakpoint min, rx_breakpoint max) {
    if (!g_display_manager) rx_display_init();
    return g_display_manager->current_bp >= min && g_display_manager->current_bp <= max;
}

/* Grid columns for responsive layouts */
int rx_grid_columns(void) {
    if (!g_display_manager) rx_display_init();
    
    switch (g_display_manager->current_bp) {
        case RX_BP_XS: return 1;
        case RX_BP_SM: return 2;
        case RX_BP_MD: return 3;
        case RX_BP_LG: return 4;
        case RX_BP_XL: return 6;
        case RX_BP_XXL: return 12;
        default: return 4;
    }
}

/* Responsive spacing */
float rx_spacing_xs(void) { return rx_scaled(4.0f); }
float rx_spacing_sm(void) { return rx_scaled(8.0f); }
float rx_spacing_md(void) { return rx_scaled(16.0f); }
float rx_spacing_lg(void) { return rx_scaled(24.0f); }
float rx_spacing_xl(void) { return rx_scaled(32.0f); }

/* Font size for text style */
float rx_font_size(rx_text_style style) {
    if (style >= sizeof(base_font_sizes) / sizeof(base_font_sizes[0])) {
        style = RX_TEXT_BODY;
    }
    return rx_scaled(base_font_sizes[style]);
}

/* Line height for text style */
float rx_line_height(rx_text_style style) {
    if (style >= sizeof(line_height_multipliers) / sizeof(line_height_multipliers[0])) {
        style = RX_TEXT_BODY;
    }
    return rx_font_size(style) * line_height_multipliers[style];
}

/* Safe area */
rx_edge_insets rx_safe_area(void) {
    if (!g_display_manager) rx_display_init();
    return g_display_manager->primary.safe_insets;
}

rx_rect rx_safe_content_rect(rx_rect bounds) {
    rx_edge_insets safe = rx_safe_area();
    return (rx_rect){
        bounds.x + safe.left,
        bounds.y + safe.top,
        bounds.width - safe.left - safe.right,
        bounds.height - safe.top - safe.bottom
    };
}

bool rx_is_in_safe_area(rx_point pt) {
    rx_display_info *disp = rx_display_get_primary();
    rx_edge_insets safe = disp->safe_insets;
    
    return pt.x >= safe.left && 
           pt.x <= disp->width_px - safe.right &&
           pt.y >= safe.top && 
           pt.y <= disp->height_px - safe.bottom;
}

/* Responsive value system */
rx_responsive_float rx_responsive(float base) {
    rx_responsive_float resp = {0};
    for (int i = 0; i < RX_BP_COUNT; i++) {
        resp.values[i] = base;
        resp.defined[i] = (i == 0);
    }
    return resp;
}

rx_responsive_float rx_responsive_at(float base, rx_breakpoint bp, float value) {
    rx_responsive_float resp = rx_responsive(base);
    if (bp < RX_BP_COUNT) {
        resp.values[bp] = value;
        resp.defined[bp] = true;
    }
    return resp;
}

float rx_responsive_get(rx_responsive_float *resp, rx_breakpoint current) {
    if (!resp) return 0;
    
    /* Find the value at or below current breakpoint */
    for (int i = current; i >= 0; i--) {
        if (resp->defined[i]) {
            return resp->values[i];
        }
    }
    return resp->values[0];
}

/* Sqrtf fallback if not available */
#ifndef sqrtf
static float sqrtf(float x) {
    if (x <= 0) return 0;
    float guess = x / 2.0f;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0f;
    }
    return guess;
}
#endif
