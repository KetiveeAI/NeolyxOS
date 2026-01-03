/*
 * NeolyxOS TCP/IP Protocol Stack
 * 
 * Protocols:
 *   - IPv4 (IPv6 future)
 *   - ICMP (ping)
 *   - UDP (datagram)
 *   - TCP (reliable stream)
 *   - ARP (address resolution)
 *   - DHCP (auto-configuration)
 *   - DNS (name resolution)
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_TCPIP_H
#define NEOLYX_TCPIP_H

#include <stdint.h>
#include "net/network.h"

/* ============ IP Protocol Numbers ============ */

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/* ============ Ports ============ */

#define PORT_FTP_DATA   20
#define PORT_FTP        21
#define PORT_SSH        22
#define PORT_TELNET     23
#define PORT_SMTP       25
#define PORT_DNS        53
#define PORT_DHCP_S     67
#define PORT_DHCP_C     68
#define PORT_HTTP       80
#define PORT_HTTPS      443
#define PORT_NTP        123

/* ============ IPv4 Header ============ */

typedef struct {
    uint8_t version_ihl;        /* Version (4) + IHL (5) */
    uint8_t tos;                /* Type of Service */
    uint16_t total_length;      /* Total Length */
    uint16_t id;                /* Identification */
    uint16_t flags_frag;        /* Flags + Fragment Offset */
    uint8_t ttl;                /* Time To Live */
    uint8_t protocol;           /* Protocol */
    uint16_t checksum;          /* Header Checksum */
    ipv4_addr_t src;            /* Source Address */
    ipv4_addr_t dst;            /* Destination Address */
} __attribute__((packed)) ipv4_header_t;

#define IPV4_HEADER_LEN 20

/* ============ ICMP Header ============ */

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

#define ICMP_ECHO_REQUEST   8
#define ICMP_ECHO_REPLY     0

/* ============ UDP Header ============ */

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

#define UDP_HEADER_LEN 8

/* ============ TCP Header ============ */

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset;        /* 4 bits + reserved */
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_header_t;

#define TCP_HEADER_LEN 20

/* TCP Flags */
#define TCP_FIN     0x01
#define TCP_SYN     0x02
#define TCP_RST     0x04
#define TCP_PSH     0x08
#define TCP_ACK     0x10
#define TCP_URG     0x20

/* ============ ARP Header ============ */

typedef struct {
    uint16_t hw_type;           /* 1 = Ethernet */
    uint16_t proto_type;        /* 0x0800 = IPv4 */
    uint8_t hw_len;             /* 6 for MAC */
    uint8_t proto_len;          /* 4 for IPv4 */
    uint16_t operation;         /* 1 = request, 2 = reply */
    mac_addr_t sender_mac;
    ipv4_addr_t sender_ip;
    mac_addr_t target_mac;
    ipv4_addr_t target_ip;
} __attribute__((packed)) arp_header_t;

#define ARP_REQUEST 1
#define ARP_REPLY   2

/* ============ DHCP Header ============ */

typedef struct {
    uint8_t op;                 /* 1 = BOOTREQUEST, 2 = BOOTREPLY */
    uint8_t htype;              /* 1 = Ethernet */
    uint8_t hlen;               /* 6 = MAC length */
    uint8_t hops;               /* 0 for client */
    uint32_t xid;               /* Transaction ID */
    uint16_t secs;              /* Seconds elapsed */
    uint16_t flags;             /* Flags (0x8000 = broadcast) */
    uint32_t ciaddr;            /* Client IP (if known) */
    uint32_t yiaddr;            /* 'Your' IP (offered) */
    uint32_t siaddr;            /* Server IP */
    uint32_t giaddr;            /* Gateway IP */
    uint8_t chaddr[16];         /* Client MAC (padded) */
    uint8_t sname[64];          /* Server name */
    uint8_t file[128];          /* Boot filename */
    uint8_t options[312];       /* DHCP options */
} __attribute__((packed)) dhcp_packet_t;

/* DHCP Message Types */
#define DHCP_DISCOVER   1
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_DECLINE    4
#define DHCP_ACK        5
#define DHCP_NAK        6
#define DHCP_RELEASE    7
#define DHCP_INFORM     8

/* DHCP Option Codes */
#define DHCP_OPT_PAD            0
#define DHCP_OPT_SUBNET         1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_HOSTNAME       12
#define DHCP_OPT_DOMAIN         15
#define DHCP_OPT_BROADCAST      28
#define DHCP_OPT_REQUESTED_IP   50
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_PARAM_LIST     55
#define DHCP_OPT_END            255

/* DHCP Magic Cookie */
#define DHCP_MAGIC_COOKIE       0x63825363

/* ============ DHCP API ============ */

/**
 * Perform DHCP discovery and configuration.
 * Returns 0 on success, -1 on failure.
 */
int dhcp_discover(net_interface_t *iface);

/**
 * Process incoming DHCP response.
 */
void dhcp_input(net_interface_t *iface, void *data, uint32_t len);

/* ============ Socket Types ============ */

typedef enum {
    SOCK_STREAM = 1,    /* TCP */
    SOCK_DGRAM = 2,     /* UDP */
    SOCK_RAW = 3,       /* Raw IP */
} socket_type_t;

/* ============ Socket Address ============ */

typedef struct {
    uint16_t family;        /* AF_INET */
    uint16_t port;          /* Network byte order */
    ipv4_addr_t addr;
    uint8_t zero[8];
} sockaddr_in_t;

#define AF_INET 2

/* ============ Socket ============ */

typedef struct socket {
    int fd;
    socket_type_t type;
    int protocol;
    
    /* Local binding */
    ipv4_addr_t local_ip;
    uint16_t local_port;
    
    /* Remote (connected) */
    ipv4_addr_t remote_ip;
    uint16_t remote_port;
    int connected;
    
    /* TCP State */
    int tcp_state;
    uint32_t seq_num;
    uint32_t ack_num;
    
    /* Buffers */
    uint8_t *rx_buffer;
    uint32_t rx_len;
    uint32_t rx_capacity;
    
    /* Interface */
    net_interface_t *iface;
    
    struct socket *next;
} socket_t;

/* TCP States */
#define TCP_CLOSED      0
#define TCP_LISTEN      1
#define TCP_SYN_SENT    2
#define TCP_SYN_RCVD    3
#define TCP_ESTABLISHED 4
#define TCP_FIN_WAIT_1  5
#define TCP_FIN_WAIT_2  6
#define TCP_CLOSE_WAIT  7
#define TCP_CLOSING     8
#define TCP_LAST_ACK    9
#define TCP_TIME_WAIT   10

/* ============ TCP/IP Stack API ============ */

/**
 * Initialize TCP/IP stack.
 */
void tcpip_init(void);

/**
 * Process incoming packet.
 */
void tcpip_input(net_interface_t *iface, void *data, uint32_t len);

/**
 * Calculate checksum.
 */
uint16_t tcpip_checksum(const void *data, uint32_t len);

/* ============ IP Layer ============ */

/**
 * Send IP packet.
 */
int ip_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
            const void *data, uint32_t len);

/**
 * Process IP packet.
 */
void ip_input(net_interface_t *iface, void *data, uint32_t len);

/* ============ ICMP Layer ============ */

/**
 * Send ICMP echo (ping).
 */
int icmp_ping(net_interface_t *iface, ipv4_addr_t dst, uint16_t seq);

/**
 * Process ICMP packet.
 */
void icmp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len);

/* ============ UDP Layer ============ */

/**
 * Send UDP packet.
 */
int udp_send(socket_t *sock, const void *data, uint32_t len);

/**
 * Process UDP packet.
 */
void udp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len);

/* ============ TCP Layer ============ */

/**
 * Send TCP data.
 */
int tcp_send(socket_t *sock, const void *data, uint32_t len);

/**
 * Process TCP packet.
 */
void tcp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len);

/**
 * TCP connect (client).
 */
int tcp_connect(socket_t *sock);

/**
 * TCP accept (server).
 */
socket_t *tcp_accept(socket_t *listen_sock);

/* ============ ARP Layer ============ */

/**
 * ARP lookup.
 */
int arp_resolve(net_interface_t *iface, ipv4_addr_t ip, mac_addr_t *mac);

/**
 * Process ARP packet.
 */
void arp_input(net_interface_t *iface, void *data, uint32_t len);

/* ============ Socket API ============ */

/**
 * Create a socket.
 */
int socket_create(socket_type_t type, int protocol);

/**
 * Bind socket to address.
 */
int socket_bind(int fd, sockaddr_in_t *addr);

/**
 * Listen for connections.
 */
int socket_listen(int fd, int backlog);

/**
 * Accept connection.
 */
int socket_accept(int fd, sockaddr_in_t *addr);

/**
 * Connect to remote.
 */
int socket_connect(int fd, sockaddr_in_t *addr);

/**
 * Send data.
 */
int socket_send(int fd, const void *data, uint32_t len);

/**
 * Receive data.
 */
int socket_recv(int fd, void *buf, uint32_t len);

/**
 * Send to address (UDP).
 */
int socket_sendto(int fd, const void *data, uint32_t len, sockaddr_in_t *addr);

/**
 * Receive from address (UDP).
 */
int socket_recvfrom(int fd, void *buf, uint32_t len, sockaddr_in_t *addr);

/**
 * Close socket.
 */
int socket_close(int fd);

#endif /* NEOLYX_TCPIP_H */
