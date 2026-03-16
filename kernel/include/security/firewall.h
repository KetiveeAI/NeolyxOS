/*
 * NeolyxOS Firewall Header
 * 
 * Kernel-level packet filtering and network security.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_FIREWALL_H
#define NEOLYX_FIREWALL_H

#include <stdint.h>

/* Rule actions */
#define FW_ACTION_ALLOW     0
#define FW_ACTION_DENY      1
#define FW_ACTION_LOG       2

/* Rule directions */
#define FW_DIR_INPUT        0
#define FW_DIR_OUTPUT       1
#define FW_DIR_FORWARD      2

/* Protocols */
#define FW_PROTO_ANY        0
#define FW_PROTO_TCP        6
#define FW_PROTO_UDP        17
#define FW_PROTO_ICMP       1

/* Max rules */
#define FW_MAX_RULES        256

/* Firewall rule structure */
typedef struct {
    uint32_t id;
    uint8_t  direction;     /* INPUT/OUTPUT/FORWARD */
    uint8_t  action;        /* ALLOW/DENY/LOG */
    uint8_t  protocol;      /* TCP/UDP/ICMP/ANY */
    uint8_t  enabled;
    
    uint32_t src_ip;        /* Source IP (0 = any) */
    uint32_t src_mask;      /* Source netmask */
    uint32_t dst_ip;        /* Destination IP (0 = any) */
    uint32_t dst_mask;      /* Destination netmask */
    
    uint16_t src_port_min;  /* Source port range start */
    uint16_t src_port_max;  /* Source port range end */
    uint16_t dst_port_min;  /* Dest port range start */
    uint16_t dst_port_max;  /* Dest port range end */
    
    uint64_t packets_matched;
    uint64_t bytes_matched;
} firewall_rule_t;

/* Packet info for filtering */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  direction;
    uint32_t length;
} packet_info_t;

/* Firewall statistics */
typedef struct {
    uint64_t packets_allowed;
    uint64_t packets_denied;
    uint64_t packets_logged;
    uint64_t total_bytes;
    uint32_t active_rules;
    uint8_t  enabled;
} firewall_stats_t;

/* API Functions */
void firewall_init(void);
int  firewall_enable(void);
int  firewall_disable(void);
int  firewall_is_enabled(void);

/* Rule management */
int  firewall_add_rule(firewall_rule_t *rule);
int  firewall_remove_rule(uint32_t rule_id);
int  firewall_enable_rule(uint32_t rule_id);
int  firewall_disable_rule(uint32_t rule_id);
int  firewall_get_rule(uint32_t rule_id, firewall_rule_t *out);
int  firewall_get_rule_count(void);
void firewall_clear_rules(void);

/* Packet checking */
int  firewall_check_packet(packet_info_t *pkt);

/* Statistics */
void firewall_get_stats(firewall_stats_t *stats);
void firewall_reset_stats(void);

/* Default policies */
void firewall_set_default_policy(uint8_t direction, uint8_t action);

/* ============ Rate Limiting (Anti-DDoS) ============ */

#define FW_RATE_LIMIT_ENTRIES   256

typedef struct {
    uint32_t ip;
    uint32_t packet_count;
    uint64_t window_start;      /* Timestamp of window start */
} rate_limit_entry_t;

/* Rate limiting API */
void firewall_set_rate_limit(uint32_t max_pps, uint16_t burst);
int firewall_check_rate_limit(uint32_t ip);
void firewall_rate_limit_reset(void);

/* ============ Connection Tracking ============ */

#define FW_CONN_TABLE_SIZE      512

/* Connection states */
#define FW_CONN_NONE            0
#define FW_CONN_NEW             1
#define FW_CONN_ESTABLISHED     2
#define FW_CONN_RELATED         3
#define FW_CONN_CLOSING         4
#define FW_CONN_CLOSED          5

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  state;
    uint64_t last_seen;
    uint32_t packets;
    uint32_t bytes;
} conn_track_entry_t;

/* Connection tracking API */
void firewall_enable_conn_tracking(int enable);
int firewall_conn_track_packet(packet_info_t *pkt);
conn_track_entry_t* firewall_conn_lookup(uint32_t src_ip, uint32_t dst_ip,
                                          uint16_t src_port, uint16_t dst_port);
void firewall_conn_cleanup(uint16_t tcp_timeout, uint16_t udp_timeout);
int firewall_conn_count(void);

/* ============ Port Scan Detection ============ */

typedef struct {
    uint32_t src_ip;
    uint16_t ports_accessed[32];
    uint8_t port_count;
    uint64_t first_access;
} scan_detect_entry_t;

int firewall_check_port_scan(uint32_t src_ip, uint16_t port);
void firewall_scan_cleanup(void);

/* ============ App-Level Network Control ============ */

/* App network permission flags */
#define FW_APP_NET_NONE         0x00
#define FW_APP_NET_OUTGOING     0x01
#define FW_APP_NET_INCOMING     0x02
#define FW_APP_NET_ALL          0x03
#define FW_APP_NET_BLOCKED      0x80

/* Max tracked apps */
#define FW_MAX_APPS             64

/* App network registration */
typedef struct {
    uint32_t pid;
    uint8_t  permissions;
    uint16_t allowed_ports[8];   /* Outgoing ports allowed */
    uint8_t  port_count;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t blocked_attempts;
} firewall_app_t;

/* App control API */
int  firewall_register_app(uint32_t pid, uint8_t permissions);
int  firewall_revoke_app(uint32_t pid);
int  firewall_app_allow_port(uint32_t pid, uint16_t port);
int  firewall_app_block_port(uint32_t pid, uint16_t port);
int  firewall_app_set_permissions(uint32_t pid, uint8_t perms);
int  firewall_check_app_packet(uint32_t pid, packet_info_t *pkt);
void firewall_get_app_stats(uint32_t pid, firewall_app_t *out);

#endif /* NEOLYX_FIREWALL_H */
