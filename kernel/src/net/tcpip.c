/*
 * NeolyxOS TCP/IP Stack Implementation
 * 
 * Core networking protocols.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "net/tcpip.h"
#include "net/network.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ State ============ */

static socket_t *sockets = NULL;
static int next_socket_fd = 3;  /* 0,1,2 reserved for stdin/stdout/stderr */
static uint16_t next_ephemeral_port = 49152;

/* ARP Cache */
#define ARP_CACHE_SIZE 32
typedef struct {
    ipv4_addr_t ip;
    mac_addr_t mac;
    int valid;
    uint32_t timestamp;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

/* DNS Response Buffer (for dns_resolve in netinfra.c) */
uint8_t dns_response_buffer[512];
volatile int dns_response_ready = 0;
volatile int dns_response_len = 0;

/* ============ Helpers ============ */

static void *tcpip_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *tcpip_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static int ip_equal(ipv4_addr_t a, ipv4_addr_t b) {
    return a.bytes[0] == b.bytes[0] && a.bytes[1] == b.bytes[1] &&
           a.bytes[2] == b.bytes[2] && a.bytes[3] == b.bytes[3];
}

static uint16_t htons(uint16_t h) {
    return ((h & 0xFF) << 8) | ((h >> 8) & 0xFF);
}

static uint16_t ntohs(uint16_t n) {
    return htons(n);
}

static uint32_t htonl(uint32_t h) {
    return ((h & 0xFF) << 24) | ((h & 0xFF00) << 8) |
           ((h >> 8) & 0xFF00) | ((h >> 24) & 0xFF);
}

static uint32_t ntohl(uint32_t n) {
    return htonl(n);
}

static void serial_ip(ipv4_addr_t ip) {
    for (int i = 0; i < 4; i++) {
        if (i > 0) serial_putc('.');
        int v = ip.bytes[i];
        if (v >= 100) serial_putc('0' + v / 100);
        if (v >= 10) serial_putc('0' + (v / 10) % 10);
        serial_putc('0' + v % 10);
    }
}

/* ============ Checksum ============ */

uint16_t tcpip_checksum(const void *data, uint32_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/* ============ Socket Management ============ */

static socket_t *socket_find(int fd) {
    socket_t *s = sockets;
    while (s) {
        if (s->fd == fd) return s;
        s = s->next;
    }
    return NULL;
}

static socket_t *socket_find_by_port(uint16_t port, int proto) {
    socket_t *s = sockets;
    while (s) {
        if (s->local_port == port && s->protocol == proto) return s;
        s = s->next;
    }
    return NULL;
}

/* ============ ARP ============ */

static int arp_cache_lookup(ipv4_addr_t ip, mac_addr_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && ip_equal(arp_cache[i].ip, ip)) {
            *mac = arp_cache[i].mac;
            return 0;
        }
    }
    return -1;
}

static void arp_cache_add(ipv4_addr_t ip, mac_addr_t mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            arp_cache[i].mac = mac;
            arp_cache[i].valid = 1;
            return;
        }
    }
    /* Replace first entry if full */
    arp_cache[0].ip = ip;
    arp_cache[0].mac = mac;
}

int arp_resolve(net_interface_t *iface, ipv4_addr_t ip, mac_addr_t *mac) {
    /* Check cache first */
    if (arp_cache_lookup(ip, mac) == 0) return 0;
    
    /* Retry up to 3 times with timeout */
    for (int attempt = 0; attempt < 3; attempt++) {
        /* Build ARP request */
        uint8_t pkt[42];
        eth_frame_t *eth = (eth_frame_t *)pkt;
        arp_header_t *arp = (arp_header_t *)(pkt + ETH_HEADER_LEN);
        
        for (int i = 0; i < 6; i++) eth->dest.bytes[i] = 0xFF;
        eth->src = iface->mac;
        eth->type = htons(ETH_TYPE_ARP);
        
        arp->hw_type = htons(1);
        arp->proto_type = htons(ETH_TYPE_IPV4);
        arp->hw_len = 6;
        arp->proto_len = 4;
        arp->operation = htons(ARP_REQUEST);
        arp->sender_mac = iface->mac;
        arp->sender_ip = iface->ip;
        tcpip_memset(&arp->target_mac, 0, 6);
        arp->target_ip = ip;
        
        network_iface_send(iface, pkt, 42);
        
        /* Wait for reply (~100ms per attempt) */
        volatile int timeout = 100000;
        while (timeout-- > 0) {
            __asm__ volatile("pause");
            if (arp_cache_lookup(ip, mac) == 0) {
                return 0;  /* Got reply */
            }
        }
    }
    
    return -1;  /* Failed after retries */
}

void arp_input(net_interface_t *iface, void *data, uint32_t len) {
    if (len < sizeof(arp_header_t)) return;
    
    arp_header_t *arp = (arp_header_t *)data;
    
    /* Add sender to cache */
    arp_cache_add(arp->sender_ip, arp->sender_mac);
    
    if (ntohs(arp->operation) == ARP_REQUEST) {
        /* Is it for us? */
        if (ip_equal(arp->target_ip, iface->ip)) {
            /* Send reply */
            uint8_t pkt[42];
            eth_frame_t *eth = (eth_frame_t *)pkt;
            arp_header_t *reply = (arp_header_t *)(pkt + ETH_HEADER_LEN);
            
            eth->dest = arp->sender_mac;
            eth->src = iface->mac;
            eth->type = htons(ETH_TYPE_ARP);
            
            reply->hw_type = htons(1);
            reply->proto_type = htons(ETH_TYPE_IPV4);
            reply->hw_len = 6;
            reply->proto_len = 4;
            reply->operation = htons(ARP_REPLY);
            reply->sender_mac = iface->mac;
            reply->sender_ip = iface->ip;
            reply->target_mac = arp->sender_mac;
            reply->target_ip = arp->sender_ip;
            
            network_iface_send(iface, pkt, 42);
        }
    }
}

/* ============ IP Layer ============ */

int ip_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
            const void *data, uint32_t len) {
    uint32_t total = ETH_HEADER_LEN + IPV4_HEADER_LEN + len;
    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;
    
    eth_frame_t *eth = (eth_frame_t *)pkt;
    ipv4_header_t *ip = (ipv4_header_t *)(pkt + ETH_HEADER_LEN);
    uint8_t *payload = pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN;
    
    /* Get destination MAC */
    mac_addr_t dst_mac;
    
    /* Check if on same subnet */
    ipv4_addr_t target = dst;
    if ((dst.bytes[0] & iface->netmask.bytes[0]) != (iface->ip.bytes[0] & iface->netmask.bytes[0]) ||
        (dst.bytes[1] & iface->netmask.bytes[1]) != (iface->ip.bytes[1] & iface->netmask.bytes[1]) ||
        (dst.bytes[2] & iface->netmask.bytes[2]) != (iface->ip.bytes[2] & iface->netmask.bytes[2]) ||
        (dst.bytes[3] & iface->netmask.bytes[3]) != (iface->ip.bytes[3] & iface->netmask.bytes[3])) {
        target = iface->gateway;  /* Use gateway */
    }
    
    if (arp_resolve(iface, target, &dst_mac) != 0) {
        /* Broadcast if ARP fails */
        for (int i = 0; i < 6; i++) dst_mac.bytes[i] = 0xFF;
    }
    
    /* Ethernet header */
    eth->dest = dst_mac;
    eth->src = iface->mac;
    eth->type = htons(ETH_TYPE_IPV4);
    
    /* IP header */
    ip->version_ihl = 0x45;  /* IPv4, 5 DWORDs */
    ip->tos = 0;
    ip->total_length = htons(IPV4_HEADER_LEN + len);
    ip->id = htons(1);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src = iface->ip;
    ip->dst = dst;
    ip->checksum = tcpip_checksum(ip, IPV4_HEADER_LEN);
    
    /* Payload */
    tcpip_memcpy(payload, data, len);
    
    int ret = network_iface_send(iface, pkt, total);
    kfree(pkt);
    
    return ret;
}

void ip_input(net_interface_t *iface, void *data, uint32_t len) {
    if (len < IPV4_HEADER_LEN) return;
    
    ipv4_header_t *ip = (ipv4_header_t *)data;
    
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    void *payload = (uint8_t *)data + ihl;
    uint32_t payload_len = ntohs(ip->total_length) - ihl;
    
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            icmp_input(iface, ip, payload, payload_len);
            break;
        case IP_PROTO_UDP:
            udp_input(iface, ip, payload, payload_len);
            break;
        case IP_PROTO_TCP:
            tcp_input(iface, ip, payload, payload_len);
            break;
    }
}

/* ============ ICMP ============ */

int icmp_ping(net_interface_t *iface, ipv4_addr_t dst, uint16_t seq) {
    uint8_t pkt[sizeof(icmp_header_t) + 32];
    icmp_header_t *icmp = (icmp_header_t *)pkt;
    
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(1);
    icmp->sequence = htons(seq);
    icmp->checksum = 0;
    
    /* Add some data */
    for (int i = 0; i < 32; i++) {
        pkt[sizeof(icmp_header_t) + i] = i;
    }
    
    icmp->checksum = tcpip_checksum(pkt, sizeof(pkt));
    
    serial_puts("[ICMP] Ping ");
    serial_ip(dst);
    serial_puts("\n");
    
    return ip_send(iface, dst, IP_PROTO_ICMP, pkt, sizeof(pkt));
}

void icmp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len) {
    if (len < sizeof(icmp_header_t)) return;
    
    icmp_header_t *icmp = (icmp_header_t *)data;
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Send reply */
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = tcpip_checksum(data, len);
        
        ip_send(iface, ip->src, IP_PROTO_ICMP, data, len);
        
        serial_puts("[ICMP] Pong to ");
        serial_ip(ip->src);
        serial_puts("\n");
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        serial_puts("[ICMP] Reply from ");
        serial_ip(ip->src);
        serial_puts(" seq=");
        serial_putc('0' + (ntohs(icmp->sequence) % 10));
        serial_puts("\n");
    }
}

/* ============ UDP ============ */

int udp_send(socket_t *sock, const void *data, uint32_t len) {
    if (!sock || sock->type != SOCK_DGRAM) return -1;
    
    uint32_t total = UDP_HEADER_LEN + len;
    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;
    
    udp_header_t *udp = (udp_header_t *)pkt;
    udp->src_port = htons(sock->local_port);
    udp->dst_port = htons(sock->remote_port);
    udp->length = htons(total);
    udp->checksum = 0;  /* Optional for IPv4 */
    
    tcpip_memcpy(pkt + UDP_HEADER_LEN, data, len);
    
    int ret = ip_send(sock->iface, sock->remote_ip, IP_PROTO_UDP, pkt, total);
    kfree(pkt);
    
    return ret;
}

void udp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len) {
    if (len < UDP_HEADER_LEN) return;
    (void)iface;
    
    udp_header_t *udp = (udp_header_t *)data;
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t src_port = ntohs(udp->src_port);
    uint32_t payload_len = len - UDP_HEADER_LEN;
    
    /* Check if this is a DNS response (source port 53) */
    if (src_port == 53 && payload_len > 0 && payload_len <= 512) {
        tcpip_memcpy(dns_response_buffer, (uint8_t *)data + UDP_HEADER_LEN, payload_len);
        dns_response_len = payload_len;
        dns_response_ready = 1;
        return;
    }
    
    socket_t *sock = socket_find_by_port(dst_port, IP_PROTO_UDP);
    if (sock && sock->rx_buffer) {
        if (sock->rx_len + payload_len <= sock->rx_capacity) {
            tcpip_memcpy(sock->rx_buffer + sock->rx_len, 
                        (uint8_t *)data + UDP_HEADER_LEN, payload_len);
            sock->rx_len += payload_len;
        }
    }
}

/* ============ TCP ============ */

/* Calculate TCP checksum with pseudo-header */
static uint16_t tcp_checksum(ipv4_addr_t src, ipv4_addr_t dst,
                              const void *tcp_seg, uint32_t len) {
    uint32_t sum = 0;
    
    /* Pseudo-header */
    sum += (src.bytes[0] << 8) | src.bytes[1];
    sum += (src.bytes[2] << 8) | src.bytes[3];
    sum += (dst.bytes[0] << 8) | dst.bytes[1];
    sum += (dst.bytes[2] << 8) | dst.bytes[3];
    sum += IP_PROTO_TCP;  /* Protocol = 6 */
    sum += len;           /* TCP segment length */
    
    /* TCP segment */
    const uint16_t *ptr = (const uint16_t *)tcp_seg;
    uint32_t remaining = len;
    while (remaining > 1) {
        sum += *ptr++;
        remaining -= 2;
    }
    if (remaining == 1) {
        sum += *(const uint8_t *)ptr;
    }
    
    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

int tcp_send(socket_t *sock, const void *data, uint32_t len) {
    if (!sock || sock->type != SOCK_STREAM) return -1;
    if (sock->tcp_state != TCP_ESTABLISHED) return -1;
    
    uint32_t total = TCP_HEADER_LEN + len;
    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;
    
    tcp_header_t *tcp = (tcp_header_t *)pkt;
    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->seq_num);
    tcp->ack_num = htonl(sock->ack_num);
    tcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
    tcp->flags = TCP_ACK | TCP_PSH;
    tcp->window = htons(8192);
    tcp->checksum = 0;
    tcp->urgent = 0;
    
    tcpip_memcpy(pkt + TCP_HEADER_LEN, data, len);
    
    /* Calculate proper TCP checksum with pseudo-header */
    tcp->checksum = tcp_checksum(sock->iface->ip, sock->remote_ip, pkt, total);
    
    sock->seq_num += len;
    
    int ret = ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, pkt, total);
    kfree(pkt);
    
    return ret;
}

void tcp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len) {
    if (len < TCP_HEADER_LEN) return;
    
    tcp_header_t *tcp = (tcp_header_t *)data;
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint16_t src_port = ntohs(tcp->src_port);
    
    socket_t *sock = socket_find_by_port(dst_port, IP_PROTO_TCP);
    if (!sock) return;
    
    uint8_t flags = tcp->flags;
    uint32_t data_offset = ((tcp->data_offset >> 4) & 0xF) * 4;
    uint32_t payload_len = len - data_offset;
    
    /* Handle based on state */
    switch (sock->tcp_state) {
        case TCP_LISTEN:
            if (flags & TCP_SYN) {
                /* TODO: Create new socket for connection */
                sock->tcp_state = TCP_SYN_RCVD;
                sock->ack_num = ntohl(tcp->seq_num) + 1;
                
                /* Send SYN-ACK */
                uint8_t resp[TCP_HEADER_LEN];
                tcp_header_t *rtcp = (tcp_header_t *)resp;
                rtcp->src_port = htons(sock->local_port);
                rtcp->dst_port = htons(src_port);
                rtcp->seq_num = htonl(sock->seq_num++);
                rtcp->ack_num = htonl(sock->ack_num);
                rtcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
                rtcp->flags = TCP_SYN | TCP_ACK;
                rtcp->window = htons(8192);
                rtcp->checksum = 0;
                rtcp->urgent = 0;
                
                ip_send(iface, ip->src, IP_PROTO_TCP, resp, TCP_HEADER_LEN);
            }
            break;
            
        case TCP_SYN_SENT:
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                sock->ack_num = ntohl(tcp->seq_num) + 1;
                sock->tcp_state = TCP_ESTABLISHED;
                
                /* Send ACK */
                uint8_t resp[TCP_HEADER_LEN];
                tcp_header_t *rtcp = (tcp_header_t *)resp;
                rtcp->src_port = htons(sock->local_port);
                rtcp->dst_port = htons(sock->remote_port);
                rtcp->seq_num = htonl(sock->seq_num);
                rtcp->ack_num = htonl(sock->ack_num);
                rtcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
                rtcp->flags = TCP_ACK;
                rtcp->window = htons(8192);
                rtcp->checksum = 0;
                rtcp->urgent = 0;
                
                ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, resp, TCP_HEADER_LEN);
                
                serial_puts("[TCP] Connected\n");
            }
            break;
            
        case TCP_SYN_RCVD:
            if (flags & TCP_ACK) {
                sock->tcp_state = TCP_ESTABLISHED;
                serial_puts("[TCP] Connection accepted\n");
            }
            break;
            
        case TCP_ESTABLISHED:
            if (payload_len > 0 && sock->rx_buffer) {
                if (sock->rx_len + payload_len <= sock->rx_capacity) {
                    tcpip_memcpy(sock->rx_buffer + sock->rx_len,
                                (uint8_t *)data + data_offset, payload_len);
                    sock->rx_len += payload_len;
                    sock->ack_num += payload_len;
                }
                
                /* Send ACK */
                uint8_t resp[TCP_HEADER_LEN];
                tcp_header_t *rtcp = (tcp_header_t *)resp;
                rtcp->src_port = htons(sock->local_port);
                rtcp->dst_port = htons(sock->remote_port);
                rtcp->seq_num = htonl(sock->seq_num);
                rtcp->ack_num = htonl(sock->ack_num);
                rtcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
                rtcp->flags = TCP_ACK;
                rtcp->window = htons(8192);
                rtcp->checksum = 0;
                rtcp->urgent = 0;
                
                ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, resp, TCP_HEADER_LEN);
            }
            
            if (flags & TCP_FIN) {
                sock->ack_num++;
                sock->tcp_state = TCP_CLOSE_WAIT;
            }
            break;
    }
}

/* ============ Socket API ============ */

int socket_create(socket_type_t type, int protocol) {
    socket_t *sock = (socket_t *)kzalloc(sizeof(socket_t));
    if (!sock) return -1;
    
    sock->fd = next_socket_fd++;
    sock->type = type;
    sock->protocol = (type == SOCK_STREAM) ? IP_PROTO_TCP : IP_PROTO_UDP;
    sock->tcp_state = TCP_CLOSED;
    sock->rx_capacity = 8192;
    sock->rx_buffer = (uint8_t *)kmalloc(sock->rx_capacity);
    
    sock->next = sockets;
    sockets = sock;
    
    return sock->fd;
}

int socket_bind(int fd, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -1;
    
    sock->local_ip = addr->addr;
    sock->local_port = ntohs(addr->port);
    
    return 0;
}

int socket_listen(int fd, int backlog) {
    socket_t *sock = socket_find(fd);
    if (!sock || sock->type != SOCK_STREAM) return -1;
    
    sock->tcp_state = TCP_LISTEN;
    (void)backlog;
    
    return 0;
}

int socket_accept(int fd, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock || sock->type != SOCK_STREAM) return -1;
    if (sock->tcp_state != TCP_LISTEN) return -1;
    
    /* In a real implementation, this would:
     * 1. Wait for incoming connection
     * 2. Create new socket for the connection
     * 3. Fill addr with remote address
     * For now, return -1 (no pending connections)
     */
    (void)addr;
    return -1;  /* No pending connection */
}

int socket_connect(int fd, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -1;
    
    sock->remote_ip = addr->addr;
    sock->remote_port = ntohs(addr->port);
    
    if (sock->local_port == 0) {
        sock->local_port = next_ephemeral_port++;
    }
    
    sock->iface = network_get_interface_by_index(0);
    if (!sock->iface) return -1;
    
    if (sock->type == SOCK_STREAM) {
        /* Send SYN */
        sock->seq_num = 1000;
        sock->tcp_state = TCP_SYN_SENT;
        
        uint8_t pkt[TCP_HEADER_LEN];
        tcp_header_t *tcp = (tcp_header_t *)pkt;
        tcp->src_port = htons(sock->local_port);
        tcp->dst_port = htons(sock->remote_port);
        tcp->seq_num = htonl(sock->seq_num++);
        tcp->ack_num = 0;
        tcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
        tcp->flags = TCP_SYN;
        tcp->window = htons(8192);
        tcp->checksum = 0;
        tcp->urgent = 0;
        
        ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, pkt, TCP_HEADER_LEN);
    }
    
    sock->connected = 1;
    return 0;
}

int socket_send(int fd, const void *data, uint32_t len) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -1;
    
    if (sock->type == SOCK_STREAM) {
        return tcp_send(sock, data, len);
    } else {
        return udp_send(sock, data, len);
    }
}

int socket_recv(int fd, void *buf, uint32_t len) {
    socket_t *sock = socket_find(fd);
    if (!sock || !sock->rx_buffer) return -1;
    
    uint32_t available = sock->rx_len;
    if (available == 0) return 0;
    
    uint32_t to_copy = (len < available) ? len : available;
    tcpip_memcpy(buf, sock->rx_buffer, to_copy);
    
    /* Shift remaining data */
    sock->rx_len -= to_copy;
    if (sock->rx_len > 0) {
        tcpip_memcpy(sock->rx_buffer, sock->rx_buffer + to_copy, sock->rx_len);
    }
    
    return to_copy;
}

int socket_sendto(int fd, const void *data, uint32_t len, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock || sock->type != SOCK_DGRAM) return -1;
    if (!addr) return -1;
    
    /* Temporarily set remote for this send */
    ipv4_addr_t orig_ip = sock->remote_ip;
    uint16_t orig_port = sock->remote_port;
    
    sock->remote_ip = addr->addr;
    sock->remote_port = ntohs(addr->port);
    
    if (sock->local_port == 0) {
        sock->local_port = next_ephemeral_port++;
    }
    if (!sock->iface) {
        sock->iface = network_get_interface_by_index(0);
    }
    if (!sock->iface) return -1;
    
    int ret = udp_send(sock, data, len);
    
    /* Restore original */
    sock->remote_ip = orig_ip;
    sock->remote_port = orig_port;
    
    return ret;
}

int socket_recvfrom(int fd, void *buf, uint32_t len, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock || !sock->rx_buffer) return -1;
    
    if (sock->rx_len == 0) return 0;
    
    uint32_t to_copy = (len < sock->rx_len) ? len : sock->rx_len;
    tcpip_memcpy(buf, sock->rx_buffer, to_copy);
    
    /* Shift remaining */
    sock->rx_len -= to_copy;
    if (sock->rx_len > 0) {
        tcpip_memcpy(sock->rx_buffer, sock->rx_buffer + to_copy, sock->rx_len);
    }
    
    /* Set addr to last known remote if available */
    if (addr) {
        addr->family = AF_INET;
        addr->port = htons(sock->remote_port);
        addr->addr = sock->remote_ip;
    }
    
    return to_copy;
}

int socket_close(int fd) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -1;
    
    if (sock->type == SOCK_STREAM && sock->tcp_state == TCP_ESTABLISHED) {
        /* Send FIN */
        uint8_t pkt[TCP_HEADER_LEN];
        tcp_header_t *tcp = (tcp_header_t *)pkt;
        tcp->src_port = htons(sock->local_port);
        tcp->dst_port = htons(sock->remote_port);
        tcp->seq_num = htonl(sock->seq_num++);
        tcp->ack_num = htonl(sock->ack_num);
        tcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
        tcp->flags = TCP_FIN | TCP_ACK;
        tcp->window = 0;
        tcp->checksum = 0;
        tcp->urgent = 0;
        
        ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, pkt, TCP_HEADER_LEN);
        sock->tcp_state = TCP_FIN_WAIT_1;
    }
    
    /* Free resources */
    if (sock->rx_buffer) kfree(sock->rx_buffer);
    
    /* Remove from list */
    if (sockets == sock) {
        sockets = sock->next;
    } else {
        socket_t *prev = sockets;
        while (prev && prev->next != sock) prev = prev->next;
        if (prev) prev->next = sock->next;
    }
    
    kfree(sock);
    return 0;
}

/* ============ Main Input ============ */

void tcpip_input(net_interface_t *iface, void *data, uint32_t len) {
    if (len < ETH_HEADER_LEN) return;
    
    eth_frame_t *eth = (eth_frame_t *)data;
    void *payload = (uint8_t *)data + ETH_HEADER_LEN;
    uint32_t payload_len = len - ETH_HEADER_LEN;
    
    switch (ntohs(eth->type)) {
        case ETH_TYPE_IPV4:
            ip_input(iface, payload, payload_len);
            break;
        case ETH_TYPE_ARP:
            arp_input(iface, payload, payload_len);
            break;
    }
}

/* Note: DHCP client implementation is in network.c (dhcp_discover, dhcp_input) */

/* Firewall is initialized by boot.c boot sequence */

void tcpip_init(void) {
    serial_puts("[TCP/IP] Initializing protocol stack...\n");
    
    tcpip_memset(arp_cache, 0, sizeof(arp_cache));
    sockets = NULL;
    
    /* Note: firewall_init() is called by boot_services, not here */
    
    serial_puts("[TCP/IP] Ready\n");
}

