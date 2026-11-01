/*
 * nxserver - window protocol message checks
 *
 * Pure functions with no syscalls and no shell state, so every rule of
 * docs/desktop/proposals/nxserver-window-protocol.md ("Errors and limits")
 * is checked the same way in the desktop and in tests/host/nxw_check_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXSERVER_NXW_CHECK_H
#define NXSERVER_NXW_CHECK_H

#include <stdint.h>
#include "../../../sdk/nxwindow/nxwin_proto.h"

/* Protocol violations: the connection is closed and the client's windows
 * go away. 0 means the frame is well formed. */
typedef enum {
    NXW_FRAME_OK = 0,
    NXW_V_SHORT,            /* shorter than the header */
    NXW_V_LEN,              /* hdr.len differs from the bytes received */
    NXW_V_OP,               /* unknown op, or an event op sent to the server */
    NXW_V_SIZE,             /* length does not match the op's struct */
    NXW_V_HANDLES,          /* wrong number of handles for the op */
    NXW_V_BEFORE_HELLO,     /* anything other than HELLO first */
    NXW_V_NOT_SHM,          /* BUF_ATTACH handle is not mappable shared memory */
    NXW_V_COUNT
} nxw_violation_t;

/* Short reason for the log ("length does not match the op"). */
const char *nxw_violation_str(int v);

/* Exact size of a request's struct, or 0 when op is not a request. */
uint32_t nxw_request_size(uint16_t op);

/*
 * Check one received message's framing.
 * @data, @data_len: the bytes received (data may be NULL only if data_len 0)
 * @handle_count:    handles that came with it
 * @hello_done:      nonzero once HELLO was accepted on this connection
 * @op_out:          the op when the result is NXW_FRAME_OK (may be NULL)
 * @return NXW_FRAME_OK or an nxw_violation_t
 */
int nxw_check_frame(const void *data, uint32_t data_len, uint32_t handle_count,
                    int hello_done, uint16_t *op_out);

/* Content size of a window or buffer: 1..NXW_SIZE_MAX each. 0 or -NXW_EINVAL. */
int nxw_check_size(uint32_t width, uint32_t height);

/*
 * BUF_ATTACH geometry against the shared-memory object's size: stride a
 * multiple of 4, at least width * 4 and at most NXW_STRIDE_MAX; the pixels
 * [offset, offset + stride * height) inside the object (64-bit math, no
 * overflow). 0 or -NXW_EINVAL.
 */
int nxw_check_buffer(uint32_t width, uint32_t height, uint32_t stride,
                     uint64_t offset, uint64_t object_size);

/*
 * Title from the wire: must hold a NUL within NXW_TITLE_MAX bytes
 * (-NXW_EINVAL otherwise, out untouched). Copied into out with every
 * invalid UTF-8 sequence and control character replaced by '?', so what is
 * drawn is always valid UTF-8. Returns 0.
 */
int nxw_title_sanitize(const char in[NXW_TITLE_MAX], char out[NXW_TITLE_MAX]);

/* Clip r to a bw x bh buffer (64-bit math). 1 and *out set when something
 * is left, 0 when the result is empty. */
int nxw_clip_rect(const nxw_rect_t *r, uint32_t bw, uint32_t bh, nxw_rect_t *out);

#endif /* NXSERVER_NXW_CHECK_H */
