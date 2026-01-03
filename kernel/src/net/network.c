/*
 * NeolyxOS Network Subsystem Implementation
 * 
 * Network interface management and basic TCP/IP.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "net/network.h"
#include "net/tcpip.h"
#include "drivers/e1000.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ State ============ */

static net_interface_t *interfaces = NULL;
static int interface_count = 0;

/* ============ Helpers ============ */

static void net_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int net_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ============ Network API ============ */

int network_init(void) {
    serial_puts("[NET] Initializing network subsystem...\n");
    
    interfaces = NULL;
    interface_count = 0;
    
    /* Create loopback interface */
    net_interface_t *lo = (net_interface_t *)kzalloc(sizeof(net_interface_t));
    net_strcpy(lo->name, "lo", 16);
    lo->type = NET_IF_LOOPBACK;
    lo->state = NET_STATE_UP;
    lo->ip.bytes[0] = 127;
    lo->ip.bytes[1] = 0;
    lo->ip.bytes[2] = 0;
    lo->ip.bytes[3] = 1;
    lo->netmask.bytes[0] = 255;
    lo->netmask.bytes[1] = 0;
    lo->netmask.bytes[2] = 0;
    lo->netmask.bytes[3] = 0;
    network_register(lo);
    
    /* Initialize Ethernet driver */
    if (e1000_init() == 0) {
        /* Create interface for each e1000 device */
        for (int i = 0; ; i++) {
            e1000_device_t *dev = e1000_get_device(i);
            if (!dev) break;
            
            net_interface_t *eth = (net_interface_t *)kzalloc(sizeof(net_interface_t));
            eth->name[0] = 'e';
            eth->name[1] = 't';
            eth->name[2] = 'h';
            eth->name[3] = '0' + i;
            eth->name[4] = '\0';
            eth->type = NET_IF_ETHERNET;
            eth->state = NET_STATE_DOWN;
            
            /* Copy MAC */
            e1000_get_mac(dev, &eth->mac);
            
            eth->driver_data = dev;
            eth->send = network_iface_send;
            eth->receive = network_iface_recv;
            
            network_register(eth);
            
            /* Bring interface UP automatically */
            network_up(eth);
        }
    }
    
    serial_puts("[NET] Ready (");
    serial_putc('0' + interface_count);
    serial_puts(" interfaces)\n");
    
    return 0;
}

int network_register(net_interface_t *iface) {
    iface->next = interfaces;
    interfaces = iface;
    interface_count++;
    
    serial_puts("[NET] Registered: ");
    serial_puts(iface->name);
    serial_puts("\n");
    
    return 0;
}

int network_unregister(net_interface_t *iface) {
    if (interfaces == iface) {
        interfaces = iface->next;
    } else {
        net_interface_t *prev = interfaces;
        while (prev && prev->next != iface) {
            prev = prev->next;
        }
        if (prev) prev->next = iface->next;
    }
    interface_count--;
    return 0;
}

net_interface_t *network_get_interface(const char *name) {
    net_interface_t *iface = interfaces;
    while (iface) {
        if (net_strcmp(iface->name, name) == 0) {
            return iface;
        }
        iface = iface->next;
    }
    return NULL;
}

net_interface_t *network_get_interface_by_index(int index) {
    net_interface_t *iface = interfaces;
    int i = 0;
    while (iface && i < index) {
        iface = iface->next;
        i++;
    }
    return iface;
}

int network_interface_count(void) {
    return interface_count;
}

int network_up(net_interface_t *iface) {
    if (!iface) return -1;
    
    iface->state = NET_STATE_UP;
    serial_puts("[NET] ");
    serial_puts(iface->name);
    serial_puts(" UP\n");
    
    return 0;
}

int network_down(net_interface_t *iface) {
    if (!iface) return -1;
    
    iface->state = NET_STATE_DOWN;
    serial_puts("[NET] ");
    serial_puts(iface->name);
    serial_puts(" DOWN\n");
    
    return 0;
}

int network_iface_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!iface || iface->state != NET_STATE_UP) return -1;
    
    if (iface->type == NET_IF_ETHERNET) {
        e1000_device_t *dev = (e1000_device_t *)iface->driver_data;
        if (dev) {
            int ret = e1000_send(dev, data, len);
            if (ret > 0) {
                iface->tx_packets++;
                iface->tx_bytes += ret;
            } else {
                iface->tx_errors++;
            }
            return ret;
        }
    }
    
    return -1;
}

int network_iface_recv(net_interface_t *iface, void *buf, uint32_t max_len) {
    if (!iface || iface->state != NET_STATE_UP) return -1;
    
    if (iface->type == NET_IF_ETHERNET) {
        e1000_device_t *dev = (e1000_device_t *)iface->driver_data;
        if (dev) {
            int ret = e1000_receive(dev, buf, max_len);
            if (ret > 0) {
                iface->rx_packets++;
                iface->rx_bytes += ret;
            }
            return ret;
        }
    }
    
    return 0;
}

int network_set_ip(net_interface_t *iface, ipv4_addr_t ip, 
                   ipv4_addr_t netmask, ipv4_addr_t gateway) {
    if (!iface) return -1;
    
    for (int i = 0; i < 4; i++) {
        iface->ip.bytes[i] = ip.bytes[i];
        iface->netmask.bytes[i] = netmask.bytes[i];
        iface->gateway.bytes[i] = gateway.bytes[i];
    }
    
    serial_puts("[NET] ");
    serial_puts(iface->name);
    serial_puts(" IP: ");
    for (int i = 0; i < 4; i++) {
        if (i > 0) serial_putc('.');
        int v = ip.bytes[i];
        if (v >= 100) serial_putc('0' + v / 100);
        if (v >= 10) serial_putc('0' + (v / 10) % 10);
        serial_putc('0' + v % 10);
    }
    serial_puts("\n");
    
    return 0;
}

int network_set_dns(net_interface_t *iface, ipv4_addr_t dns) {
    if (!iface) return -1;
    
    for (int i = 0; i < 4; i++) {
        iface->dns.bytes[i] = dns.bytes[i];
    }
    
    return 0;
}

/* ============ DHCP Client Implementation ============ */

/* DHCP State */
static uint32_t dhcp_xid = 0x12345678;
static ipv4_addr_t dhcp_offered_ip;
static ipv4_addr_t dhcp_server_ip;

static void dhcp_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

static void dhcp_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static uint16_t dhcp_htons(uint16_t h) {
    return ((h & 0xFF) << 8) | ((h >> 8) & 0xFF);
}

static uint32_t dhcp_htonl(uint32_t h) {
    return ((h & 0xFF) << 24) | ((h & 0xFF00) << 8) |
           ((h >> 8) & 0xFF00) | ((h >> 24) & 0xFF);
}

static int dhcp_send_discover(net_interface_t *iface) {
    /* Build DHCP DISCOVER packet */
    uint8_t pkt[ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN + sizeof(dhcp_packet_t)];
    dhcp_memset(pkt, 0, sizeof(pkt));
    
    eth_frame_t *eth = (eth_frame_t *)pkt;
    ipv4_header_t *ip = (ipv4_header_t *)(pkt + ETH_HEADER_LEN);
    udp_header_t *udp = (udp_header_t *)(pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN);
    dhcp_packet_t *dhcp = (dhcp_packet_t *)(pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN);
    
    /* Ethernet: broadcast */
    for (int i = 0; i < 6; i++) eth->dest.bytes[i] = 0xFF;
    eth->src = iface->mac;
    eth->type = dhcp_htons(ETH_TYPE_IPV4);
    
    /* IP header */
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = dhcp_htons(IPV4_HEADER_LEN + UDP_HEADER_LEN + sizeof(dhcp_packet_t));
    ip->id = dhcp_htons(1);
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    ip->checksum = 0;
    /* Source: 0.0.0.0, Dest: 255.255.255.255 */
    for (int i = 0; i < 4; i++) {
        ip->src.bytes[i] = 0;
        ip->dst.bytes[i] = 255;
    }
    ip->checksum = tcpip_checksum(ip, IPV4_HEADER_LEN);
    
    /* UDP header */
    udp->src_port = dhcp_htons(PORT_DHCP_C);  /* 68 */
    udp->dst_port = dhcp_htons(PORT_DHCP_S);  /* 67 */
    udp->length = dhcp_htons(UDP_HEADER_LEN + sizeof(dhcp_packet_t));
    udp->checksum = 0;  /* Optional for IPv4 */
    
    /* DHCP packet */
    dhcp->op = 1;      /* BOOTREQUEST */
    dhcp->htype = 1;   /* Ethernet */
    dhcp->hlen = 6;    /* MAC length */
    dhcp->hops = 0;
    dhcp->xid = dhcp_xid;
    dhcp->secs = 0;
    dhcp->flags = dhcp_htons(0x8000);  /* Broadcast flag */
    
    /* Copy MAC to chaddr */
    for (int i = 0; i < 6; i++) {
        dhcp->chaddr[i] = iface->mac.bytes[i];
    }
    
    /* Options: Magic cookie + DHCP DISCOVER + End */
    uint8_t *opt = dhcp->options;
    *opt++ = 0x63; *opt++ = 0x82; *opt++ = 0x53; *opt++ = 0x63;  /* Magic cookie */
    *opt++ = DHCP_OPT_MSG_TYPE; *opt++ = 1; *opt++ = DHCP_DISCOVER;
    *opt++ = DHCP_OPT_PARAM_LIST; *opt++ = 4;
    *opt++ = DHCP_OPT_SUBNET; *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_DNS; *opt++ = DHCP_OPT_LEASE_TIME;
    *opt++ = DHCP_OPT_END;
    
    serial_puts("[DHCP] Sending DISCOVER...\n");
    return network_iface_send(iface, pkt, sizeof(pkt));
}

static int dhcp_send_request(net_interface_t *iface) {
    uint8_t pkt[ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN + sizeof(dhcp_packet_t)];
    dhcp_memset(pkt, 0, sizeof(pkt));
    
    eth_frame_t *eth = (eth_frame_t *)pkt;
    ipv4_header_t *ip = (ipv4_header_t *)(pkt + ETH_HEADER_LEN);
    udp_header_t *udp = (udp_header_t *)(pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN);
    dhcp_packet_t *dhcp = (dhcp_packet_t *)(pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN);
    
    /* Ethernet: broadcast */
    for (int i = 0; i < 6; i++) eth->dest.bytes[i] = 0xFF;
    eth->src = iface->mac;
    eth->type = dhcp_htons(ETH_TYPE_IPV4);
    
    /* IP header */
    ip->version_ihl = 0x45;
    ip->total_length = dhcp_htons(IPV4_HEADER_LEN + UDP_HEADER_LEN + sizeof(dhcp_packet_t));
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    for (int i = 0; i < 4; i++) {
        ip->src.bytes[i] = 0;
        ip->dst.bytes[i] = 255;
    }
    ip->checksum = tcpip_checksum(ip, IPV4_HEADER_LEN);
    
    /* UDP */
    udp->src_port = dhcp_htons(PORT_DHCP_C);
    udp->dst_port = dhcp_htons(PORT_DHCP_S);
    udp->length = dhcp_htons(UDP_HEADER_LEN + sizeof(dhcp_packet_t));
    
    /* DHCP REQUEST */
    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = dhcp_xid;
    dhcp->flags = dhcp_htons(0x8000);
    for (int i = 0; i < 6; i++) dhcp->chaddr[i] = iface->mac.bytes[i];
    
    /* Options */
    uint8_t *opt = dhcp->options;
    *opt++ = 0x63; *opt++ = 0x82; *opt++ = 0x53; *opt++ = 0x63;  /* Magic */
    *opt++ = DHCP_OPT_MSG_TYPE; *opt++ = 1; *opt++ = DHCP_REQUEST;
    *opt++ = DHCP_OPT_REQUESTED_IP; *opt++ = 4;
    for (int i = 0; i < 4; i++) *opt++ = dhcp_offered_ip.bytes[i];
    *opt++ = DHCP_OPT_SERVER_ID; *opt++ = 4;
    for (int i = 0; i < 4; i++) *opt++ = dhcp_server_ip.bytes[i];
    *opt++ = DHCP_OPT_END;
    
    serial_puts("[DHCP] Sending REQUEST...\n");
    return network_iface_send(iface, pkt, sizeof(pkt));
}

void dhcp_input(net_interface_t *iface, void *data, uint32_t len) {
    if (len < sizeof(dhcp_packet_t)) return;
    
    dhcp_packet_t *dhcp = (dhcp_packet_t *)data;
    
    /* Check transaction ID */
    if (dhcp->xid != dhcp_xid) return;
    
    /* Parse options to find message type */
    uint8_t *opt = dhcp->options + 4;  /* Skip magic cookie */
    uint8_t msg_type = 0;
    ipv4_addr_t subnet = {{0}};
    ipv4_addr_t router = {{0}};
    ipv4_addr_t dns = {{0}};
    
    while (*opt != DHCP_OPT_END && opt < dhcp->options + 312) {
        uint8_t code = *opt++;
        if (code == DHCP_OPT_PAD) continue;
        uint8_t olen = *opt++;
        
        switch (code) {
            case DHCP_OPT_MSG_TYPE:
                msg_type = *opt;
                break;
            case DHCP_OPT_SUBNET:
                for (int i = 0; i < 4 && i < olen; i++) subnet.bytes[i] = opt[i];
                break;
            case DHCP_OPT_ROUTER:
                for (int i = 0; i < 4 && i < olen; i++) router.bytes[i] = opt[i];
                break;
            case DHCP_OPT_DNS:
                for (int i = 0; i < 4 && i < olen; i++) dns.bytes[i] = opt[i];
                break;
            case DHCP_OPT_SERVER_ID:
                for (int i = 0; i < 4 && i < olen; i++) dhcp_server_ip.bytes[i] = opt[i];
                break;
        }
        opt += olen;
    }
    
    /* Extract offered IP from yiaddr */
    dhcp_offered_ip.bytes[0] = (dhcp->yiaddr >> 0) & 0xFF;
    dhcp_offered_ip.bytes[1] = (dhcp->yiaddr >> 8) & 0xFF;
    dhcp_offered_ip.bytes[2] = (dhcp->yiaddr >> 16) & 0xFF;
    dhcp_offered_ip.bytes[3] = (dhcp->yiaddr >> 24) & 0xFF;
    
    if (msg_type == DHCP_OFFER) {
        serial_puts("[DHCP] Received OFFER: ");
        for (int i = 0; i < 4; i++) {
            if (i > 0) serial_putc('.');
            int v = dhcp_offered_ip.bytes[i];
            if (v >= 100) serial_putc('0' + v / 100);
            if (v >= 10) serial_putc('0' + (v / 10) % 10);
            serial_putc('0' + v % 10);
        }
        serial_puts("\n");
        dhcp_send_request(iface);
    } else if (msg_type == DHCP_ACK) {
        serial_puts("[DHCP] Received ACK - Configuring interface\n");
        network_set_ip(iface, dhcp_offered_ip, subnet, router);
        network_set_dns(iface, dns);
        iface->state = NET_STATE_CONNECTED;
        serial_puts("[DHCP] Configuration complete!\n");
    }
}

int dhcp_discover(net_interface_t *iface) {
    if (!iface) return -1;
    dhcp_xid++;  /* New transaction */
    return dhcp_send_discover(iface);
}

int network_dhcp_request(net_interface_t *iface) {
    if (!iface) return -1;
    
    serial_puts("[NET] DHCP request on ");
    serial_puts(iface->name);
    serial_puts("...\n");
    
    /* Send DHCP DISCOVER and wait for response */
    return dhcp_discover(iface);
}

int network_dhcp_release(net_interface_t *iface) {
    if (!iface) return -1;
    
    /* Clear IP */
    for (int i = 0; i < 4; i++) {
        iface->ip.bytes[i] = 0;
        iface->netmask.bytes[i] = 0;
        iface->gateway.bytes[i] = 0;
    }
    
    iface->state = NET_STATE_UP;
    
    return 0;
}

/* ============ Status ============ */

void network_print_status(void) {
    serial_puts("\n=== Network Interfaces ===\n");
    
    net_interface_t *iface = interfaces;
    while (iface) {
        serial_puts(iface->name);
        serial_puts(": ");
        
        switch (iface->state) {
            case NET_STATE_DOWN: serial_puts("DOWN"); break;
            case NET_STATE_UP: serial_puts("UP"); break;
            case NET_STATE_CONNECTING: serial_puts("CONNECTING"); break;
            case NET_STATE_CONNECTED: serial_puts("CONNECTED"); break;
            case NET_STATE_ERROR: serial_puts("ERROR"); break;
        }
        serial_puts("\n");
        
        if (iface->type == NET_IF_ETHERNET) {
            serial_puts("  MAC: ");
            for (int i = 0; i < 6; i++) {
                if (i > 0) serial_putc(':');
                serial_putc("0123456789ABCDEF"[(iface->mac.bytes[i] >> 4) & 0xF]);
                serial_putc("0123456789ABCDEF"[iface->mac.bytes[i] & 0xF]);
            }
            serial_puts("\n");
        }
        
        serial_puts("  IP: ");
        for (int i = 0; i < 4; i++) {
            if (i > 0) serial_putc('.');
            int v = iface->ip.bytes[i];
            if (v >= 100) serial_putc('0' + v / 100);
            if (v >= 10) serial_putc('0' + (v / 10) % 10);
            serial_putc('0' + v % 10);
        }
        serial_puts("\n");
        
        iface = iface->next;
    }
    
    serial_puts("==========================\n\n");
}
