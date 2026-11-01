/*
 * NeolyxOS Network Status ABI (public)
 *
 * ABI 1.4:
 *   133 net.if_list   int net_if_list(nx_net_if_info_t *buf, uint32_t cap,
 *                                     uint32_t elem_size)
 *
 * Read-only: reports each network interface's link, IPv4 configuration
 * and traffic counters. Changing configuration and Wi-Fi are separate,
 * later calls (docs/desktop/proposals/net.md). Spec:
 * docs/network/wired-networking.md. Check NX_ABI_FLAG_NET in
 * nx_abi_info_t.flags (syscall 0) before calling. Layouts here are
 * frozen once 1.0 ships.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_NET_H
#define NEOLYX_NX_NET_H

#include <stdint.h>

/* nx_net_if_info_t.kind */
#define NX_NET_IF_ETHERNET      0u
#define NX_NET_IF_WIFI          1u
#define NX_NET_IF_LOOPBACK      2u

/* nx_net_if_info_t.state */
#define NX_NET_STATE_DISABLED   0u      /* administratively down */
#define NX_NET_STATE_NO_LINK    1u      /* up, no cable / no carrier */
#define NX_NET_STATE_CONFIGURING 2u     /* link up, no address yet (DHCP running) */
#define NX_NET_STATE_CONNECTED  3u      /* link up and IPv4 configured */
#define NX_NET_STATE_ERROR      4u

/* nx_net_if_info_t.ipv4_method */
#define NX_NET_IPV4_DHCP        0u
#define NX_NET_IPV4_MANUAL      1u

#define NX_NET_IF_MAX           8u      /* interfaces the kernel reports at most */
#define NX_NET_DNS_MAX          3u

/*
 * One interface. Addresses are in network order (addr[0] is the first
 * octet). The kernel writes min(elem_size, sizeof) bytes per entry and
 * sets `size` to the bytes it wrote, so fields can be appended later.
 */
typedef struct {
    uint32_t size;              /* bytes the kernel filled */
    uint32_t index;             /* stable for this boot */
    char     name[16];          /* "eth0", "lo"; NUL-terminated */
    uint8_t  kind;              /* NX_NET_IF_* */
    uint8_t  state;             /* NX_NET_STATE_* */
    uint8_t  link_up;           /* carrier present */
    uint8_t  primary;           /* carries the default route */
    uint8_t  mac[6];            /* 00..00 for loopback */
    uint8_t  ipv4_method;       /* NX_NET_IPV4_* */
    uint8_t  dns_count;         /* 0..NX_NET_DNS_MAX */
    uint32_t speed_mbps;        /* 0 = unknown or no link */
    uint8_t  addr[4];
    uint8_t  mask[4];
    uint8_t  gateway[4];        /* 0.0.0.0 = none */
    uint8_t  dns[NX_NET_DNS_MAX][4];
    uint32_t lease_seconds;     /* DHCP lease length; 0 = none/unknown */
    uint32_t reserved;          /* 0 */
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_errors, tx_errors;
} nx_net_if_info_t;

#define NX_NET_IF_INFO_V1_SIZE  120u

/* nx_abi_info_t.flags: net.if_list is available */
#define NX_ABI_FLAG_NET         0x0020u

_Static_assert(sizeof(nx_net_if_info_t) == 120, "nx_net_if_info_t v1 layout is frozen at 120 bytes");

#endif /* NEOLYX_NX_NET_H */
