#ifndef KERNEL_NETWORK_H
#define KERNEL_NETWORK_H

#include <stdint.h>
#include <stddef.h>

// Network protocols
#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17

// WiFi security types
typedef enum {
    WIFI_SECURITY_OPEN,
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_WPA,
    WIFI_SECURITY_WPA2,
    WIFI_SECURITY_WPA3
} wifi_security_t;

// WiFi network info
typedef struct {
    char ssid[32];
    uint8_t bssid[6];
    int signal_strength;
    wifi_security_t security;
    int channel;
} wifi_network_t;

// TCP connection state
typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

// TCP connection
typedef struct tcp_connection {
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    tcp_state_t state;
    uint32_t seq_num;
    uint32_t ack_num;
    void* data_buffer;
    size_t buffer_size;
    struct tcp_connection* next;
} tcp_connection_t;

// Network interface
typedef struct {
    char name[16];
    uint8_t mac_address[6];
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;
    int is_up;
    int is_wifi;
    wifi_network_t wifi_info;
} network_interface_t;

// Network functions
int network_init(void);
void network_deinit(void);

// Interface management
int network_add_interface(network_interface_t* iface);
int network_remove_interface(const char* name);
network_interface_t* network_get_interface(const char* name);

// WiFi functions
int wifi_scan_networks(wifi_network_t* networks, int max_count);
int wifi_connect(const char* ssid, const char* password);
int wifi_disconnect(void);
int wifi_get_status(wifi_network_t* info);

// TCP/IP functions
int tcp_connect(uint32_t remote_ip, uint16_t remote_port);
int tcp_listen(uint16_t port);
int tcp_accept(int listen_socket);
int tcp_send(int socket, const void* data, size_t len);
int tcp_receive(int socket, void* buffer, size_t max_len);
int tcp_close(int socket);

// UDP functions
int udp_socket(void);
int udp_bind(int socket, uint16_t port);
int udp_send(int socket, uint32_t remote_ip, uint16_t remote_port, const void* data, size_t len);
int udp_receive(int socket, uint32_t* remote_ip, uint16_t* remote_port, void* buffer, size_t max_len);
int udp_close(int socket);

// DHCP functions
int dhcp_request_ip(const char* interface_name);
int dhcp_release_ip(const char* interface_name);

// DNS functions
int dns_resolve(const char* hostname, uint32_t* ip_address);
int dns_set_server(uint32_t dns_server);

#endif // KERNEL_NETWORK_H 