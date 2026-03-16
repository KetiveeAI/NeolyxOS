/*
 * NeolyxOS Network Indicator Implementation
 * 
 * Shows network connection status and security state in navigation bar.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "../include/network_indicator.h"
#include "../include/nxi_render.h"
#include "../include/nxsyscall.h"
#include "../include/desktop_shell.h"

/* Include config for reading security settings */
#include "nx_config.h"

/* Global indicator state */
network_indicator_t g_network_indicator = {0};

/* Indicator position (set by navigation bar) */
static int ind_x = 0, ind_y = 0;
static int ind_w = 48, ind_h = 24;

/* Colors from theme */
#define IND_COLOR_CONNECTED   0xFF22C55E  /* Green */
#define IND_COLOR_WARNING     0xFFFBBF24  /* Yellow */
#define IND_COLOR_ERROR       0xFFEF4444  /* Red */
#define IND_COLOR_INACTIVE    0xFF71717A  /* Gray */

/* ============ Initialization ============ */

void network_indicator_init(void) {
    g_network_indicator.state = NET_STATE_DISCONNECTED;
    g_network_indicator.firewall_active = g_nx_config.security.firewall_enabled;
    g_network_indicator.threats_blocked = false;
    g_network_indicator.blocked_count = 0;
    g_network_indicator.active_connections = 0;
    g_network_indicator.signal_strength = 0;
    
    /* Clear IP */
    for (int i = 0; i < 16; i++) {
        g_network_indicator.ip_address[i] = 0;
    }
}

/* ============ Update from kernel/network state ============ */

void network_indicator_update(void) {
    /* Read firewall state from config */
    g_network_indicator.firewall_active = g_nx_config.security.firewall_enabled;
    
    /* TODO: Read actual network state via syscall when available */
    /* For now, default to ethernet connected if network init succeeded */
    
    /* Check if threats were blocked (placeholder - will read from firewall) */
    /* firewall_get_stats() would provide this data */
}

/* ============ Rendering ============ */

/* Forward declare drawing primitives from desktop_shell */
extern void desktop_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);

/* Using desktop_fill_rect since we don't have direct access to backbuffer */
void network_indicator_render(int x, int y) {
    ind_x = x;
    ind_y = y;
    
    uint32_t tint_color;
    
    /* Choose color based on state */
    switch (g_network_indicator.state) {
        case NET_STATE_DISCONNECTED:
            tint_color = IND_COLOR_INACTIVE;
            break;
        case NET_STATE_CONNECTING:
            tint_color = IND_COLOR_WARNING;
            break;
        case NET_STATE_ETHERNET:
        case NET_STATE_WIFI:
            tint_color = IND_COLOR_CONNECTED;
            break;
        case NET_STATE_WIFI_WEAK:
            tint_color = IND_COLOR_WARNING;
            break;
        case NET_STATE_VPN:
            tint_color = IND_COLOR_CONNECTED;
            break;
        default:
            tint_color = IND_COLOR_INACTIVE;
    }
    
    /* Render network indicator as a circle with signal bars */
    /* Using simple primitives since we don't have direct fb access */
    desktop_fill_rect(x, y, 16, 16, tint_color);
    
    /* If firewall is active, show small shield indicator (green dot) */
    if (g_network_indicator.firewall_active) {
        desktop_fill_rect(x + 14, y + 10, 6, 6, IND_COLOR_CONNECTED);
    }
    
    /* If threats blocked, show warning dot (red) */
    if (g_network_indicator.threats_blocked) {
        desktop_fill_rect(x + 12, y, 4, 4, IND_COLOR_ERROR);
    }
}

/* ============ Interaction ============ */

int network_indicator_click(int mx, int my) {
    if (mx >= ind_x && mx < ind_x + ind_w &&
        my >= ind_y && my < ind_y + ind_h) {
        /* TODO: Show network popup with details */
        return 1;
    }
    return 0;
}

void network_indicator_get_bounds(int *x, int *y, int *w, int *h) {
    if (x) *x = ind_x;
    if (y) *y = ind_y;
    if (w) *w = ind_w;
    if (h) *h = ind_h;
}
