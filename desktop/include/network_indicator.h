/*
 * NeolyxOS Network Indicator (menu bar status item + dropdown)
 *
 * Owned by the Networking area. State comes from the NXNet client
 * (nxnet.h); this module only turns it into an icon and a dropdown.
 *
 * Integration (menu bar / Control Center owners):
 *   - Once per second (or when the dropdown opens):
 *         network_indicator_update();
 *   - Status icon in the menu bar, replacing a fixed NXSYM_WIFI draw:
 *         network_indicator_draw_icon(c, cx, cy, 16.0f, th->text_primary);
 *   - Dropdown under the icon (r.x/r.y = top-left, size from _menu_size):
 *         network_indicator_draw_menu(c, r, 1);
 *     with pointer events in dropdown-local coordinates:
 *         network_indicator_menu_mouse(lx, ly, buttons, prev_buttons);
 *     A return of NETIND_ACTION_CLOSE means the caller should hide it.
 *
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef NETWORK_INDICATOR_H
#define NETWORK_INDICATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "nxcanvas.h"

/* Network connection states */
typedef enum {
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING,
    NET_STATE_ETHERNET,
    NET_STATE_WIFI,
    NET_STATE_WIFI_WEAK,
    NET_STATE_VPN,
    NET_STATE_UNAVAILABLE      /* networking client returned UNSUPPORTED */
} net_indicator_state_t;

/* Network indicator data */
typedef struct {
    net_indicator_state_t state;
    bool firewall_active;
    bool threats_blocked;
    uint32_t blocked_count;       /* Packets blocked today */
    uint32_t active_connections;  /* Active tracked connections */
    char ip_address[16];          /* Current IP ("" when none) */
    int signal_strength;          /* 0-4 for WiFi */
} network_indicator_t;

/* Global indicator state (read-only for other modules) */
extern network_indicator_t g_network_indicator;

/* Initialize network indicator */
void network_indicator_init(void);

/* Update indicator state from NXNet (cheap; call about once per second) */
void network_indicator_update(void);

/* Legacy: draw the 16x16 icon with its top-left at (x, y) on the NXUI canvas. */
void network_indicator_render(int x, int y);

/* Legacy hit test against the last network_indicator_render() position. */
int network_indicator_click(int mx, int my);

/* Get indicator bounds for hit testing */
void network_indicator_get_bounds(int *x, int *y, int *w, int *h);

/* ============ NXCanvas API ============ */

/* Status icon centered at (cx, cy): Ethernet glyph, Wi-Fi with 0..4 bars,
 * or Wi-Fi-off when offline/unavailable. */
void network_indicator_draw_icon(nxc_canvas_t *c, float cx, float cy, float size, nxc_color_t color);

/* Dropdown content size in px. */
void network_indicator_menu_size(float *w, float *h);

/* Draw the dropdown at r (absolute). background=1 also draws the glass
 * panel and shadow; 0 draws content only (caller supplies the surface). */
void network_indicator_draw_menu(nxc_canvas_t *c, nxc_rect_t r, int background);

typedef enum {
    NETIND_ACTION_NONE = 0,      /* not handled */
    NETIND_ACTION_HANDLED,       /* hover/toggle handled, keep open */
    NETIND_ACTION_CLOSE          /* an item was chosen; caller hides the dropdown */
} netind_action_t;

/* Pointer in dropdown-local coordinates. Choosing "Network Settings..."
 * or a secured network opens Settings > Network (settings_view_show_pane). */
netind_action_t network_indicator_menu_mouse(float lx, float ly, uint8_t buttons, uint8_t prev_buttons);

/* Settings > Network pane: open the join sheet for ssid (shell/views/settings_network_pane.c).
 * Returns 0 if the network is listed, -1 otherwise. */
int network_pane_begin_join(const char *ssid);

#endif /* NETWORK_INDICATOR_H */
