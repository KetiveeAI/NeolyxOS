/*
 * NeolyxOS - NXNet: userspace networking client API
 *
 * The one place desktop code asks about the network: interfaces, IPv4
 * configuration, Wi-Fi, connectivity, hostname and traffic counters.
 * Settings > Network, the menu bar indicator and the Control Center tile
 * all read through this API so they always agree.
 *
 * Contract:
 *   - Every call returns an nxnet_status_t. Output structs are written
 *     only when the call returns NXNET_OK; on any error they are left
 *     zeroed (never filled with guessed values).
 *   - All string inputs are validated before anything reaches the
 *     backend (SSID length, passphrase length, IPv4 syntax, hostname
 *     charset). Invalid input returns NXNET_ERR_INVALID_ARG.
 *   - Where the kernel has no path from userspace to the requested
 *     state, calls return NXNET_ERR_UNSUPPORTED and the client logs it
 *     once. The UI must show an honest "unavailable" state for it.
 *     (Kernel syscalls needed: docs/desktop/proposals/net.md.)
 *   - Configuration changes (IPv4, Wi-Fi join/forget/power, hostname)
 *     are privileged in the kernel (PRIV_SYSTEM + audit). They can
 *     return NXNET_ERR_PERMISSION.
 *   - Secrets (Wi-Fi passphrases) are never logged and are wiped from
 *     client buffers after use.
 *
 * Not thread-safe: call from the shell's UI thread.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXNET_H
#define NXNET_H

#include <stdint.h>
#include <stddef.h>

/* ============ Status codes ============ */

typedef enum {
    NXNET_OK                  =  0,
    NXNET_ERR_INVALID_ARG     = -1,  /* NULL pointer, bad index, malformed string */
    NXNET_ERR_UNSUPPORTED     = -2,  /* kernel exposes no path for this yet */
    NXNET_ERR_NOT_FOUND       = -3,  /* no such interface / network */
    NXNET_ERR_PERMISSION      = -4,  /* needs PRIV_SYSTEM */
    NXNET_ERR_BUFFER_TOO_SMALL= -5,  /* output capacity too small (count still reported) */
    NXNET_ERR_BUSY            = -6,  /* scan or join already in progress */
    NXNET_ERR_AUTH            = -7,  /* Wi-Fi authentication failed */
    NXNET_ERR_STATE           = -8,  /* wrong state, e.g. Wi-Fi is off */
    NXNET_ERR_IO              = -9,  /* driver or kernel failure */
    NXNET_ERR_TIMEOUT         = -10
} nxnet_status_t;

/* Short human-readable text for a status, never NULL. */
const char *nxnet_status_str(nxnet_status_t st);

/* ============ Limits ============ */

#define NXNET_IFNAME_MAX       16   /* incl. NUL ("eth0", "wlan0", "lo") */
#define NXNET_SSID_MAX         32   /* bytes, IEEE 802.11 */
#define NXNET_PASSPHRASE_MIN    8   /* WPA/WPA2/WPA3 personal */
#define NXNET_PASSPHRASE_MAX   63
#define NXNET_HOSTNAME_MAX     63   /* one DNS label */
#define NXNET_DNS_MAX           3
#define NXNET_MAX_INTERFACES    8
#define NXNET_MAX_SCAN         32   /* matches kernel WIFI_MAX_SCAN_RESULTS */
#define NXNET_IPV4_STR_MAX     16   /* "255.255.255.255" + NUL */
#define NXNET_MAC_STR_MAX      18   /* "aa:bb:cc:dd:ee:ff" + NUL */

/* ============ Addresses ============ */

typedef struct { uint8_t b[4]; } nxnet_ipv4_t;   /* network order: b[0] is first octet */
typedef struct { uint8_t b[6]; } nxnet_mac_t;

/* ============ Interfaces ============ */

typedef enum {
    NXNET_IF_ETHERNET = 0,
    NXNET_IF_WIFI     = 1,
    NXNET_IF_LOOPBACK = 2
} nxnet_if_kind_t;

typedef enum {
    NXNET_IF_DISABLED    = 0,  /* administratively down / radio off */
    NXNET_IF_NO_LINK     = 1,  /* up, no cable / not associated */
    NXNET_IF_CONFIGURING = 2,  /* link up, waiting for DHCP / associating */
    NXNET_IF_CONNECTED   = 3,  /* link up and IPv4 configured */
    NXNET_IF_ERROR       = 4
} nxnet_if_state_t;

typedef enum {
    NXNET_IPV4_DHCP   = 0,
    NXNET_IPV4_MANUAL = 1
} nxnet_ipv4_method_t;

typedef struct {
    nxnet_ipv4_method_t method;
    nxnet_ipv4_t address;
    nxnet_ipv4_t netmask;
    nxnet_ipv4_t gateway;          /* 0.0.0.0 = none */
    nxnet_ipv4_t dns[NXNET_DNS_MAX];
    uint32_t     dns_count;        /* 0..NXNET_DNS_MAX */
    uint32_t     lease_seconds;    /* DHCP only; 0 = unknown */
} nxnet_ipv4_config_t;

typedef struct {
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_errors, tx_errors;
} nxnet_counters_t;

typedef struct {
    uint32_t            index;                     /* stable for this boot */
    char                name[NXNET_IFNAME_MAX];
    nxnet_if_kind_t     kind;
    nxnet_if_state_t    state;
    uint8_t             link_up;                   /* carrier / associated */
    uint8_t             primary;                   /* carries the default route */
    uint8_t             _pad[2];
    nxnet_mac_t         mac;
    uint32_t            speed_mbps;                /* 0 = unknown */
    nxnet_ipv4_config_t ipv4;
    nxnet_counters_t    counters;
} nxnet_interface_t;

/* ============ Wi-Fi ============ */

typedef enum {
    NXNET_WIFI_SEC_OPEN = 0,
    NXNET_WIFI_SEC_WEP  = 1,
    NXNET_WIFI_SEC_WPA  = 2,
    NXNET_WIFI_SEC_WPA2 = 3,
    NXNET_WIFI_SEC_WPA3 = 4
} nxnet_wifi_security_t;

typedef enum {
    NXNET_WIFI_OFF          = 0,
    NXNET_WIFI_DISCONNECTED = 1,
    NXNET_WIFI_SCANNING     = 2,
    NXNET_WIFI_CONNECTING   = 3,
    NXNET_WIFI_CONNECTED    = 4,
    NXNET_WIFI_ERROR        = 5
} nxnet_wifi_state_t;

typedef struct {
    char                  ssid[NXNET_SSID_MAX + 1];
    nxnet_mac_t           bssid;
    int8_t                rssi_dbm;      /* e.g. -45 */
    uint8_t               channel;
    nxnet_wifi_security_t security;
    uint8_t               known;         /* saved network */
    uint8_t               current;       /* the network we are joined to */
    uint8_t               _pad[2];
} nxnet_wifi_network_t;

typedef struct {
    nxnet_wifi_state_t    state;
    uint32_t              if_index;      /* interface index of the radio */
    char                  ssid[NXNET_SSID_MAX + 1];   /* "" unless CONNECTED/CONNECTING */
    int8_t                rssi_dbm;
    uint8_t               channel;
    nxnet_wifi_security_t security;
} nxnet_wifi_status_t;

/* ============ Connectivity ============ */

typedef enum {
    NXNET_CONN_OFFLINE   = 0,  /* no interface with link */
    NXNET_CONN_LINK_ONLY = 1,  /* link but no usable IPv4 (e.g. DHCP failed) */
    NXNET_CONN_LOCAL     = 2,  /* IPv4 configured, no default gateway */
    NXNET_CONN_ROUTABLE  = 3   /* IPv4 + default gateway (internet not verified) */
} nxnet_connectivity_t;

/* A compact summary for status icons (menu bar, Control Center). */
typedef struct {
    nxnet_connectivity_t connectivity;
    int32_t              primary_index;   /* -1 if none */
    nxnet_if_kind_t      primary_kind;
    nxnet_wifi_state_t   wifi_state;      /* NXNET_WIFI_OFF if no radio */
    uint8_t              wifi_present;
    uint8_t              wifi_bars;       /* 0..4 */
    uint8_t              _pad[2];
    char                 wifi_ssid[NXNET_SSID_MAX + 1];
    nxnet_ipv4_t         primary_address;
} nxnet_summary_t;

/* ============ Lifecycle ============ */

/* Probe the backend. Idempotent; call again to retry after UNSUPPORTED.
 * Returns NXNET_OK when a networking backend answers. */
nxnet_status_t nxnet_init(void);

/* NXNET_OK if the backend is reachable, else the reason (usually UNSUPPORTED). */
nxnet_status_t nxnet_available(void);

/* Monotonic counter bumped whenever client-visible state changes
 * (refresh, join, config). UIs compare it to skip redundant work. */
uint32_t nxnet_generation(void);

/* ============ Queries ============ */

/* Fill up to cap interfaces. *count receives the total available even
 * when cap is too small (then returns NXNET_ERR_BUFFER_TOO_SMALL). */
nxnet_status_t nxnet_interfaces(nxnet_interface_t *out, uint32_t cap, uint32_t *count);

/* One interface by index (as reported in nxnet_interface_t.index). */
nxnet_status_t nxnet_interface_get(uint32_t index, nxnet_interface_t *out);

nxnet_status_t nxnet_summary(nxnet_summary_t *out);

nxnet_status_t nxnet_hostname_get(char *buf, size_t cap);

/* ============ Wi-Fi ============ */

nxnet_status_t nxnet_wifi_status(nxnet_wifi_status_t *out);

/* Results of the last scan (strongest first). Does not start a scan. */
nxnet_status_t nxnet_wifi_scan_results(nxnet_wifi_network_t *out, uint32_t cap, uint32_t *count);

/* Start a scan. NXNET_ERR_STATE if Wi-Fi is off, NXNET_ERR_BUSY if one is running. */
nxnet_status_t nxnet_wifi_scan(void);

/* Privileged. */
nxnet_status_t nxnet_wifi_set_power(int on);

/* Privileged. passphrase must be NULL/"" for OPEN, 8..63 printable ASCII
 * (or exactly 64 hex digits) for WPA/WPA2/WPA3. WEP is refused as insecure
 * (NXNET_ERR_UNSUPPORTED). The client wipes its copy after the call. */
nxnet_status_t nxnet_wifi_join(const char *ssid, const char *passphrase,
                               nxnet_wifi_security_t security);

nxnet_status_t nxnet_wifi_disconnect(void);       /* privileged */
nxnet_status_t nxnet_wifi_forget(const char *ssid);  /* privileged */

/* ============ Configuration (privileged) ============ */

/* Validated with nxnet_ipv4_config_validate() first. For DHCP only
 * .method is used. */
nxnet_status_t nxnet_ipv4_configure(uint32_t if_index, const nxnet_ipv4_config_t *cfg);

nxnet_status_t nxnet_hostname_set(const char *name);

/* ============ Validation and formatting (pure, no backend) ============ */

/* Strict dotted quad: exactly 4 decimal octets 0..255, no leading zeros
 * (except "0"), no spaces, no trailing characters. */
nxnet_status_t nxnet_ipv4_parse(const char *s, nxnet_ipv4_t *out);
nxnet_status_t nxnet_ipv4_format(nxnet_ipv4_t ip, char *buf, size_t cap);
nxnet_status_t nxnet_mac_format(nxnet_mac_t mac, char *buf, size_t cap);

int nxnet_ipv4_is_zero(nxnet_ipv4_t ip);
/* Contiguous ones from the top bit, not 0.0.0.0. /32 allowed. */
int nxnet_netmask_valid(nxnet_ipv4_t mask);
int nxnet_netmask_prefix(nxnet_ipv4_t mask);   /* -1 if not contiguous */

nxnet_status_t nxnet_validate_ssid(const char *ssid);
nxnet_status_t nxnet_validate_passphrase(const char *pass, nxnet_wifi_security_t sec);
/* RFC 1123 label: 1..63 of [A-Za-z0-9-], not starting/ending with '-'. */
nxnet_status_t nxnet_validate_hostname(const char *name);

/* Which field of a manual config is wrong (for inline UI errors). */
typedef enum {
    NXNET_FIELD_NONE = 0,
    NXNET_FIELD_ADDRESS,
    NXNET_FIELD_NETMASK,
    NXNET_FIELD_GATEWAY,
    NXNET_FIELD_DNS
} nxnet_field_t;

/* Manual configs: address is unicast and not the network/broadcast
 * address, netmask contiguous, gateway (if set) inside the subnet and
 * different from the address, DNS entries unicast. *bad_field (may be
 * NULL) names the first offending field. */
nxnet_status_t nxnet_ipv4_config_validate(const nxnet_ipv4_config_t *cfg, nxnet_field_t *bad_field);

/* RSSI to 0..4 bars (>= -55: 4, -66: 3, -77: 2, -88: 1, else 0). */
uint8_t nxnet_rssi_bars(int8_t rssi_dbm);

const char *nxnet_security_str(nxnet_wifi_security_t sec);   /* "WPA2 Personal" ... */
const char *nxnet_if_kind_str(nxnet_if_kind_t kind);          /* "Ethernet" ... */
const char *nxnet_if_state_str(nxnet_if_state_t state);       /* "Connected" ... */

/* ============ Host preview only ============ */
#ifdef NX_HOST_PREVIEW
/* Demo backend controls for UI review. Not present in OS builds. */
void nxnet_preview_set_unavailable(void);   /* behave like a kernel without net syscalls */
void nxnet_preview_set_available(void);     /* demo Ethernet + Wi-Fi (default) */
void nxnet_preview_wifi_off(void);
#endif

#endif /* NXNET_H */
