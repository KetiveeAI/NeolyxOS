# Desktop networking

How the NeolyxOS desktop shows and changes network state. Everything runs
in userspace. The kernel is reached only through the NXNet client.

```
Settings > Network pane ─┐
Menu bar indicator ──────┼──> NXNet client (desktop/lib/nxnet.c) ──> kernel syscalls (proposed)
Control Center tile ─────┘                                        └─> preview demo backend (NX_HOST_PREVIEW)
```

## Files

| File | Role |
|---|---|
| `desktop/include/nxnet.h`, `desktop/lib/nxnet.c` | Client API: interfaces, IPv4, Wi-Fi, connectivity, hostname, counters, validation |
| `desktop/shell/views/settings_network_pane.c` | `settings_pane_network` (see `settings_pane.h`) |
| `desktop/include/network_indicator.h`, `desktop/shell/network_indicator.c` | Menu bar icon and dropdown |
| `docs/desktop/proposals/net.md` | Kernel syscalls the client needs |

## Current behaviour on the OS

The kernel does not yet expose network state to userspace (see the
proposal). Every NXNet call returns `NXNET_ERR_UNSUPPORTED`, and the
first call logs `[NXNET] kernel exposes no network status syscalls ...`
once. Because of this:

- Settings > Network shows **"Networking service unavailable"** and a
  **Try Again** button, which calls `nxnet_init()` again.
- The menu bar icon draws Wi-Fi-off. The dropdown says "Status
  unavailable" and offers "Network Settings…".

Nothing is guessed. No part of the UI shows "Connected" unless the
backend reported it.

## Client contract (nxnet.h)

- Every call returns `nxnet_status_t`. An output struct is written only
  when the call returns `NXNET_OK`. On an error it is zeroed.
- Inputs are validated before they reach the backend:
  - IPv4 strings must be a strict dotted quad: four octets, each 0–255,
    with no leading zeros and no extra characters.
  - A netmask must be contiguous.
  - In a manual configuration, the address must be unicast and must not
    be the network or broadcast address. The gateway must be in the same
    subnet and must differ from the address.
  - An SSID must be 1–32 bytes with no control characters.
  - WPA, WPA2 and WPA3 passphrases must be 8–63 printable characters.
    WPA and WPA2 also accept exactly 64 hex digits. WEP is refused.
  - A hostname must be one RFC 1123 label.
- Passphrases are never logged. The client wipes its copy after a join,
  and the pane wipes its buffer when a sheet closes.
- `nxnet_generation()` changes whenever state changes. UIs compare it
  with the last value they saw to know when to re-read.
- Configuration calls (`nxnet_ipv4_configure`, `nxnet_wifi_join`,
  `_forget`, `_set_power`, `nxnet_hostname_set`) are PRIV_SYSTEM in the
  kernel and can return `NXNET_ERR_PERMISSION`.

## Settings > Network pane

- **Left column:** one row per interface (loopback is hidden). Each row
  has a status dot: green for connected, amber for connecting, grey for
  off or no link, red for an error. The computer name is shown below the
  list.
- **Right column:**
  - A header with the interface name and status, plus the Wi-Fi power
    toggle, or a "Primary" badge on a primary Ethernet interface.
  - A detail card: IP address, subnet mask, router, DNS servers, MAC
    address and IPv4 method.
  - For Wi-Fi, a network list with signal bars, lock icons and a
    checkmark on the current network.
  - For Ethernet, the traffic counters.
  - The **Advanced…** button, and **Forget Network** for Wi-Fi.
- **Join sheet:** password field (masked, with a Show password option), a
  hint for how many more characters are needed, and inline errors such
  as "Incorrect password". Join stays disabled until the passphrase
  passes validation.
- **Advanced sheet:** Using DHCP or Manually. Manual has fields for IP
  address, subnet mask, router and DNS, which accept only digits and
  dots. The first invalid field gets a red outline and a specific
  message.
- **Keys:** Tab moves between fields, Enter submits, Esc cancels. If the
  host passes `ascii == 0`, the pane maps PS/2 set-1 scancodes itself.

## Menu bar indicator

`network_indicator_draw_icon()` draws one of these states:

- the Ethernet glyph,
- Wi-Fi with 0–4 lit bars over a faint full fan,
- dim Wi-Fi while connecting,
- Wi-Fi-off when offline or unavailable.

`network_indicator_draw_menu()` draws the dropdown:

- Wi-Fi header with a toggle,
- the current network,
- up to four other networks,
- the Ethernet row,
- "Network Settings…".

Choosing an open network joins it directly. Choosing a secured network
opens Settings > Network and calls `network_pane_begin_join(ssid)`, so
the password is entered only in Settings.

## Previewing

Under `NX_HOST_PREVIEW`, `nxnet.c` uses a demo backend: `eth0` connected
at 1 Gb/s, `wlan0` on "Ketivee Lab", and six scan results. Joining with
the passphrase `incorrect` returns `NXNET_ERR_AUTH`, so you can review
the error UI. Use these preview script calls:

```
5   call net_preview_open          # Settings-like window hosting the pane
10  call net_preview_select_wifi
20  call net_preview_join          # join sheet for "Neolyx-5G"
30  call net_preview_advanced
40  call net_preview_menu          # window showing the menu bar dropdown
50  call net_preview_unavailable   # behave like today's kernel
60  call net_preview_wifi_off
70  call net_preview_light         # light appearance
```

None of this code is compiled into the OS build.
