# Wired networking: how it runs and how to test it

This page covers the e1000 driver and the TCP/IP stack in `kernel/src/net`:
what runs at boot, what works, what does not, and how to check it in QEMU.

## What runs at boot

1. `nx_kernel_start()` (`kernel/src/core/main.c`) starts the network service
   thread, `netd`, just before the scheduler. Networking never delays the
   desktop: a missing cable or DHCP server only affects `netd`.
2. `netd` (`kernel/src/net/network.c`) calls `network_init()`. That function
   registers `lo` and one `ethN` per e1000 controller (`kernel/src/drivers/e1000.c`).
   Then `tcpip_init()` runs, and DHCP starts on every Ethernet interface.
3. After that, `netd` loops every 10 ms. Each pass drains received frames,
   checks the link every 500 ms, and runs the DHCP and TCP timers.

The generic `ethernet` driver in `kernel/src/drivers/network/ethernet.c` is no
longer started. It writes to a fixed I/O port, talks to no real device, and
used to log "Ethernet driver initialized" even when no network card existed.

## Serial log of a good boot (QEMU, `-netdev user`)

```
[E1000] MAC: 52:54:00:12:34:56
[E1000] Link UP (1000 Mb/s)
[DHCP] eth0: DISCOVER
[DHCP] OFFER 10.0.2.15 from 10.0.2.2
[DHCP] eth0: REQUEST
[NET] eth0 IP: 10.0.2.15/24 gateway 10.0.2.2
[DHCP] ACK: eth0 bound to 10.0.2.15, lease 86400 s, DNS 10.0.2.3
```

## What works

| Layer | Behaviour |
|---|---|
| e1000 | Polls the card with its interrupts masked. All RX and TX buffers are allocated once at init. Bad or oversized frames are dropped whole. Unicast filtering uses our MAC (RAL/RAH). There is no promiscuous mode. |
| DHCP | DISCOVER, OFFER, REQUEST and ACK, with a random xid and exponential retry (2 s up to 32 s, ±1 s). Replies are checked for xid, MAC and magic cookie, and options are bounds-checked. Records up to 3 DNS servers and the lease. Renews at T1 and drops the address when the lease expires. A NAK or a link change restarts discovery. |
| ARP | 32-entry cache with a 10-minute lifetime. Learns only from senders on our subnet. Answers requests for our address. |
| IPv4 | Checks the header and its checksum. Accepts only packets for our address or a broadcast (any address before DHCP finishes). Sets DF on every send. Fragments are dropped, because there is no reassembly. |
| ICMP | Answers echo requests sent to our address, never to a broadcast. |
| UDP | Computes and checks checksums. A connected socket only hears its peer. Datagram boundaries are kept. |
| TCP | Client only: connect, send, recv, close. MSS option, both windows respected, go-back-N retransmission (RTO 1 s doubling to 30 s), orderly FIN close in the background after `closesock`, and RST for segments that match no connection. |
| DNS | `dns_resolve()` (`netinfra.c`) asks the DHCP-provided servers for an A record. Parsing is bounds-checked, follows CNAME chains in the answer, and has a pointer-loop guard. Nothing calls it from userspace yet. |
| Sockets | Owned by the creating process: other processes get `-EBADF`. Released when the process exits. Limits are 16 per process and 64 in total. |

## Socket syscall behaviour (40-47)

The numbers and arguments are unchanged. These are the results:

- `connect` (TCP) blocks until the handshake finishes. It returns `0`,
  `-EIO` if the peer refused, `-ETIMEDOUT` after about 16 s, or `-ENXIO` when
  no interface has an address yet.
- `send` returns the number of bytes queued, or `-EPIPE` once the connection
  is closed or reset. It waits up to 5 s for buffer space, then returns `-EAGAIN`.
- `recv` never blocks. It returns the bytes copied, `0` at the end of the
  stream (peer closed), `-EAGAIN` when nothing has arrived yet, or `-EIO`
  after a reset. A UDP `recv` returns one datagram.
- `listen` and `accept` return `-ENOSYS`, because there is no server-side TCP yet.

## Network status for the desktop: `net.if_list` (syscall 157, ABI 1.4)

```c
int64_t net.if_list(nx_net_if_info_t *buf, uint32_t cap, uint32_t elem_size);
```

A read-only call open to every process (PRIV_USER). It fills `buf` with up
to `cap` interfaces, each `elem_size` bytes apart, and returns the **total**
number of interfaces, which can be larger than `cap`. Call it with `cap = 0`
and `buf = NULL` to get just the count. The struct, its constants and the
capability flag are in `kernel/include/core/nx_net.h`.

- `elem_size` must be at least `NX_NET_IF_INFO_V1_SIZE` (120) and at most
  1024, otherwise `-EINVAL`. The kernel copies `min(elem_size, sizeof)`
  bytes per entry, so a later, larger struct keeps old programs working.
- `cap` above 64 gives `-EINVAL`. A bad `buf` gives `-EFAULT`.
- Each entry has the name, kind (loopback or Ethernet), state (disabled,
  no link, connecting, connected, error), link and speed, MAC, IPv4 method
  (DHCP or manual), address, mask, gateway, up to 3 DNS servers, the DHCP
  lease, and byte, packet and error counters. `primary` marks the interface
  that carries the default route.
- Programs check for it first: `abi.query` must report version 1.4 or later
  and `NX_ABI_FLAG_NET` (0x10) in `flags`. `desktop/lib/nxnet.c` does this,
  so on an older kernel Settings still shows "unavailable" instead of
  calling an unknown syscall number.

With this call, the menu bar indicator and Settings > Network show the
wired connection, its address, gateway and DNS.

## Not done yet

- No way to change settings from the desktop: setting a manual address,
  the hostname and Wi-Fi still return "unsupported" in Settings.
- No userspace DNS call. Programs must send their own queries to a server they
  already know.
- Not implemented: server-side TCP, IP fragmentation, IPv6, Wi-Fi joins, and
  network drivers other than e1000.

## Testing in QEMU

`tests/userland/nettest` is installed as `/apps/nettest`. Launch it from the
app drawer. It first waits for DHCP and checks `net.if_list` (step 0: the
ABI flag, that a bad `elem_size` is refused, and that a connected, primary
Ethernet interface has an address, gateway and DNS). Then it runs three checks, a DNS lookup of example.com through
10.0.2.3, an HTTP GET to the host at 10.0.2.2:8080, and an HTTP GET to the
address from DNS, and logs one `[NETTEST] PASS/FAIL/SKIP` line for each. It
ends with `[NETTEST] RESULT: PASS` only when the interface check, DNS and
the internet GET all worked.

This output came from a cloud container whose DNS answers "no such domain"
for every name and whose proxy answers outbound HTTP with 403:

```
[NETTEST] FAIL dns example.com (error -5)
[TCP] Connected to 10.0.2.2:8080
[NETTEST] PASS host http 10.0.2.2:8080: "HTTP/1.0 200 OK", 202 bytes
[NETTEST] using address from the command line: 93.184.215.14
[TCP] Connected to 93.184.215.14:80
[NETTEST] PASS internet http example.com: "HTTP/1.1 403 Forbidden", 259 bytes
[NETTEST] RESULT: FAIL
```

The host check talks to a web server on the host. Start one with
`python3 -m http.server 8080 --bind 127.0.0.1`. Without a server, that check
reports SKIP. When DNS is filtered, `nettest A.B.C.D` uses that address for
the internet check instead.
