/*
 * NeolyxOS Firewall Implementation
 * 
 * Kernel-level packet filtering for network security.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "security/firewall.h"

/* External dependencies */
extern void serial_puts(const char *s);

/* Rule storage */
static firewall_rule_t rules[FW_MAX_RULES];
static int rule_count = 0;
static uint32_t next_rule_id = 1;

/* Firewall state */
static uint8_t fw_enabled = 0;
static uint8_t default_policy[3] = { FW_ACTION_ALLOW, FW_ACTION_ALLOW, FW_ACTION_ALLOW };

/* Statistics */
static firewall_stats_t stats;

/* Rate limiting state */
static rate_limit_entry_t rate_limit_table[FW_RATE_LIMIT_ENTRIES];
static uint32_t rate_limit_max_pps = 1000;
static uint16_t rate_limit_burst = 50;

/* Connection tracking state */
static conn_track_entry_t conn_table[FW_CONN_TABLE_SIZE];
static int conn_tracking_enabled = 1;

/* Port scan detection */
static scan_detect_entry_t scan_table[64];

/* Time helper (from kernel) */
extern uint64_t pit_get_ticks(void);

/* ============ Initialization ============ */

void firewall_init(void) {
    serial_puts("[FIREWALL] Initializing firewall...\n");
    
    rule_count = 0;
    next_rule_id = 1;
    fw_enabled = 0;
    
    /* Clear rules */
    for (int i = 0; i < FW_MAX_RULES; i++) {
        rules[i].id = 0;
        rules[i].enabled = 0;
    }
    
    /* Reset stats */
    stats.packets_allowed = 0;
    stats.packets_denied = 0;
    stats.packets_logged = 0;
    stats.total_bytes = 0;
    stats.active_rules = 0;
    stats.enabled = 0;
    
    /* Default allow policy */
    default_policy[FW_DIR_INPUT] = FW_ACTION_ALLOW;
    default_policy[FW_DIR_OUTPUT] = FW_ACTION_ALLOW;
    default_policy[FW_DIR_FORWARD] = FW_ACTION_DENY;
    
    /* Initialize rate limiting */
    rate_limit_max_pps = 1000;  /* Default: 1000 packets/sec from config */
    rate_limit_burst = 50;
    for (int i = 0; i < FW_RATE_LIMIT_ENTRIES; i++) {
        rate_limit_table[i].ip = 0;
    }
    
    /* Initialize connection tracking */
    conn_tracking_enabled = 1;
    for (int i = 0; i < FW_CONN_TABLE_SIZE; i++) {
        conn_table[i].state = FW_CONN_NONE;
    }
    
    /* Initialize scan detection */
    for (int i = 0; i < 64; i++) {
        scan_table[i].src_ip = 0;
    }
    
    serial_puts("[FIREWALL] Initialized with rate limiting and connection tracking\n");
}

/* ============ Enable/Disable ============ */

int firewall_enable(void) {
    fw_enabled = 1;
    stats.enabled = 1;
    serial_puts("[FIREWALL] Enabled\n");
    return 0;
}

int firewall_disable(void) {
    fw_enabled = 0;
    stats.enabled = 0;
    serial_puts("[FIREWALL] Disabled\n");
    return 0;
}

int firewall_is_enabled(void) {
    return fw_enabled;
}

/* ============ Rule Management ============ */

int firewall_add_rule(firewall_rule_t *rule) {
    if (!rule) return -1;
    if (rule_count >= FW_MAX_RULES) {
        serial_puts("[FIREWALL] ERROR: Max rules reached\n");
        return -1;
    }
    
    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (rules[i].id == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) return -1;
    
    /* Copy rule */
    rules[slot] = *rule;
    rules[slot].id = next_rule_id++;
    rules[slot].enabled = 1;
    rules[slot].packets_matched = 0;
    rules[slot].bytes_matched = 0;
    
    rule_count++;
    stats.active_rules++;
    
    serial_puts("[FIREWALL] Rule added: ID=");
    /* TODO: Print rule ID */
    serial_puts("\n");
    
    return rules[slot].id;
}

int firewall_remove_rule(uint32_t rule_id) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (rules[i].id == rule_id) {
            rules[i].id = 0;
            rules[i].enabled = 0;
            rule_count--;
            stats.active_rules--;
            serial_puts("[FIREWALL] Rule removed\n");
            return 0;
        }
    }
    return -1;
}

int firewall_enable_rule(uint32_t rule_id) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (rules[i].id == rule_id) {
            rules[i].enabled = 1;
            return 0;
        }
    }
    return -1;
}

int firewall_disable_rule(uint32_t rule_id) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (rules[i].id == rule_id) {
            rules[i].enabled = 0;
            return 0;
        }
    }
    return -1;
}

int firewall_get_rule(uint32_t rule_id, firewall_rule_t *out) {
    if (!out) return -1;
    
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (rules[i].id == rule_id) {
            *out = rules[i];
            return 0;
        }
    }
    return -1;
}

int firewall_get_rule_count(void) {
    return rule_count;
}

void firewall_clear_rules(void) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        rules[i].id = 0;
        rules[i].enabled = 0;
    }
    rule_count = 0;
    stats.active_rules = 0;
    serial_puts("[FIREWALL] All rules cleared\n");
}

/* ============ Packet Checking ============ */

static int match_ip(uint32_t pkt_ip, uint32_t rule_ip, uint32_t mask) {
    if (rule_ip == 0) return 1;  /* 0 = any */
    return (pkt_ip & mask) == (rule_ip & mask);
}

static int match_port(uint16_t pkt_port, uint16_t min, uint16_t max) {
    if (min == 0 && max == 0) return 1;  /* 0 = any */
    return (pkt_port >= min && pkt_port <= max);
}

int firewall_check_packet(packet_info_t *pkt) {
    if (!pkt) return FW_ACTION_DENY;
    if (!fw_enabled) return FW_ACTION_ALLOW;
    
    /* Check each rule in order */
    for (int i = 0; i < FW_MAX_RULES; i++) {
        firewall_rule_t *r = &rules[i];
        
        if (r->id == 0 || !r->enabled) continue;
        if (r->direction != pkt->direction) continue;
        
        /* Protocol check */
        if (r->protocol != FW_PROTO_ANY && r->protocol != pkt->protocol) continue;
        
        /* IP checks */
        if (!match_ip(pkt->src_ip, r->src_ip, r->src_mask)) continue;
        if (!match_ip(pkt->dst_ip, r->dst_ip, r->dst_mask)) continue;
        
        /* Port checks (TCP/UDP only) */
        if (pkt->protocol == FW_PROTO_TCP || pkt->protocol == FW_PROTO_UDP) {
            if (!match_port(pkt->src_port, r->src_port_min, r->src_port_max)) continue;
            if (!match_port(pkt->dst_port, r->dst_port_min, r->dst_port_max)) continue;
        }
        
        /* Rule matched! */
        r->packets_matched++;
        r->bytes_matched += pkt->length;
        
        if (r->action == FW_ACTION_ALLOW) {
            stats.packets_allowed++;
        } else if (r->action == FW_ACTION_DENY) {
            stats.packets_denied++;
        } else if (r->action == FW_ACTION_LOG) {
            stats.packets_logged++;
        }
        stats.total_bytes += pkt->length;
        
        return r->action;
    }
    
    /* No rule matched - use default policy */
    uint8_t action = default_policy[pkt->direction];
    if (action == FW_ACTION_ALLOW) {
        stats.packets_allowed++;
    } else {
        stats.packets_denied++;
    }
    stats.total_bytes += pkt->length;
    
    return action;
}

/* ============ Statistics ============ */

void firewall_get_stats(firewall_stats_t *out) {
    if (out) {
        *out = stats;
    }
}

void firewall_reset_stats(void) {
    stats.packets_allowed = 0;
    stats.packets_denied = 0;
    stats.packets_logged = 0;
    stats.total_bytes = 0;
}

/* ============ Default Policy ============ */

void firewall_set_default_policy(uint8_t direction, uint8_t action) {
    if (direction <= FW_DIR_FORWARD) {
        default_policy[direction] = action;
        serial_puts("[FIREWALL] Default policy updated\n");
    }
}

/* ============ App-Level Network Control ============ */

static firewall_app_t app_registry[FW_MAX_APPS];
static int app_count = 0;

int firewall_register_app(uint32_t pid, uint8_t permissions) {
    if (app_count >= FW_MAX_APPS) return -1;
    
    /* Check if already registered */
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            app_registry[i].permissions = permissions;
            return 0;
        }
    }
    
    /* Find empty slot */
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == 0) {
            app_registry[i].pid = pid;
            app_registry[i].permissions = permissions;
            app_registry[i].port_count = 0;
            app_registry[i].bytes_sent = 0;
            app_registry[i].bytes_received = 0;
            app_registry[i].blocked_attempts = 0;
            app_count++;
            serial_puts("[FIREWALL] App registered\n");
            return 0;
        }
    }
    return -1;
}

int firewall_revoke_app(uint32_t pid) {
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            app_registry[i].pid = 0;
            app_registry[i].permissions = FW_APP_NET_BLOCKED;
            app_count--;
            serial_puts("[FIREWALL] App revoked\n");
            return 0;
        }
    }
    return -1;
}

int firewall_app_allow_port(uint32_t pid, uint16_t port) {
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            if (app_registry[i].port_count < 8) {
                app_registry[i].allowed_ports[app_registry[i].port_count++] = port;
                return 0;
            }
            return -1; /* Max ports */
        }
    }
    return -1;
}

int firewall_app_block_port(uint32_t pid, uint16_t port) {
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            for (int j = 0; j < app_registry[i].port_count; j++) {
                if (app_registry[i].allowed_ports[j] == port) {
                    /* Shift remaining */
                    for (int k = j; k < app_registry[i].port_count - 1; k++) {
                        app_registry[i].allowed_ports[k] = app_registry[i].allowed_ports[k + 1];
                    }
                    app_registry[i].port_count--;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int firewall_app_set_permissions(uint32_t pid, uint8_t perms) {
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            app_registry[i].permissions = perms;
            return 0;
        }
    }
    return -1;
}

int firewall_check_app_packet(uint32_t pid, packet_info_t *pkt) {
    if (!pkt) return FW_ACTION_DENY;
    
    /* Find app */
    firewall_app_t *app = NULL;
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            app = &app_registry[i];
            break;
        }
    }
    
    /* Unregistered apps - use default policy */
    if (!app) {
        return firewall_check_packet(pkt);
    }
    
    /* Check if blocked */
    if (app->permissions & FW_APP_NET_BLOCKED) {
        app->blocked_attempts++;
        serial_puts("[FIREWALL] App blocked\n");
        return FW_ACTION_DENY;
    }
    
    /* Check direction permissions */
    if (pkt->direction == FW_DIR_OUTPUT) {
        if (!(app->permissions & FW_APP_NET_OUTGOING)) {
            app->blocked_attempts++;
            return FW_ACTION_DENY;
        }
        /* Check allowed ports */
        if (app->port_count > 0) {
            int allowed = 0;
            for (int i = 0; i < app->port_count; i++) {
                if (app->allowed_ports[i] == pkt->dst_port) {
                    allowed = 1;
                    break;
                }
            }
            if (!allowed) {
                app->blocked_attempts++;
                return FW_ACTION_DENY;
            }
        }
        app->bytes_sent += pkt->length;
    } else if (pkt->direction == FW_DIR_INPUT) {
        if (!(app->permissions & FW_APP_NET_INCOMING)) {
            app->blocked_attempts++;
            return FW_ACTION_DENY;
        }
        app->bytes_received += pkt->length;
    }
    
    /* Pass through regular firewall */
    return firewall_check_packet(pkt);
}

void firewall_get_app_stats(uint32_t pid, firewall_app_t *out) {
    if (!out) return;
    for (int i = 0; i < FW_MAX_APPS; i++) {
        if (app_registry[i].pid == pid) {
            *out = app_registry[i];
            return;
        }
    }
}

/* ============ Rate Limiting Implementation ============ */

void firewall_set_rate_limit(uint32_t max_pps, uint16_t burst) {
    rate_limit_max_pps = max_pps;
    rate_limit_burst = burst;
    serial_puts("[FIREWALL] Rate limit set\n");
}

int firewall_check_rate_limit(uint32_t ip) {
    uint64_t now = pit_get_ticks();
    int lru_idx = 0;
    uint64_t lru_time = ~0ULL;
    
    /* Find existing entry or LRU slot */
    for (int i = 0; i < FW_RATE_LIMIT_ENTRIES; i++) {
        if (rate_limit_table[i].ip == ip) {
            /* Check if in same second window */
            if (now - rate_limit_table[i].window_start < 1000) {
                rate_limit_table[i].packet_count++;
                if (rate_limit_table[i].packet_count > rate_limit_max_pps + rate_limit_burst) {
                    stats.packets_denied++;
                    return FW_ACTION_DENY;
                }
            } else {
                /* New window */
                rate_limit_table[i].window_start = now;
                rate_limit_table[i].packet_count = 1;
            }
            return FW_ACTION_ALLOW;
        }
        if (rate_limit_table[i].window_start < lru_time) {
            lru_time = rate_limit_table[i].window_start;
            lru_idx = i;
        }
    }
    
    /* New IP - use LRU slot */
    rate_limit_table[lru_idx].ip = ip;
    rate_limit_table[lru_idx].window_start = now;
    rate_limit_table[lru_idx].packet_count = 1;
    return FW_ACTION_ALLOW;
}

void firewall_rate_limit_reset(void) {
    for (int i = 0; i < FW_RATE_LIMIT_ENTRIES; i++) {
        rate_limit_table[i].ip = 0;
        rate_limit_table[i].packet_count = 0;
    }
}

/* ============ Connection Tracking Implementation ============ */

void firewall_enable_conn_tracking(int enable) {
    conn_tracking_enabled = enable;
}

static uint32_t conn_hash(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port) {
    return (src_ip ^ dst_ip ^ ((uint32_t)src_port << 16) ^ dst_port) % FW_CONN_TABLE_SIZE;
}

conn_track_entry_t* firewall_conn_lookup(uint32_t src_ip, uint32_t dst_ip,
                                          uint16_t src_port, uint16_t dst_port) {
    uint32_t idx = conn_hash(src_ip, dst_ip, src_port, dst_port);
    
    for (int i = 0; i < 16; i++) {
        uint32_t slot = (idx + i) % FW_CONN_TABLE_SIZE;
        conn_track_entry_t *e = &conn_table[slot];
        if (e->state != FW_CONN_NONE &&
            e->src_ip == src_ip && e->dst_ip == dst_ip &&
            e->src_port == src_port && e->dst_port == dst_port) {
            return e;
        }
    }
    return (conn_track_entry_t*)0;
}

int firewall_conn_track_packet(packet_info_t *pkt) {
    if (!conn_tracking_enabled || !pkt) return FW_CONN_NEW;
    
    /* Look for existing connection */
    conn_track_entry_t *conn = firewall_conn_lookup(pkt->src_ip, pkt->dst_ip,
                                                     pkt->src_port, pkt->dst_port);
    
    /* Check reverse direction */
    if (!conn) {
        conn = firewall_conn_lookup(pkt->dst_ip, pkt->src_ip,
                                    pkt->dst_port, pkt->src_port);
        if (conn && conn->state == FW_CONN_NEW) {
            /* Reply to new connection = established */
            conn->state = FW_CONN_ESTABLISHED;
        }
    }
    
    if (conn) {
        conn->last_seen = pit_get_ticks();
        conn->packets++;
        conn->bytes += pkt->length;
        return conn->state;
    }
    
    /* New connection - find empty slot */
    uint32_t idx = conn_hash(pkt->src_ip, pkt->dst_ip, pkt->src_port, pkt->dst_port);
    for (int i = 0; i < 16; i++) {
        uint32_t slot = (idx + i) % FW_CONN_TABLE_SIZE;
        if (conn_table[slot].state == FW_CONN_NONE || 
            conn_table[slot].state == FW_CONN_CLOSED) {
            conn_table[slot].src_ip = pkt->src_ip;
            conn_table[slot].dst_ip = pkt->dst_ip;
            conn_table[slot].src_port = pkt->src_port;
            conn_table[slot].dst_port = pkt->dst_port;
            conn_table[slot].protocol = pkt->protocol;
            conn_table[slot].state = FW_CONN_NEW;
            conn_table[slot].last_seen = pit_get_ticks();
            conn_table[slot].packets = 1;
            conn_table[slot].bytes = pkt->length;
            return FW_CONN_NEW;
        }
    }
    
    return FW_CONN_NEW; /* Table full, treat as new */
}

void firewall_conn_cleanup(uint16_t tcp_timeout, uint16_t udp_timeout) {
    uint64_t now = pit_get_ticks();
    
    for (int i = 0; i < FW_CONN_TABLE_SIZE; i++) {
        conn_track_entry_t *e = &conn_table[i];
        if (e->state == FW_CONN_NONE) continue;
        
        uint16_t timeout = (e->protocol == FW_PROTO_TCP) ? tcp_timeout : udp_timeout;
        uint64_t timeout_ms = timeout * 1000ULL;
        
        if (now - e->last_seen > timeout_ms) {
            e->state = FW_CONN_CLOSED;
        }
    }
}

int firewall_conn_count(void) {
    int count = 0;
    for (int i = 0; i < FW_CONN_TABLE_SIZE; i++) {
        if (conn_table[i].state != FW_CONN_NONE && 
            conn_table[i].state != FW_CONN_CLOSED) {
            count++;
        }
    }
    return count;
}

/* ============ Port Scan Detection ============ */

int firewall_check_port_scan(uint32_t src_ip, uint16_t port) {
    uint64_t now = pit_get_ticks();
    int lru_idx = 0;
    uint64_t lru_time = ~0ULL;
    
    for (int i = 0; i < 64; i++) {
        scan_detect_entry_t *e = &scan_table[i];
        
        if (e->src_ip == src_ip) {
            /* Check if window expired (60 seconds) */
            if (now - e->first_access > 60000) {
                e->port_count = 0;
                e->first_access = now;
            }
            
            /* Check if port already recorded */
            for (int j = 0; j < e->port_count; j++) {
                if (e->ports_accessed[j] == port) {
                    return 0; /* Already seen this port */
                }
            }
            
            /* Add new port */
            if (e->port_count < 32) {
                e->ports_accessed[e->port_count++] = port;
            }
            
            /* Check threshold */
            if (e->port_count >= 10) {
                serial_puts("[FIREWALL] Port scan detected!\n");
                return 1; /* Scan detected */
            }
            return 0;
        }
        
        if (e->first_access < lru_time) {
            lru_time = e->first_access;
            lru_idx = i;
        }
    }
    
    /* New IP */
    scan_table[lru_idx].src_ip = src_ip;
    scan_table[lru_idx].first_access = now;
    scan_table[lru_idx].port_count = 1;
    scan_table[lru_idx].ports_accessed[0] = port;
    return 0;
}

void firewall_scan_cleanup(void) {
    uint64_t now = pit_get_ticks();
    for (int i = 0; i < 64; i++) {
        if (now - scan_table[i].first_access > 60000) {
            scan_table[i].src_ip = 0;
            scan_table[i].port_count = 0;
        }
    }
}
