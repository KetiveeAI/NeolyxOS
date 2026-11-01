/*
 * libnxwindow - open windows on the NeolyxOS window server
 *
 * Spec: docs/desktop/proposals/libnxwindow.md (approved by the project owner
 * on 2026-11-01). The library speaks window protocol 1.0 (nxwin_proto.h)
 * with nxserver, the window server inside the desktop, so an app never
 * builds protocol messages or handles shared memory itself.
 *
 * Model:
 *   - One connection per process, kept in static state: nxw_connect() once,
 *     then create windows. No heap is used.
 *   - Each window has one XRGB8888 buffer that the library owns. Draw only
 *     between nxw_frame_begin() and nxw_frame_commit(); after a commit the
 *     buffer is busy (nxw_frame_begin returns -NXW_EBUSY) until the
 *     NXW_EV_FRAME event says the server has presented it. The buffer keeps
 *     its contents between frames, so redraw only what changed.
 *   - After NXW_EV_CONFIGURE with a new size, the next nxw_frame_begin()
 *     gives a new, zeroed buffer of that size and sets frame.full = 1.
 *   - Single-threaded: call everything from the same thread.
 *
 * Every call returns 0 (or a positive count) on success and a negative
 * errno on failure (kernel values, named NXW_E* below and in nxwin_proto.h).
 * Every failure is also logged to the serial console with an [NXWIN] prefix,
 * except the expected ones: -NXW_EBUSY from nxw_frame_begin (flow control)
 * and -NXW_ENOTCONN from the event calls after a disconnect.
 *
 * Build: sdk/nxwindow/Makefile builds libnxwindow.a. Include paths needed:
 * sdk/nxwindow, desktop/include and kernel/include/core.
 *
 * API 1.x changes only by addition.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NXWINDOW_H
#define NEOLYX_NXWINDOW_H

#include <stdint.h>
#include "nx_ipc.h"
#include "nxwin_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NXWL_API_MAJOR 1
#define NXWL_API_MINOR 0

/* Error codes returned negated, in addition to the NXW_E* values of
 * nxwin_proto.h. The first six are the kernel's numbers; the kernel has no
 * code for the last three, which use the customary numbers it never uses. */
#define NXW_ENOENT      2       /* no window server (port nx.display missing) */
#define NXW_EBUSY       16      /* the buffer is waiting for FRAME_DONE */
#define NXW_EPIPE       32      /* the connection broke during the call */
#define NXW_ENOSYS      38      /* the kernel has no shm or IPC */
#define NXW_ETIMEDOUT   110     /* the server did not answer in time */
#define NXW_EPROTO      71      /* the server refused the protocol version */
#define NXW_EISCONN     106     /* already connected */
#define NXW_ENOTCONN    107     /* not connected, or the server went away */

/* Opaque. A window pointer is a checked token, never dereferenced by the
 * app; a stale or foreign one is rejected with -NXW_EBADF. */
typedef struct nxw_window nxw_window_t;

typedef struct {
    uint16_t proto_major, proto_minor;  /* from WELCOME */
    uint32_t screen_w, screen_h;
    uint32_t max_windows;               /* per client */
    uint32_t max_size;                  /* largest width or height */
} nxw_display_info_t;

typedef struct {
    const char *title;          /* UTF-8, at most NXW_TITLE_MAX - 1 bytes plus the NUL */
    uint32_t    width, height;  /* content size, 1..max_size */
    int32_t     x, y;           /* frame top-left on screen, or NXW_POS_AUTO */
    uint32_t    flags;          /* NXW_WIN_RESIZABLE or 0 */
} nxw_window_desc_t;

typedef struct {
    uint32_t *pixels;           /* XRGB8888; row r starts at pixels + r * (stride / 4) */
    uint32_t  width, height;    /* the window's current content size */
    uint32_t  stride;           /* bytes per row */
    int       full;             /* 1: new buffer (zeroed), draw everything */
} nxw_frame_t;

/* Event types */
#define NXW_EV_CONFIGURE     1  /* size or state changed: redraw at the new size */
#define NXW_EV_CLOSE         2  /* the user clicked close: save, then destroy */
#define NXW_EV_KEY           3
#define NXW_EV_POINTER       4
#define NXW_EV_FRAME         5  /* the last commit is on screen; the buffer is free */
#define NXW_EV_ERROR         6  /* the server refused an asynchronous request */
#define NXW_EV_DISCONNECTED  7  /* the server went away; every window is gone */

typedef struct {
    uint32_t      type;         /* NXW_EV_* */
    nxw_window_t *window;       /* NULL for DISCONNECTED and connection-wide ERROR */
    union {
        struct { uint32_t width, height, state; } configure;    /* state: NXW_STATE_* */
        struct { uint32_t commit_seq; uint64_t time_ms; } frame;
        struct { uint8_t scancode, ascii, pressed, modifiers; uint32_t time_ms; } key;
        struct { uint32_t kind; int32_t x, y; uint32_t buttons; int32_t wheel; uint32_t time_ms; } pointer;
        struct { uint16_t op; int32_t code; } error;            /* code: negative errno */
    };
} nxw_event_t;

/* ---- Connection ---- */

/* Checks the kernel (abi.query: shm and IPC), connects to nx.display, sends
 * HELLO and waits up to 2 s for WELCOME. info may be NULL.
 * -NXW_ENOSYS, -NXW_ENOENT, -NXW_EPROTO, -NXW_ETIMEDOUT, -NXW_EPIPE,
 * -NXW_EISCONN. */
int nxw_connect(nxw_display_info_t *info);

/* Destroys every window, unmaps every buffer, closes the connection. Safe
 * to call when not connected. */
void nxw_disconnect(void);

/* The connection's channel, for apps that wait on other objects too: when
 * obj.wait reports it READABLE (or PEER_CLOSED), call nxw_poll_event.
 * NX_HANDLE_INVALID when not connected. */
nx_handle_t nxw_connection_handle(void);

/* ---- Windows ---- */

/* WIN_CREATE; waits up to 2 s for the reply. *out is set on success only.
 * The window appears at its first commit. -NXW_EINVAL (size, flags, title
 * or a NULL pointer), -NXW_EMFILE, -NXW_ENOTCONN, -NXW_ETIMEDOUT, -NXW_EPIPE. */
int nxw_window_create(const nxw_window_desc_t *desc, nxw_window_t **out);

/* WIN_DESTROY and unmap its buffer; the pointer is invalid afterwards.
 * -NXW_EBADF when w is not a live window. */
int nxw_window_destroy(nxw_window_t *w);

/* -NXW_EINVAL (title), -NXW_EBADF, -NXW_ENOTCONN, -NXW_EPIPE. */
int nxw_window_set_title(nxw_window_t *w, const char *title);

/* Current content size and NXW_STATE_* (from the last CONFIGURE, or the
 * size granted at creation). Any out pointer may be NULL. -NXW_EBADF. */
int nxw_window_get_size(nxw_window_t *w, uint32_t *width, uint32_t *height, uint32_t *state);

/* ---- Frames ---- */

/* Fills f with the buffer to draw into. On first use and after a size
 * change, creates, maps and attaches a new buffer (f->full = 1).
 * -NXW_EBUSY (waiting for FRAME_DONE), -NXW_ENOMEM (shm quota or mappings),
 * -NXW_EBADF, -NXW_ENOTCONN, -NXW_EPIPE. */
int nxw_frame_begin(nxw_window_t *w, nxw_frame_t *f);

/* COMMIT what was drawn since nxw_frame_begin. count 0 means the whole
 * buffer. Rectangles are clipped to the buffer and empty ones dropped; more
 * than NXW_DAMAGE_MAX are merged into their bounding box. The buffer is then
 * busy until NXW_EV_FRAME. -NXW_EINVAL (no frame begun, or rects NULL with
 * count > 0), -NXW_EBADF, -NXW_ENOTCONN, -NXW_EPIPE. */
int nxw_frame_commit(nxw_window_t *w, const nxw_rect_t *rects, uint32_t count);

/* ---- Events ---- */

/* Never blocks. 1 an event, 0 none, negative errno (-NXW_ENOTCONN after
 * the NXW_EV_DISCONNECTED event has been delivered, -NXW_EINVAL ev NULL). */
int nxw_poll_event(nxw_event_t *ev);

/* Waits up to timeout_ms (NX_WAIT_FOREVER: no limit). 1 an event, 0 timeout,
 * negative errno as nxw_poll_event. */
int nxw_wait_event(nxw_event_t *ev, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NEOLYX_NXWINDOW_H */
