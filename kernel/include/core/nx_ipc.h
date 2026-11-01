/*
 * NeolyxOS Shared Memory and IPC - public ABI types (ABI v1.1)
 *
 * Spec: docs/desktop/proposals/kernel-shm-ipc.md (approved 2026-11-01).
 * Fixed-width types only and no kernel includes, so the same header serves
 * the kernel, the desktop, libnxwindow and the REOX runtime. Pointers inside
 * structs are uint64_t so the layout is identical from every language.
 *
 * FROZEN once 1.0 ships: numbers, values and struct layouts never change.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_IPC_H
#define NEOLYX_NX_IPC_H

#include <stdint.h>

typedef int32_t nx_handle_t;            /* > 0 valid. 0 is never a handle. */
#define NX_HANDLE_INVALID      0

/* Rights carried by a handle */
#define NX_RIGHT_READ          0x01u    /* shm: map readable. chan: recv. port: accept. All: wait */
#define NX_RIGHT_WRITE         0x02u    /* shm: map writable. chan: send */
#define NX_RIGHT_MAP           0x04u    /* shm: may be mapped */
#define NX_RIGHT_TRANSFER      0x08u    /* may be sent in a channel message */
#define NX_RIGHT_DUPLICATE     0x10u    /* may be passed to handle.dup */
#define NX_RIGHTS_SAME         0x80000000u /* handle.dup: keep the same rights */

/* shm.map prot */
#define NX_SHM_PROT_READ       0x1u
#define NX_SHM_PROT_WRITE      0x2u     /* on x86 a writable page is also readable */

/* Signals (obj.wait) */
#define NX_SIGNAL_READABLE     0x1u     /* chan: a message is queued. port: a connection is pending */
#define NX_SIGNAL_WRITABLE     0x2u     /* chan: the peer's queue has room */
#define NX_SIGNAL_PEER_CLOSED  0x4u     /* chan: the other endpoint is gone */

#define NX_WAIT_FOREVER        0xFFFFFFFFFFFFFFFFULL   /* UINT64_MAX */

/* Limits (v1.1). Exceeding any of them is an error, never a silent clamp. */
#define NX_HANDLE_MAX          256                  /* handles per process */
#define NX_SHM_SIZE_MAX        (64ULL << 20)        /* bytes per shm object */
#define NX_SHM_PROC_QUOTA      (256ULL << 20)       /* shm bytes created and alive, per process */
#define NX_SHM_MAP_MAX         128                  /* live mappings per process */
#define NX_CHAN_DATA_MAX       1024                 /* bytes per message */
#define NX_CHAN_HANDLES_MAX    4                    /* handles per message */
#define NX_CHAN_QUEUE_MAX      64                   /* queued messages per endpoint */
#define NX_CHAN_PROC_QUEUED    (1ULL << 20)         /* bytes sent by a process, not yet received */
#define NX_WAIT_ITEMS_MAX      64
#define NX_PORT_NAME_MAX       31                   /* bytes, excluding the NUL */
#define NX_PORT_BACKLOG        16                   /* pending connections per port */
#define NX_PORT_MAX            32                   /* ports system-wide */

/* Channel message descriptor (chan.send / chan.recv) */
typedef struct {
    uint64_t data;          /* user pointer to the message bytes */
    uint64_t handles;       /* user pointer to an nx_handle_t array */
    uint32_t data_len;      /* send: bytes to send. recv: in = capacity, out = received (or needed) */
    uint32_t handle_count;  /* send: handles to move. recv: in = capacity, out = received (or needed) */
    uint32_t sender_pid;    /* recv out: sender PID, filled by the kernel. send: must be 0 */
    uint32_t flags;         /* must be 0 */
} nx_chan_msg_t;

/* One entry of an obj.wait set */
typedef struct {
    nx_handle_t handle;
    uint32_t    signals;    /* in: signals that end the wait */
    uint32_t    observed;   /* out: all signals asserted on return */
    uint32_t    reserved;   /* must be 0 */
} nx_wait_item_t;

_Static_assert(sizeof(nx_handle_t) == 4, "nx_handle_t must be 4 bytes");
_Static_assert(sizeof(nx_chan_msg_t) == 32, "nx_chan_msg_t layout is frozen at 32 bytes");
_Static_assert(sizeof(nx_wait_item_t) == 16, "nx_wait_item_t layout is frozen at 16 bytes");

/* abi.query capability flags (nx_abi_info_t.flags) */
#define NX_ABI_FLAG_SHM        0x0001u  /* shm.* and handle.* are available */
#define NX_ABI_FLAG_IPC        0x0002u  /* chan.*, obj.wait and port.* are available */

#endif /* NEOLYX_NX_IPC_H */
