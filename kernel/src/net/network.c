/*
 * NeolyxOS Network Subsystem Implementation
 *
 * Interface management, the DHCP client and the network service thread.
 *
 * Model:
 *   - The kernel is not preemptible (core/process.c), so the network
 *     service thread ("netd") and syscalls that use the stack never run
 *     packet code at the same time. No interrupt handler touches the
 *     stack: the e1000 driver is polled.
 *   - netd brings the interfaces up, starts DHCP on every Ethernet
 *     interface with a link, then loops: drain received frames, run
 *     timers (DHCP retransmit/renewal, ARP expiry, TCP retransmission),
 *     sleep NETD_PERIOD_MS.
 *   - A syscall that waits on the network (connect, a DNS query) calls
 *     network_wait(), which polls input itself and sleeps in between.
 *
 * Copyright (c) 2025 KetiveeAI.
 */

#include "net/network.h"
#include "net/tcpip.h"
#include "drivers/e1000.h"
#include "mm/kheap.h"
#include "core/process.h"
#include "core/syscall.h"   /* errno values, user-copy helpers */
#include "core/nx_net.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/* ============ Tunables ============ */

#define NETD_PERIOD_MS          10      /* service loop period */
#define NET_POLL_BUDGET         64      /* frames per network_poll() call */
#define NET_FRAME_MAX           (ETH_HEADER_LEN + ETH_MTU)

#define DHCP_MAX_CLIENTS        4
#define DHCP_RETRY_FIRST_MS     2000u
#define DHCP_RETRY_MAX_MS       32000u
#define DHCP_REQUEST_TRIES      4
#define DHCP_MIN_LEASE_S        60u     /* shorter leases are treated as 60 s */

/* ============ State ============ */

static net_interface_t *interfaces = NULL;
static int interface_count = 0;
static int net_initialized = 0;
static int netd_pid = -1;

/* Frame buffer shared by every poll: input processing never re-enters
 * network_poll() (see poll_depth), so one buffer is enough. */
static uint8_t rx_frame[NET_FRAME_MAX] __attribute__((aligned(16)));
static int poll_depth = 0;

/* ============ Helpers ============ */

static void net_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int net_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void serial_dec(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v && i < 11) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    while (i--) serial_putc(buf[i]);
}

static void serial_ip(ipv4_addr_t ip) {
    for (int i = 0; i < 4; i++) {
        if (i > 0) serial_putc('.');
        serial_dec(ip.bytes[i]);
    }
}

static int ip_is_zero(ipv4_addr_t ip) {
    return (ip.bytes[0] | ip.bytes[1] | ip.bytes[2] | ip.bytes[3]) == 0;
}

static int mask_prefix(ipv4_addr_t m) {
    int n = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t b = m.bytes[i];
        while (b & 0x80) { n++; b = (uint8_t)(b << 1); }
    }
    return n;
}

uint64_t network_now_ms(void) {
    return pit_get_ticks();
}

/* Not cryptographic: TSC mixed with a counter, for DHCP xids, TCP initial
 * sequence numbers, DNS ids and ephemeral ports. */
uint32_t net_random32(void) {
    static uint32_t state = 0x9E3779B9u;
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    state ^= lo ^ (hi << 7) ^ (state << 13);
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* ============ Interface API ============ */

static net_interface_t *iface_alloc(const char *name, net_if_type_t type) {
    net_interface_t *iface = (net_interface_t *)kzalloc(sizeof(net_interface_t));
    if (!iface) {
        serial_puts("[NET] ERROR: out of memory creating interface ");
        serial_puts(name);
        serial_puts("\n");
        return NULL;
    }
    net_strcpy(iface->name, name, 16);
    iface->type = type;
    iface->state = NET_STATE_DOWN;
    return iface;
}

int network_init(void) {
    if (net_initialized) return 0;

    serial_puts("[NET] Initializing network subsystem...\n");

    interfaces = NULL;
    interface_count = 0;

    net_interface_t *lo = iface_alloc("lo", NET_IF_LOOPBACK);
    if (lo) {
        lo->state = NET_STATE_UP;
        lo->ip.bytes[0] = 127;
        lo->ip.bytes[3] = 1;
        lo->netmask.bytes[0] = 255;
        network_register(lo);
    }

    if (e1000_init() == 0) {
        for (int i = 0; i < 10; i++) {
            e1000_device_t *dev = e1000_get_device(i);
            if (!dev) break;

            char name[5] = { 'e', 't', 'h', (char)('0' + i), '\0' };
            net_interface_t *eth = iface_alloc(name, NET_IF_ETHERNET);
            if (!eth) break;

            e1000_get_mac(dev, &eth->mac);
            eth->driver_data = dev;
            eth->send = network_iface_send;
            eth->receive = network_iface_recv;
            eth->link_up = (uint8_t)e1000_link_poll(dev);

            network_register(eth);
            network_up(eth);
        }
    } else {
        serial_puts("[NET] No supported Ethernet controller; wired networking unavailable\n");
    }

    net_initialized = 1;
    serial_puts("[NET] Ready (");
    serial_dec((uint32_t)interface_count);
    serial_puts(" interfaces)\n");

    return 0;
}

int network_register(net_interface_t *iface) {
    if (!iface) return -1;

    /* Append, so index 0 is the first interface registered (lo) and
     * eth0 follows: indices stay stable as interfaces are added. */
    iface->next = NULL;
    if (!interfaces) {
        interfaces = iface;
    } else {
        net_interface_t *tail = interfaces;
        while (tail->next) tail = tail->next;
        tail->next = iface;
    }
    interface_count++;

    serial_puts("[NET] Registered: ");
    serial_puts(iface->name);
    serial_puts("\n");

    return 0;
}

int network_unregister(net_interface_t *iface) {
    if (!iface) return -1;

    if (interfaces == iface) {
        interfaces = iface->next;
    } else {
        net_interface_t *prev = interfaces;
        while (prev && prev->next != iface) prev = prev->next;
        if (!prev) return -1;
        prev->next = iface->next;
    }
    interface_count--;
    return 0;
}

net_interface_t *network_get_interface(const char *name) {
    if (!name) return NULL;
    for (net_interface_t *iface = interfaces; iface; iface = iface->next) {
        if (net_strcmp(iface->name, name) == 0) return iface;
    }
    return NULL;
}

net_interface_t *network_get_interface_by_index(int index) {
    if (index < 0) return NULL;
    net_interface_t *iface = interfaces;
    for (int i = 0; iface && i < index; i++) iface = iface->next;
    return iface;
}

int network_interface_count(void) {
    return interface_count;
}

net_interface_t *network_default_iface(void) {
    net_interface_t *fallback = NULL;
    for (net_interface_t *iface = interfaces; iface; iface = iface->next) {
        if (iface->type == NET_IF_LOOPBACK) continue;
        if (iface->state != NET_STATE_CONNECTED || ip_is_zero(iface->ip)) continue;
        if (!ip_is_zero(iface->gateway)) return iface;
        if (!fallback) fallback = iface;
    }
    return fallback;
}

int network_up(net_interface_t *iface) {
    if (!iface) return -1;

    if (iface->state == NET_STATE_DOWN) iface->state = NET_STATE_UP;
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
    if (!iface || !data || len == 0) return -1;
    if (iface->state == NET_STATE_DOWN) return -1;

    if (iface->type == NET_IF_ETHERNET) {
        e1000_device_t *dev = (e1000_device_t *)iface->driver_data;
        if (dev) {
            int ret = e1000_send(dev, data, len);
            if (ret > 0) {
                iface->tx_packets++;
                iface->tx_bytes += (uint64_t)ret;
            } else {
                iface->tx_errors++;
            }
            return ret;
        }
    }

    return -1;
}

int network_iface_recv(net_interface_t *iface, void *buf, uint32_t max_len) {
    if (!iface || !buf) return -1;
    if (iface->state == NET_STATE_DOWN) return 0;

    if (iface->type == NET_IF_ETHERNET) {
        e1000_device_t *dev = (e1000_device_t *)iface->driver_data;
        if (dev) {
            int ret = e1000_receive(dev, buf, max_len);
            if (ret > 0) {
                iface->rx_packets++;
                iface->rx_bytes += (uint64_t)ret;
            }
            return ret;
        }
    }

    return 0;
}

int network_set_ip(net_interface_t *iface, ipv4_addr_t ip,
                   ipv4_addr_t netmask, ipv4_addr_t gateway) {
    if (!iface) return -1;

    iface->ip = ip;
    iface->netmask = netmask;
    iface->gateway = gateway;

    serial_puts("[NET] ");
    serial_puts(iface->name);
    serial_puts(" IP: ");
    serial_ip(ip);
    serial_puts("/");
    serial_dec((uint32_t)mask_prefix(netmask));
    serial_puts(" gateway ");
    serial_ip(gateway);
    serial_puts("\n");

    return 0;
}

int network_set_dns(net_interface_t *iface, ipv4_addr_t dns) {
    if (!iface) return -1;

    iface->dns = dns;
    iface->dns_servers[0] = dns;
    iface->dns_count = ip_is_zero(dns) ? 0 : 1;
    return 0;
}

/* ============ DHCP Client ============ */

typedef enum {
    DHCP_IDLE = 0,      /* not running on this interface */
    DHCP_SELECTING,     /* DISCOVER sent, waiting for an OFFER */
    DHCP_REQUESTING,    /* REQUEST sent, waiting for ACK/NAK */
    DHCP_BOUND,         /* address in use */
    DHCP_REBINDING,     /* lease past T1: broadcast REQUEST with ciaddr */
} dhcp_state_t;

typedef struct {
    net_interface_t *iface;
    dhcp_state_t state;
    uint32_t xid;
    uint32_t tries;
    uint32_t retry_ms;
    uint64_t next_send_ms;
    uint64_t start_ms;
    uint64_t t1_ms, expiry_ms;
    ipv4_addr_t offered;
    ipv4_addr_t server;
} dhcp_client_t;

static dhcp_client_t dhcp_clients[DHCP_MAX_CLIENTS];

#define DHCP_FIXED_LEN  236     /* dhcp_packet_t without options[] */

static uint16_t dhcp_htons(uint16_t h) {
    return (uint16_t)(((h & 0xFF) << 8) | ((h >> 8) & 0xFF));
}

static dhcp_client_t *dhcp_find(net_interface_t *iface, int create) {
    dhcp_client_t *free_slot = NULL;
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (dhcp_clients[i].iface == iface) return &dhcp_clients[i];
        if (!dhcp_clients[i].iface && !free_slot) free_slot = &dhcp_clients[i];
    }
    if (!create || !free_slot) return NULL;
    uint8_t *p = (uint8_t *)free_slot;
    for (uint32_t i = 0; i < sizeof(*free_slot); i++) p[i] = 0;
    free_slot->iface = iface;
    return free_slot;
}

/*
 * Build and broadcast one DHCP message. `ciaddr` is our current address
 * when rebinding, else 0.0.0.0. REQUEST in SELECTING carries the
 * requested address and server id (RFC 2131 section 4.3.2).
 */
static int dhcp_send(dhcp_client_t *c, uint8_t msg_type) {
    net_interface_t *iface = c->iface;
    uint8_t pkt[ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN + sizeof(dhcp_packet_t)];
    for (uint32_t i = 0; i < sizeof(pkt); i++) pkt[i] = 0;

    eth_frame_t *eth = (eth_frame_t *)pkt;
    ipv4_header_t *ip = (ipv4_header_t *)(pkt + ETH_HEADER_LEN);
    udp_header_t *udp = (udp_header_t *)(pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN);
    dhcp_packet_t *dhcp = (dhcp_packet_t *)(pkt + ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN);

    int rebinding = (c->state == DHCP_REBINDING);

    dhcp->op = 1;       /* BOOTREQUEST */
    dhcp->htype = 1;    /* Ethernet */
    dhcp->hlen = 6;
    dhcp->xid = c->xid;
    uint64_t secs = (network_now_ms() - c->start_ms) / 1000;
    dhcp->secs = dhcp_htons((uint16_t)(secs > 0xFFFF ? 0xFFFF : secs));
    dhcp->flags = rebinding ? 0 : dhcp_htons(0x8000);   /* ask for broadcast replies */
    if (rebinding) {
        uint8_t *ci = (uint8_t *)&dhcp->ciaddr;
        for (int i = 0; i < 4; i++) ci[i] = iface->ip.bytes[i];
    }
    for (int i = 0; i < 6; i++) dhcp->chaddr[i] = iface->mac.bytes[i];

    uint8_t *opt = dhcp->options;
    *opt++ = 0x63; *opt++ = 0x82; *opt++ = 0x53; *opt++ = 0x63;   /* magic cookie */
    *opt++ = DHCP_OPT_MSG_TYPE; *opt++ = 1; *opt++ = msg_type;
    /* Client identifier: hardware type + MAC */
    *opt++ = 61; *opt++ = 7; *opt++ = 1;
    for (int i = 0; i < 6; i++) *opt++ = iface->mac.bytes[i];
    if (msg_type == DHCP_REQUEST && !rebinding) {
        *opt++ = DHCP_OPT_REQUESTED_IP; *opt++ = 4;
        for (int i = 0; i < 4; i++) *opt++ = c->offered.bytes[i];
        *opt++ = DHCP_OPT_SERVER_ID; *opt++ = 4;
        for (int i = 0; i < 4; i++) *opt++ = c->server.bytes[i];
    }
    *opt++ = DHCP_OPT_PARAM_LIST; *opt++ = 4;
    *opt++ = DHCP_OPT_SUBNET; *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_DNS; *opt++ = DHCP_OPT_LEASE_TIME;
    *opt++ = DHCP_OPT_END;

    uint32_t dhcp_len = (uint32_t)sizeof(dhcp_packet_t);
    for (int i = 0; i < 6; i++) eth->dest.bytes[i] = 0xFF;
    eth->src = iface->mac;
    eth->type = dhcp_htons(ETH_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->total_length = dhcp_htons((uint16_t)(IPV4_HEADER_LEN + UDP_HEADER_LEN + dhcp_len));
    ip->id = dhcp_htons((uint16_t)net_random32());
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    if (rebinding) ip->src = iface->ip;
    for (int i = 0; i < 4; i++) ip->dst.bytes[i] = 255;
    ip->checksum = 0;
    ip->checksum = tcpip_checksum(ip, IPV4_HEADER_LEN);

    udp->src_port = dhcp_htons(PORT_DHCP_C);
    udp->dst_port = dhcp_htons(PORT_DHCP_S);
    udp->length = dhcp_htons((uint16_t)(UDP_HEADER_LEN + dhcp_len));
    udp->checksum = 0;  /* optional for IPv4 */

    serial_puts("[DHCP] ");
    serial_puts(iface->name);
    serial_puts(msg_type == DHCP_DISCOVER ? ": DISCOVER\n" : ": REQUEST\n");

    int ret = network_iface_send(iface, pkt, sizeof(pkt));
    if (ret < 0) {
        serial_puts("[DHCP] ERROR: send failed on ");
        serial_puts(iface->name);
        serial_puts("\n");
    }
    return ret;
}

static void dhcp_schedule_retry(dhcp_client_t *c) {
    uint64_t now = network_now_ms();
    /* +/- 1 s randomization (RFC 2131 section 4.1) */
    uint32_t jitter = net_random32() % 2000u;
    c->next_send_ms = now + c->retry_ms - 1000u + jitter;
    if (c->retry_ms < DHCP_RETRY_MAX_MS) c->retry_ms *= 2;
}

static void dhcp_restart(dhcp_client_t *c) {
    c->state = DHCP_SELECTING;
    c->xid = net_random32();
    c->tries = 0;
    c->retry_ms = DHCP_RETRY_FIRST_MS;
    c->start_ms = network_now_ms();
    c->next_send_ms = c->start_ms;   /* send at the next timer run */
}

static void dhcp_clear_address(net_interface_t *iface) {
    ipv4_addr_t zero = {{0, 0, 0, 0}};
    iface->ip = zero;
    iface->netmask = zero;
    iface->gateway = zero;
    iface->dns = zero;
    iface->dns_count = 0;
    iface->lease_seconds = 0;
    iface->ipv4_dhcp = 0;
    if (iface->state == NET_STATE_CONNECTED) iface->state = NET_STATE_UP;
}

void dhcp_input(net_interface_t *iface, void *data, uint32_t len) {
    if (!iface || !data || len < DHCP_FIXED_LEN + 4) return;

    dhcp_client_t *c = dhcp_find(iface, 0);
    if (!c || c->state == DHCP_IDLE || c->state == DHCP_BOUND) return;

    dhcp_packet_t *dhcp = (dhcp_packet_t *)data;
    if (dhcp->op != 2 || dhcp->htype != 1 || dhcp->hlen != 6) return;
    if (dhcp->xid != c->xid) return;
    for (int i = 0; i < 6; i++) {
        if (dhcp->chaddr[i] != iface->mac.bytes[i]) return;
    }

    const uint8_t *opt = dhcp->options;
    const uint8_t *end = (const uint8_t *)data + len;
    if (opt[0] != 0x63 || opt[1] != 0x82 || opt[2] != 0x53 || opt[3] != 0x63) return;
    opt += 4;

    uint8_t msg_type = 0;
    ipv4_addr_t subnet = {{0}}, router = {{0}}, server = {{0}};
    ipv4_addr_t dns[3];
    uint8_t dns_count = 0;
    uint32_t lease = 0;

    while (opt < end) {
        uint8_t code = *opt++;
        if (code == DHCP_OPT_PAD) continue;
        if (code == DHCP_OPT_END) break;
        if (opt >= end) break;
        uint8_t olen = *opt++;
        if (olen > (uint32_t)(end - opt)) break;     /* truncated option */

        switch (code) {
            case DHCP_OPT_MSG_TYPE:
                if (olen >= 1) msg_type = opt[0];
                break;
            case DHCP_OPT_SUBNET:
                if (olen >= 4) for (int i = 0; i < 4; i++) subnet.bytes[i] = opt[i];
                break;
            case DHCP_OPT_ROUTER:
                if (olen >= 4) for (int i = 0; i < 4; i++) router.bytes[i] = opt[i];
                break;
            case DHCP_OPT_DNS:
                for (uint8_t k = 0; k + 4 <= olen && dns_count < 3; k += 4) {
                    for (int i = 0; i < 4; i++) dns[dns_count].bytes[i] = opt[k + i];
                    dns_count++;
                }
                break;
            case DHCP_OPT_SERVER_ID:
                if (olen >= 4) for (int i = 0; i < 4; i++) server.bytes[i] = opt[i];
                break;
            case DHCP_OPT_LEASE_TIME:
                if (olen >= 4) {
                    lease = ((uint32_t)opt[0] << 24) | ((uint32_t)opt[1] << 16) |
                            ((uint32_t)opt[2] << 8) | opt[3];
                }
                break;
        }
        opt += olen;
    }

    ipv4_addr_t yiaddr;
    const uint8_t *y = (const uint8_t *)&dhcp->yiaddr;
    for (int i = 0; i < 4; i++) yiaddr.bytes[i] = y[i];

    if (msg_type == DHCP_OFFER && c->state == DHCP_SELECTING) {
        if (ip_is_zero(yiaddr) || ip_is_zero(server) || yiaddr.bytes[0] == 127 ||
            yiaddr.bytes[0] >= 224) {
            serial_puts("[DHCP] Ignoring OFFER with an unusable address or no server id\n");
            return;
        }
        c->offered = yiaddr;
        c->server = server;
        serial_puts("[DHCP] OFFER ");
        serial_ip(yiaddr);
        serial_puts(" from ");
        serial_ip(server);
        serial_puts("\n");
        c->state = DHCP_REQUESTING;
        c->tries = 0;
        c->retry_ms = DHCP_RETRY_FIRST_MS;
        dhcp_send(c, DHCP_REQUEST);
        dhcp_schedule_retry(c);
    } else if (msg_type == DHCP_ACK &&
               (c->state == DHCP_REQUESTING || c->state == DHCP_REBINDING)) {
        if (ip_is_zero(yiaddr)) return;
        if (ip_is_zero(subnet)) {
            /* No mask option: use the classful default */
            subnet.bytes[0] = 255;
            if (yiaddr.bytes[0] >= 128) subnet.bytes[1] = 255;
            if (yiaddr.bytes[0] >= 192) subnet.bytes[2] = 255;
        }

        network_set_ip(iface, yiaddr, subnet, router);
        iface->dns_count = dns_count;
        for (int i = 0; i < 3; i++) {
            iface->dns_servers[i] = (i < dns_count) ? dns[i] : (ipv4_addr_t){{0}};
        }
        iface->dns = dns_count ? dns[0] : (ipv4_addr_t){{0}};
        iface->ipv4_dhcp = 1;
        iface->lease_seconds = lease;
        iface->state = NET_STATE_CONNECTED;

        uint64_t now = network_now_ms();
        if (lease == 0xFFFFFFFFu || lease == 0) {
            c->t1_ms = 0;              /* infinite (or unspecified) lease */
            c->expiry_ms = 0;
        } else {
            uint32_t l = lease < DHCP_MIN_LEASE_S ? DHCP_MIN_LEASE_S : lease;
            c->t1_ms = now + (uint64_t)l * 500u;     /* T1 = lease / 2 */
            c->expiry_ms = now + (uint64_t)l * 1000u;
        }
        c->state = DHCP_BOUND;

        serial_puts("[DHCP] ACK: ");
        serial_puts(iface->name);
        serial_puts(" bound to ");
        serial_ip(yiaddr);
        serial_puts(", lease ");
        serial_dec(lease);
        serial_puts(" s, DNS ");
        if (dns_count == 0) serial_puts("none");
        for (int i = 0; i < dns_count; i++) {
            if (i) serial_puts(", ");
            serial_ip(dns[i]);
        }
        serial_puts("\n");

        /* Learn the gateway's MAC now so the first connection does not wait */
        if (!ip_is_zero(router)) arp_request(iface, router);
    } else if (msg_type == DHCP_NAK) {
        serial_puts("[DHCP] NAK from server: restarting discovery on ");
        serial_puts(iface->name);
        serial_puts("\n");
        dhcp_clear_address(iface);
        dhcp_restart(c);
    }
}

int dhcp_discover(net_interface_t *iface) {
    if (!iface || iface->type != NET_IF_ETHERNET) return -1;
    dhcp_client_t *c = dhcp_find(iface, 1);
    if (!c) {
        serial_puts("[DHCP] ERROR: no free DHCP client slot\n");
        return -1;
    }
    dhcp_restart(c);
    return 0;
}

static void dhcp_timers(void) {
    uint64_t now = network_now_ms();

    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        dhcp_client_t *c = &dhcp_clients[i];
        if (!c->iface || c->state == DHCP_IDLE) continue;
        if (!c->iface->link_up) continue;    /* resumes when the link returns */

        switch (c->state) {
            case DHCP_SELECTING:
                if (now >= c->next_send_ms) {
                    c->tries++;
                    dhcp_send(c, DHCP_DISCOVER);
                    dhcp_schedule_retry(c);
                    if (c->tries == 5) {
                        serial_puts("[DHCP] No DHCP server answered yet on ");
                        serial_puts(c->iface->name);
                        serial_puts("; still trying\n");
                    }
                }
                break;
            case DHCP_REQUESTING:
                if (now >= c->next_send_ms) {
                    if (++c->tries >= DHCP_REQUEST_TRIES) {
                        serial_puts("[DHCP] No ACK: restarting discovery\n");
                        dhcp_restart(c);
                    } else {
                        dhcp_send(c, DHCP_REQUEST);
                        dhcp_schedule_retry(c);
                    }
                }
                break;
            case DHCP_BOUND:
                if (c->t1_ms && now >= c->t1_ms) {
                    serial_puts("[DHCP] Lease half over: renewing\n");
                    c->state = DHCP_REBINDING;
                    c->xid = net_random32();
                    c->retry_ms = DHCP_RETRY_FIRST_MS * 8;
                    c->next_send_ms = now;
                }
                break;
            case DHCP_REBINDING:
                if (c->expiry_ms && now >= c->expiry_ms) {
                    serial_puts("[DHCP] Lease expired: address removed from ");
                    serial_puts(c->iface->name);
                    serial_puts("\n");
                    dhcp_clear_address(c->iface);
                    dhcp_restart(c);
                } else if (now >= c->next_send_ms) {
                    dhcp_send(c, DHCP_REQUEST);
                    dhcp_schedule_retry(c);
                }
                break;
            default:
                break;
        }
    }
}

int network_dhcp_request(net_interface_t *iface) {
    if (!iface) return -1;

    serial_puts("[NET] DHCP request on ");
    serial_puts(iface->name);
    serial_puts("\n");

    /* The service thread sends the DISCOVER and handles the replies */
    return dhcp_discover(iface);
}

int network_dhcp_release(net_interface_t *iface) {
    if (!iface) return -1;

    dhcp_client_t *c = dhcp_find(iface, 0);
    if (c) c->state = DHCP_IDLE;
    dhcp_clear_address(iface);
    serial_puts("[DHCP] Released address on ");
    serial_puts(iface->name);
    serial_puts("\n");

    return 0;
}

/* ============ Polling and Service Thread ============ */

int network_poll(void) {
    if (!net_initialized || poll_depth > 0) return 0;
    poll_depth++;

    int frames = 0;
    for (net_interface_t *iface = interfaces; iface; iface = iface->next) {
        if (iface->type != NET_IF_ETHERNET || iface->state == NET_STATE_DOWN) continue;
        while (frames < NET_POLL_BUDGET) {
            int len = network_iface_recv(iface, rx_frame, sizeof(rx_frame));
            if (len <= 0) break;
            frames++;
            tcpip_input(iface, rx_frame, (uint32_t)len);
        }
    }

    poll_depth--;
    return frames;
}

int network_in_input(void) {
    return poll_depth > 0;
}

int network_may_block(void) {
    if (poll_depth > 0) return 0;
    process_t *self = process_current();
    if (self && netd_pid > 0 && (int)self->pid == netd_pid) return 0;
    return 1;
}

void network_wait(uint32_t ms) {
    network_poll();
    process_sleep(ms ? ms : 1);
    network_poll();
}

static void link_check(void) {
    for (net_interface_t *iface = interfaces; iface; iface = iface->next) {
        if (iface->type != NET_IF_ETHERNET) continue;
        e1000_device_t *dev = (e1000_device_t *)iface->driver_data;
        if (!dev) continue;

        uint8_t up = (uint8_t)e1000_link_poll(dev);
        if (up == iface->link_up) continue;
        iface->link_up = up;

        dhcp_client_t *c = dhcp_find(iface, 0);
        if (!up) {
            serial_puts("[NET] ");
            serial_puts(iface->name);
            serial_puts(": cable/link lost\n");
            if (iface->state == NET_STATE_CONNECTED) iface->state = NET_STATE_UP;
        } else if (c && c->state != DHCP_IDLE) {
            /* The network may have changed while the link was down */
            serial_puts("[NET] ");
            serial_puts(iface->name);
            serial_puts(": link back, renewing DHCP\n");
            dhcp_clear_address(iface);
            dhcp_restart(c);
        }
    }
}

static void netd_main(void *arg) {
    (void)arg;
    serial_puts("[NETD] Network service running\n");

    network_init();
    tcpip_init();

    for (net_interface_t *iface = interfaces; iface; iface = iface->next) {
        if (iface->type == NET_IF_ETHERNET) {
            if (!iface->link_up) {
                serial_puts("[NETD] ");
                serial_puts(iface->name);
                serial_puts(": no link yet; DHCP starts when it comes up\n");
            }
            dhcp_discover(iface);
        }
    }

    uint64_t last_link_ms = 0;
    for (;;) {
        network_poll();

        uint64_t now = network_now_ms();
        if (now - last_link_ms >= 500) {
            link_check();
            last_link_ms = now;
        }
        dhcp_timers();
        tcpip_timers();

        process_sleep(NETD_PERIOD_MS);
    }
}

int network_service_start(void) {
    if (netd_pid > 0) return netd_pid;

    netd_pid = process_create_thread("netd", netd_main, NULL);
    if (netd_pid < 0) {
        serial_puts("[NET] ERROR: could not create the network service thread\n");
        return -1;
    }
    return netd_pid;
}

/* ============ net.if_list (syscall 133, ABI 1.4) ============ */

#define NET_IF_LIST_MAX_CAP     64u
#define NET_IF_LIST_MAX_ELEM    1024u

static void if_info_fill(net_interface_t *iface, uint32_t index,
                         net_interface_t *primary, nx_net_if_info_t *out) {
    uint8_t *z = (uint8_t *)out;
    for (uint32_t i = 0; i < sizeof(*out); i++) z[i] = 0;

    out->size = sizeof(*out);
    out->index = index;
    net_strcpy(out->name, iface->name, sizeof(out->name));
    out->kind = (uint8_t)iface->type;

    int is_loop = (iface->type == NET_IF_LOOPBACK);
    uint8_t link = is_loop ? 1 : iface->link_up;
    out->link_up = link;
    out->primary = (iface == primary) ? 1 : 0;

    switch (iface->state) {
        case NET_STATE_DOWN:      out->state = NX_NET_STATE_DISABLED; break;
        case NET_STATE_ERROR:     out->state = NX_NET_STATE_ERROR; break;
        case NET_STATE_CONNECTED: out->state = NX_NET_STATE_CONNECTED; break;
        default:
            if (is_loop) out->state = NX_NET_STATE_CONNECTED;
            else if (!link) out->state = NX_NET_STATE_NO_LINK;
            else out->state = NX_NET_STATE_CONFIGURING;
            break;
    }
    /* A lost link outranks a stale address */
    if (!is_loop && !link && out->state == NX_NET_STATE_CONNECTED) {
        out->state = NX_NET_STATE_NO_LINK;
    }

    for (int i = 0; i < 6; i++) out->mac[i] = iface->mac.bytes[i];
    out->ipv4_method = (is_loop || (!iface->ipv4_dhcp && !ip_is_zero(iface->ip)))
                       ? NX_NET_IPV4_MANUAL : NX_NET_IPV4_DHCP;

    if (iface->type == NET_IF_ETHERNET && iface->driver_data && link) {
        out->speed_mbps = ((e1000_device_t *)iface->driver_data)->speed_mbps;
    }

    for (int i = 0; i < 4; i++) {
        out->addr[i] = iface->ip.bytes[i];
        out->mask[i] = iface->netmask.bytes[i];
        out->gateway[i] = iface->gateway.bytes[i];
    }
    uint8_t n = iface->dns_count > NX_NET_DNS_MAX ? NX_NET_DNS_MAX : iface->dns_count;
    out->dns_count = n;
    for (uint8_t d = 0; d < n; d++) {
        for (int i = 0; i < 4; i++) out->dns[d][i] = iface->dns_servers[d].bytes[i];
    }
    out->lease_seconds = iface->lease_seconds;

    out->rx_bytes = iface->rx_bytes;
    out->tx_bytes = iface->tx_bytes;
    out->rx_packets = iface->rx_packets;
    out->tx_packets = iface->tx_packets;
    out->rx_errors = iface->rx_errors;
    out->tx_errors = iface->tx_errors;
    if (iface->type == NET_IF_ETHERNET && iface->driver_data) {
        out->rx_errors += ((e1000_device_t *)iface->driver_data)->rx_dropped;
        out->tx_errors += ((e1000_device_t *)iface->driver_data)->tx_ring_full;
    }
}

/*
 * int net_if_list(nx_net_if_info_t *buf, uint32_t cap, uint32_t elem_size)
 *
 * Copies up to `cap` entries, each `elem_size` bytes apart (the kernel
 * fills min(elem_size, sizeof) bytes of each), and returns the total
 * number of interfaces, which is larger than cap when buf was too small.
 * cap 0 with buf NULL just counts. Read-only; any process may call it.
 */
int64_t sys_net_if_list_impl(uint64_t ubuf, uint64_t cap, uint64_t elem_size,
                             uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;

    if (elem_size < NX_NET_IF_INFO_V1_SIZE || elem_size > NET_IF_LIST_MAX_ELEM) return -EINVAL;
    if (cap > NET_IF_LIST_MAX_CAP) return -EINVAL;
    if (cap > 0) {
        if (!ubuf || syscall_validate_ptr((void *)ubuf, (size_t)(cap * elem_size), 1) != 0) {
            return -EFAULT;
        }
    }

    net_interface_t *primary = network_default_iface();
    uint32_t total = 0;
    uint64_t copy = elem_size < sizeof(nx_net_if_info_t) ? elem_size : sizeof(nx_net_if_info_t);

    for (net_interface_t *iface = interfaces; iface && total < NX_NET_IF_MAX; iface = iface->next) {
        if (total < cap) {
            nx_net_if_info_t info;
            if_info_fill(iface, total, primary, &info);
            info.size = (uint32_t)copy;
            void *dst = (uint8_t *)(uintptr_t)ubuf + (uint64_t)total * elem_size;
            if (syscall_copy_to_user(dst, &info, (size_t)copy) != 0) return -EFAULT;
        }
        total++;
    }
    return (int64_t)total;
}

/* ============ Status ============ */

void network_print_status(void) {
    serial_puts("\n=== Network Interfaces ===\n");

    for (net_interface_t *iface = interfaces; iface; iface = iface->next) {
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
        serial_ip(iface->ip);
        serial_puts("\n");
    }

    serial_puts("==========================\n\n");
}
