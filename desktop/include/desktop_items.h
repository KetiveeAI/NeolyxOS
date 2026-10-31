/*
 * NeolyxOS - Desktop items: the files in ~/Desktop shown on the desktop
 *
 * Items come from the Path relay (nxpath_relay.h); the desktop never
 * reads the file system itself. Items are laid out in a column from the
 * top right. Click selects; double-click opens a folder in Path (a file
 * opens its enclosing Desktop folder in Path until apps can be launched
 * for documents).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef DESKTOP_ITEMS_H
#define DESKTOP_ITEMS_H

#include <stdint.h>
#include "nxcanvas.h"

/* Draw the items. reserve_first: leave the first slot free (a connected
 * device icon sits there). */
void desktop_items_render(nxc_canvas_t *c, int32_t screen_w, int32_t screen_h, int reserve_first);

/* Left-button press on the bare desktop at (x, y). Returns 1 if an item
 * was hit (selected or opened), 0 otherwise (clears the selection). */
int desktop_items_click(int32_t x, int32_t y, int32_t screen_w, int32_t screen_h);

#endif /* DESKTOP_ITEMS_H */
