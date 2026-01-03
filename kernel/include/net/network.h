/*
 * NeolyxOS Network Interface
 * 
 * Common network interface structures and API.
 * Supports Ethernet (LAN) and WiFi (Desktop only).
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_NETWORK_H
#define NEOLYX_NETWORK_H

#include <stdint.h>

/* ============ MAC Address ============ */

typedef struct {
    uint8_t bytes[6];
} mac_addr_t;

/* ============ IP Address ============ */

typedef struct {
    uint8_t bytes[4];   /* IPv4 */
} ipv4_addr_t;

typedef struct {
    uint8_t bytes[16];  /* IPv6 */
} ipv6_addr_t;

/* ============ Network Interface Types ============ */

typedef enum {
    NET_IF_ETHERNET = 0,
    NET_IF_WIFI = 1,
    NET_IF_LOOPBACK = 2,
} net_if_type_t;

/* ============ Interface State ============ */

typedef enum {
    NET_STATE_DOWN = 0,
    NET_STATE_UP = 1,
    NET_STATE_CONNECTING = 2,
    NET_STATE_CONNECTED = 3,
    NET_STATE_ERROR = 4,
} net_state_t;

/* ============ Network Interface ============ */

typedef struct net_interface {
    char name[16];              /* "eth0", "wlan0", "lo" */
    net_if_type_t type;
    net_state_t state;
    
    mac_addr_t mac;
    ipv4_addr_t ip;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    ipv4_addr_t dns;
    
    /* Statistics */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    
    /* Driver operations */
    int (*send)(struct net_interface *iface, const void *data, uint32_t len);
    int (*receive)(struct net_interface *iface, void *buf, uint32_t max_len);
    int (*set_state)(struct net_interface *iface, net_state_t state);
    
    /* Private driver data */
    void *driver_data;
    
    /* Linked list */
    struct net_interface *next;
} net_interface_t;

/* ============ Packet Types ============ */

#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPV6   0x86DD

/* ============ Ethernet Frame ============ */

typedef struct {
    mac_addr_t dest;
    mac_addr_t src;
    uint16_t type;
    uint8_t payload[];
} __attribute__((packed)) eth_frame_t;

#define ETH_MTU         1500
#define ETH_HEADER_LEN  14

/* ============ Network API ============ */

/**
 * Initialize the network subsystem.
 */
int network_init(void);

/**
 * Register a network interface.
 */
int network_register(net_interface_t *iface);

/**
 * Unregister a network interface.
 */
int network_unregister(net_interface_t *iface);

/**
 * Get interface by name.
 */
net_interface_t *network_get_interface(const char *name);

/**
 * Get interface by index.
 */
net_interface_t *network_get_interface_by_index(int index);

/**
 * Get number of interfaces.
 */
int network_interface_count(void);

/**
 * Send a packet (interface level).
 */
int network_iface_send(net_interface_t *iface, const void *data, uint32_t len);

/**
 * Receive a packet (interface level).
 */
int network_iface_recv(net_interface_t *iface, void *buf, uint32_t max_len);

/**
 * Bring interface up.
 */
int network_up(net_interface_t *iface);

/**
 * Bring interface down.
 */
int network_down(net_interface_t *iface);

/* ============ DHCP ============ */

/**
 * Request IP via DHCP.
 */
int network_dhcp_request(net_interface_t *iface);

/**
 * Release DHCP lease.
 */
int network_dhcp_release(net_interface_t *iface);

/* ============ Configuration ============ */

/**
 * Set static IP configuration.
 */
int network_set_ip(net_interface_t *iface, ipv4_addr_t ip, 
                   ipv4_addr_t netmask, ipv4_addr_t gateway);

/**
 * Set DNS server.
 */
int network_set_dns(net_interface_t *iface, ipv4_addr_t dns);

#endif /* NEOLYX_NETWORK_H */
