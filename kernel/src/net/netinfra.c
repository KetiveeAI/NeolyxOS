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

/*
 * Resolve an A record. Asks the DNS servers DHCP gave the default
 * interface, then any configured with netinfra (dns_servers list), each
 * twice with a 2 s timeout. Returns 0 and the address in *result, or -1.
 * Must not be called on the network input path.
 */
#define DNS_TIMEOUT_MS  2000u
#define DNS_TRIES       2

static int dns_build_query(const char *hostname, uint16_t txid, uint8_t *q, int cap) {
    int pos = 0;
    if (cap < 12 + 2 + 4) return -1;
    q[pos++] = (uint8_t)(txid >> 8);
    q[pos++] = (uint8_t)txid;
    q[pos++] = 0x01;    /* RD */
    q[pos++] = 0x00;
    q[pos++] = 0x00; q[pos++] = 0x01;   /* QDCOUNT 1 */
    for (int i = 0; i < 6; i++) q[pos++] = 0;

    const char *p = hostname;
    int total = 0;
    while (*p) {
        const char *dot = p;
        int label_len = 0;
        while (*dot && *dot != '.') {
            char c = *dot;
            int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!ok) return -1;
            dot++;
            label_len++;
        }
        if (label_len == 0 || label_len > 63) return -1;
        total += label_len + 1;
        if (total > 253 || pos + label_len + 1 + 5 > cap) return -1;
        q[pos++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) q[pos++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.') {
            p++;
            if (*p == '\0') break;  /* trailing dot */
        }
    }
    if (total == 0) return -1;
    q[pos++] = 0x00;
    q[pos++] = 0x00; q[pos++] = 0x01;   /* QTYPE A */
    q[pos++] = 0x00; q[pos++] = 0x01;   /* QCLASS IN */
    return pos;
}

/* Skip a (possibly compressed) name. Returns the new offset or -1. */
static int dns_skip_name(const uint8_t *r, int len, int pos) {
    int guard = 0;
    while (pos < len && guard++ < 128) {
        uint8_t b = r[pos];
        if (b == 0) return pos + 1;
        if ((b & 0xC0) == 0xC0) return (pos + 2 <= len) ? pos + 2 : -1;
        if (b & 0xC0) return -1;
        pos += b + 1;
    }
    return -1;
}

static int dns_parse_a(const uint8_t *r, int len, uint16_t txid, ipv4_addr_t *out) {
    if (len < 12) return -1;
    if (r[0] != (uint8_t)(txid >> 8) || r[1] != (uint8_t)txid) return -1;
    if (!(r[2] & 0x80)) return -1;                 /* not a response */
    if ((r[3] & 0x0F) != 0) {
        serial_puts("[DNS] Server returned error code ");
        serial_putc((char)('0' + (r[3] & 0x0F)));
        serial_puts("\n");
        return -1;
    }
    int qd = (r[4] << 8) | r[5];
    int an = (r[6] << 8) | r[7];

    int pos = 12;
    for (int i = 0; i < qd; i++) {
        pos = dns_skip_name(r, len, pos);
        if (pos < 0 || pos + 4 > len) return -1;
        pos += 4;
    }
    for (int i = 0; i < an; i++) {
        pos = dns_skip_name(r, len, pos);
        if (pos < 0 || pos + 10 > len) return -1;
        uint16_t type = (uint16_t)((r[pos] << 8) | r[pos + 1]);
        uint16_t klass = (uint16_t)((r[pos + 2] << 8) | r[pos + 3]);
        uint16_t rdlen = (uint16_t)((r[pos + 8] << 8) | r[pos + 9]);
        pos += 10;
        if (pos + rdlen > len) return -1;
        if (type == 1 && klass == 1 && rdlen == 4) {
            for (int k = 0; k < 4; k++) out->bytes[k] = r[pos + k];
            return 0;
        }
        pos += rdlen;   /* CNAME and others: keep looking */
    }
    return -1;
}

static int dns_ask(net_interface_t *iface, ipv4_addr_t server, const char *hostname,
                   ipv4_addr_t *result) {
    uint8_t query[300];
    uint8_t resp[512];

    for (int attempt = 0; attempt < DNS_TRIES; attempt++) {
        uint16_t txid = (uint16_t)net_random32();
        int qlen = dns_build_query(hostname, txid, query, sizeof(query));
        if (qlen < 0) {
            serial_puts("[DNS] Invalid host name\n");
            return -1;
        }
        int n = net_udp_query(iface, server, PORT_DNS, query, (uint32_t)qlen,
                              resp, sizeof(resp), DNS_TIMEOUT_MS);
        if (n > 0) {
            return dns_parse_a(resp, n, txid, result);
        }
    }
    serial_puts("[DNS] No answer from ");
    serial_ip(server);
    serial_puts("\n");
    return -1;
}

int dns_resolve(const char *hostname, ipv4_addr_t *result) {
    if (!hostname || !result) return -1;

    net_interface_t *iface = network_default_iface();
    if (!iface) {
        serial_puts("[DNS] No configured network interface\n");
        return -1;
    }

    serial_puts("[DNS] Resolving ");
    serial_puts(hostname);
    serial_puts("\n");

    for (int i = 0; i < iface->dns_count && i < 3; i++) {
        if (dns_ask(iface, iface->dns_servers[i], hostname, result) == 0) goto resolved;
    }
    for (dns_server_t *d = dns_servers; d; d = d->next) {
        if (dns_ask(iface, d->address, hostname, result) == 0) goto resolved;
    }

    serial_puts("[DNS] Could not resolve ");
    serial_puts(hostname);
    serial_puts("\n");
    return -1;

resolved:
    serial_puts("[DNS] ");
    serial_puts(hostname);
    serial_puts(" -> ");
    serial_ip(*result);
    serial_puts("\n");
    return 0;
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
