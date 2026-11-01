/*
 * NeolyxOS Framebuffer Presentation ABI (public)
 *
 * fb.flip_rects (syscall 148, ABI 1.2): copy only the given rectangles of a
 * linear back buffer to the screen. Used by the window server (the desktop)
 * to present damaged regions instead of whole frames.
 *
 * Spec: docs/desktop/proposals/nxserver-window-protocol.md (approved
 * 2026-11-01). Layouts here are frozen once 1.0 ships.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_FB_H
#define NEOLYX_NX_FB_H

#include <stdint.h>

/* One screen rectangle, in pixels */
typedef struct {
    int32_t  x, y;          /* top-left; may be negative (clipped) */
    uint32_t w, h;          /* 0 = empty (skipped) */
} nx_fb_rect_t;

#define NX_FB_RECTS_MAX         64          /* rectangles per fb.flip_rects call */

/* nx_abi_info_t.flags: fb.flip_rects is available */
#define NX_ABI_FLAG_FB_RECTS    0x0004u

_Static_assert(sizeof(nx_fb_rect_t) == 16, "nx_fb_rect_t layout is frozen at 16 bytes");

/*
 * Clip r to a screen of sw x sh pixels. Pure arithmetic in 64 bits, so no
 * input can overflow. Shared by the kernel and the host unit test.
 * @return 1 and [*x0, *x1) x [*y0, *y1) set when something is left, 0 when empty
 */
static inline int nx_fb_clip_rect(const nx_fb_rect_t *r, uint32_t sw, uint32_t sh,
                                  uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1) {
    int64_t l = r->x, t = r->y;
    int64_t rr = l + (int64_t)r->w, b = t + (int64_t)r->h;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (rr > (int64_t)sw) rr = (int64_t)sw;
    if (b > (int64_t)sh) b = (int64_t)sh;
    if (l >= rr || t >= b) return 0;
    *x0 = (uint32_t)l;
    *y0 = (uint32_t)t;
    *x1 = (uint32_t)rr;
    *y1 = (uint32_t)b;
    return 1;
}

#endif /* NEOLYX_NX_FB_H */
