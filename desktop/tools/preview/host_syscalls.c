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
#include <dirent.h>
#include <sys/stat.h>

/* ============ Emulated machine ============ */

static uint32_t *g_fb;               /* "hardware" framebuffer */
static uint8_t  *g_heap;             /* userspace heap (nx_brk base) */
static uint64_t  g_clock_ms = 1000;  /* virtual monotonic clock */
static uint32_t  g_frames;           /* frame ticks (desktop sleeps) so far */
static uint32_t  g_flips;            /* fb.flip and fb.flip_rects calls */

uint32_t preview_frame_count(void) { return g_frames; }
uint32_t preview_flip_count(void) { return g_flips; }
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

/* ============ File system (preview only) ============ */
/*
 * SYS_READDIR / SYS_STAT read a host folder that stands in for the
 * NeolyxOS root: $NX_PREVIEW_FSROOT. Without it, both return -ENOSYS
 * (the shell then shows its "cannot list files" state). Paths reaching
 * here were already validated by the Path relay; ".." is refused again.
 */
typedef struct { char name[256]; int32_t type; uint64_t inode; uint64_t size; } host_dirent_t;
typedef struct { uint64_t ino; uint32_t mode, nlink, uid, gid; uint64_t size, at, mt, ct; } host_stat_t;

static int host_fs_path(const char *vpath, char *out, size_t cap) {
    const char *root = getenv("NX_PREVIEW_FSROOT");
    if (!root || !root[0] || !vpath || vpath[0] != '/' || strstr(vpath, "..")) return -1;
    int n = snprintf(out, cap, "%s%s", root, vpath);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

static int64_t host_readdir(const char *vpath, host_dirent_t *ents, uint64_t count, uint32_t *nread) {
    char hp[1024];
    if (!getenv("NX_PREVIEW_FSROOT")) return -38;
    if (host_fs_path(vpath, hp, sizeof hp) != 0 || !ents) return -14;
    DIR *d = opendir(hp);
    if (!d) return -1;                                  /* VFS_ERR_NOT_FOUND */
    uint32_t n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < count) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char cp[1280];
        struct stat st;
        snprintf(cp, sizeof cp, "%s/%s", hp, de->d_name);
        if (stat(cp, &st) != 0) continue;
        memset(&ents[n], 0, sizeof ents[n]);
        snprintf(ents[n].name, sizeof ents[n].name, "%s", de->d_name);
        ents[n].type = S_ISDIR(st.st_mode) ? 2 : 1;
        ents[n].inode = (uint64_t)st.st_ino;
        ents[n].size = S_ISDIR(st.st_mode) ? 0 : (uint64_t)st.st_size;
        n++;
    }
    closedir(d);
    if (nread) *nread = n;
    return 0;
}

static int64_t host_stat(const char *vpath, host_stat_t *out) {
    char hp[1024];
    struct stat st;
    if (!getenv("NX_PREVIEW_FSROOT")) return -38;
    if (host_fs_path(vpath, hp, sizeof hp) != 0 || !out) return -14;
    if (stat(hp, &st) != 0) return -1;
    memset(out, 0, sizeof *out);
    out->mode = S_ISDIR(st.st_mode) ? 0040755u : 0100644u;
    out->size = (uint64_t)st.st_size;
    return 0;
}

int64_t nx_host_syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    if (num == 30)
        return host_readdir((const char *)(uintptr_t)a1, (host_dirent_t *)(uintptr_t)a2, (uint64_t)a3,
                            (uint32_t *)(uintptr_t)a4);
    return nx_host_syscall(num, a1, a2, a3);
}

/* ============ Dispatcher ============ */

int64_t nx_host_syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    switch (num) {
        case SYS_TIME:
            return (int64_t)g_clock_ms;
        case SYS_STAT:
            return host_stat((const char *)(uintptr_t)a1, (host_stat_t *)(uintptr_t)a2);
        case SYS_SLEEP:
            /* The desktop sleeps once per frame tick (drawn or skipped), so
             * the tick, not the flip, is the harness's frame. */
            if (a1 > 0 && a1 < 100000) g_clock_ms += (uint64_t)a1;
            g_frames++;
            preview_on_flip(g_frames);  /* script actions, screenshots, exit */
            return 0;
        case SYS_YIELD:
            return 0;
        case SYS_BRK: {
            /* Program break inside the preallocated heap: brk(0) reports the
             * current break, brk(addr) moves it (nx_malloc grows on demand). */
            static uint64_t brk_cur;
            uint64_t base = (uint64_t)(uintptr_t)g_heap, limit = base + (96ull << 20);
            if (!brk_cur) brk_cur = base;
            if (a1 == 0) return (int64_t)brk_cur;
            if ((uint64_t)a1 < base || (uint64_t)a1 > limit) return -12;   /* ENOMEM */
            brk_cur = (uint64_t)a1;
            return (int64_t)brk_cur;
        }
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
            g_flips++;
            return 0;
        }
        case SYS_FB_FLIP_RECTS: {
            /* Same contract as the kernel (kernel/src/core/nx_fb.c) */
            const uint32_t *src = (const uint32_t *)(uintptr_t)a1;
            const nx_fb_rect_t *r = (const nx_fb_rect_t *)(uintptr_t)a2;
            if (a3 <= 0 || a3 > NX_FB_RECTS_MAX) return -22;
            if (!src || !r) return -14;
            for (int64_t i = 0; i < a3; i++) {
                uint32_t x0, y0, x1, y1;
                if (!nx_fb_clip_rect(&r[i], PREVIEW_W, PREVIEW_H, &x0, &y0, &x1, &y1)) continue;
                for (uint32_t y = y0; y < y1; y++)
                    memcpy(g_fb + (size_t)y * PREVIEW_W + x0, src + (size_t)y * PREVIEW_W + x0,
                           (size_t)(x1 - x0) * 4);
            }
            g_flips++;
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
        case SYS_CLOSE: case SYS_READ: case SYS_WRITE: case SYS_SEEK:
            return -9;
        case SYS_EXIT:
            fprintf(stderr, "preview: desktop called exit(%lld)\n", (long long)a1);
            exit((int)a1);
        default:
            return -38;  /* ENOSYS */
    }
}
