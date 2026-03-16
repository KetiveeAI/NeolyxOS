/*
 * NeolyxOS Intrusion Detection System (IDS)
 * 
 * Detects and optionally blocks network-based attacks:
 * - SYN floods
 * - Port scanning (integrated with firewall)
 * - Malformed packets
 * - Suspicious traffic patterns
 * - Known attack signatures
 * 
 * Configuration via /System/Config/security.nlc [ids] section.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef IDS_H
#define IDS_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Threat Types ============ */

typedef enum {
    IDS_THREAT_NONE = 0,
    IDS_THREAT_PORT_SCAN,          /* Sequential port probing */
    IDS_THREAT_SYN_FLOOD,          /* TCP SYN flood attack */
    IDS_THREAT_PING_FLOOD,         /* ICMP flood */
    IDS_THREAT_MALFORMED_PACKET,   /* Invalid packet structure */
    IDS_THREAT_SUSPICIOUS_PAYLOAD, /* Known attack signature */
    IDS_THREAT_BRUTE_FORCE,        /* Repeated connection attempts */
    IDS_THREAT_DOS,                /* Denial of service patterns */
    IDS_THREAT_COUNT
} ids_threat_type_t;

/* Threat severity levels */
typedef enum {
    IDS_SEVERITY_LOW = 1,
    IDS_SEVERITY_MEDIUM,
    IDS_SEVERITY_HIGH,
    IDS_SEVERITY_CRITICAL
} ids_severity_t;

/* ============ Threat Detection Entry ============ */

#define IDS_THREAT_LOG_SIZE    64
#define IDS_BLOCKED_IP_SIZE    128

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
    ids_threat_type_t threat_type;
    ids_severity_t severity;
    uint64_t timestamp;
    uint32_t packet_count;         /* Packets in this event */
    bool auto_blocked;             /* Was source auto-blocked? */
} ids_threat_entry_t;

/* Blocked IP entry */
typedef struct {
    uint32_t ip;
    ids_threat_type_t reason;
    uint64_t blocked_at;
    uint64_t block_until;          /* 0 = permanent */
    uint32_t blocked_packets;
} ids_blocked_ip_t;

/* IDS Statistics */
typedef struct {
    uint64_t packets_analyzed;
    uint64_t threats_detected;
    uint64_t threats_blocked;
    uint32_t port_scans;
    uint32_t syn_floods;
    uint32_t blocked_ips;
    uint64_t last_threat_time;
} ids_stats_t;

/* ============ SYN Flood Detection State ============ */

#define IDS_SYN_TABLE_SIZE     128

typedef struct {
    uint32_t src_ip;
    uint16_t syn_count;            /* SYN packets received */
    uint16_t ack_count;            /* Completed handshakes */
    uint64_t window_start;
} ids_syn_entry_t;

/* ============ API Functions ============ */

/* Initialization and shutdown */
void ids_init(void);
void ids_shutdown(void);
void ids_enable(bool enabled);
bool ids_is_enabled(void);

/* Configuration */
void ids_set_sensitivity(uint8_t level);  /* 1=low, 2=medium, 3=high */
void ids_set_auto_block(bool enabled);
bool ids_get_auto_block(void);

/* Packet analysis - call for each incoming packet */
ids_threat_type_t ids_analyze_packet(
    uint32_t src_ip, uint32_t dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, const uint8_t* payload, uint16_t len,
    uint8_t tcp_flags
);

/* Check if IP is blocked */
bool ids_is_ip_blocked(uint32_t ip);

/* Block/unblock IP manually */
void ids_block_ip(uint32_t ip, ids_threat_type_t reason, uint32_t duration_sec);
void ids_unblock_ip(uint32_t ip);

/* Threat log access */
int ids_get_threat_count(void);
ids_threat_entry_t* ids_get_threat_log(void);
void ids_clear_threat_log(void);

/* Statistics */
void ids_get_stats(ids_stats_t* out);
void ids_reset_stats(void);

/* Periodic maintenance (call from timer) */
void ids_tick(void);

/* Get string name for threat type */
const char* ids_threat_name(ids_threat_type_t type);

#endif /* IDS_H */
