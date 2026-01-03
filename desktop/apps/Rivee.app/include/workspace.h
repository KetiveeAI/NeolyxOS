/*
 * Rivee - Workspace Manager
 * Saveable/loadable workspace layouts
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef RIVEE_WORKSPACE_H
#define RIVEE_WORKSPACE_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Panel Types ============ */

typedef enum {
    PANEL_TOOLBOX,       /* Main tool palette */
    PANEL_PROPERTIES,    /* Shape properties */
    PANEL_LAYERS,        /* Layer panel */
    PANEL_PAGES,         /* Multi-page view */
    PANEL_COLOR,         /* Color palette/picker */
    PANEL_BRUSHES,       /* Art brushes */
    PANEL_SYMBOLS,       /* Symbol library */
    PANEL_NAVIGATOR,     /* Document navigator */
    PANEL_HISTORY,       /* Undo history */
    PANEL_HINTS,         /* Quick tips */
    PANEL_TRACE,         /* Image trace settings */
    PANEL_EFFECTS,       /* Effects docker */
    PANEL_ALIGN,         /* Align & distribute */
    PANEL_TRANSFORM,     /* Transform docker */
    PANEL_STYLES,        /* Object styles */
    PANEL_COUNT
} panel_type_t;

/* ============ Panel Position ============ */

typedef enum {
    DOCK_LEFT,
    DOCK_RIGHT,
    DOCK_TOP,
    DOCK_BOTTOM,
    DOCK_FLOATING
} dock_position_t;

/* ============ Panel State ============ */

typedef struct {
    panel_type_t type;
    const char *name;
    dock_position_t dock;
    int x, y;
    int width, height;
    bool visible;
    bool collapsed;
    int tab_group;       /* -1 = not tabbed */
    int tab_index;
} panel_state_t;

/* ============ Toolbar State ============ */

typedef struct {
    const char *id;
    bool visible;
    dock_position_t dock;
    int position;        /* Order in dock */
} toolbar_state_t;

/* ============ Workspace ============ */

typedef struct {
    char name[64];
    char description[128];
    
    /* Panels */
    panel_state_t panels[PANEL_COUNT];
    int panel_count;
    
    /* Toolbars */
    toolbar_state_t toolbars[16];
    int toolbar_count;
    
    /* Window state */
    int window_x, window_y;
    int window_width, window_height;
    bool maximized;
    bool fullscreen;
    
    /* Canvas settings */
    float zoom;
    float pan_x, pan_y;
    bool rulers_visible;
    bool guides_visible;
    bool grid_visible;
    
    /* Color theme */
    bool dark_mode;
    uint32_t accent_color;
} workspace_t;

/* ============ Preset Workspaces ============ */

typedef enum {
    WORKSPACE_DEFAULT,      /* Balanced layout */
    WORKSPACE_ILLUSTRATION, /* Focus on drawing */
    WORKSPACE_LAYOUT,       /* Page layout focus */
    WORKSPACE_LOGO,         /* Logo design */
    WORKSPACE_PHOTO,        /* Photo editing */
    WORKSPACE_MINIMAL,      /* Minimal UI */
    WORKSPACE_CUSTOM
} workspace_preset_t;

/* ============ Workspace API ============ */

/* Create and destroy */
workspace_t *workspace_create(const char *name);
void workspace_destroy(workspace_t *ws);

/* Load presets */
workspace_t *workspace_load_preset(workspace_preset_t preset);

/* Save/Load custom workspaces */
int workspace_save(const workspace_t *ws, const char *path);
workspace_t *workspace_load(const char *path);

/* Apply workspace */
void workspace_apply(const workspace_t *ws);

/* Get current workspace state */
workspace_t *workspace_capture_current(void);

/* Panel management */
void workspace_show_panel(workspace_t *ws, panel_type_t panel);
void workspace_hide_panel(workspace_t *ws, panel_type_t panel);
void workspace_dock_panel(workspace_t *ws, panel_type_t panel, dock_position_t dock);
void workspace_float_panel(workspace_t *ws, panel_type_t panel, int x, int y);
void workspace_tab_panel(workspace_t *ws, panel_type_t panel, panel_type_t attach_to);

/* Toolbar management */
void workspace_show_toolbar(workspace_t *ws, const char *id);
void workspace_hide_toolbar(workspace_t *ws, const char *id);

/* Quick commands */
void workspace_reset(void);
void workspace_toggle_fullscreen(void);
void workspace_cycle_next(void);

/* ============ Component Browser ============ */

typedef enum {
    COMPONENT_SYMBOL,    /* Reusable symbol */
    COMPONENT_BRUSH,     /* Art brush */
    COMPONENT_PATTERN,   /* Fill pattern */
    COMPONENT_GRADIENT,  /* Gradient preset */
    COMPONENT_TEMPLATE,  /* Document template */
    COMPONENT_PALETTE,   /* Color palette */
} component_type_t;

typedef struct {
    char name[64];
    char path[256];
    component_type_t type;
    char thumbnail[256];
    char tags[256];
} component_t;

/* Browse components */
int component_list(component_type_t type, component_t *out, int max);
int component_search(const char *query, component_t *out, int max);

/* Add to document */
void component_insert(const component_t *comp, float x, float y);

#endif /* RIVEE_WORKSPACE_H */
