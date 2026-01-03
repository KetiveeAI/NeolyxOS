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
    
    serial_puts("[FIREWALL] Initialized with default ALLOW policy\n");
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

