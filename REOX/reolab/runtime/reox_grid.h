/*
 * REOX Responsive Grid Layout
 * 
 * CSS Grid and Flexbox-like responsive layout system.
 * Adapts to different screen sizes automatically.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef REOX_GRID_H
#define REOX_GRID_H

#include "reox_ui.h"
#include "reox_display.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Grid Track Sizing
 * ============================================================================ */

typedef enum rx_track_size_type {
    RX_TRACK_FIXED,       /* Fixed pixels */
    RX_TRACK_FR,          /* Fractional unit (flexible) */
    RX_TRACK_AUTO,        /* Content-based */
    RX_TRACK_MIN_CONTENT, /* Minimum content size */
    RX_TRACK_MAX_CONTENT, /* Maximum content size */
    RX_TRACK_FIT_CONTENT, /* Fit content with max */
} rx_track_size_type;

typedef struct rx_track_size {
    rx_track_size_type type;
    float value;           /* For FIXED: pixels, for FR: fraction count */
    float min;             /* Optional minimum */
    float max;             /* Optional maximum */
} rx_track_size;

/* Track size constructors */
RX_INLINE rx_track_size track_px(float pixels) {
    return (rx_track_size){ RX_TRACK_FIXED, pixels, 0, 0 };
}

RX_INLINE rx_track_size track_fr(float fraction) {
    return (rx_track_size){ RX_TRACK_FR, fraction, 0, 0 };
}

RX_INLINE rx_track_size track_auto(void) {
    return (rx_track_size){ RX_TRACK_AUTO, 0, 0, 0 };
}

RX_INLINE rx_track_size track_minmax(float min, float max) {
    return (rx_track_size){ RX_TRACK_FIXED, 0, min, max };
}

/* ============================================================================
 * Grid Container
 * ============================================================================ */

typedef struct rx_grid_layout {
    /* Column definitions */
    rx_track_size *columns;
    int column_count;
    
    /* Row definitions */
    rx_track_size *rows;
    int row_count;
    
    /* Gap between cells */
    float column_gap;
    float row_gap;
    
    /* Alignment */
    rx_alignment justify_items;   /* Horizontal alignment of items */
    rx_alignment align_items;     /* Vertical alignment of items */
    rx_alignment justify_content; /* Horizontal alignment of grid */
    rx_alignment align_content;   /* Vertical alignment of grid */
    
    /* Auto placement */
    bool auto_flow_dense;         /* Dense packing */
    bool auto_flow_row;           /* Row-first (vs column-first) */
    rx_track_size auto_rows;      /* Size for implicitly created rows */
    rx_track_size auto_columns;   /* Size for implicitly created columns */
} rx_grid_layout;

/* Grid item placement */
typedef struct rx_grid_placement {
    int column_start;
    int column_end;           /* -1 = span 1 */
    int row_start;
    int row_end;              /* -1 = span 1 */
    rx_alignment justify_self;
    rx_alignment align_self;
} rx_grid_placement;

/* ============================================================================
 * Responsive Grid
 * ============================================================================ */

/* Responsive column count per breakpoint */
typedef struct rx_responsive_grid {
    int columns[RX_BP_COUNT];    /* Number of columns at each breakpoint */
    float gap[RX_BP_COUNT];      /* Gap at each breakpoint */
    bool defined[RX_BP_COUNT];
} rx_responsive_grid;

/* Create responsive grid */
rx_responsive_grid rx_rgrid_new(int default_cols, float default_gap);
void rx_rgrid_set(rx_responsive_grid *grid, rx_breakpoint bp, int cols, float gap);
int rx_rgrid_get_columns(rx_responsive_grid *grid);
float rx_rgrid_get_gap(rx_responsive_grid *grid);

/* ============================================================================
 * Grid View
 * ============================================================================ */

typedef struct rx_grid_view {
    rx_view base;
    rx_grid_layout layout;
    rx_grid_placement *placements;  /* Placement per child */
    rx_responsive_grid responsive;
    bool use_responsive;
} rx_grid_view;

/* Grid view functions */
extern rx_grid_view *grid_view_new(int columns, int rows);
extern rx_grid_view *grid_view_responsive(rx_responsive_grid grid);
extern void grid_view_add_item(rx_grid_view *grid, rx_view *item, rx_grid_placement placement);
extern void grid_view_set_gap(rx_grid_view *grid, float column_gap, float row_gap);
extern void grid_view_set_columns(rx_grid_view *grid, rx_track_size *cols, int count);
extern void grid_view_set_rows(rx_grid_view *grid, rx_track_size *rows, int count);

/* ============================================================================
 * Flex Container
 * ============================================================================ */

typedef enum rx_flex_direction {
    RX_FLEX_ROW,
    RX_FLEX_ROW_REVERSE,
    RX_FLEX_COLUMN,
    RX_FLEX_COLUMN_REVERSE,
} rx_flex_direction;

typedef enum rx_flex_wrap {
    RX_FLEX_NO_WRAP,
    RX_FLEX_WRAP,
    RX_FLEX_WRAP_REVERSE,
} rx_flex_wrap;

typedef struct rx_flex_layout {
    rx_flex_direction direction;
    rx_flex_wrap wrap;
    rx_alignment justify_content;  /* Main axis */
    rx_alignment align_items;      /* Cross axis */
    rx_alignment align_content;    /* Multi-line cross axis */
    float gap;
} rx_flex_layout;

/* Flex item properties */
typedef struct rx_flex_item {
    float grow;        /* Flex grow factor (0 = don't grow) */
    float shrink;      /* Flex shrink factor (1 = shrink as needed) */
    float basis;       /* Initial size (-1 = auto) */
    rx_alignment align_self;
    int order;         /* Visual order (lower = earlier) */
} rx_flex_item;

/* ============================================================================
 * Flex View
 * ============================================================================ */

typedef struct rx_flex_view {
    rx_view base;
    rx_flex_layout layout;
    rx_flex_item *items;  /* Item properties per child */
} rx_flex_view;

/* Flex view functions */
extern rx_flex_view *flex_view_new(rx_flex_direction direction);
extern void flex_view_add_item(rx_flex_view *flex, rx_view *item, rx_flex_item props);
extern void flex_view_set_gap(rx_flex_view *flex, float gap);
extern void flex_view_set_wrap(rx_flex_view *flex, rx_flex_wrap wrap);
extern void flex_view_set_align(rx_flex_view *flex, rx_alignment justify, rx_alignment align);

/* ============================================================================
 * Responsive Visibility
 * ============================================================================ */

/* Show/hide based on breakpoint */
typedef struct rx_visibility {
    bool visible[RX_BP_COUNT];
} rx_visibility;

/* Visibility helpers */
rx_visibility rx_visible_all(void);
rx_visibility rx_visible_none(void);
rx_visibility rx_visible_at(rx_breakpoint bp);
rx_visibility rx_visible_above(rx_breakpoint bp);
rx_visibility rx_visible_below(rx_breakpoint bp);
bool rx_is_visible(rx_visibility *vis);

/* Apply to view */
extern void view_set_responsive_visibility(rx_view *view, rx_visibility vis);

/* ============================================================================
 * Container Queries (element-based responsiveness)
 * ============================================================================ */

typedef enum rx_container_type {
    RX_CONTAINER_INLINE,   /* Width-based */
    RX_CONTAINER_BLOCK,    /* Height-based */
    RX_CONTAINER_SIZE,     /* Both */
} rx_container_type;

typedef struct rx_container_query {
    rx_container_type type;
    float min_width;
    float max_width;
    float min_height;
    float max_height;
} rx_container_query;

/* Check if container matches query */
bool rx_container_matches(rx_rect container, rx_container_query query);

/* ============================================================================
 * Aspect Ratio
 * ============================================================================ */

typedef struct rx_aspect_ratio {
    float width;
    float height;
} rx_aspect_ratio;

/* Common aspect ratios */
#define RX_ASPECT_1_1    ((rx_aspect_ratio){ 1.0f, 1.0f })
#define RX_ASPECT_16_9   ((rx_aspect_ratio){ 16.0f, 9.0f })
#define RX_ASPECT_4_3    ((rx_aspect_ratio){ 4.0f, 3.0f })
#define RX_ASPECT_21_9   ((rx_aspect_ratio){ 21.0f, 9.0f })
#define RX_ASPECT_9_16   ((rx_aspect_ratio){ 9.0f, 16.0f })

/* Calculate size maintaining aspect ratio */
rx_size rx_fit_aspect(rx_aspect_ratio ratio, rx_size container, bool contain);

#ifdef __cplusplus
}
#endif

#endif /* REOX_GRID_H */
