/*
 * NeolyxOS TCP/IP Stack Implementation
 *
 * ARP, IPv4, ICMP echo, UDP and a TCP client, plus the socket layer
 * behind syscalls 40-47.
 *
 * Model:
 *   - Single-threaded in effect: the kernel is not preemptible, packets
 *     are processed by network_poll() (net/network.c) and syscalls run
 *     with interrupts masked, so no locks are needed. Code on the input
 *     path never waits for more input (network_in_input()).
 *   - TCP: active open (connect) with retransmission (go-back-N from the
 *     oldest unacknowledged byte), in-order receive, flow control from
 *     both windows, orderly close. Passive open (listen/accept) is not
 *     implemented: a SYN to any port gets RST.
 *   - Sockets belong to the process that created them; other processes
 *     get -EBADF. A process's sockets are released when it exits.
 *   - No IP fragmentation or reassembly: fragments are dropped, and sends
 *     larger than one frame return -EMSGSIZE.
 *
 * Copyright (c) 2025 KetiveeAI.
 */

#include "net/tcpip.h"
#include "net/network.h"
#include "mm/kheap.h"
#include "core/process.h"
#include "core/syscall.h"   /* errno values */

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Tunables ============ */

#define ARP_CACHE_SIZE          32
#define ARP_ENTRY_TTL_MS        (10u * 60u * 1000u)
#define ARP_ATTEMPTS            3
#define ARP_ATTEMPT_WAIT_MS     300u

#define IP_MAX_PAYLOAD          (ETH_MTU - IPV4_HEADER_LEN)     /* 1480 */
#define UDP_MAX_PAYLOAD         (IP_MAX_PAYLOAD - UDP_HEADER_LEN)

#define SOCK_MAX_TOTAL          64
#define SOCK_MAX_PER_PROCESS    16
#define UDP_RX_CAPACITY         8192u
#define TCP_RX_CAPACITY         32768u
#define TCP_TX_CAPACITY         16384u

#define TCP_MSS_LOCAL           (IP_MAX_PAYLOAD - TCP_HEADER_LEN)  /* 1460 */
#define TCP_MSS_DEFAULT         536
#define TCP_RTO_INITIAL_MS      1000u
#define TCP_RTO_MAX_MS          30000u
#define TCP_SYN_RETRIES         4           /* ~1+2+4+8 s, then -ETIMEDOUT */
#define TCP_DATA_RETRIES        8
#define TCP_CONNECT_TIMEOUT_MS  16000u
#define TCP_SEND_WAIT_MS        5000u
#define TCP_TIME_WAIT_MS        2000u       /* shortened 2*MSL for an end host */
#define TCP_FIN_WAIT_2_MS       30000u

#define EPHEMERAL_FIRST         49152u
#define EPHEMERAL_LAST          65535u

/* ============ State ============ */

static socket_t *sockets = NULL;
static int socket_count = 0;
static int next_socket_fd = 3;  /* 0,1,2 reserved for stdin/stdout/stderr */
static uint16_t next_ephemeral_port = EPHEMERAL_FIRST;
static uint16_t ip_ident = 1;

typedef struct {
    ipv4_addr_t ip;
    mac_addr_t mac;
    int valid;
    uint64_t expires_ms;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

/* One kernel-internal UDP exchange at a time (net_udp_query) */
static struct {
    int active;
    int done;
    uint16_t local_port;
    ipv4_addr_t peer;
    uint16_t peer_port;
    uint8_t *buf;
    uint32_t cap;
    uint32_t len;
} udp_query;

/* Drop counters, logged at most once per kind to keep the serial log usable */
static uint32_t drops_fragment = 0;
static uint32_t drops_checksum = 0;

/* ============ Helpers ============ */

static void *tcpip_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *tcpip_memmove(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
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

static int ip_is_zero(ipv4_addr_t a) {
    return (a.bytes[0] | a.bytes[1] | a.bytes[2] | a.bytes[3]) == 0;
}

static int ip_is_limited_broadcast(ipv4_addr_t a) {
    return (a.bytes[0] & a.bytes[1] & a.bytes[2] & a.bytes[3]) == 0xFF;
}

static int ip_same_subnet(net_interface_t *iface, ipv4_addr_t a) {
    for (int i = 0; i < 4; i++) {
        if ((a.bytes[i] & iface->netmask.bytes[i]) !=
            (iface->ip.bytes[i] & iface->netmask.bytes[i])) return 0;
    }
    return 1;
}

static int ip_is_subnet_broadcast(net_interface_t *iface, ipv4_addr_t a) {
    if (ip_is_zero(iface->netmask) || !ip_same_subnet(iface, a)) return 0;
    for (int i = 0; i < 4; i++) {
        if ((a.bytes[i] | iface->netmask.bytes[i]) != 0xFF) return 0;
    }
    return 1;
}

static uint16_t htons(uint16_t h) {
    return (uint16_t)(((h & 0xFF) << 8) | ((h >> 8) & 0xFF));
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

/* Sequence-space comparisons (RFC 793, modulo 2^32) */
static int seq_lt(uint32_t a, uint32_t b)  { return (int32_t)(a - b) < 0; }
static int seq_leq(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

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

static void serial_endpoint(ipv4_addr_t ip, uint16_t port) {
    serial_ip(ip);
    serial_putc(':');
    serial_dec(port);
}

static uint32_t current_pid(void) {
    process_t *self = process_current();
    return self ? self->pid : 0;
}

/* ============ Checksum ============ */

/*
 * One's-complement sum over `len` bytes, read as native 16-bit words.
 * Summing in native order and storing the folded complement natively
 * gives the correct network-order checksum on a little-endian CPU, as
 * long as every part of a checksum (pseudo-header included) is summed
 * the same way.
 */
static uint32_t csum_add(uint32_t sum, const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len > 1) {
        sum += (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        p += 2;
        len -= 2;
    }
    if (len == 1) sum += p[0];
    return sum;
}

static uint16_t csum_fold(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t tcpip_checksum(const void *data, uint32_t len) {
    return csum_fold(csum_add(0, data, len));
}

/* Checksum of a UDP or TCP segment with the IPv4 pseudo-header */
static uint16_t l4_checksum(ipv4_addr_t src, ipv4_addr_t dst, uint8_t proto,
                            const void *seg, uint32_t len) {
    uint8_t pseudo[12];
    for (int i = 0; i < 4; i++) {
        pseudo[i] = src.bytes[i];
        pseudo[4 + i] = dst.bytes[i];
    }
    pseudo[8] = 0;
    pseudo[9] = proto;
    pseudo[10] = (uint8_t)(len >> 8);
    pseudo[11] = (uint8_t)len;
    uint32_t sum = csum_add(0, pseudo, sizeof(pseudo));
    sum = csum_add(sum, seg, len);
    return csum_fold(sum);
}

/* ============ ARP ============ */

static int arp_cache_lookup(ipv4_addr_t ip, mac_addr_t *mac) {
    uint64_t now = network_now_ms();
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && ip_equal(arp_cache[i].ip, ip)) {
            if (now >= arp_cache[i].expires_ms) {
                arp_cache[i].valid = 0;
                return -1;
            }
            *mac = arp_cache[i].mac;
            return 0;
        }
    }
    return -1;
}

static void arp_cache_add(ipv4_addr_t ip, mac_addr_t mac) {
    if (ip_is_zero(ip) || (mac.bytes[0] & 0x01)) return;   /* no 0.0.0.0, no multicast MACs */

    uint64_t now = network_now_ms();
    int slot = -1;
    uint64_t oldest = ~0ULL;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && ip_equal(arp_cache[i].ip, ip)) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (!arp_cache[i].valid) { slot = i; break; }
            if (arp_cache[i].expires_ms < oldest) { oldest = arp_cache[i].expires_ms; slot = i; }
        }
    }
    arp_cache[slot].ip = ip;
    arp_cache[slot].mac = mac;
    arp_cache[slot].valid = 1;
    arp_cache[slot].expires_ms = now + ARP_ENTRY_TTL_MS;
}

int arp_request(net_interface_t *iface, ipv4_addr_t ip) {
    if (!iface) return -EINVAL;

    uint8_t pkt[ETH_HEADER_LEN + sizeof(arp_header_t)];
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

    return network_iface_send(iface, pkt, sizeof(pkt)) > 0 ? 0 : -EIO;
}

int arp_resolve(net_interface_t *iface, ipv4_addr_t ip, mac_addr_t *mac) {
    if (!iface || !mac) return -EINVAL;
    if (arp_cache_lookup(ip, mac) == 0) return 0;

    if (!network_may_block()) {
        /* Input path or the service thread: ask, but do not wait. The
         * caller drops this packet; TCP retransmits it. */
        arp_request(iface, ip);
        return -EAGAIN;
    }

    for (int attempt = 0; attempt < ARP_ATTEMPTS; attempt++) {
        arp_request(iface, ip);
        uint64_t deadline = network_now_ms() + ARP_ATTEMPT_WAIT_MS;
        while (network_now_ms() < deadline) {
            network_wait(2);
            if (arp_cache_lookup(ip, mac) == 0) return 0;
        }
    }

    serial_puts("[ARP] No reply from ");
    serial_ip(ip);
    serial_puts("\n");
    return -ETIMEDOUT;
}

void arp_input(net_interface_t *iface, void *data, uint32_t len) {
    if (len < sizeof(arp_header_t)) return;

    arp_header_t *arp = (arp_header_t *)data;
    if (ntohs(arp->hw_type) != 1 || ntohs(arp->proto_type) != ETH_TYPE_IPV4 ||
        arp->hw_len != 6 || arp->proto_len != 4) return;

    int for_us = !ip_is_zero(iface->ip) && ip_equal(arp->target_ip, iface->ip);

    /* Learn the sender if it is on our subnet (RFC 826 merge rule, limited
     * to our own network so strangers cannot fill the cache) */
    if (!ip_is_zero(iface->ip) && ip_same_subnet(iface, arp->sender_ip)) {
        arp_cache_add(arp->sender_ip, arp->sender_mac);
    }

    if (ntohs(arp->operation) == ARP_REQUEST && for_us) {
        uint8_t pkt[ETH_HEADER_LEN + sizeof(arp_header_t)];
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

        network_iface_send(iface, pkt, sizeof(pkt));
    }
}

/* ============ IP Layer ============ */

int ip_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
            const void *data, uint32_t len) {
    if (!iface || !data) return -EINVAL;
    if (len > IP_MAX_PAYLOAD) return -EMSGSIZE;

    mac_addr_t dst_mac;
    int broadcast = ip_is_limited_broadcast(dst) || ip_is_subnet_broadcast(iface, dst);

    if (broadcast) {
        for (int i = 0; i < 6; i++) dst_mac.bytes[i] = 0xFF;
    } else {
        if (ip_is_zero(iface->ip)) return -ENXIO;          /* not configured yet */
        ipv4_addr_t next_hop = dst;
        if (!ip_same_subnet(iface, dst)) {
            if (ip_is_zero(iface->gateway)) return -ENXIO; /* no route */
            next_hop = iface->gateway;
        }
        int r = arp_resolve(iface, next_hop, &dst_mac);
        if (r != 0) return r;
    }

    uint8_t frame[ETH_HEADER_LEN + IPV4_HEADER_LEN + IP_MAX_PAYLOAD];
    uint32_t total = ETH_HEADER_LEN + IPV4_HEADER_LEN + len;

    eth_frame_t *eth = (eth_frame_t *)frame;
    ipv4_header_t *ip = (ipv4_header_t *)(frame + ETH_HEADER_LEN);

    eth->dest = dst_mac;
    eth->src = iface->mac;
    eth->type = htons(ETH_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = htons((uint16_t)(IPV4_HEADER_LEN + len));
    ip->id = htons(ip_ident++);
    ip->flags_frag = htons(0x4000);    /* Don't Fragment: we never fragment */
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src = iface->ip;
    ip->dst = dst;
    ip->checksum = tcpip_checksum(ip, IPV4_HEADER_LEN);

    tcpip_memcpy(frame + ETH_HEADER_LEN + IPV4_HEADER_LEN, data, len);

    int ret = network_iface_send(iface, frame, total);
    return ret > 0 ? (int)len : -EIO;
}

void ip_input(net_interface_t *iface, void *data, uint32_t len) {
    if (len < IPV4_HEADER_LEN) return;

    ipv4_header_t *ip = (ipv4_header_t *)data;
    if ((ip->version_ihl >> 4) != 4) return;

    uint32_t ihl = (uint32_t)(ip->version_ihl & 0x0F) * 4;
    uint32_t total = ntohs(ip->total_length);
    if (ihl < IPV4_HEADER_LEN || total < ihl || total > len) return;

    if (tcpip_checksum(ip, ihl) != 0) {
        if (drops_checksum++ == 0) serial_puts("[IP] Dropping packets with bad header checksums\n");
        return;
    }

    /* More-fragments flag or a fragment offset: no reassembly */
    if (ntohs(ip->flags_frag) & 0x3FFF) {
        if (drops_fragment++ == 0) serial_puts("[IP] Dropping fragmented packets (no reassembly)\n");
        return;
    }

    /* Ours, a broadcast, or anything while unconfigured (DHCP replies) */
    if (!ip_is_zero(iface->ip) && !ip_equal(ip->dst, iface->ip) &&
        !ip_is_limited_broadcast(ip->dst) && !ip_is_subnet_broadcast(iface, ip->dst)) {
        return;
    }

    void *payload = (uint8_t *)data + ihl;
    uint32_t payload_len = total - ihl;

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

    for (int i = 0; i < 32; i++) {
        pkt[sizeof(icmp_header_t) + i] = (uint8_t)i;
    }

    icmp->checksum = tcpip_checksum(pkt, sizeof(pkt));

    serial_puts("[ICMP] Ping ");
    serial_ip(dst);
    serial_puts("\n");

    return ip_send(iface, dst, IP_PROTO_ICMP, pkt, sizeof(pkt));
}

void icmp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len) {
    if (len < sizeof(icmp_header_t) || len > IP_MAX_PAYLOAD) return;
    if (tcpip_checksum(data, len) != 0) return;

    icmp_header_t *icmp = (icmp_header_t *)data;

    if (icmp->type == ICMP_ECHO_REQUEST && icmp->code == 0) {
        /* Never answer pings sent to a broadcast address (smurf) */
        if (!ip_equal(ip->dst, iface->ip)) return;
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = tcpip_checksum(data, len);
        ip_send(iface, ip->src, IP_PROTO_ICMP, data, len);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        serial_puts("[ICMP] Reply from ");
        serial_ip(ip->src);
        serial_puts(" seq=");
        serial_dec(ntohs(icmp->sequence));
        serial_puts("\n");
    }
}

/* ============ Ports ============ */

static int port_in_use(uint16_t port, int proto) {
    if (port == 0) return 1;
    if (proto == IP_PROTO_UDP && udp_query.active && udp_query.local_port == port) return 1;
    for (socket_t *s = sockets; s; s = s->next) {
        if (s->protocol == proto && s->local_port == port) return 1;
    }
    return 0;
}

static uint16_t alloc_ephemeral_port(int proto) {
    uint32_t range = EPHEMERAL_LAST - EPHEMERAL_FIRST + 1;
    for (uint32_t i = 0; i < range; i++) {
        uint16_t p = next_ephemeral_port;
        next_ephemeral_port = (p == EPHEMERAL_LAST) ? EPHEMERAL_FIRST : (uint16_t)(p + 1);
        if (!port_in_use(p, proto)) return p;
    }
    return 0;
}

/* ============ UDP ============ */

static int udp_output(net_interface_t *iface, uint16_t sport, ipv4_addr_t dst,
                      uint16_t dport, const void *data, uint32_t len) {
    if (len > UDP_MAX_PAYLOAD) return -EMSGSIZE;

    uint8_t pkt[UDP_HEADER_LEN + UDP_MAX_PAYLOAD];
    uint32_t total = UDP_HEADER_LEN + len;

    udp_header_t *udp = (udp_header_t *)pkt;
    udp->src_port = htons(sport);
    udp->dst_port = htons(dport);
    udp->length = htons((uint16_t)total);
    udp->checksum = 0;
    tcpip_memcpy(pkt + UDP_HEADER_LEN, data, len);

    uint16_t c = l4_checksum(iface->ip, dst, IP_PROTO_UDP, pkt, total);
    udp->checksum = c ? c : 0xFFFF;     /* 0 means "no checksum" in UDP */

    int ret = ip_send(iface, dst, IP_PROTO_UDP, pkt, total);
    return ret < 0 ? ret : (int)len;
}

int udp_send(socket_t *sock, const void *data, uint32_t len) {
    if (!sock || sock->type != SOCK_DGRAM) return -EINVAL;
    if (ip_is_zero(sock->remote_ip) || sock->remote_port == 0) return -EINVAL;

    if (!sock->iface) sock->iface = network_default_iface();
    if (!sock->iface) return -ENXIO;
    if (sock->local_port == 0) {
        sock->local_port = alloc_ephemeral_port(IP_PROTO_UDP);
        if (sock->local_port == 0) return -EBUSY;
    }

    return udp_output(sock->iface, sock->local_port, sock->remote_ip,
                      sock->remote_port, data, len);
}

void udp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len) {
    if (len < UDP_HEADER_LEN) return;

    udp_header_t *udp = (udp_header_t *)data;
    uint32_t ulen = ntohs(udp->length);
    if (ulen < UDP_HEADER_LEN || ulen > len) return;
    if (udp->checksum != 0 &&
        l4_checksum(ip->src, ip->dst, IP_PROTO_UDP, data, ulen) != 0) {
        if (drops_checksum++ == 0) serial_puts("[UDP] Dropping datagrams with bad checksums\n");
        return;
    }

    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t src_port = ntohs(udp->src_port);
    uint8_t *payload = (uint8_t *)data + UDP_HEADER_LEN;
    uint32_t payload_len = ulen - UDP_HEADER_LEN;

    if (dst_port == PORT_DHCP_C && src_port == PORT_DHCP_S) {
        dhcp_input(iface, payload, payload_len);
        return;
    }

    if (udp_query.active && !udp_query.done && dst_port == udp_query.local_port &&
        src_port == udp_query.peer_port && ip_equal(ip->src, udp_query.peer)) {
        uint32_t n = payload_len < udp_query.cap ? payload_len : udp_query.cap;
        tcpip_memcpy(udp_query.buf, payload, n);
        udp_query.len = n;
        udp_query.done = 1;
        return;
    }

    for (socket_t *s = sockets; s; s = s->next) {
        if (s->protocol != IP_PROTO_UDP || s->local_port != dst_port || s->user_closed) continue;
        /* A connected socket only hears its peer */
        if (s->remote_port && (s->remote_port != src_port || !ip_equal(s->remote_ip, ip->src))) {
            continue;
        }
        if (!s->rx_buffer || payload_len > 0xFFFF) return;
        if (s->rx_len + 2 + payload_len > s->rx_capacity) return;    /* full: drop */
        s->rx_buffer[s->rx_len] = (uint8_t)(payload_len >> 8);
        s->rx_buffer[s->rx_len + 1] = (uint8_t)payload_len;
        tcpip_memcpy(s->rx_buffer + s->rx_len + 2, payload, payload_len);
        s->rx_len += 2 + payload_len;
        return;
    }
}

int net_udp_query(net_interface_t *iface, ipv4_addr_t dst, uint16_t dport,
                  const void *query, uint32_t qlen,
                  void *resp, uint32_t resp_cap, uint32_t timeout_ms) {
    if (!iface || !query || !resp || resp_cap == 0) return -EINVAL;
    if (network_in_input()) return -EAGAIN;
    if (udp_query.active) return -EBUSY;

    uint16_t port = alloc_ephemeral_port(IP_PROTO_UDP);
    if (port == 0) return -EBUSY;

    udp_query.active = 1;
    udp_query.done = 0;
    udp_query.local_port = port;
    udp_query.peer = dst;
    udp_query.peer_port = dport;
    udp_query.buf = (uint8_t *)resp;
    udp_query.cap = resp_cap;
    udp_query.len = 0;

    int ret = udp_output(iface, port, dst, dport, query, qlen);
    if (ret >= 0) {
        uint64_t deadline = network_now_ms() + timeout_ms;
        while (!udp_query.done && network_now_ms() < deadline) {
            network_wait(2);
        }
        ret = udp_query.done ? (int)udp_query.len : 0;
    }

    udp_query.active = 0;
    udp_query.buf = NULL;
    return ret;
}

/* ============ TCP ============ */

static uint32_t tcp_rx_window(socket_t *s) {
    uint32_t free_bytes = s->rx_capacity - s->rx_len;
    return free_bytes > 0xFFFF ? 0xFFFF : free_bytes;
}

static uint16_t tcp_mss(socket_t *s) {
    uint16_t m = s->peer_mss ? s->peer_mss : TCP_MSS_DEFAULT;
    return m > TCP_MSS_LOCAL ? TCP_MSS_LOCAL : m;
}

/* Send one segment. `seq` is its first sequence number. */
static int tcp_output(socket_t *s, uint8_t flags, uint32_t seq,
                      const uint8_t *data, uint32_t len) {
    if (!s->iface) return -ENXIO;

    uint8_t seg[TCP_HEADER_LEN + 4 + TCP_MSS_LOCAL];
    uint32_t hlen = TCP_HEADER_LEN + ((flags & TCP_SYN) ? 4 : 0);
    if (len > TCP_MSS_LOCAL) return -EMSGSIZE;

    tcp_header_t *tcp = (tcp_header_t *)seg;
    tcp->src_port = htons(s->local_port);
    tcp->dst_port = htons(s->remote_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = (flags & TCP_ACK) ? htonl(s->ack_num) : 0;
    tcp->data_offset = (uint8_t)((hlen / 4) << 4);
    tcp->flags = flags;
    tcp->window = htons((uint16_t)tcp_rx_window(s));
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (flags & TCP_SYN) {
        seg[20] = 2;    /* MSS option */
        seg[21] = 4;
        seg[22] = (uint8_t)(TCP_MSS_LOCAL >> 8);
        seg[23] = (uint8_t)TCP_MSS_LOCAL;
    }
    if (len) tcpip_memcpy(seg + hlen, data, len);

    tcp->checksum = l4_checksum(s->iface->ip, s->remote_ip, IP_PROTO_TCP, seg, hlen + len);

    return ip_send(s->iface, s->remote_ip, IP_PROTO_TCP, seg, hlen + len);
}

/* RST for a segment that matches no connection (RFC 793 "Reset Generation") */
static void tcp_send_reset(net_interface_t *iface, ipv4_header_t *ip, tcp_header_t *in,
                           uint32_t seg_len) {
    if (in->flags & TCP_RST) return;

    uint8_t seg[TCP_HEADER_LEN];
    tcp_header_t *tcp = (tcp_header_t *)seg;
    tcp->src_port = in->dst_port;
    tcp->dst_port = in->src_port;
    if (in->flags & TCP_ACK) {
        tcp->seq_num = in->ack_num;
        tcp->ack_num = 0;
        tcp->flags = TCP_RST;
    } else {
        tcp->seq_num = 0;
        uint32_t ack = ntohl(in->seq_num) + seg_len +
                       ((in->flags & TCP_SYN) ? 1 : 0) + ((in->flags & TCP_FIN) ? 1 : 0);
        tcp->ack_num = htonl(ack);
        tcp->flags = TCP_RST | TCP_ACK;
    }
    tcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
    tcp->window = 0;
    tcp->checksum = 0;
    tcp->urgent = 0;
    tcp->checksum = l4_checksum(iface->ip, ip->src, IP_PROTO_TCP, seg, TCP_HEADER_LEN);
    ip_send(iface, ip->src, IP_PROTO_TCP, seg, TCP_HEADER_LEN);
}

static void tcp_arm_timer(socket_t *s) {
    if (s->rto_ms == 0) s->rto_ms = TCP_RTO_INITIAL_MS;
    s->rto_deadline_ms = network_now_ms() + s->rto_ms;
}

/* Send whatever the windows allow, then a FIN if close() asked for one */
static void tcp_push(socket_t *s) {
    if (s->tcp_state != TCP_ESTABLISHED && s->tcp_state != TCP_CLOSE_WAIT &&
        s->tcp_state != TCP_FIN_WAIT_1 && s->tcp_state != TCP_LAST_ACK &&
        s->tcp_state != TCP_CLOSING) return;

    uint16_t mss = tcp_mss(s);
    uint32_t window = s->snd_wnd;
    uint32_t cwnd = 8u * mss;                  /* fixed cap on data in flight */
    if (window > cwnd) window = cwnd;

    for (;;) {
        uint32_t in_flight = s->seq_num - s->snd_una;
        if (s->fin_sent) break;
        uint32_t sent_data = in_flight;        /* no FIN in flight yet */
        if (sent_data >= s->tx_len) break;     /* everything sent */
        uint32_t allowed = window;
        if (allowed == 0 && in_flight == 0) allowed = 1;   /* zero-window probe */
        if (in_flight >= allowed) break;

        uint32_t n = s->tx_len - sent_data;
        if (n > mss) n = mss;
        if (n > allowed - in_flight) n = allowed - in_flight;

        int r = tcp_output(s, TCP_ACK | TCP_PSH, s->seq_num, s->tx_buffer + sent_data, n);
        if (r < 0 && r != -EAGAIN) break;
        /* -EAGAIN: next hop not resolved yet; count it as sent and let
         * the retransmission timer resend it. */
        s->seq_num += n;
        if (s->rto_deadline_ms == 0) tcp_arm_timer(s);
        if (r < 0) break;
    }

    if (s->fin_pending && !s->fin_sent && (s->seq_num - s->snd_una) == s->tx_len) {
        tcp_output(s, TCP_FIN | TCP_ACK, s->seq_num, NULL, 0);
        s->seq_num++;
        s->fin_sent = 1;
        if (s->rto_deadline_ms == 0) tcp_arm_timer(s);
    }
}

static void tcp_set_closed(socket_t *s, const char *why) {
    if (why) {
        serial_puts("[TCP] ");
        serial_endpoint(s->remote_ip, s->remote_port);
        serial_puts(": ");
        serial_puts(why);
        serial_puts("\n");
    }
    s->tcp_state = TCP_CLOSED;
    s->rto_deadline_ms = 0;
    s->connected = 0;
}

static socket_t *tcp_find(ipv4_addr_t src, uint16_t sport, uint16_t dport) {
    for (socket_t *s = sockets; s; s = s->next) {
        if (s->protocol != IP_PROTO_TCP || s->tcp_state == TCP_CLOSED ||
            s->tcp_state == TCP_LISTEN) continue;
        if (s->local_port == dport && s->remote_port == sport && ip_equal(s->remote_ip, src)) {
            return s;
        }
    }
    return NULL;
}

int tcp_send(socket_t *sock, const void *data, uint32_t len) {
    if (!sock || sock->type != SOCK_STREAM) return -EINVAL;
    if (sock->rst_seen) return -EPIPE;
    if (sock->tcp_state != TCP_ESTABLISHED && sock->tcp_state != TCP_CLOSE_WAIT) {
        return sock->tcp_state == TCP_SYN_SENT ? -EAGAIN : -EPIPE;
    }
    if (len == 0) return 0;

    /* Wait (bounded) for room in the send buffer */
    uint64_t deadline = network_now_ms() + TCP_SEND_WAIT_MS;
    while (sock->tx_len >= sock->tx_capacity) {
        if (network_now_ms() >= deadline) return -EAGAIN;
        network_wait(2);
        if (sock->rst_seen || (sock->tcp_state != TCP_ESTABLISHED &&
                               sock->tcp_state != TCP_CLOSE_WAIT)) return -EPIPE;
    }

    uint32_t room = sock->tx_capacity - sock->tx_len;
    uint32_t n = len < room ? len : room;
    tcpip_memcpy(sock->tx_buffer + sock->tx_len, data, n);
    sock->tx_len += n;

    tcp_push(sock);
    return (int)n;
}

void tcp_input(net_interface_t *iface, ipv4_header_t *ip, void *data, uint32_t len) {
    if (len < TCP_HEADER_LEN || len > IP_MAX_PAYLOAD) return;

    tcp_header_t *tcp = (tcp_header_t *)data;
    uint32_t hlen = ((uint32_t)(tcp->data_offset >> 4) & 0xF) * 4;
    if (hlen < TCP_HEADER_LEN || hlen > len) return;

    if (l4_checksum(ip->src, ip->dst, IP_PROTO_TCP, data, len) != 0) {
        if (drops_checksum++ == 0) serial_puts("[TCP] Dropping segments with bad checksums\n");
        return;
    }

    uint16_t dst_port = ntohs(tcp->dst_port);
    uint16_t src_port = ntohs(tcp->src_port);
    uint8_t flags = tcp->flags;
    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t *payload = (uint8_t *)data + hlen;
    uint32_t payload_len = len - hlen;

    socket_t *s = tcp_find(ip->src, src_port, dst_port);
    if (!s) {
        tcp_send_reset(iface, ip, tcp, payload_len);
        return;
    }

    /* ---- SYN-SENT ---- */
    if (s->tcp_state == TCP_SYN_SENT) {
        if ((flags & TCP_ACK) && ack != s->seq_num) {
            if (!(flags & TCP_RST)) tcp_send_reset(iface, ip, tcp, payload_len);
            return;
        }
        if (flags & TCP_RST) {
            if (flags & TCP_ACK) {
                s->rst_seen = 1;
                tcp_set_closed(s, "connection refused");
            }
            return;
        }
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            /* MSS option */
            uint8_t *o = (uint8_t *)data + TCP_HEADER_LEN;
            uint8_t *oend = (uint8_t *)data + hlen;
            while (o < oend) {
                if (o[0] == 0) break;
                if (o[0] == 1) { o++; continue; }
                if (o + 1 >= oend || o[1] < 2 || o + o[1] > oend) break;
                if (o[0] == 2 && o[1] == 4) {
                    uint16_t m = (uint16_t)((o[2] << 8) | o[3]);
                    if (m >= 64) s->peer_mss = m;
                }
                o += o[1];
            }
            s->ack_num = seq + 1;
            s->snd_una = ack;
            s->snd_wnd = ntohs(tcp->window);
            s->tcp_state = TCP_ESTABLISHED;
            s->rto_deadline_ms = 0;
            s->rto_ms = TCP_RTO_INITIAL_MS;
            s->retries = 0;
            tcp_output(s, TCP_ACK, s->seq_num, NULL, 0);
            serial_puts("[TCP] Connected to ");
            serial_endpoint(s->remote_ip, s->remote_port);
            serial_puts("\n");
        }
        return;
    }

    /* ---- Synchronized states ---- */
    uint32_t rcv_wnd = tcp_rx_window(s);
    int acceptable = (seq == s->ack_num) ||
                     (seq_leq(s->ack_num, seq) && seq_lt(seq, s->ack_num + (rcv_wnd ? rcv_wnd : 1))) ||
                     (payload_len && seq_lt(seq, s->ack_num) && seq_lt(s->ack_num, seq + payload_len));

    if (flags & TCP_RST) {
        if (seq_leq(s->ack_num, seq) && seq_lt(seq, s->ack_num + (rcv_wnd ? rcv_wnd : 1))) {
            s->rst_seen = 1;
            tcp_set_closed(s, "connection reset by peer");
        }
        return;
    }

    if (!acceptable) {
        /* Old duplicate or beyond the window: re-ACK what we expect */
        tcp_output(s, TCP_ACK, s->seq_num, NULL, 0);
        return;
    }

    if (flags & TCP_SYN) {
        /* SYN inside the connection: peer restarted; treat as reset */
        s->rst_seen = 1;
        tcp_set_closed(s, "unexpected SYN, connection aborted");
        return;
    }

    if (!(flags & TCP_ACK)) return;

    /* ACK processing */
    if (seq_lt(s->snd_una, ack) && seq_leq(ack, s->seq_num)) {
        uint32_t acked = ack - s->snd_una;
        uint32_t data_acked = acked > s->tx_len ? s->tx_len : acked;
        if (data_acked) {
            tcpip_memmove(s->tx_buffer, s->tx_buffer + data_acked, s->tx_len - data_acked);
            s->tx_len -= data_acked;
        }
        int fin_acked = s->fin_sent && ack == s->seq_num;
        s->snd_una = ack;
        s->retries = 0;
        s->rto_ms = TCP_RTO_INITIAL_MS;
        if (s->snd_una == s->seq_num) s->rto_deadline_ms = 0;
        else tcp_arm_timer(s);

        if (fin_acked) {
            if (s->tcp_state == TCP_FIN_WAIT_1) {
                s->tcp_state = TCP_FIN_WAIT_2;
                s->state_deadline_ms = network_now_ms() + TCP_FIN_WAIT_2_MS;
            } else if (s->tcp_state == TCP_CLOSING) {
                s->tcp_state = TCP_TIME_WAIT;
                s->state_deadline_ms = network_now_ms() + TCP_TIME_WAIT_MS;
            } else if (s->tcp_state == TCP_LAST_ACK) {
                tcp_set_closed(s, NULL);
                return;
            }
        }
    }
    if (seq_leq(s->snd_una, ack)) s->snd_wnd = ntohs(tcp->window);

    /* Data */
    int need_ack = 0;
    if (payload_len > 0) {
        need_ack = 1;
        if (s->tcp_state == TCP_ESTABLISHED || s->tcp_state == TCP_FIN_WAIT_1 ||
            s->tcp_state == TCP_FIN_WAIT_2) {
            uint32_t skip = seq_lt(seq, s->ack_num) ? s->ack_num - seq : 0;
            if (skip < payload_len && seq_leq(seq, s->ack_num)) {
                uint32_t n = payload_len - skip;
                uint32_t room = s->rx_capacity - s->rx_len;
                if (n > room) n = room;     /* the rest is retransmitted later */
                tcpip_memcpy(s->rx_buffer + s->rx_len, payload + skip, n);
                s->rx_len += n;
                s->ack_num += n;
                if (n < payload_len - skip) flags &= (uint8_t)~TCP_FIN;  /* FIN not reached */
            } else {
                flags &= (uint8_t)~TCP_FIN;     /* out of order: wait for the gap */
            }
        }
    }

    /* FIN: only once every byte before it has arrived */
    if ((flags & TCP_FIN) && seq + payload_len == s->ack_num) {
        s->ack_num++;
        s->peer_fin = 1;
        need_ack = 1;
        switch (s->tcp_state) {
            case TCP_ESTABLISHED:
                s->tcp_state = TCP_CLOSE_WAIT;
                break;
            case TCP_FIN_WAIT_1:
                s->tcp_state = TCP_CLOSING;
                break;
            case TCP_FIN_WAIT_2:
                s->tcp_state = TCP_TIME_WAIT;
                s->state_deadline_ms = network_now_ms() + TCP_TIME_WAIT_MS;
                break;
            default:
                break;
        }
    }

    if (need_ack) tcp_output(s, TCP_ACK, s->seq_num, NULL, 0);

    /* The ACK may have opened the window */
    tcp_push(s);
}

static void tcp_retransmit(socket_t *s) {
    s->retries++;
    int limit = (s->tcp_state == TCP_SYN_SENT) ? TCP_SYN_RETRIES : TCP_DATA_RETRIES;
    if (s->retries > limit) {
        s->rst_seen = 1;
        tcp_set_closed(s, "no response, connection timed out");
        return;
    }

    s->rto_ms = s->rto_ms * 2 > TCP_RTO_MAX_MS ? TCP_RTO_MAX_MS : s->rto_ms * 2;
    tcp_arm_timer(s);

    if (s->tcp_state == TCP_SYN_SENT) {
        tcp_output(s, TCP_SYN, s->snd_una, NULL, 0);
        return;
    }

    /* Go back to the oldest unacknowledged byte and resend from there */
    s->seq_num = s->snd_una;
    s->fin_sent = 0;
    s->rto_deadline_ms = 0;
    tcp_push(s);
    if (s->rto_deadline_ms == 0) tcp_arm_timer(s);
}

/* ============ Socket Management ============ */

static socket_t *socket_find(int fd) {
    uint32_t pid = current_pid();
    for (socket_t *s = sockets; s; s = s->next) {
        if (s->fd == fd && !s->user_closed) {
            return (s->owner_pid == pid) ? s : NULL;
        }
    }
    return NULL;
}

static void socket_free(socket_t *sock) {
    if (sockets == sock) {
        sockets = sock->next;
    } else {
        socket_t *prev = sockets;
        while (prev && prev->next != sock) prev = prev->next;
        if (prev) prev->next = sock->next;
    }
    if (sock->rx_buffer) kfree(sock->rx_buffer);
    if (sock->tx_buffer) kfree(sock->tx_buffer);
    kfree(sock);
    socket_count--;
}

int socket_create(socket_type_t type, int protocol) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -EINVAL;
    if (protocol != 0 &&
        !(type == SOCK_STREAM && protocol == IP_PROTO_TCP) &&
        !(type == SOCK_DGRAM && protocol == IP_PROTO_UDP)) return -EINVAL;

    uint32_t pid = current_pid();
    int mine = 0;
    for (socket_t *s = sockets; s; s = s->next) {
        if (s->owner_pid == pid && !s->user_closed) mine++;
    }
    if (mine >= SOCK_MAX_PER_PROCESS) return -EMFILE;
    if (socket_count >= SOCK_MAX_TOTAL) return -ENFILE;

    socket_t *sock = (socket_t *)kzalloc(sizeof(socket_t));
    if (!sock) return -ENOMEM;

    sock->type = type;
    sock->protocol = (type == SOCK_STREAM) ? IP_PROTO_TCP : IP_PROTO_UDP;
    sock->tcp_state = TCP_CLOSED;
    sock->owner_pid = pid;
    sock->rx_capacity = (type == SOCK_STREAM) ? TCP_RX_CAPACITY : UDP_RX_CAPACITY;
    sock->rx_buffer = (uint8_t *)kmalloc(sock->rx_capacity);
    if (type == SOCK_STREAM) {
        sock->tx_capacity = TCP_TX_CAPACITY;
        sock->tx_buffer = (uint8_t *)kmalloc(sock->tx_capacity);
    }
    if (!sock->rx_buffer || (type == SOCK_STREAM && !sock->tx_buffer)) {
        if (sock->rx_buffer) kfree(sock->rx_buffer);
        if (sock->tx_buffer) kfree(sock->tx_buffer);
        kfree(sock);
        return -ENOMEM;
    }

    /* Descriptor numbers are never reused while the counter lasts */
    if (next_socket_fd >= 0x7FFFFFF0) next_socket_fd = 3;
    sock->fd = next_socket_fd++;

    sock->next = sockets;
    sockets = sock;
    socket_count++;

    return sock->fd;
}

int socket_bind(int fd, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    if (!addr || addr->family != AF_INET) return -EINVAL;
    if (sock->local_port != 0) return -EINVAL;

    uint16_t port = ntohs(addr->port);
    if (port == 0) {
        port = alloc_ephemeral_port(sock->protocol);
        if (port == 0) return -EBUSY;
    } else if (port_in_use(port, sock->protocol)) {
        return -EBUSY;
    }

    sock->local_ip = addr->addr;
    sock->local_port = port;
    return 0;
}

int socket_listen(int fd, int backlog) {
    (void)backlog;
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    if (sock->type != SOCK_STREAM) return -EINVAL;

    /* Passive open is not implemented; refuse rather than pretend */
    return -ENOSYS;
}

int socket_accept(int fd, sockaddr_in_t *addr) {
    (void)addr;
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    return -ENOSYS;
}

int socket_connect(int fd, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    if (!addr || addr->family != AF_INET) return -EINVAL;

    ipv4_addr_t rip = addr->addr;
    uint16_t rport = ntohs(addr->port);
    if (ip_is_zero(rip) || rport == 0) return -EINVAL;
    if (rip.bytes[0] >= 224 && sock->type == SOCK_STREAM) return -EINVAL;  /* multicast/broadcast */

    net_interface_t *iface = network_default_iface();
    if (!iface) return -ENXIO;      /* no configured interface (yet) */

    if (sock->type == SOCK_DGRAM) {
        sock->iface = iface;
        sock->remote_ip = rip;
        sock->remote_port = rport;
        if (sock->local_port == 0) {
            sock->local_port = alloc_ephemeral_port(IP_PROTO_UDP);
            if (sock->local_port == 0) return -EBUSY;
        }
        sock->connected = 1;
        return 0;
    }

    if (sock->tcp_state != TCP_CLOSED || sock->connected) return -EINVAL;
    if (sock->local_port == 0) {
        sock->local_port = alloc_ephemeral_port(IP_PROTO_TCP);
        if (sock->local_port == 0) return -EBUSY;
    }

    sock->iface = iface;
    sock->remote_ip = rip;
    sock->remote_port = rport;
    uint32_t iss = net_random32();
    sock->snd_una = iss;
    sock->seq_num = iss + 1;
    sock->tcp_state = TCP_SYN_SENT;
    sock->rst_seen = 0;
    sock->retries = 0;
    sock->rto_ms = TCP_RTO_INITIAL_MS;
    sock->connected = 1;

    int r = tcp_output(sock, TCP_SYN, iss, NULL, 0);
    if (r < 0 && r != -EAGAIN) {
        tcp_set_closed(sock, NULL);
        return r;
    }
    tcp_arm_timer(sock);

    uint64_t deadline = network_now_ms() + TCP_CONNECT_TIMEOUT_MS;
    while (sock->tcp_state == TCP_SYN_SENT && network_now_ms() < deadline) {
        network_wait(2);
    }

    if (sock->tcp_state == TCP_ESTABLISHED) return 0;

    int was_refused = sock->rst_seen && sock->retries <= TCP_SYN_RETRIES;
    if (sock->tcp_state == TCP_SYN_SENT) tcp_set_closed(sock, "connect timed out");
    sock->connected = 0;
    return was_refused ? -EIO : -ETIMEDOUT;
}

int socket_send(int fd, const void *data, uint32_t len) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    if (!data && len) return -EFAULT;

    if (sock->type == SOCK_STREAM) {
        return tcp_send(sock, data, len);
    }
    return udp_send(sock, data, len);
}

int socket_recv(int fd, void *buf, uint32_t len) {
    socket_t *sock = socket_find(fd);
    if (!sock || !sock->rx_buffer) return -EBADF;
    if (!buf || len == 0) return -EINVAL;

    /* Pick up anything that arrived since the service thread last ran */
    network_poll();

    if (sock->type == SOCK_DGRAM) {
        if (sock->rx_len < 2) return -EAGAIN;
        uint32_t dlen = ((uint32_t)sock->rx_buffer[0] << 8) | sock->rx_buffer[1];
        uint32_t n = dlen < len ? dlen : len;      /* excess is discarded */
        tcpip_memcpy(buf, sock->rx_buffer + 2, n);
        uint32_t used = 2 + dlen;
        sock->rx_len -= used;
        tcpip_memmove(sock->rx_buffer, sock->rx_buffer + used, sock->rx_len);
        return (int)n;
    }

    if (sock->rx_len == 0) {
        if (sock->rst_seen) return -EIO;
        if (sock->peer_fin) return 0;                       /* end of stream */
        if (sock->tcp_state != TCP_ESTABLISHED && sock->tcp_state != TCP_FIN_WAIT_1 &&
            sock->tcp_state != TCP_FIN_WAIT_2) return -EPIPE;
        return -EAGAIN;
    }

    uint32_t window_before = tcp_rx_window(sock);
    uint32_t to_copy = len < sock->rx_len ? len : sock->rx_len;
    tcpip_memcpy(buf, sock->rx_buffer, to_copy);
    sock->rx_len -= to_copy;
    tcpip_memmove(sock->rx_buffer, sock->rx_buffer + to_copy, sock->rx_len);

    /* Window update when the reader frees a meaningful amount of space */
    if (window_before < tcp_mss(sock) && tcp_rx_window(sock) >= tcp_mss(sock) &&
        sock->tcp_state != TCP_CLOSED) {
        tcp_output(sock, TCP_ACK, sock->seq_num, NULL, 0);
    }

    return (int)to_copy;
}

int socket_sendto(int fd, const void *data, uint32_t len, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    if (sock->type != SOCK_DGRAM || !addr || addr->family != AF_INET) return -EINVAL;

    net_interface_t *iface = sock->iface ? sock->iface : network_default_iface();
    if (!iface) return -ENXIO;
    if (sock->local_port == 0) {
        sock->local_port = alloc_ephemeral_port(IP_PROTO_UDP);
        if (sock->local_port == 0) return -EBUSY;
    }
    return udp_output(iface, sock->local_port, addr->addr, ntohs(addr->port), data, len);
}

int socket_recvfrom(int fd, void *buf, uint32_t len, sockaddr_in_t *addr) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;

    int n = socket_recv(fd, buf, len);
    if (n >= 0 && addr) {
        addr->family = AF_INET;
        addr->port = htons(sock->remote_port);
        addr->addr = sock->remote_ip;
    }
    return n;
}

/* Release the descriptor; a TCP connection finishes closing in the background */
static void socket_release(socket_t *sock) {
    sock->user_closed = 1;

    if (sock->type == SOCK_STREAM) {
        switch (sock->tcp_state) {
            case TCP_ESTABLISHED:
                sock->fin_pending = 1;
                sock->tcp_state = TCP_FIN_WAIT_1;
                tcp_push(sock);
                return;
            case TCP_CLOSE_WAIT:
                sock->fin_pending = 1;
                sock->tcp_state = TCP_LAST_ACK;
                tcp_push(sock);
                return;
            case TCP_FIN_WAIT_1:
            case TCP_FIN_WAIT_2:
            case TCP_CLOSING:
            case TCP_LAST_ACK:
            case TCP_TIME_WAIT:
                return;     /* already closing */
            default:
                break;
        }
    }
    socket_free(sock);
}

int socket_close(int fd) {
    socket_t *sock = socket_find(fd);
    if (!sock) return -EBADF;
    socket_release(sock);
    return 0;
}

void network_process_exit(uint32_t pid) {
    socket_t *s = sockets;
    while (s) {
        socket_t *next = s->next;
        if (s->owner_pid == pid && !s->user_closed) socket_release(s);
        s = next;
    }
}

/* ============ Timers ============ */

void tcpip_timers(void) {
    uint64_t now = network_now_ms();

    socket_t *s = sockets;
    while (s) {
        socket_t *next = s->next;

        if (s->protocol == IP_PROTO_TCP) {
            if (s->rto_deadline_ms && now >= s->rto_deadline_ms &&
                s->tcp_state != TCP_CLOSED && s->tcp_state != TCP_TIME_WAIT) {
                tcp_retransmit(s);
            }
            if ((s->tcp_state == TCP_TIME_WAIT || s->tcp_state == TCP_FIN_WAIT_2) &&
                s->state_deadline_ms && now >= s->state_deadline_ms) {
                tcp_set_closed(s, NULL);
            }
            if (s->user_closed && s->tcp_state == TCP_CLOSED) {
                socket_free(s);
            }
        }
        s = next;
    }
}

/* ============ Main Input ============ */

void tcpip_input(net_interface_t *iface, void *data, uint32_t len) {
    if (!iface || !data || len < ETH_HEADER_LEN) return;

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

/* Firewall is initialized by boot.c boot sequence */

void tcpip_init(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    serial_puts("[TCP/IP] Initializing protocol stack...\n");

    tcpip_memset(arp_cache, 0, sizeof(arp_cache));
    tcpip_memset(&udp_query, 0, sizeof(udp_query));
    sockets = NULL;
    socket_count = 0;
    next_ephemeral_port = (uint16_t)(EPHEMERAL_FIRST +
                                     net_random32() % (EPHEMERAL_LAST - EPHEMERAL_FIRST));

    serial_puts("[TCP/IP] Ready\n");
}
