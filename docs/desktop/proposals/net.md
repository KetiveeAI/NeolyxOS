# Proposal: kernel syscalls for desktop networking

Status: **proposal, not implemented**. Owner: Networking area (desktop).
Kernel changes need approval from the kernel owner before work starts.

## Why

The desktop needs to show and change network state: Settings > Network,
the menu bar indicator, and the Control Center Wi-Fi tile. The userspace
client for this is `desktop/include/nxnet.h`. Today it returns
`NXNET_ERR_UNSUPPORTED` for every request, because the kernel has no
syscall that exposes this state:

| What the desktop needs | Kernel state today | Reachable from userspace? |
|---|---|---|
| Interface list, kind, MAC | `net_interface_t` list in `kernel/src/net/network.c` (`lo`, `ethN` from e1000) | No |
| Link up/down, speed | e1000 `E1000_STATUS` is read once at init (`src/drivers/e1000.c`). Link is logged but not stored, and speed is not read | No |
| IPv4 address, mask, gateway, DNS | `net_interface_t.ip/netmask/gateway/dns` (one DNS server) | No |
| DHCP or manual | `network_dhcp_request` / `network_set_ip`, but the method is not recorded | No |
| Traffic counters | `rx_bytes`, `tx_bytes`, packets, errors in `net_interface_t` | No |
| Wi-Fi status, scan, join, forget | `wifi_device_t` in `src/drivers/wifi.c` (`wifi_scan`, `wifi_connect`, saved networks) | No |
| Hostname | Not stored anywhere | No |
| Sockets | `socket` … `closesock` (40–47) | Yes, but they carry no status information |
| `sys_info` (50) | OS name, version, memory, uptime | Yes, but it has no network fields |

### Kernel problems found during the investigation (report only, not fixed)

1. `wifi_connect()` in `src/drivers/wifi.c` reports success without any
   handshake (`/* TODO: Actual connection handshake */ /* Simulate success */`).
   If a syscall exposes this as it stands, the UI will show
   "Connected" for a network that was never joined. `wifi.join` must
   not ship until this returns a real result, or returns `-ENOSYS`.
2. Wi-Fi devices fill in `dev->interface` but never call
   `network_register()`, so `wlanN` is missing from the interface list.
3. There are two `network_init()` definitions:
   `src/net/network.c` (real) and `src/network/network.c` (a stub that
   prints "Not implemented yet"). The Makefile builds both objects
   (`network_core.o`, `network.o`). Check which one gets linked.
4. `wifi_get_saved_networks()` keeps passphrases in plain kernel memory.
   This is acceptable for now, but a passphrase must never be copied
   back to userspace.
5. `network_register()` and `network_init()` call `kzalloc` without
   checking for NULL.

## Proposed syscalls

The numbers below are **tentative**. 133+ is currently unused in
`kernel/include/core/syscall_table.def`; the kernel owner allocates the
final numbers. The format matches the existing table:

```c
/* Networking (140-147) */
NX_SYSCALL(140, "net.if_list",      sys_net_if_list_impl,      3, PRIV_USER,   0)
NX_SYSCALL(141, "net.if_config",    sys_net_if_config_impl,    2, PRIV_SYSTEM, NX_FLAG_AUDIT)
NX_SYSCALL(142, "net.hostname_get", sys_net_hostname_get_impl, 2, PRIV_USER,   0)
NX_SYSCALL(143, "net.hostname_set", sys_net_hostname_set_impl, 1, PRIV_SYSTEM, NX_FLAG_AUDIT)
NX_SYSCALL(144, "wifi.status",      sys_wifi_status_impl,      1, PRIV_USER,   0)
NX_SYSCALL(145, "wifi.scan",        sys_wifi_scan_impl,        3, PRIV_USER,   NX_FLAG_ASYNC)
NX_SYSCALL(146, "wifi.join",        sys_wifi_join_impl,        1, PRIV_SYSTEM, NX_FLAG_AUDIT)
NX_SYSCALL(147, "wifi.ctl",         sys_wifi_ctl_impl,         2, PRIV_SYSTEM, NX_FLAG_AUDIT)
```

Errors use the existing negative errno values from `core/syscall.h`
(`-EFAULT`, `-EINVAL`, `-EPERM`, `-ENODEV`, `-EBUSY`, `-ERANGE`,
`-ENOSYS`). The client maps them to `nxnet_status_t`: `-EPERM` becomes
`NXNET_ERR_PERMISSION`, `-ENOSYS` becomes `NXNET_ERR_UNSUPPORTED`, and so on.

### Argument structs (`kernel/include/net/nx_net_abi.h`, new)

Every struct starts with `uint32_t size`, which the caller sets to
`sizeof`. The kernel rejects a size it does not know (`-EINVAL`), so the
struct can be extended later without breaking the ABI. The structs use
fixed-width fields only and contain no pointers, except where a field is
documented as a user buffer.

```c
typedef struct {
    uint32_t size;              /* = sizeof(nx_net_if_info_t) */
    uint32_t index;
    char     name[16];
    uint8_t  kind;              /* 0 ethernet, 1 wifi, 2 loopback */
    uint8_t  state;             /* 0 disabled, 1 no link, 2 configuring, 3 connected, 4 error */
    uint8_t  link_up;
    uint8_t  primary;           /* carries the default route */
    uint8_t  mac[6];
    uint8_t  ipv4_method;       /* 0 DHCP, 1 manual  (kernel must start recording this) */
    uint8_t  dns_count;         /* 0..3 */
    uint32_t speed_mbps;        /* 0 = unknown (e1000: STATUS bits 7:6) */
    uint8_t  addr[4], mask[4], gateway[4];
    uint8_t  dns[3][4];
    uint32_t lease_seconds;
    uint64_t rx_bytes, tx_bytes, rx_packets, tx_packets, rx_errors, tx_errors;
} nx_net_if_info_t;

typedef struct {
    uint32_t size;
    uint8_t  method;            /* 0 DHCP, 1 manual */
    uint8_t  dns_count;
    uint8_t  _pad[2];
    uint8_t  addr[4], mask[4], gateway[4];
    uint8_t  dns[3][4];
} nx_net_ipv4_cfg_t;

typedef struct {
    uint32_t size;
    uint8_t  state;             /* 0 off .. 5 error (see nxnet_wifi_state_t) */
    uint8_t  security;          /* 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA3 */
    int8_t   rssi_dbm;
    uint8_t  channel;
    uint32_t if_index;
    char     ssid[33];
    uint8_t  _pad[3];
} nx_wifi_status_t;

typedef struct {               /* one scan result; never contains secrets */
    char     ssid[33];
    uint8_t  bssid[6];
    int8_t   rssi_dbm;
    uint8_t  channel;
    uint8_t  security;
    uint8_t  known;
    uint8_t  current;
} nx_wifi_ap_t;

typedef struct {
    uint32_t size;
    char     ssid[33];
    uint8_t  security;
    uint8_t  save;              /* store in saved networks */
    uint8_t  _pad;
    char     passphrase[65];    /* 0, 8..63 printable, or 64 hex; zeroed by the kernel after use */
    uint8_t  _pad2[3];
} nx_wifi_join_t;
```

### Semantics

- **`net.if_list(buf, cap, out_count)`**: copies up to `cap`
  `nx_net_if_info_t` entries and writes the total to `*out_count`.
  Returns the number copied, or `-ERANGE` when `cap` is too small (the
  total is still written). Every pointer is checked with
  `syscall_validate_ptr`.
- **`net.if_config(if_index, cfg)`**: PRIV_SYSTEM and audited. The kernel
  validates the request again (contiguous mask; address unicast and not
  the network or broadcast address; gateway in the subnet) and does not
  trust the client's checks. DHCP calls `network_dhcp_request`; manual
  calls `network_set_ip` and `network_set_dns`. The audit record has the
  interface, method and addresses.
- **`net.hostname_get(buf, len)` / `net.hostname_set(name)`**: the kernel
  stores one RFC 1123 label of at most 63 characters, `[A-Za-z0-9-]`.
  `hostname_set` is audited.
- **`wifi.status(out)`**: reads `wifi_device_t` for the first radio.
  Returns `-ENODEV` if there is no radio.
- **`wifi.scan(op, buf, cap)`**: `op=0` starts a scan and returns at once
  (`-EBUSY` if a scan is already running, `-EAGAIN` if the radio is off).
  `op=1` copies the last results as `nx_wifi_ap_t`. The kernel must not
  block the caller for the 5 s scan timeout that `wifi_scan()` uses today.
  Scans are rate-limited per process, for example to one every 5 s.
- **`wifi.join(req)`**: PRIV_SYSTEM and audited. The audit record has the
  SSID, security type and result, **never the passphrase**. The kernel
  copies the request, wipes the user-supplied passphrase from its own
  copy after the handshake, and returns `-EACCES` when authentication
  fails. Requires kernel problem 1 above to be fixed first.
- **`wifi.ctl(op, arg)`**: PRIV_SYSTEM and audited. The operations are
  `0` power off, `1` power on, `2` disconnect, and `3` forget, where
  `arg` is a user pointer to a NUL-terminated SSID of at most 32 bytes.

### Privilege model

Reads are PRIV_USER. Anything that changes system configuration is
PRIV_SYSTEM with `NX_FLAG_AUDIT`, the same as `reboot` and `rtc.set`. The
desktop shows `NXNET_ERR_PERMISSION` as "Administrator permission
required". This proposal does not decide how the desktop gets
elevation. That decision is a separate security choice for the project
owner.

## Client wiring once the syscalls exist

Only the `kb_*` functions in `desktop/lib/nxnet.c` change. The public
API, the Settings pane and the indicator stay as they are. `kb_probe()`
should call `abi.query` (syscall 0) and check a capability flag (for
example `NX_CAP_NET_INFO` in `nx_abi_info_t.flags`) before it calls
`net.if_list`. It must not issue an unallocated syscall number on an
older kernel.
