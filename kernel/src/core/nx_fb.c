/*
 * NeolyxOS fb.flip_rects (syscall 148, ABI 1.2)
 *
 * Copies only the given rectangles of the caller's linear back buffer
 * (width * 4 bytes per row, height rows, the layout fb.flip takes) to the
 * hardware framebuffer, converting to the hardware pitch. Unlike fb.flip it
 * never draws the kernel's fallback cursor: the window server draws its own.
 *
 * Only system processes (the desktop / window server) may call it.
 * Spec: docs/desktop/proposals/nxserver-window-protocol.md (approved 2026-11-01).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_fb.h"
#include "core/process.h"
#include "core/syscall.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);

extern uint64_t g_framebuffer_addr;
extern uint32_t g_framebuffer_width;
extern uint32_t g_framebuffer_height;
extern uint32_t g_framebuffer_pitch;

#define FB_DENY_LOG_MAX  8      /* -EPERM lines logged per boot, then one summary */

static void fb_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    serial_puts(&buf[i]);
}

/* Log a refused call; rate-limited so a looping app cannot flood serial */
static void fb_log_denied(const process_t *proc) {
    static uint32_t denied = 0;
    denied++;
    if (denied <= FB_DENY_LOG_MAX) {
        serial_puts("[FB] DENIED fb.flip_rects from PID ");
        fb_dec(proc ? proc->pid : 0);
        serial_puts(" (not a system process, -EPERM)\n");
        if (denied == FB_DENY_LOG_MAX) {
            serial_puts("[FB] further fb.flip_rects denials are not logged\n");
        }
    }
}

/* Copy rows [y0, y1) of columns [x0, x1) from the linear source to the screen */
static void fb_copy_rect(const uint8_t *src, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    const uint64_t src_stride = (uint64_t)g_framebuffer_width * 4ULL;
    const uint64_t dst_stride = g_framebuffer_pitch;
    const uint32_t words = x1 - x0;
    uint8_t *fb = (uint8_t *)g_framebuffer_addr;

    for (uint32_t y = y0; y < y1; y++) {
        const uint32_t *s = (const uint32_t *)(src + (uint64_t)y * src_stride + (uint64_t)x0 * 4ULL);
        volatile uint32_t *d = (volatile uint32_t *)(fb + (uint64_t)y * dst_stride + (uint64_t)x0 * 4ULL);
        for (uint32_t i = 0; i < words; i++) d[i] = s[i];
    }
}

/* int sys_fb_flip_rects(const void *src, const nx_fb_rect_t *rects, uint32_t count) */
int64_t sys_fb_flip_rects_impl(uint64_t src, uint64_t urects, uint64_t count,
                               uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;

    if (!g_framebuffer_addr || g_framebuffer_width == 0 || g_framebuffer_height == 0 ||
        g_framebuffer_pitch < g_framebuffer_width * 4U) {
        return -ENODEV;
    }

    /* Privilege first, so an app learns nothing from argument checks */
    process_t *proc = process_current();
    if (!proc || !(proc->flags & PROC_FLAG_SYSTEM)) {
        fb_log_denied(proc);
        return -EPERM;
    }

    if (count == 0 || count > NX_FB_RECTS_MAX) return -EINVAL;

    nx_fb_rect_t rects[NX_FB_RECTS_MAX];
    if (syscall_copy_from_user(rects, (const void *)urects, count * sizeof(nx_fb_rect_t)) != 0) {
        return -EFAULT;
    }

    /* The whole back buffer must be the caller's readable memory, exactly as
     * for fb.flip (64-bit size, cannot wrap) */
    uint64_t src_size = (uint64_t)g_framebuffer_width * 4ULL * (uint64_t)g_framebuffer_height;
    if (!src || syscall_validate_ptr((const void *)src, (size_t)src_size, 0) != 0) {
        return -EFAULT;
    }

    for (uint64_t i = 0; i < count; i++) {
        uint32_t x0, y0, x1, y1;
        if (!nx_fb_clip_rect(&rects[i], g_framebuffer_width, g_framebuffer_height,
                             &x0, &y0, &x1, &y1)) {
            continue;   /* empty or fully off screen */
        }
        fb_copy_rect((const uint8_t *)src, x0, y0, x1, y1);
    }
    return 0;
}
