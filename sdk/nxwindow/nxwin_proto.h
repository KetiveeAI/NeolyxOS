/*
 * NeolyxOS window protocol, version 1.0 (nxserver <-> apps)
 *
 * Spec: docs/desktop/proposals/nxserver-window-protocol.md (approved by the
 * project owner on 2026-11-01). libnxwindow (week 4) is the supported way to
 * speak it; this header is the wire format both sides compile against.
 *
 * Transport: an app calls port.connect("nx.display") and gets a channel.
 * Every message is exactly one channel message holding exactly one of the
 * structs below, little-endian, with hdr.len equal to sizeof(that struct).
 * Only NXW_OP_BUF_ATTACH carries a handle (exactly one shared-memory
 * handle); every other message carries none.
 *
 * Fixed-width types only and no other includes, so the same header serves
 * the desktop, libnxwindow, the REOX runtime and test programs.
 *
 * FROZEN for protocol 1.x: numbers, values and struct layouts never change.
 * New requests or events come as a new minor version; a server refuses ops
 * it does not know, so an old server fails cleanly.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NXWIN_PROTO_H
#define NEOLYX_NXWIN_PROTO_H

#include <stdint.h>

/* ============ Version, names and limits ============ */

#define NXW_PROTO_MAJOR        1
#define NXW_PROTO_MINOR        0

#define NXW_PORT_NAME          "nx.display"

#define NXW_TITLE_MAX          64          /* bytes of UTF-8 including the NUL */
#define NXW_DAMAGE_MAX         16          /* rectangles per COMMIT */
#define NXW_SIZE_MAX           4096        /* window and buffer width/height */
#define NXW_STRIDE_MAX         16384       /* bytes per buffer row */
#define NXW_WINDOWS_PER_CLIENT 8
#define NXW_WINDOWS_TOTAL      16          /* shared with the shell's own windows */
#define NXW_CLIENTS_MAX        16

/* WIN_CREATE x and y: let the server place the window */
#define NXW_POS_AUTO           ((int32_t)(-2147483647 - 1))

/* WIN_CREATE flags (all other bits must be 0) */
#define NXW_WIN_RESIZABLE      0x1u

/* CONFIGURE state bits */
#define NXW_STATE_MAXIMIZED    0x1u
#define NXW_STATE_TILED        0x2u
#define NXW_STATE_ACTIVE       0x4u

/* POINTER kind */
#define NXW_PTR_MOTION         1u
#define NXW_PTR_BUTTON         2u
#define NXW_PTR_ENTER          3u
#define NXW_PTR_LEAVE          4u
#define NXW_PTR_WHEEL          5u

/* POINTER buttons (bit set = held) */
#define NXW_BUTTON_LEFT        0x1u
#define NXW_BUTTON_RIGHT       0x2u
#define NXW_BUTTON_MIDDLE      0x4u

/* KEY modifiers (bit set = held when the key event happened) */
#define NXW_MOD_SHIFT          0x1u
#define NXW_MOD_CTRL           0x2u
#define NXW_MOD_ALT            0x4u
#define NXW_MOD_NX             0x8u

/* ERROR codes: negative kernel errno values (kernel/include/core/syscall.h) */
#define NXW_EPERM              1           /* another client's window */
#define NXW_EBADF              9           /* no such window */
#define NXW_ENOMEM             12          /* the server is out of mappings or memory */
#define NXW_EINVAL             22          /* bad size, flags, buffer, title or order */
#define NXW_EMFILE             24          /* too many windows */

/* ============ Ops ============ */

/* Requests (client to server) */
#define NXW_OP_HELLO           0x0101u
#define NXW_OP_WIN_CREATE      0x0102u
#define NXW_OP_WIN_DESTROY     0x0103u
#define NXW_OP_WIN_SET_TITLE   0x0104u
#define NXW_OP_BUF_ATTACH      0x0105u
#define NXW_OP_COMMIT          0x0106u

/* Events and replies (server to client) */
#define NXW_OP_WELCOME         0x0201u
#define NXW_OP_WIN_CREATED     0x0202u
#define NXW_OP_CONFIGURE       0x0203u
#define NXW_OP_FRAME_DONE      0x0204u
#define NXW_OP_CLOSE_REQUEST   0x0205u
#define NXW_OP_KEY             0x0206u
#define NXW_OP_POINTER         0x0207u
#define NXW_OP_ERROR           0x02FFu

/* ============ Common parts ============ */

typedef struct {
    uint16_t op;            /* NXW_OP_* */
    uint16_t len;           /* whole message in bytes, header included */
    uint32_t seq;           /* client: request number; server: echoes it in
                               replies and errors, 0 in events */
} nxw_hdr_t;

typedef struct {
    int32_t  x, y;
    uint32_t w, h;
} nxw_rect_t;

/* ============ Requests ============ */

/* Must be the first message. Reply: WELCOME, or ERROR and the connection
 * is closed when the major version is not spoken. */
typedef struct {
    nxw_hdr_t hdr;
    uint16_t  major, minor;
    uint32_t  flags;        /* 0 */
} nxw_hello_t;

/* Content size in pixels (decorations are added outside it). x/y are the
 * window frame's top-left on screen, or NXW_POS_AUTO. The window is not
 * shown until its first COMMIT. Reply: WIN_CREATED with the size actually
 * granted (the server fits windows to the screen). */
typedef struct {
    nxw_hdr_t hdr;
    uint32_t  width, height;
    int32_t   x, y;
    uint32_t  flags;        /* NXW_WIN_RESIZABLE */
    char      title[NXW_TITLE_MAX];
} nxw_win_create_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
} nxw_win_destroy_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    char      title[NXW_TITLE_MAX];
} nxw_win_set_title_t;

/* Carries exactly one shared-memory handle (READ and MAP rights needed;
 * the server maps it read-only and closes the handle). XRGB8888 pixels,
 * alpha ignored. The buffer replaces the current one at the next COMMIT. */
typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    uint32_t  width, height;
    uint32_t  stride;       /* bytes per row: multiple of 4, >= width * 4 */
    uint64_t  offset;       /* first pixel's byte offset in the object */
} nxw_buf_attach_t;

/* count 0: the whole buffer changed; otherwise rects[0..count) in buffer
 * coordinates. Do not draw into the buffer again until FRAME_DONE. */
typedef struct {
    nxw_hdr_t  hdr;
    uint32_t   win;
    uint32_t   count;
    nxw_rect_t rects[NXW_DAMAGE_MAX];
} nxw_commit_t;

/* ============ Events and replies ============ */

typedef struct {
    nxw_hdr_t hdr;
    uint16_t  major, minor;
    uint32_t  screen_w, screen_h;
    uint32_t  max_windows;  /* per client */
    uint32_t  max_size;     /* largest width or height */
} nxw_welcome_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    uint32_t  width, height;
} nxw_win_created_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    uint32_t  width, height;
    uint32_t  state;        /* NXW_STATE_* */
} nxw_configure_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    uint32_t  commit_seq;   /* hdr.seq of the COMMIT that was presented */
    uint64_t  time_ms;
} nxw_frame_done_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
} nxw_close_request_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    uint8_t   scancode;     /* PS/2 set 1 make code */
    uint8_t   ascii;        /* decoded character, 0 if none */
    uint8_t   pressed;      /* 1 down (or repeat), 0 up */
    uint8_t   modifiers;    /* NXW_MOD_* */
    uint32_t  time_ms;
} nxw_key_t;

typedef struct {
    nxw_hdr_t hdr;
    uint32_t  win;
    uint32_t  kind;         /* NXW_PTR_* */
    int32_t   x, y;         /* content-local pixels */
    uint32_t  buttons;      /* NXW_BUTTON_* held */
    int32_t   wheel;        /* WHEEL: notches, > 0 scrolls toward the end */
    uint32_t  time_ms;
} nxw_pointer_t;

typedef struct {
    nxw_hdr_t hdr;
    uint16_t  op;           /* the request's op */
    uint16_t  reserved;     /* 0 */
    int32_t   code;         /* negative errno */
    uint32_t  win;          /* window concerned, 0 if none */
} nxw_error_t;

/* ============ Frozen layouts ============ */

_Static_assert(sizeof(nxw_hdr_t) == 8, "nxw_hdr_t is frozen at 8 bytes");
_Static_assert(sizeof(nxw_rect_t) == 16, "nxw_rect_t is frozen at 16 bytes");
_Static_assert(sizeof(nxw_hello_t) == 16, "nxw_hello_t is frozen at 16 bytes");
_Static_assert(sizeof(nxw_win_create_t) == 92, "nxw_win_create_t is frozen at 92 bytes");
_Static_assert(sizeof(nxw_win_destroy_t) == 12, "nxw_win_destroy_t is frozen at 12 bytes");
_Static_assert(sizeof(nxw_win_set_title_t) == 76, "nxw_win_set_title_t is frozen at 76 bytes");
_Static_assert(sizeof(nxw_buf_attach_t) == 32, "nxw_buf_attach_t is frozen at 32 bytes");
_Static_assert(sizeof(nxw_commit_t) == 272, "nxw_commit_t is frozen at 272 bytes");
_Static_assert(sizeof(nxw_welcome_t) == 28, "nxw_welcome_t is frozen at 28 bytes");
_Static_assert(sizeof(nxw_win_created_t) == 20, "nxw_win_created_t is frozen at 20 bytes");
_Static_assert(sizeof(nxw_configure_t) == 24, "nxw_configure_t is frozen at 24 bytes");
_Static_assert(sizeof(nxw_frame_done_t) == 24, "nxw_frame_done_t is frozen at 24 bytes");
_Static_assert(sizeof(nxw_close_request_t) == 12, "nxw_close_request_t is frozen at 12 bytes");
_Static_assert(sizeof(nxw_key_t) == 20, "nxw_key_t is frozen at 20 bytes");
_Static_assert(sizeof(nxw_pointer_t) == 36, "nxw_pointer_t is frozen at 36 bytes");
_Static_assert(sizeof(nxw_error_t) == 20, "nxw_error_t is frozen at 20 bytes");

#endif /* NEOLYX_NXWIN_PROTO_H */
