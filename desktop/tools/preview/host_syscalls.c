/*
 * NeolyxOS - Desktop preview harness: host syscall dispatcher
 *
 * The preview build compiles the real desktop shell for the development
 * machine with NX_HOST_PREVIEW defined; nxsyscall.h then routes every
 * syscall here. We emulate the framebuffer, a virtual clock, a fixed RTC,
 * the userspace heap and a scripted input queue, and write PNG frames.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../include/nxsyscall.h"
#include "preview.h"

/* ============ Emulated machine ============ */

static uint32_t *g_fb;               /* "hardware" framebuffer */
static uint8_t  *g_heap;             /* userspace heap (nx_brk base) */
static uint64_t  g_clock_ms = 1000;  /* virtual monotonic clock */
static uint32_t  g_frames;           /* number of flips so far */

uint32_t preview_frame_count(void) { return g_frames; }
const uint32_t *preview_framebuffer(void) { return g_fb; }

void preview_machine_init(void) {
    g_fb = calloc((size_t)PREVIEW_W * PREVIEW_H, 4);
    /* 96 MiB: the kernel pre-maps 64 MiB; nx_malloc enforces its own cap. */
    if (posix_memalign((void **)&g_heap, 4096, 96u << 20) != 0) g_heap = NULL;
    if (!g_fb || !g_heap) {
        fprintf(stderr, "preview: out of memory\n");
        exit(2);
    }
    memset(g_heap, 0, 96u << 20);
}

/* ============ Dispatcher ============ */

int64_t nx_host_syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    (void)a3;
    switch (num) {
        case SYS_TIME:
            return (int64_t)g_clock_ms;
        case SYS_SLEEP:
            if (a1 > 0 && a1 < 100000) g_clock_ms += (uint64_t)a1;
            return 0;
        case SYS_YIELD:
            return 0;
        case SYS_BRK:
            return (int64_t)(uintptr_t)g_heap;
        case SYS_FB_INFO: {
            fb_info_t *info = (fb_info_t *)(uintptr_t)a1;
            if (!info) return -14;
            info->base_addr = (uint64_t)(uintptr_t)g_fb;
            info->width = PREVIEW_W;
            info->height = PREVIEW_H;
            info->pitch = PREVIEW_W * 4;
            info->bpp = 32;
            info->format = 0;
            info->size = (uint64_t)PREVIEW_W * PREVIEW_H * 4;
            return 0;
        }
        case SYS_FB_MAP:
            return (int64_t)(uintptr_t)g_fb;
        case SYS_FB_FLIP: {
            const uint32_t *src = (const uint32_t *)(uintptr_t)a1;
            if (src && src != g_fb) memcpy(g_fb, src, (size_t)PREVIEW_W * PREVIEW_H * 4);
            g_frames++;
            g_clock_ms += 16;           /* one 60 Hz frame */
            preview_on_flip(g_frames);  /* script actions, screenshots, exit */
            return 0;
        }
        case SYS_CURSOR_SET:
            return 0;
        case SYS_INPUT_POLL:
            return preview_input_poll((input_event_t *)(uintptr_t)a1, (int)a2, g_frames, g_clock_ms);
        case SYS_INPUT_REGISTER:
            return 0;
        case 127: /* SYS_DEBUG_PRINT */
            if (a1 && preview_verbose()) fputs((const char *)(uintptr_t)a1, stderr);
            return 0;
        case SYS_RTC_GET: {
            rtc_time_t *t = (rtc_time_t *)(uintptr_t)a1;
            if (!t) return -14;
            uint64_t secs = 22u * 3600u + 10u * 60u + (g_clock_ms / 1000u);
            t->year = 2026; t->month = 10; t->day = 25; t->weekday = 0;   /* Sunday */
            t->hour = (uint8_t)((secs / 3600u) % 24u);
            t->minute = (uint8_t)((secs / 60u) % 60u);
            t->second = (uint8_t)(secs % 60u);
            return 0;
        }
        case SYS_RTC_UNIX:
            return 1792966200 + (int64_t)(g_clock_ms / 1000u);   /* 2026-10-25 22:10 UTC-ish */
        case SYS_OPEN:
            return -2;   /* no filesystem in the preview */
        case SYS_CLOSE: case SYS_READ: case SYS_WRITE: case SYS_SEEK: case SYS_STAT:
            return -9;
        case SYS_EXIT:
            fprintf(stderr, "preview: desktop called exit(%lld)\n", (long long)a1);
            exit((int)a1);
        default:
            return -38;  /* ENOSYS */
    }
}
