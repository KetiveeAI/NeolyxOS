/*
 * NeolyxOS Intrusion Detection System (IDS) Implementation
 * 
 * Detects network attacks by analyzing packet patterns and signatures.
 * Integrates with firewall for blocking and with config for settings.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "security/ids.h"
#include "security/firewall.h"
#include "nx_config.h"

/* Serial output for debug logging */
extern void serial_puts(const char* s);

/* Time helper */
extern uint64_t pit_get_ticks(void);

/* ============ Static State ============ */

static bool g_ids_enabled = false;
static bool g_ids_auto_block = false;
static uint8_t g_ids_sensitivity = 2;  /* 1=low, 2=medium, 3=high */

/* Threat log */
static ids_threat_entry_t g_threat_log[IDS_THREAT_LOG_SIZE];
static int g_threat_log_count = 0;
static int g_threat_log_idx = 0;  /* Circular buffer index */

/* Blocked IPs */
static ids_blocked_ip_t g_blocked_ips[IDS_BLOCKED_IP_SIZE];
static int g_blocked_count = 0;

/* SYN flood detection */
static ids_syn_entry_t g_syn_table[IDS_SYN_TABLE_SIZE];

/* Statistics */
static ids_stats_t g_stats;

/* Thresholds based on sensitivity */
static uint16_t syn_threshold = 50;      /* SYN packets per second */
static uint16_t icmp_threshold = 20;     /* ICMP packets per second */
static uint16_t conn_threshold = 100;    /* Connections per second */

/* ============ Initialization ============ */

void ids_init(void) {
    serial_puts("[IDS] Initializing Intrusion Detection System...\n");
    
    /* Use default settings (config module not linked to kernel directly) */
    g_ids_enabled = true;
    g_ids_auto_block = false;
    g_ids_sensitivity = 2;  /* Medium */
    
    /* Adjust thresholds based on sensitivity */
    ids_set_sensitivity(g_ids_sensitivity);
    
    /* Clear threat log */
    g_threat_log_count = 0;
    g_threat_log_idx = 0;
    for (int i = 0; i < IDS_THREAT_LOG_SIZE; i++) {
        g_threat_log[i].threat_type = IDS_THREAT_NONE;
    }
    
    /* Clear blocked IPs */
    g_blocked_count = 0;
    for (int i = 0; i < IDS_BLOCKED_IP_SIZE; i++) {
        g_blocked_ips[i].ip = 0;
    }
    
    /* Clear SYN table */
    for (int i = 0; i < IDS_SYN_TABLE_SIZE; i++) {
        g_syn_table[i].src_ip = 0;
    }
    
    /* Reset stats */
    g_stats.packets_analyzed = 0;
    g_stats.threats_detected = 0;
    g_stats.threats_blocked = 0;
    g_stats.port_scans = 0;
    g_stats.syn_floods = 0;
    g_stats.blocked_ips = 0;
    g_stats.last_threat_time = 0;
    
    serial_puts("[IDS] Initialized with sensitivity ");
    serial_puts(g_ids_sensitivity == 1 ? "LOW" : 
                g_ids_sensitivity == 3 ? "HIGH" : "MEDIUM");
    serial_puts("\n");
}

void ids_shutdown(void) {
    g_ids_enabled = false;
    serial_puts("[IDS] Shutdown\n");
}

void ids_enable(bool enabled) {
    g_ids_enabled = enabled;
}

bool ids_is_enabled(void) {
    return g_ids_enabled;
}

/* ============ Configuration ============ */

void ids_set_sensitivity(uint8_t level) {
    g_ids_sensitivity = level;
    
    switch (level) {
        case 1:  /* Low - fewer false positives */
            syn_threshold = 100;
            icmp_threshold = 50;
            conn_threshold = 200;
            break;
        case 3:  /* High - more aggressive */
            syn_threshold = 20;
            icmp_threshold = 10;
            conn_threshold = 50;
            break;
        default: /* Medium */
            syn_threshold = 50;
            icmp_threshold = 20;
            conn_threshold = 100;
    }
}

void ids_set_auto_block(bool enabled) {
    g_ids_auto_block = enabled;
}

bool ids_get_auto_block(void) {
    return g_ids_auto_block;
}

/* ============ Internal Helpers ============ */

static void log_threat(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                       uint8_t protocol, ids_threat_type_t type, 
                       ids_severity_t severity, bool blocked) {
    
    ids_threat_entry_t* entry = &g_threat_log[g_threat_log_idx];
    entry->src_ip = src_ip;
    entry->src_port = src_port;
    entry->dst_port = dst_port;
    entry->protocol = protocol;
    entry->threat_type = type;
    entry->severity = severity;
    entry->timestamp = pit_get_ticks();
    entry->packet_count = 1;
    entry->auto_blocked = blocked;
    
    g_threat_log_idx = (g_threat_log_idx + 1) % IDS_THREAT_LOG_SIZE;
    if (g_threat_log_count < IDS_THREAT_LOG_SIZE) {
        g_threat_log_count++;
    }
    
    g_stats.threats_detected++;
    if (blocked) g_stats.threats_blocked++;
    g_stats.last_threat_time = pit_get_ticks();
}

static int find_blocked_ip_slot(uint32_t ip) {
    for (int i = 0; i < IDS_BLOCKED_IP_SIZE; i++) {
        if (g_blocked_ips[i].ip == ip) return i;
    }
    return -1;
}

static int find_free_blocked_slot(void) {
    for (int i = 0; i < IDS_BLOCKED_IP_SIZE; i++) {
        if (g_blocked_ips[i].ip == 0) return i;
    }
    /* Evict oldest entry */
    uint64_t oldest = ~0ULL;
    int oldest_idx = 0;
    for (int i = 0; i < IDS_BLOCKED_IP_SIZE; i++) {
        if (g_blocked_ips[i].blocked_at < oldest) {
            oldest = g_blocked_ips[i].blocked_at;
            oldest_idx = i;
        }
    }
    return oldest_idx;
}

/* ============ IP Blocking ============ */

bool ids_is_ip_blocked(uint32_t ip) {
    uint64_t now = pit_get_ticks();
    
    for (int i = 0; i < IDS_BLOCKED_IP_SIZE; i++) {
        if (g_blocked_ips[i].ip == ip) {
            /* Check if block has expired */
            if (g_blocked_ips[i].block_until > 0 && 
                now > g_blocked_ips[i].block_until) {
                /* Expired - remove */
                g_blocked_ips[i].ip = 0;
                g_blocked_count--;
                return false;
            }
            g_blocked_ips[i].blocked_packets++;
            return true;
        }
    }
    return false;
}

void ids_block_ip(uint32_t ip, ids_threat_type_t reason, uint32_t duration_sec) {
    int idx = find_blocked_ip_slot(ip);
    if (idx < 0) {
        idx = find_free_blocked_slot();
        g_blocked_count++;
        g_stats.blocked_ips++;
    }
    
    g_blocked_ips[idx].ip = ip;
    g_blocked_ips[idx].reason = reason;
    g_blocked_ips[idx].blocked_at = pit_get_ticks();
    g_blocked_ips[idx].block_until = (duration_sec == 0) ? 0 : 
                                      pit_get_ticks() + (duration_sec * 1000);
    g_blocked_ips[idx].blocked_packets = 0;
    
    serial_puts("[IDS] Blocked IP for ");
    serial_puts(ids_threat_name(reason));
    serial_puts("\n");
}

void ids_unblock_ip(uint32_t ip) {
    int idx = find_blocked_ip_slot(ip);
    if (idx >= 0) {
        g_blocked_ips[idx].ip = 0;
        g_blocked_count--;
    }
}

/* ============ SYN Flood Detection ============ */

static ids_threat_type_t check_syn_flood(uint32_t src_ip, uint8_t tcp_flags) {
    /* Only track SYN packets (SYN flag = 0x02, without ACK = 0x10) */
    if ((tcp_flags & 0x02) == 0 || (tcp_flags & 0x10)) {
        return IDS_THREAT_NONE;
    }
    
    uint64_t now = pit_get_ticks();
    int lru_idx = 0;
    uint64_t lru_time = ~0ULL;
    
    for (int i = 0; i < IDS_SYN_TABLE_SIZE; i++) {
        if (g_syn_table[i].src_ip == src_ip) {
            /* Check if in same window (1 second) */
            if (now - g_syn_table[i].window_start < 1000) {
                g_syn_table[i].syn_count++;
                if (g_syn_table[i].syn_count > syn_threshold) {
                    g_stats.syn_floods++;
                    return IDS_THREAT_SYN_FLOOD;
                }
            } else {
                /* New window */
                g_syn_table[i].window_start = now;
                g_syn_table[i].syn_count = 1;
            }
            return IDS_THREAT_NONE;
        }
        
        if (g_syn_table[i].window_start < lru_time) {
            lru_time = g_syn_table[i].window_start;
            lru_idx = i;
        }
    }
    
    /* New IP */
    g_syn_table[lru_idx].src_ip = src_ip;
    g_syn_table[lru_idx].window_start = now;
    g_syn_table[lru_idx].syn_count = 1;
    g_syn_table[lru_idx].ack_count = 0;
    
    return IDS_THREAT_NONE;
}

/* ============ Packet Analysis ============ */

ids_threat_type_t ids_analyze_packet(
    uint32_t src_ip, uint32_t dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, const uint8_t* payload, uint16_t len,
    uint8_t tcp_flags
) {
    (void)dst_ip;  /* Unused for now */
    
    if (!g_ids_enabled) return IDS_THREAT_NONE;
    
    g_stats.packets_analyzed++;
    ids_threat_type_t threat = IDS_THREAT_NONE;
    ids_severity_t severity = IDS_SEVERITY_LOW;
    
    /* Check if already blocked */
    if (ids_is_ip_blocked(src_ip)) {
        return IDS_THREAT_NONE;  /* Already handled */
    }
    
    /* TCP analysis */
    if (protocol == 6) {  /* TCP */
        /* SYN flood detection */
        threat = check_syn_flood(src_ip, tcp_flags);
        if (threat != IDS_THREAT_NONE) {
            severity = IDS_SEVERITY_HIGH;
            goto threat_found;
        }
        
        /* Port scan detection (defer to firewall) */
        if (firewall_check_port_scan(src_ip, dst_port)) {
            threat = IDS_THREAT_PORT_SCAN;
            severity = IDS_SEVERITY_MEDIUM;
            g_stats.port_scans++;
            goto threat_found;
        }
    }
    
    /* ICMP analysis (protocol 1) */
    if (protocol == 1) {
        /* Simple ICMP flood check via rate limiting */
        if (firewall_check_rate_limit(src_ip) == 1) {  /* DENY = flood */
            threat = IDS_THREAT_PING_FLOOD;
            severity = IDS_SEVERITY_MEDIUM;
            goto threat_found;
        }
    }
    
    /* Payload analysis for known signatures */
    if (payload && len > 0) {
        /* Check for common attack signatures */
        /* These are simplified patterns - real IDS would have more */
        
        /* Check for potential buffer overflow patterns */
        int nop_count = 0;
        for (uint16_t i = 0; i < len && i < 100; i++) {
            if (payload[i] == 0x90) nop_count++;  /* x86 NOP sled */
        }
        if (nop_count > 20) {  /* NOP sled detected */
            threat = IDS_THREAT_SUSPICIOUS_PAYLOAD;
            severity = IDS_SEVERITY_CRITICAL;
            goto threat_found;
        }
        
        /* Check for shell command injection patterns */
        for (uint16_t i = 0; i < len - 4 && i < 200; i++) {
            if (payload[i] == '/' && payload[i+1] == 'b' && 
                payload[i+2] == 'i' && payload[i+3] == 'n' &&
                payload[i+4] == '/') {
                threat = IDS_THREAT_SUSPICIOUS_PAYLOAD;
                severity = IDS_SEVERITY_HIGH;
                goto threat_found;
            }
        }
    }
    
    return IDS_THREAT_NONE;
    
threat_found:
    /* Log the threat */
    bool blocked = false;
    if (g_ids_auto_block && severity >= IDS_SEVERITY_MEDIUM) {
        uint32_t block_duration = (severity >= IDS_SEVERITY_CRITICAL) ? 3600 : 300;
        ids_block_ip(src_ip, threat, block_duration);
        blocked = true;
    }
    
    log_threat(src_ip, src_port, dst_port, protocol, threat, severity, blocked);
    return threat;
}

/* ============ Threat Log Access ============ */

int ids_get_threat_count(void) {
    return g_threat_log_count;
}

ids_threat_entry_t* ids_get_threat_log(void) {
    return g_threat_log;
}

void ids_clear_threat_log(void) {
    g_threat_log_count = 0;
    g_threat_log_idx = 0;
    for (int i = 0; i < IDS_THREAT_LOG_SIZE; i++) {
        g_threat_log[i].threat_type = IDS_THREAT_NONE;
    }
}

/* ============ Statistics ============ */

void ids_get_stats(ids_stats_t* out) {
    if (out) {
        *out = g_stats;
        out->blocked_ips = g_blocked_count;
    }
}

void ids_reset_stats(void) {
    g_stats.packets_analyzed = 0;
    g_stats.threats_detected = 0;
    g_stats.threats_blocked = 0;
    g_stats.port_scans = 0;
    g_stats.syn_floods = 0;
    /* Don't reset blocked_ips - that's current state */
}

/* ============ Periodic Maintenance ============ */

void ids_tick(void) {
    if (!g_ids_enabled) return;
    
    uint64_t now = pit_get_ticks();
    
    /* Clean up expired blocks */
    for (int i = 0; i < IDS_BLOCKED_IP_SIZE; i++) {
        if (g_blocked_ips[i].ip != 0 && 
            g_blocked_ips[i].block_until > 0 &&
            now > g_blocked_ips[i].block_until) {
            g_blocked_ips[i].ip = 0;
            g_blocked_count--;
        }
    }
    
    /* Clean up old SYN tracking entries */
    for (int i = 0; i < IDS_SYN_TABLE_SIZE; i++) {
        if (g_syn_table[i].src_ip != 0 &&
            now - g_syn_table[i].window_start > 60000) {  /* 60 second expiry */
            g_syn_table[i].src_ip = 0;
        }
    }
}

/* ============ Utility ============ */

const char* ids_threat_name(ids_threat_type_t type) {
    switch (type) {
        case IDS_THREAT_PORT_SCAN:          return "Port Scan";
        case IDS_THREAT_SYN_FLOOD:          return "SYN Flood";
        case IDS_THREAT_PING_FLOOD:         return "ICMP Flood";
        case IDS_THREAT_MALFORMED_PACKET:   return "Malformed Packet";
        case IDS_THREAT_SUSPICIOUS_PAYLOAD: return "Suspicious Payload";
        case IDS_THREAT_BRUTE_FORCE:        return "Brute Force";
        case IDS_THREAT_DOS:                return "Denial of Service";
        default:                            return "Unknown";
    }
}
