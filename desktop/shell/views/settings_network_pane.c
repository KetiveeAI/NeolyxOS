/*
 * NeolyxOS - Settings > Network pane (owned by the Networking area)
 *
 * Placeholder; the Networking area replaces this file.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_pane.h"
#include "nxui.h"

static void network_pane_render(nxc_canvas_t *c, nxc_rect_t area) {
    nxui_text_in_rect(c, NXUI_TEXT_BODY, area, "Network", nxui_theme()->text_secondary, NXC_ALIGN_CENTER);
}

static int network_pane_mouse(int32_t lx, int32_t ly, uint8_t b, uint8_t pb) {
    (void)lx; (void)ly; (void)b; (void)pb;
    return 0;
}

const settings_pane_t settings_pane_network = {
    "network", "Network", network_pane_render, network_pane_mouse, 0, 0
};
