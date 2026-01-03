/*
 * NeolyxOS Font Manager - NXRender Integration
 * 
 * Uses the existing nxrender_c library for UI rendering.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef FONTMANAGER_UI_H
#define FONTMANAGER_UI_H

/* Use existing NXRender library */
#include "../../../../gui/nxrender_c/include/nxrender.h"

/* Use NXI icons from kernel */
#include "../../../../kernel/include/ui/nxicon.h"

/* Font Manager state */
#include "state.h"

/* ============ UI Components ============ */

typedef struct {
    /* NXRender context */
    nx_context_t *ctx;
    nx_window_t *window;
    
    /* Main containers */
    nx_widget_t *sidebar;
    nx_widget_t *font_list;
    nx_widget_t *preview_panel;
    
    /* Sidebar items */
    nx_widget_t *filter_buttons[4];
    
    /* Preview panel components */
    nx_widget_t *preview_label;
    nx_widget_t *install_button;
    nx_widget_t *size_slider;
    
    /* Icon widgets */
    nx_widget_t *font_icon;
    nx_widget_t *folder_icon;
    
    /* Font Manager state */
    fm_state_t *state;
} fontmanager_ui_t;

/* ============ API ============ */

/* Initialize Font Manager UI with NXRender context */
int fontmanager_ui_init(fontmanager_ui_t *ui, nx_context_t *ctx, fm_state_t *state);

/* Build the UI tree */
void fontmanager_ui_build(fontmanager_ui_t *ui);

/* Update UI from state (after dispatch) */
void fontmanager_ui_update(fontmanager_ui_t *ui);

/* Render the UI */
void fontmanager_ui_render(fontmanager_ui_t *ui);

/* Handle events */
int fontmanager_ui_handle_event(fontmanager_ui_t *ui, nx_event_t *event);

/* Cleanup */
void fontmanager_ui_destroy(fontmanager_ui_t *ui);

/* ============ Icon Widget Wrapper ============ */

/* Custom widget that renders NXI icons */
typedef struct {
    nx_widget_t base;
    uint32_t icon_id;
    uint32_t icon_size;
    uint32_t tint_color;
} nx_nxicon_widget_t;

/* Create NXI icon widget */
nx_nxicon_widget_t* nx_nxicon_create(uint32_t icon_id, uint32_t size);

/* Set icon tint color */
void nx_nxicon_set_tint(nx_nxicon_widget_t *w, uint32_t color);

#endif /* FONTMANAGER_UI_H */
