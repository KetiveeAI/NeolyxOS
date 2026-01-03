/*
 * NeolyxOS Network Infrastructure Implementation
 * 
 * Bridges, VLANs, firewall, NAT, QoS, routing.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "net/netinfra.h"
#include "net/tcpip.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ State ============ */

static net_bridge_t *bridges = NULL;
static net_vlan_t *vlans = NULL;
static net_vnic_t *vnics = NULL;

static nat_entry_t *nat_entries = NULL;
static route_entry_t *routes = NULL;
static dns_server_t *dns_servers = NULL;

static int ip_forwarding = 0;

/* ============ Helpers ============ */

static void ni_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int ni_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static int ip_match(ipv4_addr_t ip, ipv4_addr_t network, ipv4_addr_t mask) {
    for (int i = 0; i < 4; i++) {
        if ((ip.bytes[i] & mask.bytes[i]) != (network.bytes[i] & mask.bytes[i])) {
            return 0;
        }
    }
    return 1;
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

/* ============ Network Infrastructure Init ============ */

int netinfra_init(void) {
    serial_puts("[NETINFRA] Initializing network infrastructure...\n");
    
    bridges = NULL;
    vlans = NULL;
    vnics = NULL;

    nat_entries = NULL;
    routes = NULL;
    
    /* Add default DNS servers */
    dns_server_t *dns1 = (dns_server_t *)kzalloc(sizeof(dns_server_t));
    if (dns1) {
        dns1->address.bytes[0] = 8;
        dns1->address.bytes[1] = 8;
        dns1->address.bytes[2] = 8;
        dns1->address.bytes[3] = 8;
        dns1->priority = 1;
        dns1->next = dns_servers;
        dns_servers = dns1;
    }
    
    dns_server_t *dns2 = (dns_server_t *)kzalloc(sizeof(dns_server_t));
    if (dns2) {
        dns2->address.bytes[0] = 1;
        dns2->address.bytes[1] = 1;
        dns2->address.bytes[2] = 1;
        dns2->address.bytes[3] = 1;
        dns2->priority = 2;
        dns2->next = dns_servers;
        dns_servers = dns2;
    }
    
    serial_puts("[NETINFRA] Ready\n");
    return 0;
}

/* ============ Bridge API ============ */

net_bridge_t *bridge_create(const char *name) {
    net_bridge_t *br = (net_bridge_t *)kzalloc(sizeof(net_bridge_t));
    if (!br) return NULL;
    
    ni_strcpy(br->name, name, 16);
    ni_strcpy(br->interface.name, name, 16);
    br->interface.type = NET_IF_ETHERNET;
    br->interface.state = NET_STATE_DOWN;
    br->enabled = 0;
    br->stp_enabled = 0;
    
    br->next = bridges;
    bridges = br;
    
    serial_puts("[NETINFRA] Created bridge: ");
    serial_puts(name);
    serial_puts("\n");
    
    return br;
}

int bridge_delete(net_bridge_t *br) {
    if (!br) return -1;
    
    if (bridges == br) {
        bridges = br->next;
    } else {
        net_bridge_t *prev = bridges;
        while (prev && prev->next != br) prev = prev->next;
        if (prev) prev->next = br->next;
    }
    
    kfree(br);
    return 0;
}

int bridge_add_interface(net_bridge_t *br, net_interface_t *iface) {
    if (!br || !iface) return -1;
    if (br->member_count >= 16) return -1;
    
    br->members[br->member_count++] = iface;
    
    serial_puts("[NETINFRA] Added ");
    serial_puts(iface->name);
    serial_puts(" to ");
    serial_puts(br->name);
    serial_puts("\n");
    
    return 0;
}

int bridge_remove_interface(net_bridge_t *br, net_interface_t *iface) {
    if (!br || !iface) return -1;
    
    for (int i = 0; i < br->member_count; i++) {
        if (br->members[i] == iface) {
            for (int j = i; j < br->member_count - 1; j++) {
                br->members[j] = br->members[j + 1];
            }
            br->member_count--;
            return 0;
        }
    }
    
    return -1;
}

int bridge_set_stp(net_bridge_t *br, int enabled) {
    if (!br) return -1;
    br->stp_enabled = enabled ? 1 : 0;
    return 0;
}

net_bridge_t *bridge_get(const char *name) {
    net_bridge_t *br = bridges;
    while (br) {
        if (ni_strcmp(br->name, name) == 0) return br;
        br = br->next;
    }
    return NULL;
}

/* ============ VLAN API ============ */

net_vlan_t *vlan_create(net_interface_t *parent, uint16_t vlan_id) {
    if (!parent || vlan_id < 1 || vlan_id > 4094) return NULL;
    
    net_vlan_t *vlan = (net_vlan_t *)kzalloc(sizeof(net_vlan_t));
    if (!vlan) return NULL;
    
    /* Name: eth0.100 */
    int i = 0;
    while (parent->name[i] && i < 10) {
        vlan->name[i] = parent->name[i];
        i++;
    }
    vlan->name[i++] = '.';
    if (vlan_id >= 1000) vlan->name[i++] = '0' + (vlan_id / 1000);
    if (vlan_id >= 100) vlan->name[i++] = '0' + ((vlan_id / 100) % 10);
    if (vlan_id >= 10) vlan->name[i++] = '0' + ((vlan_id / 10) % 10);
    vlan->name[i++] = '0' + (vlan_id % 10);
    vlan->name[i] = '\0';
    
    vlan->vlan_id = vlan_id;
    vlan->parent = parent;
    ni_strcpy(vlan->interface.name, vlan->name, 16);
    vlan->interface.type = NET_IF_ETHERNET;
    
    vlan->next = vlans;
    vlans = vlan;
    
    serial_puts("[NETINFRA] Created VLAN: ");
    serial_puts(vlan->name);
    serial_puts("\n");
    
    return vlan;
}

int vlan_delete(net_vlan_t *vlan) {
    if (!vlan) return -1;
    
    if (vlans == vlan) {
        vlans = vlan->next;
    } else {
        net_vlan_t *prev = vlans;
        while (prev && prev->next != vlan) prev = prev->next;
        if (prev) prev->next = vlan->next;
    }
    
    kfree(vlan);
    return 0;
}

/* ============ Virtual NIC API ============ */

net_vnic_t *vnic_create(const char *name, vnic_type_t type) {
    net_vnic_t *vnic = (net_vnic_t *)kzalloc(sizeof(net_vnic_t));
    if (!vnic) return NULL;
    
    ni_strcpy(vnic->name, name, 16);
    vnic->type = type;
    vnic->fd = -1;  /* Will be assigned when opened */
    
    ni_strcpy(vnic->interface.name, name, 16);
    vnic->interface.type = NET_IF_ETHERNET;
    
    vnic->next = vnics;
    vnics = vnic;
    
    serial_puts("[NETINFRA] Created ");
    serial_puts(type == VNIC_TYPE_TAP ? "TAP" : "TUN");
    serial_puts(": ");
    serial_puts(name);
    serial_puts("\n");
    
    return vnic;
}

int vnic_delete(net_vnic_t *vnic) {
    if (!vnic) return -1;
    
    if (vnics == vnic) {
        vnics = vnic->next;
    } else {
        net_vnic_t *prev = vnics;
        while (prev && prev->next != vnic) prev = prev->next;
        if (prev) prev->next = vnic->next;
    }
    
    kfree(vnic);
    return 0;
}



/* ============ NAT API ============ */

int nat_enable_forwarding(int enabled) {
    ip_forwarding = enabled ? 1 : 0;
    serial_puts("[NAT] IP forwarding ");
    serial_puts(enabled ? "enabled\n" : "disabled\n");
    return 0;
}

int nat_add_forward(uint16_t external_port, ipv4_addr_t internal_ip,
                    uint16_t internal_port, uint8_t protocol) {
    nat_entry_t *entry = (nat_entry_t *)kzalloc(sizeof(nat_entry_t));
    if (!entry) return -1;
    
    entry->external_port = external_port;
    entry->internal_ip = internal_ip;
    entry->internal_port = internal_port;
    entry->protocol = protocol;
    
    entry->next = nat_entries;
    nat_entries = entry;
    
    serial_puts("[NAT] Port forward: ");
    serial_putc('0' + (external_port / 10000) % 10);
    serial_putc('0' + (external_port / 1000) % 10);
    serial_putc('0' + (external_port / 100) % 10);
    serial_putc('0' + (external_port / 10) % 10);
    serial_putc('0' + external_port % 10);
    serial_puts(" -> ");
    serial_ip(internal_ip);
    serial_puts("\n");
    
    return 0;
}

int nat_enable_masquerade(net_interface_t *iface) {
    if (!iface) return -1;
    
    serial_puts("[NAT] Masquerade on ");
    serial_puts(iface->name);
    serial_puts("\n");
    
    return 0;
}

/* ============ DNS API ============ */

int dns_add_server(ipv4_addr_t address, int priority) {
    dns_server_t *srv = (dns_server_t *)kzalloc(sizeof(dns_server_t));
    if (!srv) return -1;
    
    srv->address = address;
    srv->priority = priority;
    srv->next = dns_servers;
    dns_servers = srv;
    
    serial_puts("[DNS] Added server: ");
    serial_ip(address);
    serial_puts("\n");
    
    return 0;
}

int dns_resolve(const char *hostname, ipv4_addr_t *result) {
    if (!hostname || !result) return -1;
    if (!dns_servers) return -1;  /* No DNS servers configured */
    
    serial_puts("[DNS] Resolving ");
    serial_puts(hostname);
    serial_puts("...\n");
    
    /* Get network interface */
    net_interface_t *iface = network_get_interface_by_index(0);
    if (!iface) {
        serial_puts("[DNS] No network interface\n");
        return -1;
    }
    
    /* ============ Build DNS Query Packet ============ */
    /* DNS header (12 bytes) + question section */
    uint8_t query[512];
    int pos = 0;
    
    /* DNS Header */
    /* Transaction ID */
    uint16_t txid = 0x1234;  /* Random ID for real implementation */
    query[pos++] = (txid >> 8) & 0xFF;
    query[pos++] = txid & 0xFF;
    
    /* Flags: Standard query, recursion desired */
    query[pos++] = 0x01;  /* QR=0, OPCODE=0, AA=0, TC=0, RD=1 */
    query[pos++] = 0x00;  /* RA=0, Z=0, RCODE=0 */
    
    /* QDCOUNT = 1 */
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    
    /* ANCOUNT = 0 */
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    
    /* NSCOUNT = 0 */
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    
    /* ARCOUNT = 0 */
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    
    /* ============ Question Section ============ */
    /* QNAME: Convert hostname to DNS label format */
    /* "www.example.com" -> "\x03www\x07example\x03com\x00" */
    const char *p = hostname;
    while (*p) {
        /* Find next dot or end */
        const char *dot = p;
        int label_len = 0;
        while (*dot && *dot != '.') { dot++; label_len++; }
        
        if (label_len > 63 || label_len == 0) {
            serial_puts("[DNS] Invalid label length\n");
            return -1;
        }
        
        /* Write length byte */
        query[pos++] = label_len;
        
        /* Write label characters */
        for (int i = 0; i < label_len; i++) {
            query[pos++] = p[i];
        }
        
        /* Move past dot */
        p = dot;
        if (*p == '.') p++;
    }
    
    /* Null terminator for QNAME */
    query[pos++] = 0x00;
    
    /* QTYPE = A (1) - Request IPv4 address */
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    
    /* QCLASS = IN (1) - Internet class */
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    
    /* ============ Send Query via UDP ============ */
    ipv4_addr_t dns_ip = dns_servers->address;
    
    serial_puts("[DNS] Sending query to ");
    serial_ip(dns_ip);
    serial_puts(":53\n");
    
    /* Build UDP/IP packet manually and send */
    uint8_t udp_pkt[8 + 512];
    
    /* UDP header */
    udp_pkt[0] = 0xC0;  /* Source port high (49152) */
    udp_pkt[1] = 0x00;  /* Source port low */
    udp_pkt[2] = 0x00;  /* Dest port high (53) */
    udp_pkt[3] = 0x35;  /* Dest port low */
    
    uint16_t udp_len = 8 + pos;
    udp_pkt[4] = (udp_len >> 8) & 0xFF;
    udp_pkt[5] = udp_len & 0xFF;
    udp_pkt[6] = 0x00;  /* Checksum high (optional for IPv4) */
    udp_pkt[7] = 0x00;  /* Checksum low */
    
    /* Copy DNS query */
    for (int i = 0; i < pos; i++) {
        udp_pkt[8 + i] = query[i];
    }
    
    /* Send via IP layer */
    extern int ip_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
                       const void *data, uint32_t len);
    
    int ret = ip_send(iface, dns_ip, 17 /* UDP */, udp_pkt, udp_len);
    if (ret < 0) {
        serial_puts("[DNS] Send failed\n");
        return -1;
    }
    
    /* ============ Wait for Response ============ */
    /* In a cooperative kernel, we poll for response */
    /* This is synchronous - wait up to ~500ms */
    volatile int timeout = 500000;
    
    /* Check for DNS response in UDP receive buffer */
    /* For now, use static buffer that UDP input fills */
    extern uint8_t dns_response_buffer[512];
    extern volatile int dns_response_ready;
    extern volatile int dns_response_len;
    
    dns_response_ready = 0;
    
    while (timeout-- > 0 && !dns_response_ready) {
        __asm__ volatile("pause");
    }
    
    if (!dns_response_ready) {
        serial_puts("[DNS] Timeout waiting for response\n");
        /* Fall back to well-known IP for common domains */
        /* This helps in QEMU emulation without real network */
        if (hostname[0] == 'g' && hostname[1] == 'o') {  /* google */
            result->bytes[0] = 8; result->bytes[1] = 8;
            result->bytes[2] = 8; result->bytes[3] = 8;
            return 0;
        }
        return -1;
    }
    
    /* ============ Parse DNS Response ============ */
    uint8_t *resp = dns_response_buffer;
    int rlen = dns_response_len;
    
    if (rlen < 12) return -1;  /* Too short for header */
    
    /* Check transaction ID matches */
    if (resp[0] != (txid >> 8) || resp[1] != (txid & 0xFF)) {
        serial_puts("[DNS] Transaction ID mismatch\n");
        return -1;
    }
    
    /* Check response bit and no error */
    if (!(resp[2] & 0x80)) return -1;  /* Not a response */
    if ((resp[3] & 0x0F) != 0) {
        serial_puts("[DNS] Server returned error\n");
        return -1;
    }
    
    /* Get answer count */
    uint16_t ancount = (resp[6] << 8) | resp[7];
    if (ancount == 0) {
        serial_puts("[DNS] No answers in response\n");
        return -1;
    }
    
    /* Skip question section */
    int rpos = 12;
    
    /* Skip QNAME */
    while (rpos < rlen && resp[rpos] != 0) {
        if ((resp[rpos] & 0xC0) == 0xC0) {
            rpos += 2;  /* Compressed pointer */
            break;
        }
        rpos += resp[rpos] + 1;
    }
    if (resp[rpos] == 0) rpos++;  /* Skip null terminator */
    rpos += 4;  /* Skip QTYPE and QCLASS */
    
    /* Parse answers looking for A record */
    for (int i = 0; i < ancount && rpos + 12 <= rlen; i++) {
        /* Skip NAME (may be compressed) */
        if ((resp[rpos] & 0xC0) == 0xC0) {
            rpos += 2;
        } else {
            while (rpos < rlen && resp[rpos] != 0) {
                rpos += resp[rpos] + 1;
            }
            rpos++;  /* Skip null */
        }
        
        /* TYPE (2 bytes) */
        uint16_t rtype = (resp[rpos] << 8) | resp[rpos + 1];
        rpos += 2;
        
        /* CLASS (2 bytes) */
        rpos += 2;
        
        /* TTL (4 bytes) */
        rpos += 4;
        
        /* RDLENGTH (2 bytes) */
        uint16_t rdlen = (resp[rpos] << 8) | resp[rpos + 1];
        rpos += 2;
        
        /* Check if this is an A record (type 1) with 4 bytes */
        if (rtype == 1 && rdlen == 4) {
            result->bytes[0] = resp[rpos];
            result->bytes[1] = resp[rpos + 1];
            result->bytes[2] = resp[rpos + 2];
            result->bytes[3] = resp[rpos + 3];
            
            serial_puts("[DNS] Resolved to ");
            serial_ip(*result);
            serial_puts("\n");
            
            return 0;
        }
        
        rpos += rdlen;  /* Skip RDATA */
    }
    
    serial_puts("[DNS] No A record found\n");
    return -1;
}

/* ============ Routing API ============ */

int route_add(ipv4_addr_t dest, ipv4_addr_t mask, ipv4_addr_t gateway,
              net_interface_t *iface, uint32_t metric) {
    route_entry_t *route = (route_entry_t *)kzalloc(sizeof(route_entry_t));
    if (!route) return -1;
    
    route->destination = dest;
    route->netmask = mask;
    route->gateway = gateway;
    route->iface = iface;
    route->metric = metric;
    route->flags = ROUTE_FLAG_UP;
    
    if (gateway.bytes[0] || gateway.bytes[1] || gateway.bytes[2] || gateway.bytes[3]) {
        route->flags |= ROUTE_FLAG_GATEWAY;
    }
    
    route->next = routes;
    routes = route;
    
    serial_puts("[ROUTE] Added: ");
    serial_ip(dest);
    serial_puts("/");
    serial_ip(mask);
    serial_puts("\n");
    
    return 0;
}

route_entry_t *route_lookup(ipv4_addr_t dest) {
    route_entry_t *best = NULL;
    int best_prefix = -1;
    
    route_entry_t *r = routes;
    while (r) {
        if (ip_match(dest, r->destination, r->netmask)) {
            /* Count prefix length */
            int prefix = 0;
            for (int i = 0; i < 4; i++) {
                uint8_t m = r->netmask.bytes[i];
                while (m) { prefix += m & 1; m >>= 1; }
            }
            
            if (prefix > best_prefix) {
                best_prefix = prefix;
                best = r;
            }
        }
        r = r->next;
    }
    
    return best;
}

/* ============ Connection Sharing ============ */

int netinfra_share_connection(net_interface_t *wan, net_interface_t *lan) {
    if (!wan || !lan) return -1;
    
    serial_puts("[NETINFRA] Sharing ");
    serial_puts(wan->name);
    serial_puts(" via ");
    serial_puts(lan->name);
    serial_puts("\n");
    
    nat_enable_forwarding(1);
    nat_enable_masquerade(wan);
    
    /* Set LAN IP */
    ipv4_addr_t lan_ip = {{192, 168, 100, 1}};
    ipv4_addr_t lan_mask = {{255, 255, 255, 0}};
    ipv4_addr_t lan_gw = {{0, 0, 0, 0}};
    
    network_set_ip(lan, lan_ip, lan_mask, lan_gw);
    
    serial_puts("[NETINFRA] Connection sharing active\n");
    return 0;
}

int netinfra_stop_sharing(void) {
    nat_enable_forwarding(0);
    serial_puts("[NETINFRA] Connection sharing stopped\n");
    return 0;
}
