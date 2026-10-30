/*
 * NeolyxOS Network Indicator Implementation (see network_indicator.h)
 *
 * Menu bar status icon and dropdown, driven by the NXNet client. When the
 * client reports NXNET_ERR_UNSUPPORTED the icon shows Wi-Fi-off and the
 * dropdown says the status is unavailable; nothing is guessed.
 *
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "../include/network_indicator.h"
#include "../include/app_view.h"
#include "../include/nxglass.h"
#include "../include/nxicons.h"
#include "../include/nxnet.h"
#include "../include/nxui.h"
#include "../include/nxsyscall.h"
#include "nx_config.h"

/* Global indicator state */
network_indicator_t g_network_indicator = {0};

static int ind_x = 0, ind_y = 0;
static const int ind_w = 24, ind_h = 24;

/* Cached client state for the icon and dropdown */
#define NI_MAX_NETS 5
static struct {
    nxnet_status_t       avail;
    nxnet_summary_t      sum;
    int                  have_eth;
    nxnet_if_state_t     eth_state;
    nxnet_wifi_network_t nets[NI_MAX_NETS];
    uint32_t             nnets;
    int                  hover;          /* entry index, -1 none */
    uint32_t             gen;
    int                  valid;
} g_ni = { .avail = NXNET_ERR_UNSUPPORTED, .eth_state = NXNET_IF_DISABLED, .hover = -1 };

/* ============ Initialization / update ============ */

void network_indicator_init(void) {
    g_network_indicator.state = NET_STATE_UNAVAILABLE;
    g_network_indicator.firewall_active = g_nx_config.security.firewall_enabled;
    g_network_indicator.threats_blocked = false;
    g_network_indicator.blocked_count = 0;
    g_network_indicator.active_connections = 0;
    g_network_indicator.signal_strength = 0;
    for (int i = 0; i < 16; i++) g_network_indicator.ip_address[i] = 0;
    g_ni.valid = 0;
    nxnet_init();
    network_indicator_update();
}

static void ni_refresh_lists(void) {
    g_ni.have_eth = 0;
    g_ni.eth_state = NXNET_IF_DISABLED;
    g_ni.nnets = 0;
    nxnet_interface_t ifs[NXNET_MAX_INTERFACES];
    uint32_t n = 0;
    nxnet_status_t st = nxnet_interfaces(ifs, NXNET_MAX_INTERFACES, &n);
    if (st == NXNET_ERR_BUFFER_TOO_SMALL) { n = NXNET_MAX_INTERFACES; st = NXNET_OK; }
    if (st == NXNET_OK) {
        for (uint32_t i = 0; i < n; i++) {
            if (ifs[i].kind != NXNET_IF_ETHERNET) continue;
            /* Show the best Ethernet state if there are several ports. */
            if (!g_ni.have_eth || ifs[i].state > g_ni.eth_state) g_ni.eth_state = ifs[i].state;
            g_ni.have_eth = 1;
        }
    }
    if (g_ni.sum.wifi_present && g_ni.sum.wifi_state != NXNET_WIFI_OFF) {
        uint32_t cnt = 0;
        st = nxnet_wifi_scan_results(g_ni.nets, NI_MAX_NETS, &cnt);
        if (st == NXNET_OK || st == NXNET_ERR_BUFFER_TOO_SMALL) g_ni.nnets = cnt < NI_MAX_NETS ? cnt : NI_MAX_NETS;
    }
}

void network_indicator_update(void) {
    g_network_indicator.firewall_active = g_nx_config.security.firewall_enabled;

    g_ni.avail = nxnet_available();
    nxnet_status_t st = g_ni.avail == NXNET_OK ? nxnet_summary(&g_ni.sum) : g_ni.avail;
    g_ni.gen = nxnet_generation();
    g_ni.valid = 1;
    for (int i = 0; i < 16; i++) g_network_indicator.ip_address[i] = 0;
    g_network_indicator.signal_strength = 0;

    if (st != NXNET_OK) {
        g_ni.avail = st;
        g_network_indicator.state = NET_STATE_UNAVAILABLE;
        g_ni.have_eth = 0;
        g_ni.nnets = 0;
        return;
    }
    ni_refresh_lists();

    const nxnet_summary_t *s = &g_ni.sum;
    if (s->connectivity == NXNET_CONN_OFFLINE) {
        g_network_indicator.state = (s->wifi_state == NXNET_WIFI_CONNECTING) ? NET_STATE_CONNECTING : NET_STATE_DISCONNECTED;
    } else if (s->connectivity == NXNET_CONN_LINK_ONLY) {
        g_network_indicator.state = NET_STATE_CONNECTING;
    } else if (s->primary_kind == NXNET_IF_WIFI) {
        g_network_indicator.signal_strength = s->wifi_bars;
        g_network_indicator.state = s->wifi_bars <= 1 ? NET_STATE_WIFI_WEAK : NET_STATE_WIFI;
    } else {
        g_network_indicator.state = NET_STATE_ETHERNET;
    }
    if (s->connectivity >= NXNET_CONN_LOCAL)
        nxnet_ipv4_format(s->primary_address, g_network_indicator.ip_address, sizeof(g_network_indicator.ip_address));
}

static void ni_ensure(void) {
    if (!g_ni.valid || g_ni.gen != nxnet_generation()) network_indicator_update();
}

/* ============ Icon ============ */

void network_indicator_draw_icon(nxc_canvas_t *c, float cx, float cy, float size, nxc_color_t color) {
    if (!c || size <= 0.0f) return;
    ni_ensure();
    switch (g_network_indicator.state) {
        case NET_STATE_ETHERNET:
            nxsym_draw(c, NXSYM_ETHERNET, cx, cy, size, color, 0.0f);
            break;
        case NET_STATE_WIFI:
        case NET_STATE_WIFI_WEAK: {
            float v = (float)g_network_indicator.signal_strength / 4.0f;
            /* Faint full fan behind the lit bars so weak signal still reads as Wi-Fi. */
            nxsym_draw(c, NXSYM_WIFI, cx, cy, size, NXC_WITH_ALPHA(color, 70), 1.0f);
            nxsym_draw(c, NXSYM_WIFI, cx, cy, size, color, v);
            break;
        }
        case NET_STATE_CONNECTING:
            nxsym_draw(c, NXSYM_WIFI, cx, cy, size, NXC_WITH_ALPHA(color, 120), 1.0f);
            break;
        default:   /* disconnected, unavailable */
            nxsym_draw(c, NXSYM_WIFI_OFF, cx, cy, size, NXC_WITH_ALPHA(color, 170), 0.0f);
            break;
    }
}

void network_indicator_render(int x, int y) {
    ind_x = x;
    ind_y = y;
    nxc_canvas_t *c = nxui_canvas();
    if (!c) return;
    network_indicator_draw_icon(c, (float)x + 8.0f, (float)y + 8.0f, 16.0f, nxui_theme()->text_primary);
}

int network_indicator_click(int mx, int my) {
    return mx >= ind_x && mx < ind_x + ind_w && my >= ind_y && my < ind_y + ind_h;
}

void network_indicator_get_bounds(int *x, int *y, int *w, int *h) {
    if (x) *x = ind_x;
    if (y) *y = ind_y;
    if (w) *w = ind_w;
    if (h) *h = ind_h;
}

/* ============ Dropdown layout ============ */

#define NI_W        280.0f
#define NI_PAD        6.0f
#define NI_ROW       32.0f
#define NI_HEADER    40.0f
#define NI_CAPTION   24.0f
#define NI_SEP       11.0f
#define NI_MAX_ENTRIES 16

typedef enum {
    NI_E_HEADER = 0,     /* title + Wi-Fi toggle */
    NI_E_UNAVAILABLE,
    NI_E_WIFI_OFF,
    NI_E_CAPTION,
    NI_E_NETWORK,        /* data = index into g_ni.nets */
    NI_E_ETHERNET,
    NI_E_SEPARATOR,
    NI_E_SETTINGS
} ni_entry_kind_t;

typedef struct { ni_entry_kind_t kind; float y, h; int data; } ni_entry_t;

static uint32_t ni_build(ni_entry_t *e, float *total_h) {
    uint32_t n = 0;
    float y = NI_PAD;
#define NI_ADD(k, hh, d) do { if (n < NI_MAX_ENTRIES) { e[n].kind = (k); e[n].y = y; e[n].h = (hh); e[n].data = (d); n++; } y += (hh); } while (0)
    ni_ensure();
    NI_ADD(NI_E_HEADER, NI_HEADER, 0);
    if (g_ni.avail != NXNET_OK) {
        NI_ADD(NI_E_UNAVAILABLE, 44.0f, 0);
    } else {
        if (g_ni.sum.wifi_present) {
            if (g_ni.sum.wifi_state == NXNET_WIFI_OFF) {
                NI_ADD(NI_E_WIFI_OFF, NI_ROW, 0);
            } else {
                int cur = -1;
                for (uint32_t i = 0; i < g_ni.nnets; i++) if (g_ni.nets[i].current) cur = (int)i;
                if (cur >= 0) NI_ADD(NI_E_NETWORK, NI_ROW, cur);
                if (g_ni.nnets > (cur >= 0 ? 1u : 0u)) {
                    NI_ADD(NI_E_CAPTION, NI_CAPTION, 0);
                    uint32_t shown = 0;
                    for (uint32_t i = 0; i < g_ni.nnets && shown < 4; i++) {
                        if ((int)i == cur) continue;
                        NI_ADD(NI_E_NETWORK, NI_ROW, (int)i);
                        shown++;
                    }
                }
            }
        }
        if (g_ni.have_eth) {
            NI_ADD(NI_E_SEPARATOR, NI_SEP, 0);
            NI_ADD(NI_E_ETHERNET, NI_ROW, 0);
        }
    }
    NI_ADD(NI_E_SEPARATOR, NI_SEP, 0);
    NI_ADD(NI_E_SETTINGS, NI_ROW, 0);
#undef NI_ADD
    if (total_h) *total_h = y + NI_PAD;
    return n;
}

void network_indicator_menu_size(float *w, float *h) {
    ni_entry_t e[NI_MAX_ENTRIES];
    float th = 0.0f;
    ni_build(e, &th);
    if (w) *w = NI_W;
    if (h) *h = th;
}

static nxc_rect_t ni_toggle_rect(nxc_rect_t r) {
    return nxc_rect(r.x + r.w - 14.0f - NXGLASS_TOGGLE_W, r.y + NI_PAD + (NI_HEADER - NXGLASS_TOGGLE_H) * 0.5f,
                    NXGLASS_TOGGLE_W, NXGLASS_TOGGLE_H);
}

static void ni_bars(nxc_canvas_t *c, float x, float base_y, uint8_t bars, nxc_color_t on, nxc_color_t off) {
    for (int i = 0; i < 4; i++) {
        float hh = 4.0f + 3.0f * (float)i;
        nxc_fill_rrect(c, nxc_rect(x + (float)i * 4.5f, base_y - hh, 3.0f, hh), 1.0f, i < bars ? on : off);
    }
}

void network_indicator_draw_menu(nxc_canvas_t *c, nxc_rect_t r, int background) {
    if (!c || r.w <= 0.0f || r.h <= 0.0f) return;
    const nxui_theme_t *th = nxui_theme();
    if (background) nxglass_panel(c, r, th->radius_popover, NXGLASS_THIN, 2, 1);

    ni_entry_t e[NI_MAX_ENTRIES];
    uint32_t n = ni_build(e, 0);
    for (uint32_t i = 0; i < n; i++) {
        nxc_rect_t row = nxc_rect(r.x + NI_PAD, r.y + e[i].y, r.w - 2.0f * NI_PAD, e[i].h);
        float cy = row.y + row.h * 0.5f;
        int hov = g_ni.hover == (int)i;
        switch (e[i].kind) {
            case NI_E_HEADER:
                nxui_text(c, NXUI_TEXT_HEADLINE, row.x + 10.0f, cy + 5.0f,
                          g_ni.avail == NXNET_OK && g_ni.sum.wifi_present ? "Wi-Fi" : "Network", th->text_primary, 0);
                if (g_ni.avail == NXNET_OK && g_ni.sum.wifi_present) {
                    nxc_rect_t t = ni_toggle_rect(r);
                    nxglass_toggle(c, t.x, t.y, g_ni.sum.wifi_state == NXNET_WIFI_OFF ? 0.0f : 1.0f, 0);
                }
                break;
            case NI_E_UNAVAILABLE:
                nxsym_draw(c, NXSYM_WIFI_OFF, row.x + 20.0f, cy, 16.0f, th->text_tertiary, 0.0f);
                nxui_text(c, NXUI_TEXT_BODY, row.x + 38.0f, cy - 2.0f, "Status unavailable", th->text_primary, 0);
                nxui_text(c, NXUI_TEXT_CAPTION, row.x + 38.0f, cy + 13.0f, "Networking service not running",
                          th->text_secondary, 0);
                break;
            case NI_E_WIFI_OFF:
                nxui_text(c, NXUI_TEXT_BODY, row.x + 10.0f, cy + 4.5f, "Wi-Fi is turned off", th->text_secondary, 0);
                break;
            case NI_E_CAPTION:
                nxui_text(c, NXUI_TEXT_CAPTION, row.x + 10.0f, cy + 6.0f, "Other Networks", th->text_tertiary, 0);
                break;
            case NI_E_NETWORK: {
                const nxnet_wifi_network_t *w = &g_ni.nets[e[i].data];
                if (hov) nxc_fill_rrect(c, row, 7.0f, th->control_fill_hover);
                float icx = row.x + 20.0f;
                nxc_fill_circle(c, icx, cy, 12.0f, w->current ? th->accent : th->control_fill_hover);
                ni_bars(c, icx - 7.0f, cy + 6.0f, nxnet_rssi_bars(w->rssi_dbm),
                        w->current ? th->text_on_accent : th->text_primary,
                        w->current ? NXC_WITH_ALPHA(th->text_on_accent, 90) : NXC_WITH_ALPHA(th->text_tertiary, 90));
                float rx = row.x + row.w - 10.0f;
                if (w->security != NXNET_WIFI_SEC_OPEN) {
                    nxsym_draw(c, NXSYM_LOCK, rx - 6.0f, cy, 12.0f, th->text_secondary, 0.0f);
                    rx -= 20.0f;
                }
                nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, rx - (row.x + 42.0f), 0 };
                nxui_text(c, w->current ? NXUI_TEXT_BODY_MEDIUM : NXUI_TEXT_BODY, row.x + 42.0f, cy + 4.5f, w->ssid,
                          th->text_primary, &o);
                break;
            }
            case NI_E_ETHERNET: {
                int on = g_ni.eth_state == NXNET_IF_CONNECTED;
                float icx = row.x + 20.0f;
                nxc_fill_circle(c, icx, cy, 12.0f, on ? th->accent : th->control_fill_hover);
                nxsym_draw(c, NXSYM_ETHERNET, icx, cy, 14.0f, on ? th->text_on_accent : th->text_secondary, 0.0f);
                nxui_text(c, NXUI_TEXT_BODY, row.x + 42.0f, cy + 4.5f, "Ethernet", th->text_primary, 0);
                const char *d = nxnet_if_state_str(g_ni.eth_state);
                float dw = nxui_text_width(NXUI_TEXT_FOOTNOTE, d);
                nxui_text(c, NXUI_TEXT_FOOTNOTE, row.x + row.w - 10.0f - dw, cy + 4.0f, d, th->text_secondary, 0);
                break;
            }
            case NI_E_SEPARATOR:
                nxglass_separator(c, row.x + 10.0f, row.y + row.h * 0.5f, row.w - 20.0f);
                break;
            case NI_E_SETTINGS:
                if (hov) nxc_fill_rrect(c, row, 7.0f, th->control_fill_hover);
                nxui_text(c, NXUI_TEXT_BODY, row.x + 10.0f, cy + 4.5f, "Network Settings\xE2\x80\xA6", th->text_primary, 0);
                break;
        }
    }
}

netind_action_t network_indicator_menu_mouse(float lx, float ly, uint8_t buttons, uint8_t prev_buttons) {
    ni_entry_t e[NI_MAX_ENTRIES];
    float h = 0.0f;
    uint32_t n = ni_build(e, &h);
    g_ni.hover = -1;
    if (lx < 0.0f || ly < 0.0f || lx >= NI_W || ly >= h) return NETIND_ACTION_NONE;
    int idx = -1;
    for (uint32_t i = 0; i < n; i++) if (ly >= e[i].y && ly < e[i].y + e[i].h) idx = (int)i;
    if (idx < 0) return NETIND_ACTION_HANDLED;
    if (e[idx].kind == NI_E_NETWORK || e[idx].kind == NI_E_SETTINGS) g_ni.hover = idx;

    int pressed = (buttons & 1) && !(prev_buttons & 1);
    if (!pressed) return NETIND_ACTION_HANDLED;

    switch (e[idx].kind) {
        case NI_E_HEADER: {
            nxc_rect_t t = ni_toggle_rect(nxc_rect(0, 0, NI_W, h));
            if (g_ni.avail == NXNET_OK && g_ni.sum.wifi_present && nxglass_hit(t, (int32_t)lx, (int32_t)ly)) {
                nxnet_status_t st = nxnet_wifi_set_power(g_ni.sum.wifi_state == NXNET_WIFI_OFF ? 1 : 0);
                if (st != NXNET_OK) nx_debug_print("[NETIND] Wi-Fi toggle refused\n");
                network_indicator_update();
            }
            return NETIND_ACTION_HANDLED;
        }
        case NI_E_NETWORK: {
            const nxnet_wifi_network_t *w = &g_ni.nets[e[idx].data];
            if (w->current) return NETIND_ACTION_CLOSE;
            if (w->security == NXNET_WIFI_SEC_OPEN) {
                nxnet_status_t st = nxnet_wifi_join(w->ssid, "", w->security);
                if (st != NXNET_OK) nx_debug_print("[NETIND] join of open network failed\n");
                network_indicator_update();
                return NETIND_ACTION_CLOSE;
            }
            /* Secured: password entry lives in Settings > Network. */
            char ssid[NXNET_SSID_MAX + 1];
            for (int k = 0; k <= NXNET_SSID_MAX; k++) ssid[k] = w->ssid[k];
            if (settings_view_show_pane("network") != 0)
                nx_debug_print("[NETIND] Settings unavailable for join sheet\n");
            network_pane_begin_join(ssid);
            return NETIND_ACTION_CLOSE;
        }
        case NI_E_SETTINGS:
            if (settings_view_show_pane("network") != 0)
                nx_debug_print("[NETIND] could not open Settings > Network\n");
            return NETIND_ACTION_CLOSE;
        default:
            return NETIND_ACTION_HANDLED;
    }
}
