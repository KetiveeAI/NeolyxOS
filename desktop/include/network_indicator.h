/*
 * NeolyxOS Network Indicator Widget
 * 
 * Shows network connection status and security state in navigation bar.
 * Uses NXI icons, no hardcoded graphics.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef NETWORK_INDICATOR_H
#define NETWORK_INDICATOR_H

#include <stdint.h>
#include <stdbool.h>

/* Network connection states */
typedef enum {
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING,
    NET_STATE_ETHERNET,
    NET_STATE_WIFI,
    NET_STATE_WIFI_WEAK,
    NET_STATE_VPN
} net_indicator_state_t;

/* Network indicator data */
typedef struct {
    net_indicator_state_t state;
    bool firewall_active;
    bool threats_blocked;
    uint32_t blocked_count;       /* Packets blocked today */
    uint32_t active_connections;  /* Active tracked connections */
    char ip_address[16];          /* Current IP */
    int signal_strength;          /* 0-4 for WiFi */
} network_indicator_t;

/* Global indicator state */
extern network_indicator_t g_network_indicator;

/* Initialize network indicator */
void network_indicator_init(void);

/* Update indicator state (call periodically) */
void network_indicator_update(void);

/* Render the indicator in nav bar */
void network_indicator_render(int x, int y);

/* Handle click on indicator (show popup) */
int network_indicator_click(int mx, int my);

/* Get indicator bounds for hit testing */
void network_indicator_get_bounds(int *x, int *y, int *w, int *h);

#endif /* NETWORK_INDICATOR_H */
