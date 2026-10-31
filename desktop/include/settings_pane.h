/*
 * NeolyxOS - Settings pane interface
 *
 * The Settings app (shell/views/settings_view.c) owns the window, the
 * sidebar, search and navigation. Each pane draws into the detail area
 * it is given. Panes that belong to another subsystem (Network) live in
 * that subsystem's file and are registered in settings_view.c's pane table.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETTINGS_PANE_H
#define SETTINGS_PANE_H

#include <stdint.h>
#include "nxcanvas.h"

typedef struct {
    const char *id;          /* stable id, e.g. "network" */
    const char *title;       /* sidebar label */
    /* Draw the pane into area (absolute coordinates, already clipped). */
    void (*render)(nxc_canvas_t *c, nxc_rect_t area);
    /* Pointer in pane-LOCAL coordinates (0,0 = area top-left). Return 1 if handled. */
    int  (*mouse)(int32_t lx, int32_t ly, uint8_t buttons, uint8_t prev_buttons);
    /* Return 1 if handled. May be NULL. */
    int  (*key)(uint8_t scancode, uint8_t pressed, char ascii);
    /* Called when the pane becomes visible. May be NULL. */
    void (*on_show)(void);
} settings_pane_t;

/* Panes provided outside settings_view.c */
extern const settings_pane_t settings_pane_network;   /* shell/views/settings_network_pane.c */

#endif /* SETTINGS_PANE_H */
