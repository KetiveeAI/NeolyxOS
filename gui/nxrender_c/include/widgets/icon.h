/*
 * NeolyxOS - NXRender Icon Widget
 * SVG/bitmap icon display
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_ICON_H
#define NXRENDER_ICON_H

#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_icon nx_icon_t;

/* Icon source type */
typedef enum {
    NX_ICON_NONE,
    NX_ICON_BUILTIN,   /* Built-in system icons */
    NX_ICON_PATH,      /* File path to SVG/PNG */
    NX_ICON_DATA       /* Raw image data */
} nx_icon_source_t;

/* Built-in icons */
typedef enum {
    NX_ICON_CLOSE,
    NX_ICON_MINIMIZE,
    NX_ICON_MAXIMIZE,
    NX_ICON_BACK,
    NX_ICON_FORWARD,
    NX_ICON_REFRESH,
    NX_ICON_HOME,
    NX_ICON_SETTINGS,
    NX_ICON_SEARCH,
    NX_ICON_MENU,
    NX_ICON_CHECK,
    NX_ICON_PLUS,
    NX_ICON_MINUS,
    NX_ICON_ARROW_UP,
    NX_ICON_ARROW_DOWN,
    NX_ICON_ARROW_LEFT,
    NX_ICON_ARROW_RIGHT,
    NX_ICON_CHEVRON_UP,
    NX_ICON_CHEVRON_DOWN,
    NX_ICON_CHEVRON_LEFT,
    NX_ICON_CHEVRON_RIGHT,
    NX_ICON_COUNT
} nx_builtin_icon_t;

struct nx_icon {
    nx_widget_t base;
    nx_icon_source_t source;
    nx_builtin_icon_t builtin;
    char* path;
    uint8_t* data;
    size_t data_size;
    uint32_t width;
    uint32_t height;
    nx_color_t tint;
    bool use_tint;
};

/* Create/destroy */
nx_icon_t* nx_icon_create(void);
nx_icon_t* nx_icon_create_builtin(nx_builtin_icon_t icon);
nx_icon_t* nx_icon_create_from_path(const char* path);
void nx_icon_destroy(nx_icon_t* icon);

/* Source */
void nx_icon_set_builtin(nx_icon_t* icon, nx_builtin_icon_t builtin);
void nx_icon_set_path(nx_icon_t* icon, const char* path);
void nx_icon_set_data(nx_icon_t* icon, const uint8_t* data, size_t size);

/* Size */
void nx_icon_set_size(nx_icon_t* icon, uint32_t width, uint32_t height);
uint32_t nx_icon_get_width(nx_icon_t* icon);
uint32_t nx_icon_get_height(nx_icon_t* icon);

/* Tint */
void nx_icon_set_tint(nx_icon_t* icon, nx_color_t tint);
void nx_icon_clear_tint(nx_icon_t* icon);

#ifdef __cplusplus
}
#endif
#endif
