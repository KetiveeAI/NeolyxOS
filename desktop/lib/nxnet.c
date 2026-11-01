/*
 * NeolyxOS - NXNet: userspace networking client (see nxnet.h)
 *
 * Structure:
 *   1. Pure helpers: validation, parsing, formatting. Always available.
 *   2. Backend: the only code that talks to the system.
 *        - OS build: interface status from net.if_list (syscall 133,
 *          ABI 1.4) once abi.query reports NX_ABI_FLAG_NET. Hostname,
 *          Wi-Fi and IPv4 configuration have no syscall yet and return
 *          NXNET_ERR_UNSUPPORTED; they are specified in
 *          docs/desktop/proposals/net.md.
 *        - NX_HOST_PREVIEW: an in-memory demo (Ethernet + Wi-Fi + scan
 *          results) so the UI can be reviewed. Clearly fenced; never
 *          compiled into the OS.
 *   3. Public API: validates every argument, then calls the backend.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxnet.h"
#include "nxsyscall.h"

/* ============ Small freestanding helpers ============ */

static size_t nn_strnlen(const char *s, size_t max) {
    size_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

/* Copies at most cap-1 bytes and always terminates (cap > 0). */
static void nn_strcpy(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    size_t i = 0;
    if (src) {
        while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

static void nn_zero(void *p, size_t n) {
    volatile uint8_t *b = (volatile uint8_t *)p;   /* volatile: also used to wipe secrets */
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

static void nn_log(const char *msg) { nx_debug_print(msg); }

static void nn_log2(const char *a, const char *b) {
    char line[160];
    size_t n = 0;
    for (const char *p = a; p && *p && n + 1 < sizeof(line); p++) line[n++] = *p;
    for (const char *p = b; p && *p && n + 2 < sizeof(line); p++) line[n++] = *p;
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

static uint32_t ip_u32(nxnet_ipv4_t ip) {
    return ((uint32_t)ip.b[0] << 24) | ((uint32_t)ip.b[1] << 16) | ((uint32_t)ip.b[2] << 8) | ip.b[3];
}

/* ============ Status strings ============ */

const char *nxnet_status_str(nxnet_status_t st) {
    switch (st) {
        case NXNET_OK:                   return "OK";
        case NXNET_ERR_INVALID_ARG:      return "Invalid argument";
        case NXNET_ERR_UNSUPPORTED:      return "Not supported by this system";
        case NXNET_ERR_NOT_FOUND:        return "Not found";
        case NXNET_ERR_PERMISSION:       return "Administrator permission required";
        case NXNET_ERR_BUFFER_TOO_SMALL: return "Buffer too small";
        case NXNET_ERR_BUSY:             return "Busy";
        case NXNET_ERR_AUTH:             return "Incorrect password";
        case NXNET_ERR_STATE:            return "Not available in the current state";
        case NXNET_ERR_IO:               return "Device error";
        case NXNET_ERR_TIMEOUT:          return "Timed out";
        default:                         return "Unknown error";
    }
}

const char *nxnet_security_str(nxnet_wifi_security_t sec) {
    switch (sec) {
        case NXNET_WIFI_SEC_OPEN: return "None";
        case NXNET_WIFI_SEC_WEP:  return "WEP";
        case NXNET_WIFI_SEC_WPA:  return "WPA Personal";
        case NXNET_WIFI_SEC_WPA2: return "WPA2 Personal";
        case NXNET_WIFI_SEC_WPA3: return "WPA3 Personal";
        default:                  return "Unknown";
    }
}

const char *nxnet_if_kind_str(nxnet_if_kind_t kind) {
    switch (kind) {
        case NXNET_IF_ETHERNET: return "Ethernet";
        case NXNET_IF_WIFI:     return "Wi-Fi";
        case NXNET_IF_LOOPBACK: return "Loopback";
        default:                return "Interface";
    }
}

const char *nxnet_if_state_str(nxnet_if_state_t state) {
    switch (state) {
        case NXNET_IF_DISABLED:    return "Off";
        case NXNET_IF_NO_LINK:     return "Not connected";
        case NXNET_IF_CONFIGURING: return "Connecting";
        case NXNET_IF_CONNECTED:   return "Connected";
        case NXNET_IF_ERROR:       return "Error";
        default:                   return "Unknown";
    }
}

/* ============ Parsing and formatting ============ */

nxnet_status_t nxnet_ipv4_parse(const char *s, nxnet_ipv4_t *out) {
    if (!s || !out) return NXNET_ERR_INVALID_ARG;
    nxnet_ipv4_t ip = {{0, 0, 0, 0}};
    int octet = 0;
    const char *p = s;
    if (nn_strnlen(s, NXNET_IPV4_STR_MAX) >= NXNET_IPV4_STR_MAX) return NXNET_ERR_INVALID_ARG;
    for (;;) {
        if (*p < '0' || *p > '9') return NXNET_ERR_INVALID_ARG;
        uint32_t v = 0;
        int digits = 0;
        const char *start = p;
        while (*p >= '0' && *p <= '9') {
            v = v * 10u + (uint32_t)(*p - '0');
            digits++;
            p++;
            if (digits > 3) return NXNET_ERR_INVALID_ARG;
        }
        if (v > 255u) return NXNET_ERR_INVALID_ARG;
        if (digits > 1 && start[0] == '0') return NXNET_ERR_INVALID_ARG;   /* no leading zeros */
        ip.b[octet++] = (uint8_t)v;
        if (octet == 4) break;
        if (*p != '.') return NXNET_ERR_INVALID_ARG;
        p++;
    }
    if (*p != '\0') return NXNET_ERR_INVALID_ARG;
    *out = ip;
    return NXNET_OK;
}

static size_t fmt_u8(char *dst, uint8_t v) {
    char t[3];
    size_t n = 0;
    do { t[n++] = (char)('0' + v % 10); v = (uint8_t)(v / 10); } while (v && n < 3);
    for (size_t i = 0; i < n; i++) dst[i] = t[n - 1 - i];
    return n;
}

nxnet_status_t nxnet_ipv4_format(nxnet_ipv4_t ip, char *buf, size_t cap) {
    if (!buf || cap < NXNET_IPV4_STR_MAX) return NXNET_ERR_INVALID_ARG;
    size_t n = 0;
    for (int i = 0; i < 4; i++) {
        n += fmt_u8(buf + n, ip.b[i]);
        if (i < 3) buf[n++] = '.';
    }
    buf[n] = '\0';
    return NXNET_OK;
}

nxnet_status_t nxnet_mac_format(nxnet_mac_t mac, char *buf, size_t cap) {
    static const char hex[] = "0123456789abcdef";
    if (!buf || cap < NXNET_MAC_STR_MAX) return NXNET_ERR_INVALID_ARG;
    size_t n = 0;
    for (int i = 0; i < 6; i++) {
        buf[n++] = hex[mac.b[i] >> 4];
        buf[n++] = hex[mac.b[i] & 15];
        if (i < 5) buf[n++] = ':';
    }
    buf[n] = '\0';
    return NXNET_OK;
}

int nxnet_ipv4_is_zero(nxnet_ipv4_t ip) { return ip_u32(ip) == 0; }

int nxnet_netmask_prefix(nxnet_ipv4_t mask) {
    uint32_t m = ip_u32(mask);
    uint32_t inv = ~m;
    if ((inv & (inv + 1u)) != 0) return -1;   /* host bits must be contiguous low ones */
    int prefix = 0;
    while (prefix < 32 && (m & (0x80000000u >> prefix))) prefix++;
    return prefix;
}

int nxnet_netmask_valid(nxnet_ipv4_t mask) {
    int p = nxnet_netmask_prefix(mask);
    return p >= 1 && p <= 32;
}

/* Usable unicast host address: not 0/8, loopback, multicast/reserved, broadcast. */
static int ipv4_unicast(nxnet_ipv4_t ip) {
    if (ip.b[0] == 0 || ip.b[0] == 127 || ip.b[0] >= 224) return 0;
    return 1;
}

nxnet_status_t nxnet_validate_ssid(const char *ssid) {
    if (!ssid) return NXNET_ERR_INVALID_ARG;
    size_t n = nn_strnlen(ssid, NXNET_SSID_MAX + 1);
    if (n == 0 || n > NXNET_SSID_MAX) return NXNET_ERR_INVALID_ARG;
    for (size_t i = 0; i < n; i++) {
        if ((uint8_t)ssid[i] < 0x20 || (uint8_t)ssid[i] == 0x7F) return NXNET_ERR_INVALID_ARG;
    }
    return NXNET_OK;
}

nxnet_status_t nxnet_validate_passphrase(const char *pass, nxnet_wifi_security_t sec) {
    size_t n = nn_strnlen(pass, NXNET_PASSPHRASE_MAX + 2);
    switch (sec) {
        case NXNET_WIFI_SEC_OPEN:
            return n == 0 ? NXNET_OK : NXNET_ERR_INVALID_ARG;
        case NXNET_WIFI_SEC_WEP:
            return NXNET_ERR_UNSUPPORTED;   /* broken cipher: refused by policy */
        case NXNET_WIFI_SEC_WPA:
        case NXNET_WIFI_SEC_WPA2:
        case NXNET_WIFI_SEC_WPA3:
            break;
        default:
            return NXNET_ERR_INVALID_ARG;
    }
    if (!pass) return NXNET_ERR_INVALID_ARG;
    if (n == NXNET_PASSPHRASE_MAX + 1) {
        /* 64 characters: only valid as a raw hex PSK (WPA/WPA2). */
        if (sec == NXNET_WIFI_SEC_WPA3) return NXNET_ERR_INVALID_ARG;
        for (size_t i = 0; i < n; i++) {
            char ch = pass[i];
            int hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
            if (!hex) return NXNET_ERR_INVALID_ARG;
        }
        return NXNET_OK;
    }
    if (n < NXNET_PASSPHRASE_MIN || n > NXNET_PASSPHRASE_MAX) return NXNET_ERR_INVALID_ARG;
    for (size_t i = 0; i < n; i++) {
        if ((uint8_t)pass[i] < 0x20 || (uint8_t)pass[i] > 0x7E) return NXNET_ERR_INVALID_ARG;
    }
    return NXNET_OK;
}

nxnet_status_t nxnet_validate_hostname(const char *name) {
    if (!name) return NXNET_ERR_INVALID_ARG;
    size_t n = nn_strnlen(name, NXNET_HOSTNAME_MAX + 1);
    if (n == 0 || n > NXNET_HOSTNAME_MAX) return NXNET_ERR_INVALID_ARG;
    if (name[0] == '-' || name[n - 1] == '-') return NXNET_ERR_INVALID_ARG;
    for (size_t i = 0; i < n; i++) {
        char ch = name[i];
        int ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
        if (!ok) return NXNET_ERR_INVALID_ARG;
    }
    return NXNET_OK;
}

nxnet_status_t nxnet_ipv4_config_validate(const nxnet_ipv4_config_t *cfg, nxnet_field_t *bad_field) {
    nxnet_field_t bad = NXNET_FIELD_NONE;
    nxnet_status_t st = NXNET_OK;
    if (!cfg) { st = NXNET_ERR_INVALID_ARG; goto done; }
    if (cfg->method == NXNET_IPV4_DHCP) goto done;
    if (cfg->method != NXNET_IPV4_MANUAL) { st = NXNET_ERR_INVALID_ARG; goto done; }

    if (!ipv4_unicast(cfg->address)) { bad = NXNET_FIELD_ADDRESS; goto fail; }
    if (!nxnet_netmask_valid(cfg->netmask)) { bad = NXNET_FIELD_NETMASK; goto fail; }
    {
        int prefix = nxnet_netmask_prefix(cfg->netmask);
        uint32_t mask = ip_u32(cfg->netmask), addr = ip_u32(cfg->address);
        if (prefix <= 30) {
            uint32_t host = addr & ~mask;
            if (host == 0 || host == (~mask)) { bad = NXNET_FIELD_ADDRESS; goto fail; }
        }
        if (!nxnet_ipv4_is_zero(cfg->gateway)) {
            uint32_t gw = ip_u32(cfg->gateway);
            if (!ipv4_unicast(cfg->gateway) || gw == addr || (gw & mask) != (addr & mask)) {
                bad = NXNET_FIELD_GATEWAY; goto fail;
            }
            if (prefix <= 30) {
                uint32_t gh = gw & ~mask;
                if (gh == 0 || gh == (~mask)) { bad = NXNET_FIELD_GATEWAY; goto fail; }
            }
        }
    }
    if (cfg->dns_count > NXNET_DNS_MAX) { bad = NXNET_FIELD_DNS; goto fail; }
    for (uint32_t i = 0; i < cfg->dns_count; i++) {
        if (!ipv4_unicast(cfg->dns[i])) { bad = NXNET_FIELD_DNS; goto fail; }
    }
    goto done;
fail:
    st = NXNET_ERR_INVALID_ARG;
done:
    if (bad_field) *bad_field = bad;
    return st;
}

uint8_t nxnet_rssi_bars(int8_t rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -66) return 3;
    if (rssi >= -77) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

/* ============ Backend ============ */

static int            g_inited;
static nxnet_status_t g_avail = NXNET_ERR_UNSUPPORTED;
static uint32_t       g_generation = 1;
static int            g_logged_unsupported;

static void bump(void) { g_generation++; if (g_generation == 0) g_generation = 1; }

#ifndef NX_HOST_PREVIEW
/* ---- OS build: kernel backend --------------------------------------
 * Interface status comes from net.if_list (syscall 133, ABI 1.4,
 * kernel/include/core/nx_net.h). Hostname, Wi-Fi and IPv4 configuration
 * have no syscall yet (docs/desktop/proposals/net.md), so those requests
 * are answered with UNSUPPORTED. Nothing is guessed. */

static nxnet_status_t kb_probe(void) {
    nx_abi_info_t abi;
    nn_zero(&abi, sizeof(abi));
    if (nx_abi_query(&abi) != 0) return NXNET_ERR_UNSUPPORTED;
    /* An older kernel has no syscall 133: never call it there */
    if (abi.major != 1 || abi.minor < 4 || !(abi.flags & NX_ABI_FLAG_NET)) {
        return NXNET_ERR_UNSUPPORTED;
    }
    return NXNET_OK;
}

/* Kernel errno values (kernel/include/core/syscall.h) */
#define NN_EFAULT   14
#define NN_EINVAL   22
#define NN_ENOSYS   38

static nxnet_status_t kb_errno(int r) {
    if (r == -NN_EFAULT || r == -NN_EINVAL) return NXNET_ERR_INVALID_ARG;
    if (r == -NN_ENOSYS) return NXNET_ERR_UNSUPPORTED;
    return NXNET_ERR_IO;
}

static nxnet_status_t kb_interfaces(nxnet_interface_t *out, uint32_t cap, uint32_t *count) {
    nx_net_if_info_t info[NX_NET_IF_MAX];
    nn_zero(info, sizeof(info));
    uint32_t want = cap < NX_NET_IF_MAX ? cap : NX_NET_IF_MAX;

    int r = nx_net_if_list(info, want);
    if (r < 0) return kb_errno(r);

    uint32_t total = (uint32_t)r;
    uint32_t n = total < want ? total : want;
    for (uint32_t i = 0; i < n; i++) {
        const nx_net_if_info_t *k = &info[i];
        nxnet_interface_t *f = &out[i];
        nn_zero(f, sizeof(*f));
        if (k->size < NX_NET_IF_INFO_V1_SIZE) return NXNET_ERR_IO;
        if (k->kind > NX_NET_IF_LOOPBACK || k->state > NX_NET_STATE_ERROR) return NXNET_ERR_IO;

        f->index = k->index;
        nn_strcpy(f->name, k->name, sizeof(f->name));   /* bounded, always NUL-terminated */
        f->kind = (nxnet_if_kind_t)k->kind;
        f->state = (nxnet_if_state_t)k->state;
        f->link_up = k->link_up ? 1 : 0;
        f->primary = k->primary ? 1 : 0;
        for (int b = 0; b < 6; b++) f->mac.b[b] = k->mac[b];
        f->speed_mbps = k->speed_mbps;
        f->ipv4.method = k->ipv4_method == NX_NET_IPV4_MANUAL ? NXNET_IPV4_MANUAL : NXNET_IPV4_DHCP;
        for (int b = 0; b < 4; b++) {
            f->ipv4.address.b[b] = k->addr[b];
            f->ipv4.netmask.b[b] = k->mask[b];
            f->ipv4.gateway.b[b] = k->gateway[b];
        }
        uint32_t dn = k->dns_count > NXNET_DNS_MAX ? NXNET_DNS_MAX : k->dns_count;
        for (uint32_t d = 0; d < dn; d++)
            for (int b = 0; b < 4; b++) f->ipv4.dns[d].b[b] = k->dns[d][b];
        f->ipv4.dns_count = dn;
        f->ipv4.lease_seconds = k->lease_seconds;
        f->counters.rx_bytes = k->rx_bytes;
        f->counters.tx_bytes = k->tx_bytes;
        f->counters.rx_packets = k->rx_packets;
        f->counters.tx_packets = k->tx_packets;
        f->counters.rx_errors = k->rx_errors;
        f->counters.tx_errors = k->tx_errors;
    }
    *count = total;
    return total > cap ? NXNET_ERR_BUFFER_TOO_SMALL : NXNET_OK;
}
static nxnet_status_t kb_hostname(char *buf, size_t cap) { (void)buf; (void)cap; return NXNET_ERR_UNSUPPORTED; }
static nxnet_status_t kb_wifi_status(nxnet_wifi_status_t *out) { (void)out; return NXNET_ERR_UNSUPPORTED; }
static nxnet_status_t kb_wifi_results(nxnet_wifi_network_t *out, uint32_t cap, uint32_t *count) {
    (void)out; (void)cap; (void)count; return NXNET_ERR_UNSUPPORTED;
}
static nxnet_status_t kb_wifi_scan(void) { return NXNET_ERR_UNSUPPORTED; }
static nxnet_status_t kb_wifi_power(int on) { (void)on; return NXNET_ERR_UNSUPPORTED; }
static nxnet_status_t kb_wifi_join(const char *ssid, const char *pass, nxnet_wifi_security_t sec) {
    (void)ssid; (void)pass; (void)sec; return NXNET_ERR_UNSUPPORTED;
}
static nxnet_status_t kb_wifi_disconnect(void) { return NXNET_ERR_UNSUPPORTED; }
static nxnet_status_t kb_wifi_forget(const char *ssid) { (void)ssid; return NXNET_ERR_UNSUPPORTED; }
static nxnet_status_t kb_ipv4(uint32_t idx, const nxnet_ipv4_config_t *cfg) {
    (void)idx; (void)cfg; return NXNET_ERR_UNSUPPORTED;
}
static nxnet_status_t kb_hostname_set(const char *name) { (void)name; return NXNET_ERR_UNSUPPORTED; }

#else /* NX_HOST_PREVIEW */
/* ---- PREVIEW ONLY: demo backend -------------------------------------
 * Fake data for UI review in desktop/tools/preview. Never in the OS.
 * Demo rules: joining any listed network with a valid passphrase
 * succeeds, except the passphrase "incorrect" which fails with
 * NXNET_ERR_AUTH so the error UI can be reviewed. */

static int nn_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

#define DEMO_IFS 3
static nxnet_interface_t   d_if[DEMO_IFS];
static nxnet_wifi_network_t d_scan[8];
static uint32_t            d_scan_n;
static nxnet_wifi_status_t d_wifi;
static char                d_hostname[NXNET_HOSTNAME_MAX + 1];
static int                 d_unavailable;
static int                 d_ready;

static nxnet_ipv4_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    nxnet_ipv4_t r = {{a, b, c, d}};
    return r;
}

static void demo_dhcp(nxnet_interface_t *i, uint8_t host) {
    i->ipv4.method = NXNET_IPV4_DHCP;
    i->ipv4.address = ip4(192, 168, 1, host);
    i->ipv4.netmask = ip4(255, 255, 255, 0);
    i->ipv4.gateway = ip4(192, 168, 1, 1);
    i->ipv4.dns[0] = ip4(192, 168, 1, 1);
    i->ipv4.dns[1] = ip4(1, 1, 1, 1);
    i->ipv4.dns_count = 2;
    i->ipv4.lease_seconds = 86400;
}

static void demo_scan_add(const char *ssid, int8_t rssi, uint8_t ch, nxnet_wifi_security_t sec, int known) {
    if (d_scan_n >= sizeof(d_scan) / sizeof(d_scan[0])) return;
    nxnet_wifi_network_t *n = &d_scan[d_scan_n];
    nn_zero(n, sizeof(*n));
    nn_strcpy(n->ssid, ssid, sizeof(n->ssid));
    n->rssi_dbm = rssi;
    n->channel = ch;
    n->security = sec;
    n->known = (uint8_t)known;
    for (int k = 0; k < 6; k++) n->bssid.b[k] = (uint8_t)(0x10 * (d_scan_n + 1) + k);
    d_scan_n++;
}

static void demo_mark_current(void) {
    for (uint32_t i = 0; i < d_scan_n; i++)
        d_scan[i].current = (d_wifi.state == NXNET_WIFI_CONNECTED && nn_streq(d_scan[i].ssid, d_wifi.ssid));
}

static void demo_reset(void) {
    nn_zero(d_if, sizeof(d_if));
    nxnet_interface_t *e = &d_if[0], *w = &d_if[1], *l = &d_if[2];

    e->index = 0; nn_strcpy(e->name, "eth0", sizeof(e->name));
    e->kind = NXNET_IF_ETHERNET; e->state = NXNET_IF_CONNECTED; e->link_up = 1; e->primary = 1;
    { const uint8_t m[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56}; for (int k = 0; k < 6; k++) e->mac.b[k] = m[k]; }
    e->speed_mbps = 1000;
    demo_dhcp(e, 42);
    e->counters.rx_bytes = 1834221568ull; e->counters.tx_bytes = 209715200ull;
    e->counters.rx_packets = 1452210; e->counters.tx_packets = 611032;

    w->index = 1; nn_strcpy(w->name, "wlan0", sizeof(w->name));
    w->kind = NXNET_IF_WIFI; w->state = NXNET_IF_CONNECTED; w->link_up = 1;
    { const uint8_t m[6] = {0x3c, 0x22, 0xfb, 0x8a, 0x41, 0x07}; for (int k = 0; k < 6; k++) w->mac.b[k] = m[k]; }
    w->speed_mbps = 866;
    demo_dhcp(w, 57);
    w->counters.rx_bytes = 52428800ull; w->counters.tx_bytes = 8388608ull;

    l->index = 2; nn_strcpy(l->name, "lo", sizeof(l->name));
    l->kind = NXNET_IF_LOOPBACK; l->state = NXNET_IF_CONNECTED; l->link_up = 1;
    l->ipv4.method = NXNET_IPV4_MANUAL; l->ipv4.address = ip4(127, 0, 0, 1); l->ipv4.netmask = ip4(255, 0, 0, 0);

    d_scan_n = 0;
    demo_scan_add("Ketivee Lab", -46, 36, NXNET_WIFI_SEC_WPA2, 1);
    demo_scan_add("Ketivee Guest", -58, 6, NXNET_WIFI_SEC_OPEN, 0);
    demo_scan_add("Neolyx-5G", -64, 149, NXNET_WIFI_SEC_WPA3, 0);
    demo_scan_add("Sharma Family", -73, 11, NXNET_WIFI_SEC_WPA2, 0);
    demo_scan_add("Cafe Aroma", -81, 1, NXNET_WIFI_SEC_OPEN, 0);
    demo_scan_add("DIRECT-42-Printer", -87, 6, NXNET_WIFI_SEC_WPA2, 0);

    nn_zero(&d_wifi, sizeof(d_wifi));
    d_wifi.state = NXNET_WIFI_CONNECTED;
    d_wifi.if_index = 1;
    nn_strcpy(d_wifi.ssid, "Ketivee Lab", sizeof(d_wifi.ssid));
    d_wifi.rssi_dbm = -46; d_wifi.channel = 36; d_wifi.security = NXNET_WIFI_SEC_WPA2;
    demo_mark_current();

    nn_strcpy(d_hostname, "neolyx-workstation", sizeof(d_hostname));
    d_ready = 1;
}

static void demo_update_primary(void) {
    d_if[0].primary = d_if[0].state == NXNET_IF_CONNECTED;
    d_if[1].primary = !d_if[0].primary && d_if[1].state == NXNET_IF_CONNECTED;
}

void nxnet_preview_set_unavailable(void) { d_unavailable = 1; g_inited = 0; nxnet_init(); bump(); }
void nxnet_preview_set_available(void)   { d_unavailable = 0; demo_reset(); g_inited = 0; nxnet_init(); bump(); }
void nxnet_preview_wifi_off(void)        { if (!d_ready) demo_reset(); nxnet_wifi_set_power(0); }

static nxnet_status_t kb_probe(void) {
    if (d_unavailable) return NXNET_ERR_UNSUPPORTED;
    if (!d_ready) demo_reset();
    return NXNET_OK;
}

static nxnet_status_t kb_interfaces(nxnet_interface_t *out, uint32_t cap, uint32_t *count) {
    *count = DEMO_IFS;
    if (cap < DEMO_IFS) return NXNET_ERR_BUFFER_TOO_SMALL;
    for (uint32_t i = 0; i < DEMO_IFS; i++) out[i] = d_if[i];
    return NXNET_OK;
}

static nxnet_status_t kb_hostname(char *buf, size_t cap) {
    if (nn_strnlen(d_hostname, sizeof(d_hostname)) + 1 > cap) return NXNET_ERR_BUFFER_TOO_SMALL;
    nn_strcpy(buf, d_hostname, cap);
    return NXNET_OK;
}

static nxnet_status_t kb_wifi_status(nxnet_wifi_status_t *out) { *out = d_wifi; return NXNET_OK; }

static nxnet_status_t kb_wifi_results(nxnet_wifi_network_t *out, uint32_t cap, uint32_t *count) {
    if (d_wifi.state == NXNET_WIFI_OFF) return NXNET_ERR_STATE;
    *count = d_scan_n;
    uint32_t n = d_scan_n < cap ? d_scan_n : cap;
    for (uint32_t i = 0; i < n; i++) out[i] = d_scan[i];
    return d_scan_n > cap ? NXNET_ERR_BUFFER_TOO_SMALL : NXNET_OK;
}

static nxnet_status_t kb_wifi_scan(void) {
    return d_wifi.state == NXNET_WIFI_OFF ? NXNET_ERR_STATE : NXNET_OK;
}

static nxnet_status_t kb_wifi_power(int on) {
    if (on) {
        if (d_wifi.state != NXNET_WIFI_OFF) return NXNET_OK;
        d_wifi.state = NXNET_WIFI_DISCONNECTED;
        d_wifi.ssid[0] = '\0';
        d_if[1].state = NXNET_IF_NO_LINK; d_if[1].link_up = 0;
    } else {
        d_wifi.state = NXNET_WIFI_OFF;
        d_wifi.ssid[0] = '\0';
        d_if[1].state = NXNET_IF_DISABLED; d_if[1].link_up = 0;
        nn_zero(&d_if[1].ipv4.address, sizeof(nxnet_ipv4_t));
    }
    demo_mark_current();
    demo_update_primary();
    return NXNET_OK;
}

static nxnet_status_t kb_wifi_join(const char *ssid, const char *pass, nxnet_wifi_security_t sec) {
    if (d_wifi.state == NXNET_WIFI_OFF) return NXNET_ERR_STATE;
    const nxnet_wifi_network_t *found = 0;
    for (uint32_t i = 0; i < d_scan_n; i++) if (nn_streq(d_scan[i].ssid, ssid)) found = &d_scan[i];
    if (!found) return NXNET_ERR_NOT_FOUND;
    if (found->security != sec) return NXNET_ERR_INVALID_ARG;
    if (pass && nn_streq(pass, "incorrect")) return NXNET_ERR_AUTH;
    d_wifi.state = NXNET_WIFI_CONNECTED;
    nn_strcpy(d_wifi.ssid, ssid, sizeof(d_wifi.ssid));
    d_wifi.rssi_dbm = found->rssi_dbm; d_wifi.channel = found->channel; d_wifi.security = sec;
    for (uint32_t i = 0; i < d_scan_n; i++) if (nn_streq(d_scan[i].ssid, ssid)) d_scan[i].known = 1;
    d_if[1].state = NXNET_IF_CONNECTED; d_if[1].link_up = 1;
    demo_dhcp(&d_if[1], 57);
    demo_mark_current();
    demo_update_primary();
    return NXNET_OK;
}

static nxnet_status_t kb_wifi_disconnect(void) {
    if (d_wifi.state != NXNET_WIFI_CONNECTED) return NXNET_ERR_STATE;
    d_wifi.state = NXNET_WIFI_DISCONNECTED;
    d_wifi.ssid[0] = '\0';
    d_if[1].state = NXNET_IF_NO_LINK; d_if[1].link_up = 0;
    demo_mark_current();
    demo_update_primary();
    return NXNET_OK;
}

static nxnet_status_t kb_wifi_forget(const char *ssid) {
    for (uint32_t i = 0; i < d_scan_n; i++) {
        if (nn_streq(d_scan[i].ssid, ssid)) {
            if (!d_scan[i].known) return NXNET_ERR_NOT_FOUND;
            d_scan[i].known = 0;
            if (d_scan[i].current) kb_wifi_disconnect();
            return NXNET_OK;
        }
    }
    return NXNET_ERR_NOT_FOUND;
}

static nxnet_status_t kb_ipv4(uint32_t idx, const nxnet_ipv4_config_t *cfg) {
    if (idx >= DEMO_IFS || d_if[idx].kind == NXNET_IF_LOOPBACK) return NXNET_ERR_NOT_FOUND;
    if (cfg->method == NXNET_IPV4_DHCP) demo_dhcp(&d_if[idx], idx == 0 ? 42 : 57);
    else { d_if[idx].ipv4 = *cfg; d_if[idx].ipv4.lease_seconds = 0; }
    return NXNET_OK;
}

static nxnet_status_t kb_hostname_set(const char *name) {
    nn_strcpy(d_hostname, name, sizeof(d_hostname));
    return NXNET_OK;
}
#endif /* NX_HOST_PREVIEW */

/* ============ Public API ============ */

nxnet_status_t nxnet_init(void) {
    nxnet_status_t st = kb_probe();
    if (st == NXNET_OK) {
        if (g_avail != NXNET_OK) nn_log("[NXNET] networking backend available\n");
        g_logged_unsupported = 0;
    } else if (!g_logged_unsupported) {
        nn_log("[NXNET] kernel exposes no network status syscalls; networking UI shows "
               "'unavailable' (see docs/desktop/proposals/net.md)\n");
        g_logged_unsupported = 1;
    }
    if (st != g_avail) bump();
    g_avail = st;
    g_inited = 1;
    return st;
}

nxnet_status_t nxnet_available(void) {
    if (!g_inited) return nxnet_init();
    return g_avail;
}

uint32_t nxnet_generation(void) { return g_generation; }

/* Gate for every backend call: lazily probes and reports UNSUPPORTED. */
static nxnet_status_t ready(void) { return nxnet_available(); }

nxnet_status_t nxnet_interfaces(nxnet_interface_t *out, uint32_t cap, uint32_t *count) {
    if (!count || (cap > 0 && !out)) return NXNET_ERR_INVALID_ARG;
    *count = 0;
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    nxnet_interface_t tmp[NXNET_MAX_INTERFACES];
    uint32_t n = 0;
    nn_zero(tmp, sizeof(tmp));
    st = kb_interfaces(tmp, NXNET_MAX_INTERFACES, &n);
    if (st != NXNET_OK) return st;
    if (n > NXNET_MAX_INTERFACES) return NXNET_ERR_IO;
    *count = n;
    uint32_t copy = n < cap ? n : cap;
    for (uint32_t i = 0; i < copy; i++) out[i] = tmp[i];
    return n > cap ? NXNET_ERR_BUFFER_TOO_SMALL : NXNET_OK;
}

nxnet_status_t nxnet_interface_get(uint32_t index, nxnet_interface_t *out) {
    if (!out) return NXNET_ERR_INVALID_ARG;
    nn_zero(out, sizeof(*out));
    nxnet_interface_t all[NXNET_MAX_INTERFACES];
    uint32_t n = 0;
    nxnet_status_t st = nxnet_interfaces(all, NXNET_MAX_INTERFACES, &n);
    if (st != NXNET_OK) return st;
    for (uint32_t i = 0; i < n; i++) {
        if (all[i].index == index) { *out = all[i]; return NXNET_OK; }
    }
    return NXNET_ERR_NOT_FOUND;
}

nxnet_status_t nxnet_hostname_get(char *buf, size_t cap) {
    if (!buf || cap == 0) return NXNET_ERR_INVALID_ARG;
    buf[0] = '\0';
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    st = kb_hostname(buf, cap);
    if (st != NXNET_OK) { buf[0] = '\0'; return st; }
    if (nxnet_validate_hostname(buf) != NXNET_OK) { buf[0] = '\0'; return NXNET_ERR_IO; }
    return NXNET_OK;
}

nxnet_status_t nxnet_wifi_status(nxnet_wifi_status_t *out) {
    if (!out) return NXNET_ERR_INVALID_ARG;
    nn_zero(out, sizeof(*out));
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    st = kb_wifi_status(out);
    if (st != NXNET_OK) { nn_zero(out, sizeof(*out)); return st; }
    out->ssid[NXNET_SSID_MAX] = '\0';
    return NXNET_OK;
}

nxnet_status_t nxnet_wifi_scan_results(nxnet_wifi_network_t *out, uint32_t cap, uint32_t *count) {
    if (!count || (cap > 0 && !out)) return NXNET_ERR_INVALID_ARG;
    *count = 0;
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    nxnet_wifi_network_t tmp[NXNET_MAX_SCAN];
    uint32_t n = 0;
    nn_zero(tmp, sizeof(tmp));
    st = kb_wifi_results(tmp, NXNET_MAX_SCAN, &n);
    if (st != NXNET_OK && st != NXNET_ERR_BUFFER_TOO_SMALL) return st;
    if (n > NXNET_MAX_SCAN) n = NXNET_MAX_SCAN;
    /* Strongest first (insertion sort; n <= 32). Hidden/empty SSIDs dropped. */
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) {
        tmp[i].ssid[NXNET_SSID_MAX] = '\0';
        if (nxnet_validate_ssid(tmp[i].ssid) != NXNET_OK) continue;
        nxnet_wifi_network_t v = tmp[i];
        uint32_t j = m;
        while (j > 0 && tmp[j - 1].rssi_dbm < v.rssi_dbm) { tmp[j] = tmp[j - 1]; j--; }
        tmp[j] = v;
        m++;
    }
    *count = m;
    uint32_t copy = m < cap ? m : cap;
    for (uint32_t i = 0; i < copy; i++) out[i] = tmp[i];
    return m > cap ? NXNET_ERR_BUFFER_TOO_SMALL : NXNET_OK;
}

nxnet_status_t nxnet_wifi_scan(void) {
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    st = kb_wifi_scan();
    if (st == NXNET_OK) bump();
    return st;
}

nxnet_status_t nxnet_wifi_set_power(int on) {
    if (on != 0 && on != 1) return NXNET_ERR_INVALID_ARG;
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    nn_log(on ? "[NXNET] Wi-Fi power on requested\n" : "[NXNET] Wi-Fi power off requested\n");
    st = kb_wifi_power(on);
    if (st != NXNET_OK) nn_log2("[NXNET] Wi-Fi power change failed: ", nxnet_status_str(st));
    bump();
    return st;
}

nxnet_status_t nxnet_wifi_join(const char *ssid, const char *passphrase, nxnet_wifi_security_t security) {
    nxnet_status_t st = nxnet_validate_ssid(ssid);
    if (st != NXNET_OK) return st;
    st = nxnet_validate_passphrase(passphrase, security);
    if (st != NXNET_OK) return st;
    st = ready();
    if (st != NXNET_OK) return st;

    /* Private copy so the backend never sees caller memory that may change;
     * wiped before returning. Passphrases are never logged. */
    char pass[NXNET_PASSPHRASE_MAX + 2];
    nn_strcpy(pass, passphrase ? passphrase : "", sizeof(pass));
    nn_log2("[NXNET] Wi-Fi join requested: ", ssid);
    st = kb_wifi_join(ssid, pass, security);
    nn_zero(pass, sizeof(pass));
    nn_log2(st == NXNET_OK ? "[NXNET] Wi-Fi joined: " : "[NXNET] Wi-Fi join failed: ",
            st == NXNET_OK ? ssid : nxnet_status_str(st));
    bump();
    return st;
}

nxnet_status_t nxnet_wifi_disconnect(void) {
    nxnet_status_t st = ready();
    if (st != NXNET_OK) return st;
    nn_log("[NXNET] Wi-Fi disconnect requested\n");
    st = kb_wifi_disconnect();
    bump();
    return st;
}

nxnet_status_t nxnet_wifi_forget(const char *ssid) {
    nxnet_status_t st = nxnet_validate_ssid(ssid);
    if (st != NXNET_OK) return st;
    st = ready();
    if (st != NXNET_OK) return st;
    nn_log2("[NXNET] Wi-Fi forget requested: ", ssid);
    st = kb_wifi_forget(ssid);
    bump();
    return st;
}

nxnet_status_t nxnet_ipv4_configure(uint32_t if_index, const nxnet_ipv4_config_t *cfg) {
    if (!cfg || if_index >= NXNET_MAX_INTERFACES) return NXNET_ERR_INVALID_ARG;
    nxnet_status_t st = nxnet_ipv4_config_validate(cfg, 0);
    if (st != NXNET_OK) return st;
    st = ready();
    if (st != NXNET_OK) return st;
    nn_log(cfg->method == NXNET_IPV4_DHCP ? "[NXNET] IPv4 config change: DHCP\n"
                                          : "[NXNET] IPv4 config change: manual\n");
    st = kb_ipv4(if_index, cfg);
    if (st != NXNET_OK) nn_log2("[NXNET] IPv4 config failed: ", nxnet_status_str(st));
    bump();
    return st;
}

nxnet_status_t nxnet_hostname_set(const char *name) {
    nxnet_status_t st = nxnet_validate_hostname(name);
    if (st != NXNET_OK) return st;
    st = ready();
    if (st != NXNET_OK) return st;
    nn_log2("[NXNET] hostname change requested: ", name);
    st = kb_hostname_set(name);
    bump();
    return st;
}

nxnet_status_t nxnet_summary(nxnet_summary_t *out) {
    if (!out) return NXNET_ERR_INVALID_ARG;
    nn_zero(out, sizeof(*out));
    out->primary_index = -1;
    nxnet_interface_t ifs[NXNET_MAX_INTERFACES];
    uint32_t n = 0;
    nxnet_status_t st = nxnet_interfaces(ifs, NXNET_MAX_INTERFACES, &n);
    if (st != NXNET_OK) { nn_zero(out, sizeof(*out)); out->primary_index = -1; return st; }

    nxnet_summary_t s;
    nn_zero(&s, sizeof(s));
    s.primary_index = -1;
    s.connectivity = NXNET_CONN_OFFLINE;
    const nxnet_interface_t *best = 0;
    for (uint32_t i = 0; i < n; i++) {
        const nxnet_interface_t *f = &ifs[i];
        if (f->kind == NXNET_IF_LOOPBACK) continue;
        if (f->kind == NXNET_IF_WIFI) s.wifi_present = 1;
        if (!f->link_up) continue;
        if (!best || f->primary || (!best->primary && f->state == NXNET_IF_CONNECTED && best->state != NXNET_IF_CONNECTED))
            best = f;
    }
    if (best) {
        s.primary_index = (int32_t)best->index;
        s.primary_kind = best->kind;
        if (best->state != NXNET_IF_CONNECTED || nxnet_ipv4_is_zero(best->ipv4.address)) {
            s.connectivity = NXNET_CONN_LINK_ONLY;
        } else {
            s.primary_address = best->ipv4.address;
            s.connectivity = nxnet_ipv4_is_zero(best->ipv4.gateway) ? NXNET_CONN_LOCAL : NXNET_CONN_ROUTABLE;
        }
    }
    s.wifi_state = NXNET_WIFI_OFF;
    if (s.wifi_present) {
        nxnet_wifi_status_t w;
        if (nxnet_wifi_status(&w) == NXNET_OK) {
            s.wifi_state = w.state;
            if (w.state == NXNET_WIFI_CONNECTED) {
                s.wifi_bars = nxnet_rssi_bars(w.rssi_dbm);
                nn_strcpy(s.wifi_ssid, w.ssid, sizeof(s.wifi_ssid));
            }
        }
    }
    *out = s;
    return NXNET_OK;
}
