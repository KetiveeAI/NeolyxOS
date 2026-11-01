# Proposal: kernel syscalls for desktop networking

Status: `net.if_list` is implemented (syscall 133, ABI 1.4). The rest of
this page is a **proposal, not implemented**. Owner: Networking stream (N).
Kernel changes need approval from the kernel owner before work starts.

For the public beta, stream N owns syscalls 48-49 and 133-139 and ABI flag
0x0020 (`docs/kernel/syscall-registry.md`). The numbers 140-147 in the
draft table below predate that registry and are not allocated. The beta
batch proposal is the last section, "Beta ABI batch: socket semantics and
DNS resolve".

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
3. ~~There are two `network_init()` definitions.~~ Only `src/net/network.c`
   is linked (`network.o` is not in `obj-y`). Since the wired networking fix,
   the network service thread runs it at boot. See `docs/network/wired-networking.md`.
4. `wifi_get_saved_networks()` keeps passphrases in plain kernel memory.
   This is acceptable for now, but a passphrase must never be copied
   back to userspace.
5. ~~`network_register()` and `network_init()` call `kzalloc` without
   checking for NULL.~~ Fixed with the wired networking change.

## Proposed syscalls

> **Status:** `net.if_list` is implemented as syscall **133** (ABI 1.4,
> capability flag `NX_ABI_FLAG_NET`, 0x0020). The final signature differs from the
> draft below: it is `net.if_list(buf, cap, elem_size)` and returns the total
> number of interfaces. See `kernel/include/core/nx_net.h` and
> `docs/network/wired-networking.md`. The other calls are still proposals.

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

- **`net.if_list(buf, cap, out_count)`** (superseded, see the status note above): copies up to `cap`
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

## Beta ABI batch: socket semantics and DNS resolve

Status: **proposal for the Beta ABI batch (owner decision D2, 2026-11-06)**.
Stream N, for stream S6 (TLS and HTTP) and S4 (nxlibc). Nothing in this
section is implemented yet.

S6 needs three things from the kernel to fetch `https://host/path`: turn a
host name into an address, open TCP with known timeouts, and tell a refused
connection from a reset or a timeout. Today the socket calls (40-47) work
(see `docs/network/wired-networking.md`), but:

- their timeouts are fixed constants in `kernel/src/net/tcpip.c`;
- a refused connect and a reset both return `-EIO`, and "no address yet"
  returns `-ENXIO`;
- `recv` never blocks, so a client must spin on `-EAGAIN`;
- there is no DNS call from userspace (`dns_resolve()` is kernel-only);
- the address struct lives in `kernel/include/net/tcpip.h`, which userspace
  does not include.

### What this batch adds

| No. | Name | Args | Privilege | Purpose |
|---|---|---|---|---|
| 48 | `sock.setopt` | 3 | PRIV_USER | Set a per-socket timeout or flag |
| 49 | `sock.poll` | 3 | PRIV_USER | Wait until one of several sockets is ready, with a timeout |
| 134 | `net.resolve` | 5 | PRIV_USER | Host name to IPv4 addresses, through the DHCP-provided DNS servers |

135-139 stay free for later network calls. Capability: the existing
`NX_ABI_FLAG_NET` (0x0020). See "Detection" below.

### New public header: `kernel/include/core/nx_sock.h`

Userspace builds include `kernel/include/core` only, so the shared types
move there. The kernel's `sockaddr_in_t` becomes a typedef of the public
struct, and a `_Static_assert` keeps both at 16 bytes.

```c
#define NX_AF_INET        2
#define NX_SOCK_STREAM    1      /* TCP */
#define NX_SOCK_DGRAM     2      /* UDP */

typedef struct {
    uint16_t family;             /* NX_AF_INET */
    uint16_t port;               /* network byte order */
    uint8_t  addr[4];            /* network byte order */
    uint8_t  zero[8];            /* must be zero */
} nx_sockaddr_in_t;              /* 16 bytes, same layout as today */

/* sock.setopt names. Values are uint32_t passed by value. */
#define NX_SO_CONNECT_TIMEOUT_MS  1   /* default 16000, range 1..120000 */
#define NX_SO_SEND_TIMEOUT_MS     2   /* default 5000,  range 0..120000 */
#define NX_SO_RECV_TIMEOUT_MS     3   /* default 0 (never block), range 0..120000 */

/* sock.poll */
#define NX_POLL_IN    0x0001     /* data, end of stream, or an error to read */
#define NX_POLL_OUT   0x0002     /* connected and send buffer has room */
#define NX_POLL_ERR   0x0004     /* reset, refused, or timed out (revents only) */
#define NX_POLL_HUP   0x0008     /* peer closed its side (revents only) */
#define NX_POLL_MAX   16

typedef struct {
    int32_t  fd;
    uint16_t events;             /* NX_POLL_IN | NX_POLL_OUT */
    uint16_t revents;            /* written by the kernel */
} nx_sock_pollfd_t;              /* 8 bytes */

/* net.resolve */
#define NX_RESOLVE_MAX_NAME     253
#define NX_RESOLVE_MAX_ADDRS    8
#define NX_RESOLVE_TIMEOUT_MAX  30000
```

### New error codes (`kernel/include/core/syscall.h`)

The socket calls keep using negative error codes. These are new, numbered
next to the existing `ETIMEDOUT` (110). nxlibc's `errno.h` (S4) must use the
same values, and S3 should not reuse these numbers. nxlibc already has them
(checked with S4). nxlibc also uses `EDOM` 33, `EOVERFLOW` 75 and `EILSEQ` 84
for library-only errors, so the kernel keeps those numbers free:

| Code | Value | Returned when |
|---|---|---|
| `EADDRINUSE` | 98 | `bind` to a port another socket holds |
| `ENETUNREACH` | 101 | no interface has an address, or there is no route |
| `ECONNRESET` | 104 | the peer sent RST on an open connection |
| `EISCONN` | 106 | `connect` on a socket that is already connected |
| `ENOTCONN` | 107 | `send` or `recv` on a TCP socket that never connected |
| `ECONNREFUSED` | 111 | the peer answered the SYN with RST |
| `EHOSTUNREACH` | 113 | ARP got no answer for the next hop |

### Socket semantics after the batch

Numbers and arguments of 40-47 do not change. Defaults stay as they are
today, so existing programs behave the same except for the error codes in
the last column.

| Call | Behaviour | Errors |
|---|---|---|
| `connect` (44) | Blocks until the handshake finishes or the connect timeout ends. | `-ECONNREFUSED` (was `-EIO`), `-ETIMEDOUT`, `-ENETUNREACH` (was `-ENXIO`), `-EHOSTUNREACH`, `-EISCONN`, `-EINVAL`, `-EFAULT`, `-EBADF` |
| `send` (45) | Queues as much as fits, waiting up to the send timeout for room. Returns the bytes queued, which can be fewer than asked. A timeout of 0 never waits. | `-EAGAIN` (nothing queued), `-EPIPE` (we closed or the peer finished), `-ECONNRESET` (was `-EPIPE`), `-ENOTCONN` |
| `recv` (46) | Returns what is buffered. With a receive timeout above 0, waits up to that long for the first byte. Returns 0 at end of stream. | `-EAGAIN` (timeout, or nothing and timeout 0), `-ECONNRESET` (was `-EIO`), `-ENOTCONN` |
| `closesock` (47) | Unchanged: returns at once; the FIN handshake finishes in the background. | `-EBADF` |
| `listen`, `accept` | Unchanged: `-ENOSYS`. Server-side TCP is not in the beta. | |

**Buffers and end of stream** (today's behaviour, now part of the contract):

- Each TCP socket has a 32 KB receive buffer (`TCP_RX_CAPACITY`) and a 16 KB
  send buffer. The advertised window is the free space in the receive buffer,
  at most 32 KB. There is no window scaling, and 32 KB fits in the 16-bit
  window field. A full TLS record (16 KB + 256 bytes) always fits. The size
  must not drop below 32 KB.
- Bytes are delivered in order and never dropped at the socket. A FIN is
  accepted only once every byte before it has arrived. `recv` returns all
  buffered bytes first and returns 0 only when the buffer is empty and the
  peer's FIN has been seen. A TLS `close_notify` sent just before FIN is
  therefore always readable.
- After a reset, `recv` still returns the bytes that arrived before it, then
  `-ECONNRESET`. A program can tell a clean close (0) from a cut connection
  (`-ECONNRESET`).

Every wait (connect, send, recv, poll, resolve) sleeps through
`network_wait()`, so `netd` keeps running and other processes are not
blocked. A process that exits while waiting releases its sockets as it does
today (`network_process_exit`).

### `sock.setopt(fd, name, value)` (48)

- `fd` must be a socket the caller owns, otherwise `-EBADF`.
- `name` must be one of the `NX_SO_*` values and `value` must be in its
  range, otherwise `-EINVAL`. Nothing is read from user memory.
- Changing a timeout affects the next call, not one already waiting.
- Options are per socket and reset when the socket is closed. There is no
  get call: the program set the value, so it knows it.

### `sock.poll(fds, count, timeout_ms)` (49)

- `count` is 1 to `NX_POLL_MAX` (16), `timeout_ms` at most 120000, otherwise
  `-EINVAL`. `fds` is checked for `count * 8` bytes, readable and writable,
  otherwise `-EFAULT`.
- Each entry's `revents` is written. A bad or foreign `fd` gets
  `NX_POLL_ERR` in its own entry instead of failing the whole call, so one
  closed socket does not hide the others.
- Returns the number of entries with any `revents` bit set, or 0 when the
  timeout ends first. A timeout of 0 checks once and returns.
- It is a socket wait only. It does not cover files, pipes or IPC handles
  (those have `obj.wait`).

### `net.resolve(name, name_len, out, cap, timeout_ms)` (134)

```c
int64_t net.resolve(const char *name, uint32_t name_len,
                    uint8_t (*out)[4], uint32_t cap, uint32_t timeout_ms);
```

- **Input checks.** `name_len` is 1 to 253 (a trailing dot is allowed and
  dropped). Each label is 1 to 63 bytes of `A-Z a-z 0-9 -` and does not start
  or end with `-`. `cap` is 1 to 8. `timeout_ms` is 1 to 30000. Any other
  value gives `-EINVAL`. The kernel copies the name into its own buffer
  before checking it. `name` and `out` are checked with
  `syscall_validate_ptr`, otherwise `-EFAULT`.
- **Result.** Copies up to `cap` IPv4 addresses (A records, in answer order)
  into `out` and returns how many it copied, at least 1.
- **Shortcuts.** A dotted-quad literal such as `10.0.2.2` is returned without
  a query. `localhost` returns 127.0.0.1.
- **Servers.** It asks the DHCP-provided servers in order, then any static
  servers, with 2 tries each, sharing one `timeout_ms` budget.
- **Errors.**
  - `-ENETUNREACH`: no configured interface.
  - `-ENOENT`: the name does not exist (NXDOMAIN) or has no A record.
  - `-ETIMEDOUT`: no server answered in time.
  - `-EIO`: every server failed (SERVFAIL, REFUSED, or a malformed reply).
- **Cache.** The kernel keeps up to 32 answers, each for its record TTL,
  clamped to at most 1 hour. Failures are not cached.
- **Spoofing.** Each query uses a random transaction id (as today) and a
  random source port. Today ephemeral ports start at a random point and then
  go up one at a time. This batch makes every ephemeral port random, for both
  UDP and TCP `connect`, with no internal ABI change. A reply must come from the server's address and port 53 and
  repeat the question. There is no DNSSEC in the beta. For HTTPS this is
  acceptable, because a forged address fails S6's certificate check instead
  of reaching the user's data.
- **Logging.** One serial line per lookup with the name and the result or
  error, as `dns_resolve()` logs today.

### Detection

Programs check `NX_ABI_FLAG_NET` (0x0020) in `abi.query`, as the plan
requires, never the minor version. Today's main sets that flag with
`net.if_list` only. Proposal: from the batch on, the flag means
`net.if_list`, `net.resolve`, `sock.setopt`, `sock.poll` and the new error
codes together. This spends no new flag bit. No kernel outside the tree has
shipped with the flag, so the redefinition breaks nothing. Until the batch
merges, in-tree callers must treat `-ENOSYS` from 48, 49 and 134 as "not
available". The dispatcher (`nx_syscall_dispatch`) already returns `-ENOSYS`
for numbers that have no handler.

### Tests (QEMU)

`tests/userland/nettest` gains these checks:

- **Resolve inputs:** empty, too long, bad label, `cap` 0 and an unmapped
  pointer each get the documented error.
- **Resolve answers:** a literal and `localhost` return without a query. A
  real name returns at least one address, or `-ETIMEDOUT` or `-ENOENT` on a
  network without DNS.
- **Socket errors:** connect to a closed port on the host (10.0.2.2) gets
  `-ECONNREFUSED`. Connect to an unused address on the subnet gets
  `-EHOSTUNREACH`.
- **Timeouts:** `recv` with a 500 ms timeout on an idle connection returns
  `-EAGAIN` after 450 to 700 ms.
- **Poll:** returns `NX_POLL_IN` when data arrives from the host, and gives
  `NX_POLL_ERR` for one bad `fd` in a set of two without failing the call.

### Out of scope for the beta

The following are not in the beta:

- Non-blocking connect (`-EINPROGRESS`).
- Server-side TCP (`listen`, `accept`).
- `sendto` and `recvfrom` syscalls.
- IPv6 and AAAA records.
- A per-process DNS cache or `hosts` file.
- `net.if_config`, hostname and Wi-Fi calls from the draft table above. Those
  go to the second batch (around 2026-11-24) if the desktop needs them.
