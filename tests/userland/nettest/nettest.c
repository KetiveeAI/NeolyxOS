/*
 * NeolyxOS nettest - end-to-end network test over the socket syscalls
 *
 *   0. Status: abi.query reports ABI 1.4 with NX_ABI_FLAG_NET, and
 *      net.if_list (133) lists every interface; eth0 is connected.
 *
 * Runs in the QEMU setup of boot_test.sh (e1000 on "-netdev user"), where
 * QEMU's built-in NAT puts the DNS proxy at 10.0.2.3 and the host at
 * 10.0.2.2. The kernel must already have an address from DHCP (it logs
 * "[DHCP] ACK" on the serial port); the test waits up to 20 s for one.
 *
 *   1. DNS: UDP query for example.com to 10.0.2.3:53, answer parsed here.
 *   2. Host HTTP: TCP to 10.0.2.2:8080 (a server on the host, if any),
 *      GET /. Reported as SKIP when nothing listens there.
 *   3. Internet HTTP: TCP to the address from step 1, port 80, GET /.
 *      "nettest A.B.C.D" uses that address instead when DNS fails.
 *
 * Results go to the serial log as "[NETTEST] PASS/FAIL/SKIP ...", ending
 * with "[NETTEST] RESULT: PASS" (status, DNS and internet HTTP worked) or
 * "[NETTEST] RESULT: FAIL". Exit code 0 on PASS, 1 on FAIL.
 *
 * Launch from the desktop's app drawer (/apps/nettest on the boot disk).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nx_abi.h"     /* kernel/include/core: abi.query result */
#include "nx_net.h"     /* kernel/include/core: net.if_list (ABI 1.4) */

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_SLEEP       2
#define SYS_TIME        3
#define SYS_SOCKET      40
#define SYS_CONNECT     44
#define SYS_SEND        45
#define SYS_RECV        46
#define SYS_CLOSESOCK   47
#define SYS_DEBUG_PRINT 127
#define SYS_ABI_QUERY   0
#define SYS_NET_IF_LIST 133

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define AF_INET         2

#define EIO             5
#define EAGAIN          11
#define ENXIO           6
#define ETIMEDOUT       110

typedef struct {
    uint16_t family;
    uint16_t port;          /* network byte order */
    uint8_t  addr[4];
    uint8_t  zero[8];
} sockaddr_in_t;

static inline int64_t sc1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sc2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sc3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

/* ============ Output ============ */

static char g_line[200];
static int g_len;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_sdec(int64_t v) {
    char buf[22];
    int i = 21;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 1) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) buf[--i] = '-';
    out_str(&buf[i]);
}

static void out_ip(const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++) {
        if (i) out_str(".");
        out_sdec(ip[i]);
    }
}

static void flush(void) {
    if (g_len == 0 || g_line[g_len - 1] != '\n') {
        if (g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = '\n';
    }
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

static uint64_t now_ms(void) {
    return (uint64_t)sc1(SYS_TIME, 0);
}

static void sleep_ms(uint32_t ms) {
    sc1(SYS_SLEEP, ms);
}

static void make_addr(sockaddr_in_t *a, const uint8_t ip[4], uint16_t port) {
    uint8_t *p = (uint8_t *)a;
    for (uint32_t i = 0; i < sizeof(*a); i++) p[i] = 0;
    a->family = AF_INET;
    a->port = (uint16_t)((port >> 8) | (port << 8));
    for (int i = 0; i < 4; i++) a->addr[i] = ip[i];
}

/* ============ DNS ============ */

static const uint8_t QEMU_DNS[4]  = { 10, 0, 2, 3 };
static const uint8_t QEMU_HOST[4] = { 10, 0, 2, 2 };

static int build_query(const char *name, uint16_t id, uint8_t *q) {
    int pos = 0;
    q[pos++] = (uint8_t)(id >> 8); q[pos++] = (uint8_t)id;
    q[pos++] = 0x01; q[pos++] = 0x00;           /* recursion desired */
    q[pos++] = 0; q[pos++] = 1;                 /* one question */
    for (int i = 0; i < 6; i++) q[pos++] = 0;
    const char *p = name;
    while (*p) {
        int n = 0;
        while (p[n] && p[n] != '.') n++;
        q[pos++] = (uint8_t)n;
        for (int i = 0; i < n; i++) q[pos++] = (uint8_t)p[i];
        p += n;
        if (*p == '.') p++;
    }
    q[pos++] = 0;
    q[pos++] = 0; q[pos++] = 1;                 /* A */
    q[pos++] = 0; q[pos++] = 1;                 /* IN */
    return pos;
}

static int skip_name(const uint8_t *r, int len, int pos) {
    for (int guard = 0; pos < len && guard < 128; guard++) {
        if (r[pos] == 0) return pos + 1;
        if ((r[pos] & 0xC0) == 0xC0) return pos + 2 <= len ? pos + 2 : -1;
        pos += r[pos] + 1;
    }
    return -1;
}

static int parse_a(const uint8_t *r, int len, uint16_t id, uint8_t out[4]) {
    if (len < 12 || r[0] != (uint8_t)(id >> 8) || r[1] != (uint8_t)id) return -1;
    if (!(r[2] & 0x80) || (r[3] & 0x0F)) return -1;
    int qd = (r[4] << 8) | r[5], an = (r[6] << 8) | r[7], pos = 12;
    for (int i = 0; i < qd; i++) {
        pos = skip_name(r, len, pos);
        if (pos < 0 || pos + 4 > len) return -1;
        pos += 4;
    }
    for (int i = 0; i < an; i++) {
        pos = skip_name(r, len, pos);
        if (pos < 0 || pos + 10 > len) return -1;
        int type = (r[pos] << 8) | r[pos + 1];
        int rdlen = (r[pos + 8] << 8) | r[pos + 9];
        pos += 10;
        if (pos + rdlen > len) return -1;
        if (type == 1 && rdlen == 4) {
            for (int k = 0; k < 4; k++) out[k] = r[pos + k];
            return 0;
        }
        pos += rdlen;
    }
    return -1;
}

/* Returns 0 with the address, or a negative code */
static int64_t dns_lookup(const char *name, uint8_t out[4]) {
    int64_t fd = sc2(SYS_SOCKET, SOCK_DGRAM, 0);
    if (fd < 0) return fd;

    sockaddr_in_t a;
    make_addr(&a, QEMU_DNS, 53);
    int64_t r = sc2(SYS_CONNECT, fd, (int64_t)&a);
    if (r < 0) { sc1(SYS_CLOSESOCK, fd); return r; }

    static uint8_t q[300], resp[512];
    int64_t result = -ETIMEDOUT;
    for (int attempt = 0; attempt < 3 && result != 0; attempt++) {
        uint16_t id = (uint16_t)(0x4E54 + attempt + (now_ms() & 0xFF));
        int qlen = build_query(name, id, q);
        r = sc3(SYS_SEND, fd, (int64_t)q, qlen);
        if (r < 0) { result = r; break; }

        uint64_t deadline = now_ms() + 2000;
        while (now_ms() < deadline) {
            int64_t n = sc3(SYS_RECV, fd, (int64_t)resp, sizeof(resp));
            if (n > 0) {
                result = parse_a(resp, (int)n, id, out) == 0 ? 0 : -EIO;
                break;
            }
            if (n != -EAGAIN) { result = n; break; }
            sleep_ms(10);
        }
    }
    sc1(SYS_CLOSESOCK, fd);
    return result;
}

/* ============ HTTP ============ */

static char g_status[96];

/*
 * GET / from ip:port. Returns the number of bytes received (> 0) with the
 * status line in g_status, or a negative code (connect or I/O error).
 */
static int64_t http_get(const uint8_t ip[4], uint16_t port, const char *host) {
    int64_t fd = sc2(SYS_SOCKET, SOCK_STREAM, 0);
    if (fd < 0) return fd;

    sockaddr_in_t a;
    make_addr(&a, ip, port);
    int64_t r = sc2(SYS_CONNECT, fd, (int64_t)&a);
    if (r < 0) { sc1(SYS_CLOSESOCK, fd); return r; }

    static char req[256];
    int n = 0;
    const char *parts[] = { "GET / HTTP/1.1\r\nHost: ", host,
                            "\r\nUser-Agent: NeolyxOS-nettest\r\nConnection: close\r\n\r\n", 0 };
    for (int i = 0; parts[i]; i++) {
        for (const char *p = parts[i]; *p && n < (int)sizeof(req) - 1; p++) req[n++] = *p;
    }

    int64_t sent = 0;
    while (sent < n) {
        r = sc3(SYS_SEND, fd, (int64_t)(req + sent), n - sent);
        if (r < 0) { sc1(SYS_CLOSESOCK, fd); return r; }
        sent += r;
    }

    static char buf[2048];
    int64_t total = 0;
    int status_len = 0;
    uint64_t deadline = now_ms() + 15000;
    for (;;) {
        int64_t got = sc3(SYS_RECV, fd, (int64_t)buf, sizeof(buf));
        if (got > 0) {
            for (int64_t i = 0; i < got && status_len >= 0; i++) {
                if (buf[i] == '\r' || buf[i] == '\n') { status_len = -1 - status_len; break; }
                if (status_len < (int)sizeof(g_status) - 1) g_status[status_len++] = buf[i];
            }
            total += got;
            deadline = now_ms() + 15000;
            continue;
        }
        if (got == 0) break;                    /* server closed: done */
        if (got != -EAGAIN) { if (total == 0) total = got; break; }
        if (now_ms() >= deadline) { if (total == 0) total = -ETIMEDOUT; break; }
        sleep_ms(10);
    }
    if (status_len < 0) status_len = -1 - status_len;
    g_status[status_len] = '\0';

    sc1(SYS_CLOSESOCK, fd);
    return total;
}

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

/* ============ Interface status (net.if_list) ============ */

static const char *state_name(uint8_t st) {
    switch (st) {
        case NX_NET_STATE_DISABLED:    return "disabled";
        case NX_NET_STATE_NO_LINK:     return "no link";
        case NX_NET_STATE_CONFIGURING: return "configuring";
        case NX_NET_STATE_CONNECTED:   return "connected";
        default:                       return "error";
    }
}

/* Lists interfaces; returns 1 when an Ethernet interface is connected
 * with an address, a gateway and at least one DNS server. */
static int check_if_list(void) {
    nx_abi_info_t abi;
    uint8_t *z = (uint8_t *)&abi;
    for (uint32_t i = 0; i < sizeof(abi); i++) z[i] = 0;
    int64_t r = sc1(SYS_ABI_QUERY, (int64_t)&abi);
    if (r != 0 || abi.major != 1 || abi.minor < 4 || !(abi.flags & NX_ABI_FLAG_NET)) {
        out_str("[NETTEST] FAIL abi: need 1.4 with NX_ABI_FLAG_NET (got ");
        out_sdec(abi.major); out_str("."); out_sdec(abi.minor);
        out_str(" flags "); out_sdec(abi.flags); out_str(")");
        flush();
        return 0;
    }

    /* Bad element size must be refused */
    static nx_net_if_info_t info[NX_NET_IF_MAX];
    int64_t bad = sc3(SYS_NET_IF_LIST, (int64_t)info, NX_NET_IF_MAX, 8);
    int64_t total = sc3(SYS_NET_IF_LIST, 0, 0, sizeof(nx_net_if_info_t));
    r = sc3(SYS_NET_IF_LIST, (int64_t)info, NX_NET_IF_MAX, sizeof(nx_net_if_info_t));
    if (bad != -22 || r < 0 || total != r) {
        out_str("[NETTEST] FAIL if_list (bad size -> "); out_sdec(bad);
        out_str(", count -> "); out_sdec(total); out_str(", list -> "); out_sdec(r); out_str(")");
        flush();
        return 0;
    }

    int ok = 0;
    for (int64_t i = 0; i < r && i < (int64_t)NX_NET_IF_MAX; i++) {
        const nx_net_if_info_t *f = &info[i];
        char name[17];
        for (int k = 0; k < 16; k++) name[k] = f->name[k];
        name[16] = '\0';
        out_str("[NETTEST] if "); out_str(name); out_str(": ");
        out_str(state_name(f->state));
        out_str(f->primary ? ", primary" : "");
        out_str(", "); out_ip(f->addr); out_str(" gw "); out_ip(f->gateway);
        out_str(" dns "); out_sdec(f->dns_count);
        out_str(", "); out_sdec(f->speed_mbps); out_str(" Mb/s, rx ");
        out_sdec((int64_t)f->rx_packets); out_str(" tx "); out_sdec((int64_t)f->tx_packets);
        flush();
        if (f->kind == NX_NET_IF_ETHERNET && f->state == NX_NET_STATE_CONNECTED && f->primary &&
            f->addr[0] != 0 && f->gateway[0] != 0 && f->dns_count > 0) ok = 1;
    }
    out_str(ok ? "[NETTEST] PASS if_list: Ethernet connected and primary"
               : "[NETTEST] FAIL if_list: no connected primary Ethernet interface");
    flush();
    return ok;
}

/* ============ Main ============ */

/* Parse a dotted-quad IPv4 address. Returns 0 on success. */
static int parse_ip(const char *s, uint8_t out[4]) {
    for (int i = 0; i < 4; i++) {
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 4) { v = v * 10 + (*s++ - '0'); digits++; }
        if (digits == 0 || v > 255) return -1;
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return -1; s++; }
    }
    return *s == '\0' ? 0 : -1;
}

void _start(long argc, char **argv) {
    const char *name = "example.com";

    /* "nettest A.B.C.D": use that address for the internet HTTP check
     * when DNS fails (diagnostics on networks that filter DNS) */
    uint8_t fallback_ip[4];
    int have_fallback = (argc >= 2 && argv && argv[1] && parse_ip(argv[1], fallback_ip) == 0);

    out_str("[NETTEST] start (QEMU user networking: DNS 10.0.2.3, host 10.0.2.2)");
    flush();

    /* 1. DNS (also waits for DHCP: before an address, sockets give -ENXIO) */
    uint8_t ip[4] = { 0, 0, 0, 0 };
    int64_t r = -ENXIO;
    uint64_t dhcp_deadline = now_ms() + 20000;
    while ((r = dns_lookup(name, ip)) == -ENXIO && now_ms() < dhcp_deadline) {
        sleep_ms(500);
    }

    /* 0. Interface status, now that DHCP has had its chance */
    int status_ok = check_if_list();
    int dns_ok = (r == 0);
    out_str(dns_ok ? "[NETTEST] PASS dns " : "[NETTEST] FAIL dns ");
    out_str(name);
    if (dns_ok) { out_str(" -> "); out_ip(ip); }
    else { out_str(" (error "); out_sdec(r); out_str(")"); }
    flush();

    /* 2. HTTP to a server on the host, if one is listening */
    r = http_get(QEMU_HOST, 8080, "10.0.2.2");
    if (r > 0 && starts_with(g_status, "HTTP/1.")) {
        out_str("[NETTEST] PASS host http 10.0.2.2:8080: \"");
        out_str(g_status);
        out_str("\", ");
        out_sdec(r);
        out_str(" bytes");
    } else if (r == -EIO) {
        out_str("[NETTEST] SKIP host http 10.0.2.2:8080: connection refused (no server on the host)");
    } else {
        out_str("[NETTEST] FAIL host http 10.0.2.2:8080 (result ");
        out_sdec(r);
        out_str(")");
    }
    flush();

    /* 3. HTTP to the internet */
    int web_ok = 0;
    if (!dns_ok && have_fallback) {
        for (int i = 0; i < 4; i++) ip[i] = fallback_ip[i];
        out_str("[NETTEST] using address from the command line: ");
        out_ip(ip);
        flush();
    }
    if (dns_ok || have_fallback) {
        r = http_get(ip, 80, name);
        web_ok = (r > 0 && starts_with(g_status, "HTTP/1."));
        out_str(web_ok ? "[NETTEST] PASS internet http " : "[NETTEST] FAIL internet http ");
        out_str(name);
        if (web_ok) {
            out_str(": \""); out_str(g_status); out_str("\", ");
            out_sdec(r); out_str(" bytes");
        } else {
            out_str(" (result "); out_sdec(r); out_str(")");
        }
    } else {
        out_str("[NETTEST] SKIP internet http: no address from DNS");
    }
    flush();

    /* PASS needs the whole path: status, DNS and a web page from the internet */
    int ok = status_ok && dns_ok && web_ok;
    out_str(ok ? "[NETTEST] RESULT: PASS" : "[NETTEST] RESULT: FAIL");
    flush();
    finish(ok ? 0 : 1);
}
