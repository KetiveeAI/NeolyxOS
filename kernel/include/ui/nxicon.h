/*
 * NeolyxOS Icon Format (NXI)
 * 
 * Vector-based icon format for scalable, resolution-independent icons.
 * Similar to SVG but optimized for fast kernel rendering.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXICON_H
#define NXICON_H

#include <stdint.h>

/* ============ File Format ============ */

#define NXI_MAGIC       "NXI\0"
#define NXI_VERSION     1

/* Path commands */
#define NXI_CMD_END     0   /* End of path */
#define NXI_CMD_MOVE    1   /* Move to (x, y) */
#define NXI_CMD_LINE    2   /* Line to (x, y) */
#define NXI_CMD_CURVE   3   /* Quadratic bezier: control (cx,cy), end (x,y) */
#define NXI_CMD_ARC     4   /* Arc: radius r, end (x,y) */
#define NXI_CMD_CLOSE   5   /* Close path (line to start) */
#define NXI_CMD_FILL    6   /* Fill enclosed area with color */
#define NXI_CMD_STROKE  7   /* Stroke path with color and width */

/* NXI file header */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];      /* "NXI\0" */
    uint16_t version;       /* Format version */
    uint16_t icon_count;    /* Number of icon variants */
    uint32_t flags;         /* Reserved flags */
} nxi_header_t;

/* Icon entry (each size variant) */
typedef struct __attribute__((packed)) {
    uint32_t id;            /* Icon ID */
    char     name[32];      /* Icon name */
    uint16_t base_width;    /* Base width (design size) */
    uint16_t base_height;   /* Base height */
    uint32_t path_offset;   /* Offset to path data */
    uint16_t path_count;    /* Number of path elements */
    uint16_t reserved;
} nxi_entry_t;

/* Path element */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;           /* Command type */
    int16_t  x, y;          /* Coordinates (in icon units 0-1000) */
    int16_t  cx, cy;        /* Control point (for curves) */
    uint32_t color;         /* ARGB color (for FILL/STROKE) */
    uint8_t  width;         /* Stroke width (for STROKE) */
    uint8_t  reserved[3];
} nxi_path_t;

/* ============ In-Memory Icon ============ */

#define NXI_MAX_PATHS 256
#define NXI_MAX_ICONS 64
#define NXI_MAX_COLORS 32

/* Theme modes */
typedef enum {
    NXI_THEME_LIGHT = 0,
    NXI_THEME_DARK  = 1
} nxi_theme_t;

/* ViewBox for proper scaling */
typedef struct {
    int16_t x, y;       /* Origin */
    int16_t width;      /* ViewBox width */
    int16_t height;     /* ViewBox height */
} nxi_viewbox_t;

/* Color with theme variants */
typedef struct {
    uint32_t light;     /* Light theme color */
    uint32_t dark;      /* Dark theme color */
} nxi_color_t;

typedef struct {
    uint32_t id;
    char name[32];
    uint16_t base_width;
    uint16_t base_height;
    nxi_viewbox_t viewbox;  /* ViewBox for scaling */
    nxi_path_t paths[NXI_MAX_PATHS];
    nxi_path_t paths_dark[NXI_MAX_PATHS];  /* Dark theme paths (optional) */
    uint16_t path_count;
    uint8_t has_dark_theme; /* 1 if dark theme defined */
    uint8_t reserved;
} nxi_icon_t;

typedef struct {
    nxi_icon_t icons[NXI_MAX_ICONS];
    uint16_t icon_count;
} nxi_iconset_t;

/* ============ Built-in System Icons ============ */

/* Icon IDs */
#define NXI_ICON_APP_DEFAULT    1
#define NXI_ICON_FOLDER         2
#define NXI_ICON_FOLDER_OPEN    3
#define NXI_ICON_FILE           4
#define NXI_ICON_FILE_TEXT      5
#define NXI_ICON_FILE_IMAGE     6
#define NXI_ICON_FILE_AUDIO     7
#define NXI_ICON_FILE_VIDEO     8
#define NXI_ICON_TERMINAL       9
#define NXI_ICON_SETTINGS       10
#define NXI_ICON_FONT           11
#define NXI_ICON_DISK           12
#define NXI_ICON_NETWORK        13
#define NXI_ICON_LOCK           14
#define NXI_ICON_UNLOCK         15
#define NXI_ICON_SEARCH         16
#define NXI_ICON_HOME           17
#define NXI_ICON_TRASH          18
#define NXI_ICON_REFRESH        19
#define NXI_ICON_CLOSE          20
#define NXI_ICON_MINIMIZE       21
#define NXI_ICON_MAXIMIZE       22
#define NXI_ICON_ARROW_LEFT     23
#define NXI_ICON_ARROW_RIGHT    24
#define NXI_ICON_ARROW_UP       25
#define NXI_ICON_ARROW_DOWN     26
#define NXI_ICON_CHECK          27
#define NXI_ICON_CROSS          28
#define NXI_ICON_PLUS           29
#define NXI_ICON_MINUS          30

/* ============ API Functions ============ */

/* Initialize icon system */
void nxi_init(void);

/* Load iconset from file data */
int nxi_load(nxi_iconset_t *set, const uint8_t *data, uint32_t size);

/* Get icon by ID */
const nxi_icon_t* nxi_get_icon(uint32_t id);

/* Get icon by name */
const nxi_icon_t* nxi_get_icon_by_name(const char *name);

/* Render icon to framebuffer (uses current theme) */
void nxi_render(const nxi_icon_t *icon, int x, int y, int size,
                volatile uint32_t *fb, uint32_t pitch,
                uint32_t fb_width, uint32_t fb_height);

/* Render icon with specific theme */
void nxi_render_themed(const nxi_icon_t *icon, int x, int y, int size,
                       nxi_theme_t theme,
                       volatile uint32_t *fb, uint32_t pitch,
                       uint32_t fb_width, uint32_t fb_height);

/* Render icon with custom tint color */
void nxi_render_tinted(const nxi_icon_t *icon, int x, int y, int size,
                       uint32_t tint_color,
                       volatile uint32_t *fb, uint32_t pitch,
                       uint32_t fb_width, uint32_t fb_height);

/* Get built-in system iconset */
const nxi_iconset_t* nxi_get_system_icons(void);

/* Theme management */
void nxi_set_theme(nxi_theme_t theme);
nxi_theme_t nxi_get_theme(void);

#endif /* NXICON_H */
