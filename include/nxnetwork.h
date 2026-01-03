/*
 * NeolyxOS Network Manager Header
 * WiFi, Ethernet, VPN, Bluetooth networking
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef NXNETWORK_H
#define NXNETWORK_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Network Interface Types ============ */

typedef enum {
    NETIF_WIFI,
    NETIF_ETHERNET,
    NETIF_BLUETOOTH,
    NETIF_VPN,
    NETIF_LOOPBACK
} netif_type_t;

typedef enum {
    NET_STATUS_DISCONNECTED,
    NET_STATUS_CONNECTING,
    NET_STATUS_CONNECTED,
    NET_STATUS_LIMITED,       /* No internet */
    NET_STATUS_ERROR
} net_status_t;

/* ============ Network Interface ============ */

typedef struct {
    char name[32];
    char hardware_addr[18];   /* MAC address */
    netif_type_t type;
    net_status_t status;
    
    /* IP configuration */
    char ip_address[46];      /* IPv4 or IPv6 */
    char subnet_mask[46];
    char gateway[46];
    char dns_primary[46];
    char dns_secondary[46];
    bool dhcp_enabled;
    
    /* Statistics */
    uint64_t bytes_sent;
    uint64_t bytes_received;
    int signal_strength;      /* -100 to 0 dBm for WiFi */
    int link_speed;           /* Mbps */
} nxnetif_t;

/* ============ WiFi ============ */

typedef enum {
    WIFI_SECURITY_OPEN,
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_WPA,
    WIFI_SECURITY_WPA2,
    WIFI_SECURITY_WPA3,
    WIFI_SECURITY_ENTERPRISE
} wifi_security_t;

typedef struct {
    char ssid[33];
    char bssid[18];
    wifi_security_t security;
    int signal_strength;      /* -100 to 0 dBm */
    int frequency;            /* MHz (2.4GHz or 5GHz) */
    bool is_known;            /* Previously connected */
    bool is_connected;
} wifi_network_t;

/* WiFi API */
int nxwifi_enable(bool enable);
bool nxwifi_is_enabled(void);
int nxwifi_scan(wifi_network_t *networks, int max);
int nxwifi_connect(const char *ssid, const char *password);
int nxwifi_disconnect(void);
int nxwifi_forget(const char *ssid);
wifi_network_t *nxwifi_current(void);

/* ============ Ethernet ============ */

int nxethernet_configure(const char *interface, bool dhcp,
                          const char *ip, const char *mask, const char *gateway);

/* ============ VPN ============ */

typedef enum {
    VPN_PROTOCOL_OPENVPN,
    VPN_PROTOCOL_WIREGUARD,
    VPN_PROTOCOL_IPSEC,
    VPN_PROTOCOL_L2TP,
    VPN_PROTOCOL_PPTP
} vpn_protocol_t;

typedef struct {
    char name[64];
    char server[128];
    vpn_protocol_t protocol;
    net_status_t status;
    char local_ip[46];
    uint64_t bytes_sent;
    uint64_t bytes_received;
} vpn_connection_t;

int nxvpn_list(vpn_connection_t *vpns, int max);
int nxvpn_connect(const char *name);
int nxvpn_disconnect(const char *name);
int nxvpn_add(const char *name, const char *config_path);
int nxvpn_remove(const char *name);

/* ============ Bluetooth ============ */

typedef enum {
    BT_DEVICE_UNKNOWN,
    BT_DEVICE_PHONE,
    BT_DEVICE_COMPUTER,
    BT_DEVICE_HEADPHONES,
    BT_DEVICE_SPEAKER,
    BT_DEVICE_KEYBOARD,
    BT_DEVICE_MOUSE,
    BT_DEVICE_GAMEPAD,
    BT_DEVICE_WATCH,
    BT_DEVICE_EARBUDS
} bt_device_type_t;

typedef struct {
    char name[64];
    char address[18];
    bt_device_type_t type;
    bool paired;
    bool connected;
    int battery_level;        /* 0-100, -1 if unknown */
    int signal_strength;
} bt_device_t;

int nxbluetooth_enable(bool enable);
bool nxbluetooth_is_enabled(void);
int nxbluetooth_scan(bt_device_t *devices, int max);
int nxbluetooth_pair(const char *address);
int nxbluetooth_connect(const char *address);
int nxbluetooth_disconnect(const char *address);
int nxbluetooth_forget(const char *address);
int nxbluetooth_paired_devices(bt_device_t *devices, int max);

/* ============ Network Manager API ============ */

/* Interface management */
int nxnetwork_interfaces(nxnetif_t *interfaces, int max);
nxnetif_t *nxnetwork_primary(void);
int nxnetwork_set_primary(const char *interface);

/* DNS */
int nxnetwork_set_dns(const char *primary, const char *secondary);
int nxnetwork_get_dns(char *primary, char *secondary, int max_len);

/* Proxy */
typedef struct {
    bool enabled;
    char http_proxy[128];
    int http_port;
    char https_proxy[128];
    int https_port;
    char no_proxy[256];       /* Comma-separated */
    bool use_pac;
    char pac_url[256];
} proxy_settings_t;

int nxnetwork_proxy_get(proxy_settings_t *settings);
int nxnetwork_proxy_set(const proxy_settings_t *settings);

/* Firewall */
int nxnetwork_firewall_enable(bool enable);
bool nxnetwork_firewall_is_enabled(void);

/* Hostname */
int nxnetwork_hostname_get(char *hostname, int max_len);
int nxnetwork_hostname_set(const char *hostname);

/* Connectivity check */
bool nxnetwork_is_connected(void);
bool nxnetwork_has_internet(void);

#endif /* NXNETWORK_H */
