/*
 * NeolyxOS Settings - Security Panel
 * 
 * Comprehensive security settings with network firewall, rate limiting,
 * IDS, connection tracking, and SIP controls.
 * 
 * Reads settings from g_nx_config.security (centralized config system).
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "panel_common.h"
#include "nx_config.h"

/* Forward declare firewall stats getter */
extern int firewall_conn_count(void);

/* Helper to format number as string */
static void int_to_str(int val, char* buf, int max) {
    if (max < 2) return;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[16];
    while (val > 0 && i < 15) { tmp[i++] = '0' + (val % 10); val /= 10; }
    int j = 0;
    if (neg && j < max - 1) buf[j++] = '-';
    while (i > 0 && j < max - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

rx_view* security_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(SECTION_GAP);
    if (!panel) return NULL;
    
    /* Header */
    rx_text_view* header = panel_header("Security & Network");
    if (header) view_add_child(panel, (rx_view*)header);
    
    /* ==================== Network Firewall Card ==================== */
    rx_view* fw_card = settings_card("Network Firewall");
    if (fw_card) {
        /* Firewall enable toggle - reads from config */
        rx_view* fw_toggle = setting_toggle("Enable Firewall", 
                                            g_nx_config.security.firewall_enabled);
        if (fw_toggle) view_add_child(fw_card, fw_toggle);
        
        /* Default policies */
        rx_view* policy_row = hstack_new(8.0f);
        if (policy_row) {
            rx_text_view* policy_lbl = settings_label("Default Policy:", true);
            const char* policy_str = (g_nx_config.security.default_input_policy == 0) 
                                     ? "Allow All" : "Deny All";
            rx_text_view* policy_val = text_view_new(policy_str);
            if (policy_lbl) view_add_child(policy_row, (rx_view*)policy_lbl);
            if (policy_val) {
                policy_val->color = NX_COLOR_PRIMARY;
                view_add_child(policy_row, (rx_view*)policy_val);
            }
            view_add_child(fw_card, policy_row);
        }
        
        /* Active connections count */
        rx_view* conn_row = hstack_new(8.0f);
        if (conn_row) {
            rx_text_view* conn_lbl = settings_label("Active Connections:", true);
            char conn_str[16];
            int_to_str(firewall_conn_count(), conn_str, 16);
            rx_text_view* conn_val = text_view_new(conn_str);
            if (conn_lbl) view_add_child(conn_row, (rx_view*)conn_lbl);
            if (conn_val) {
                conn_val->color = NX_COLOR_SUCCESS;
                view_add_child(conn_row, (rx_view*)conn_val);
            }
            view_add_child(fw_card, conn_row);
        }
        
        rx_button_view* fw_rules_btn = button_view_new("Configure Rules");
        if (fw_rules_btn) view_add_child(fw_card, (rx_view*)fw_rules_btn);
        
        view_add_child(panel, fw_card);
    }
    
    /* ==================== Rate Limiting Card ==================== */
    rx_view* rate_card = settings_card("Rate Limiting (Anti-DDoS)");
    if (rate_card) {
        rx_view* rate_toggle = setting_toggle("Enable Rate Limiting",
                                              g_nx_config.security.rate_limit_enabled);
        if (rate_toggle) view_add_child(rate_card, rate_toggle);
        
        /* Packets per second */
        rx_view* pps_row = hstack_new(8.0f);
        if (pps_row) {
            rx_text_view* pps_lbl = settings_label("Max Packets/sec:", true);
            char pps_str[16];
            int_to_str((int)g_nx_config.security.rate_limit_pps, pps_str, 16);
            rx_text_view* pps_val = text_view_new(pps_str);
            if (pps_lbl) view_add_child(pps_row, (rx_view*)pps_lbl);
            if (pps_val) {
                pps_val->color = NX_COLOR_TEXT;
                view_add_child(pps_row, (rx_view*)pps_val);
            }
            view_add_child(rate_card, pps_row);
        }
        
        /* Burst allowance */
        rx_view* burst_row = hstack_new(8.0f);
        if (burst_row) {
            rx_text_view* burst_lbl = settings_label("Burst Allowance:", true);
            char burst_str[16];
            int_to_str((int)g_nx_config.security.rate_limit_burst, burst_str, 16);
            rx_text_view* burst_val = text_view_new(burst_str);
            if (burst_lbl) view_add_child(burst_row, (rx_view*)burst_lbl);
            if (burst_val) {
                burst_val->color = NX_COLOR_TEXT;
                view_add_child(burst_row, (rx_view*)burst_val);
            }
            view_add_child(rate_card, burst_row);
        }
        
        view_add_child(panel, rate_card);
    }
    
    /* ==================== Connection Tracking Card ==================== */
    rx_view* conn_card = settings_card("Connection Tracking");
    if (conn_card) {
        rx_view* conn_toggle = setting_toggle("Enable Stateful Firewall",
                                              g_nx_config.security.conn_tracking_enabled);
        if (conn_toggle) view_add_child(conn_card, conn_toggle);
        
        /* TCP timeout */
        rx_view* tcp_row = hstack_new(8.0f);
        if (tcp_row) {
            rx_text_view* tcp_lbl = settings_label("TCP Timeout:", true);
            char tcp_str[16];
            int_to_str((int)g_nx_config.security.conn_timeout_tcp, tcp_str, 16);
            rx_text_view* tcp_val = text_view_new(tcp_str);
            rx_text_view* tcp_unit = settings_label("sec", true);
            if (tcp_lbl) view_add_child(tcp_row, (rx_view*)tcp_lbl);
            if (tcp_val) {
                tcp_val->color = NX_COLOR_TEXT;
                view_add_child(tcp_row, (rx_view*)tcp_val);
            }
            if (tcp_unit) view_add_child(tcp_row, (rx_view*)tcp_unit);
            view_add_child(conn_card, tcp_row);
        }
        
        /* UDP timeout */
        rx_view* udp_row = hstack_new(8.0f);
        if (udp_row) {
            rx_text_view* udp_lbl = settings_label("UDP Timeout:", true);
            char udp_str[16];
            int_to_str((int)g_nx_config.security.conn_timeout_udp, udp_str, 16);
            rx_text_view* udp_val = text_view_new(udp_str);
            rx_text_view* udp_unit = settings_label("sec", true);
            if (udp_lbl) view_add_child(udp_row, (rx_view*)udp_lbl);
            if (udp_val) {
                udp_val->color = NX_COLOR_TEXT;
                view_add_child(udp_row, (rx_view*)udp_val);
            }
            if (udp_unit) view_add_child(udp_row, (rx_view*)udp_unit);
            view_add_child(conn_card, udp_row);
        }
        
        view_add_child(panel, conn_card);
    }
    
    /* ==================== Intrusion Detection Card ==================== */
    rx_view* ids_card = settings_card("Intrusion Detection (IDS)");
    if (ids_card) {
        rx_view* ids_toggle = setting_toggle("Enable IDS",
                                             g_nx_config.security.ids_enabled);
        if (ids_toggle) view_add_child(ids_card, ids_toggle);
        
        rx_view* auto_block = setting_toggle("Auto-block Threats",
                                             g_nx_config.security.ids_auto_block);
        if (auto_block) view_add_child(ids_card, auto_block);
        
        /* Sensitivity */
        rx_view* sens_row = hstack_new(8.0f);
        if (sens_row) {
            rx_text_view* sens_lbl = settings_label("Sensitivity:", true);
            const char* sens_str = "Medium";
            if (g_nx_config.security.ids_sensitivity == 1) sens_str = "Low";
            else if (g_nx_config.security.ids_sensitivity == 3) sens_str = "High";
            rx_text_view* sens_val = text_view_new(sens_str);
            if (sens_lbl) view_add_child(sens_row, (rx_view*)sens_lbl);
            if (sens_val) {
                sens_val->color = NX_COLOR_PRIMARY;
                view_add_child(sens_row, (rx_view*)sens_val);
            }
            view_add_child(ids_card, sens_row);
        }
        
        view_add_child(panel, ids_card);
    }
    
    /* ==================== Port Scan Protection Card ==================== */
    rx_view* scan_card = settings_card("Port Scan Protection");
    if (scan_card) {
        rx_view* scan_toggle = setting_toggle("Detect Port Scans",
                                              g_nx_config.security.scan_protection_enabled);
        if (scan_toggle) view_add_child(scan_card, scan_toggle);
        
        /* Threshold */
        rx_view* thresh_row = hstack_new(8.0f);
        if (thresh_row) {
            rx_text_view* thresh_lbl = settings_label("Threshold:", true);
            char thresh_str[16];
            int_to_str((int)g_nx_config.security.scan_threshold_ports, thresh_str, 16);
            rx_text_view* thresh_val = text_view_new(thresh_str);
            rx_text_view* thresh_unit = settings_label("ports in window", true);
            if (thresh_lbl) view_add_child(thresh_row, (rx_view*)thresh_lbl);
            if (thresh_val) {
                thresh_val->color = NX_COLOR_TEXT;
                view_add_child(thresh_row, (rx_view*)thresh_val);
            }
            if (thresh_unit) view_add_child(thresh_row, (rx_view*)thresh_unit);
            view_add_child(scan_card, thresh_row);
        }
        
        /* Window */
        rx_view* window_row = hstack_new(8.0f);
        if (window_row) {
            rx_text_view* window_lbl = settings_label("Time Window:", true);
            char window_str[16];
            int_to_str((int)g_nx_config.security.scan_window_seconds, window_str, 16);
            rx_text_view* window_val = text_view_new(window_str);
            rx_text_view* window_unit = settings_label("sec", true);
            if (window_lbl) view_add_child(window_row, (rx_view*)window_lbl);
            if (window_val) {
                window_val->color = NX_COLOR_TEXT;
                view_add_child(window_row, (rx_view*)window_val);
            }
            if (window_unit) view_add_child(window_row, (rx_view*)window_unit);
            view_add_child(scan_card, window_row);
        }
        
        view_add_child(panel, scan_card);
    }
    
    /* ==================== TCP Hardening Card ==================== */
    rx_view* tcp_card = settings_card("TCP/IP Hardening");
    if (tcp_card) {
        rx_view* syn_toggle = setting_toggle("SYN Cookies (Anti-Flood)",
                                             g_nx_config.security.syn_cookies_enabled);
        if (syn_toggle) view_add_child(tcp_card, syn_toggle);
        
        rx_view* spoof_toggle = setting_toggle("IP Spoofing Protection",
                                               g_nx_config.security.spoofing_protection);
        if (spoof_toggle) view_add_child(tcp_card, spoof_toggle);
        
        /* ICMP rate limit */
        rx_view* icmp_row = hstack_new(8.0f);
        if (icmp_row) {
            rx_text_view* icmp_lbl = settings_label("ICMP Rate Limit:", true);
            char icmp_str[16];
            int_to_str((int)g_nx_config.security.icmp_rate_limit, icmp_str, 16);
            rx_text_view* icmp_val = text_view_new(icmp_str);
            rx_text_view* icmp_unit = settings_label("per sec", true);
            if (icmp_lbl) view_add_child(icmp_row, (rx_view*)icmp_lbl);
            if (icmp_val) {
                icmp_val->color = NX_COLOR_TEXT;
                view_add_child(icmp_row, (rx_view*)icmp_val);
            }
            if (icmp_unit) view_add_child(icmp_row, (rx_view*)icmp_unit);
            view_add_child(tcp_card, icmp_row);
        }
        
        view_add_child(panel, tcp_card);
    }
    
    /* ==================== System Integrity Protection ==================== */
    rx_view* sip_card = settings_card("System Integrity Protection");
    if (sip_card) {
        rx_view* sip_status = hstack_new(8.0f);
        if (sip_status) {
            rx_text_view* sip_icon = text_view_new("[protected]");
            rx_text_view* sip_text = text_view_new("SIP is enabled");
            if (sip_icon) {
                sip_icon->color = NX_COLOR_SUCCESS;
                view_add_child(sip_status, (rx_view*)sip_icon);
            }
            if (sip_text) {
                sip_text->color = NX_COLOR_TEXT;
                view_add_child(sip_status, (rx_view*)sip_text);
            }
            view_add_child(sip_card, sip_status);
        }
        
        rx_text_view* sip_info = settings_label(
            "Protects system files from modification. Disable in Recovery Mode.", true);
        if (sip_info) view_add_child(sip_card, (rx_view*)sip_info);
        
        view_add_child(panel, sip_card);
    }
    
    /* ==================== System Lock Card ==================== */
    rx_view* lock_card = settings_card("System Lock");
    if (lock_card) {
        rx_view* auto_lock = setting_toggle("Auto-lock on Sleep", true);
        if (auto_lock) view_add_child(lock_card, auto_lock);
        
        rx_view* lock_row = hstack_new(8.0f);
        if (lock_row) {
            rx_text_view* timeout_label = settings_label("Lock after:", true);
            rx_text_view* timeout_value = text_view_new("5 minutes");
            if (timeout_label) view_add_child(lock_row, (rx_view*)timeout_label);
            if (timeout_value) {
                timeout_value->color = NX_COLOR_TEXT;
                view_add_child(lock_row, (rx_view*)timeout_value);
            }
            view_add_child(lock_card, lock_row);
        }
        
        rx_button_view* change_pin = button_view_new("Change PIN");
        if (change_pin) view_add_child(lock_card, (rx_view*)change_pin);
        
        view_add_child(panel, lock_card);
    }
    
    return panel;
}
