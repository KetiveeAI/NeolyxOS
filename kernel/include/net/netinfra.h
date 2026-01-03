/*
 * NeolyxOS Advanced Network Infrastructure
 * 
 * Network subsystems:
 *   - Network bridges
 *   - Virtual networking (VLAN, VPN)
 *   - Network namespaces
 *   - Traffic shaping / QoS
 *   - Firewall rules
 *   - NAT / IP masquerading
 * 
 * Device support:
 *   - Physical NICs
 *   - Virtual NICs (TAP/TUN)
 *   - USB network adapters
 *   - Bluetooth PAN
 *   - Mobile broadband (LTE/5G modems)
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_NETINFRA_H
#define NEOLYX_NETINFRA_H

#include <stdint.h>
#include "net/network.h"

/* ============ Bridge Interface ============ */

typedef struct net_bridge {
    char name[16];              /* "br0", "br1" */
    
    /* Member interfaces */
    net_interface_t *members[16];
    int member_count;
    
    /* Bridge state */
    int enabled;
    int stp_enabled;            /* Spanning Tree Protocol */
    
    /* MAC table */
    struct {
        mac_addr_t mac;
        int port;
        uint32_t age;
    } mac_table[256];
    int mac_table_size;
    
    /* Stats */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    
    /* Virtual interface */
    net_interface_t interface;
    
    struct net_bridge *next;
} net_bridge_t;

/* ============ VLAN Interface ============ */

typedef struct net_vlan {
    char name[16];              /* "eth0.100" */
    uint16_t vlan_id;           /* 1-4094 */
    
    net_interface_t *parent;    /* Physical interface */
    
    /* QoS priority (0-7) */
    uint8_t priority;
    
    /* Virtual interface */
    net_interface_t interface;
    
    struct net_vlan *next;
} net_vlan_t;

/* ============ TAP/TUN Device ============ */

typedef enum {
    VNIC_TYPE_TAP = 0,          /* Layer 2 (Ethernet) */
    VNIC_TYPE_TUN = 1,          /* Layer 3 (IP) */
} vnic_type_t;

typedef struct net_vnic {
    char name[16];              /* "tap0", "tun0" */
    vnic_type_t type;
    
    int fd;                     /* File descriptor */
    
    /* Virtual interface */
    net_interface_t interface;
    
    struct net_vnic *next;
} net_vnic_t;

/* ============ Firewall Rules ============ */

typedef enum {
    FW_ACTION_ACCEPT = 0,
    FW_ACTION_DROP,
    FW_ACTION_REJECT,
    FW_ACTION_LOG,
} fw_action_t;

typedef enum {
    FW_CHAIN_INPUT = 0,
    FW_CHAIN_OUTPUT,
    FW_CHAIN_FORWARD,
} fw_chain_t;

typedef struct fw_rule {
    uint32_t id;
    fw_chain_t chain;
    
    /* Match criteria */
    ipv4_addr_t src_ip;
    ipv4_addr_t src_mask;
    ipv4_addr_t dst_ip;
    ipv4_addr_t dst_mask;
    uint16_t src_port;          /* 0 = any */
    uint16_t dst_port;
    uint8_t protocol;           /* TCP/UDP/ICMP */
    char iface[16];             /* Interface name or empty */
    
    /* Action */
    fw_action_t action;
    
    /* Stats */
    uint64_t packets;
    uint64_t bytes;
    
    struct fw_rule *next;
} fw_rule_t;

/* ============ NAT Entry ============ */

typedef struct nat_entry {
    ipv4_addr_t internal_ip;
    uint16_t internal_port;
    
    ipv4_addr_t external_ip;
    uint16_t external_port;
    
    uint8_t protocol;
    
    int masquerade;             /* Use outgoing interface IP */
    
    struct nat_entry *next;
} nat_entry_t;

/* ============ QoS / Traffic Shaping ============ */

typedef enum {
    QOS_CLASS_BEST_EFFORT = 0,
    QOS_CLASS_BACKGROUND,
    QOS_CLASS_VIDEO,
    QOS_CLASS_VOICE,
    QOS_CLASS_CRITICAL,
} qos_class_t;

typedef struct qos_rule {
    uint32_t id;
    
    /* Match */
    uint16_t dst_port;
    uint8_t protocol;
    
    /* Classification */
    qos_class_t traffic_class;
    
    /* Shaping */
    uint32_t rate_limit_kbps;   /* 0 = unlimited */
    uint32_t burst_size;
    
    struct qos_rule *next;
} qos_rule_t;

/* ============ DNS Configuration ============ */

typedef struct dns_server {
    ipv4_addr_t address;
    int priority;               /* Lower = higher priority */
    int reachable;
    uint32_t latency_ms;
    struct dns_server *next;
} dns_server_t;

/* ============ Routing Table ============ */

typedef struct route_entry {
    ipv4_addr_t destination;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    
    net_interface_t *iface;
    
    uint32_t metric;
    
    int flags;
    #define ROUTE_FLAG_UP       0x01
    #define ROUTE_FLAG_GATEWAY  0x02
    #define ROUTE_FLAG_HOST     0x04
    
    struct route_entry *next;
} route_entry_t;

/* ============ Network Infrastructure API ============ */

/**
 * Initialize network infrastructure.
 */
int netinfra_init(void);

/* ============ Bridge API ============ */

/**
 * Create a network bridge.
 */
net_bridge_t *bridge_create(const char *name);

/**
 * Delete a bridge.
 */
int bridge_delete(net_bridge_t *br);

/**
 * Add interface to bridge.
 */
int bridge_add_interface(net_bridge_t *br, net_interface_t *iface);

/**
 * Remove interface from bridge.
 */
int bridge_remove_interface(net_bridge_t *br, net_interface_t *iface);

/**
 * Enable/disable STP.
 */
int bridge_set_stp(net_bridge_t *br, int enabled);

/**
 * Get bridge by name.
 */
net_bridge_t *bridge_get(const char *name);

/* ============ VLAN API ============ */

/**
 * Create a VLAN interface.
 */
net_vlan_t *vlan_create(net_interface_t *parent, uint16_t vlan_id);

/**
 * Delete a VLAN.
 */
int vlan_delete(net_vlan_t *vlan);

/* ============ Virtual NIC API ============ */

/**
 * Create TAP/TUN device.
 */
net_vnic_t *vnic_create(const char *name, vnic_type_t type);

/**
 * Delete virtual NIC.
 */
int vnic_delete(net_vnic_t *vnic);

/**
 * Read from virtual NIC.
 */
int vnic_read(net_vnic_t *vnic, void *buf, uint32_t max);

/**
 * Write to virtual NIC.
 */
int vnic_write(net_vnic_t *vnic, const void *data, uint32_t len);

/* ============ Firewall API ============ */

/**
 * Add firewall rule.
 */
int firewall_add_rule(fw_rule_t *rule);

/**
 * Delete firewall rule.
 */
int firewall_delete_rule(uint32_t id);

/**
 * Flush all rules.
 */
int firewall_flush(fw_chain_t chain);

/**
 * Get rules.
 */
int firewall_get_rules(fw_chain_t chain, fw_rule_t *rules, int max);

/**
 * Set default policy.
 */
int firewall_set_policy(fw_chain_t chain, fw_action_t action);

/* ============ NAT API ============ */

/**
 * Enable IP forwarding.
 */
int nat_enable_forwarding(int enabled);

/**
 * Add port forward.
 */
int nat_add_forward(uint16_t external_port, ipv4_addr_t internal_ip,
                    uint16_t internal_port, uint8_t protocol);

/**
 * Enable masquerading on interface.
 */
int nat_enable_masquerade(net_interface_t *iface);

/* ============ QoS API ============ */

/**
 * Add QoS rule.
 */
int qos_add_rule(qos_rule_t *rule);

/**
 * Set interface bandwidth limit.
 */
int qos_set_limit(net_interface_t *iface, uint32_t download_kbps, uint32_t upload_kbps);

/* ============ DNS API ============ */

/**
 * Add DNS server.
 */
int dns_add_server(ipv4_addr_t address, int priority);

/**
 * Remove DNS server.
 */
int dns_remove_server(ipv4_addr_t address);

/**
 * Resolve hostname.
 */
int dns_resolve(const char *hostname, ipv4_addr_t *result);

/* ============ Routing API ============ */

/**
 * Add route.
 */
int route_add(ipv4_addr_t dest, ipv4_addr_t mask, ipv4_addr_t gateway,
              net_interface_t *iface, uint32_t metric);

/**
 * Delete route.
 */
int route_delete(ipv4_addr_t dest, ipv4_addr_t mask);

/**
 * Get routing table.
 */
int route_get_table(route_entry_t *entries, int max);

/**
 * Lookup route for destination.
 */
route_entry_t *route_lookup(ipv4_addr_t dest);

/* ============ Connection Sharing ============ */

/**
 * Share internet connection.
 * Sets up NAT and DHCP on internal interface.
 */
int netinfra_share_connection(net_interface_t *wan, net_interface_t *lan);

/**
 * Stop connection sharing.
 */
int netinfra_stop_sharing(void);

#endif /* NEOLYX_NETINFRA_H */
