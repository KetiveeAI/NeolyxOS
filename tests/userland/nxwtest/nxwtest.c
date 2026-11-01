/*
 * NeolyxOS nxwtest - libnxwindow against the desktop's window server
 *
 * Checks docs/desktop/proposals/libnxwindow.md (week 4, step 1) from a
 * separate process, using only the library (sdk/nxwindow/nxwindow.h):
 *
 *   1. Connection: nxw_connect reports the screen and limits; a second
 *      connect is refused.
 *   2. Errors: a 64-byte title, sizes 0 and 4097, unknown flags, the ninth
 *      window, a destroyed window, a commit without a frame.
 *   3. The frame rule: the first frame is a new zeroed buffer; after a
 *      commit the buffer is busy until NXW_EV_FRAME; the next frame is the
 *      same buffer with its contents kept; 30 small-damage frames each get
 *      their FRAME_DONE.
 *   4. Memory: opening two windows with buffers and destroying them returns
 *      all memory (after a warm-up round for the desktop's heap).
 *   5. Interactive: two windows stay open. "nxwtest: square" (resizable)
 *      animates a 64x64 square with only the old and new square as damage
 *      and redraws at the new size on CONFIGURE (checked: new buffer, full
 *      redraw); "nxwtest: ticks" redraws one bar per second. Keys and
 *      clicks are logged. Each window closes on its close button; the test
 *      ends when both are closed or after 120 s.
 *
 * Output: "[NXWTEST] PASS/FAIL ..." lines, then "[NXWTEST] RESULT: PASS" or
 * "RESULT: FAIL" for parts 1-4, then the interactive part's own PASS/FAIL
 * lines and "[NXWTEST] INTERACTIVE: PASS/FAIL". Exit code 0 or 1.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nxsyscall.h"
#include "nxwindow.h"

#define SYS_INFO_NR       50
#define LEAK_TOLERANCE    (64ULL * 1024ULL)
#define FRAME_WAIT_MS     1000
#define INTERACTIVE_MS    120000

typedef struct {            /* sysinfo_t (kernel) */
    char os_name[32];
    char os_version[32];
    char arch[16];
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t num_cpus;
    uint32_t num_processes;
} sys_info_t;

/* ============ Output ============ */

static char g_line[192];
static int g_len;
static int g_failed;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_dec(int64_t v) {
    char b[24];
    int i = 23;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    b[i] = '\0';
    if (u == 0) b[--i] = '0';
    while (u && i > 1) { b[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) b[--i] = '-';
    out_str(&b[i]);
}

static void flush(void) {
    g_line[g_len] = '\0';
    nx_debug_print(g_line);
    g_len = 0;
}

static void check(int ok, const char *what, int64_t got) {
    out_str(ok ? "[NXWTEST] PASS " : "[NXWTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_dec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

static void note(const char *what, int64_t v) {
    out_str("[NXWTEST] ");
    out_str(what);
    out_dec(v);
    out_str("\n");
    flush();
}

static void finish(int code) {
    nx_exit(code);
}

/* ============ Helpers ============ */

static uint64_t free_memory(void) {
    sys_info_t info;
    if (syscall1(SYS_INFO_NR, (int64_t)(uintptr_t)&info) != 0) return 0;
    return info.free_memory;
}

static void fill(const nxw_frame_t *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    int32_t x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int32_t)f->width) x1 = (int32_t)f->width;
    if (y1 > (int32_t)f->height) y1 = (int32_t)f->height;
    for (int32_t yy = y; yy < y1; yy++) {
        uint32_t *row = f->pixels + (uint64_t)yy * (f->stride / 4);
        for (int32_t xx = x; xx < x1; xx++) row[xx] = c;
    }
}

/* Wait for NXW_EV_FRAME of w (other events are noted and dropped).
 * 1 got it, 0 not within ms. */
static int wait_frame(nxw_window_t *w, uint64_t ms) {
    uint64_t start = nx_gettime();
    for (;;) {
        uint64_t spent = nx_gettime() - start;
        if (spent >= ms) return 0;
        nxw_event_t ev;
        int r = nxw_wait_event(&ev, ms - spent);
        if (r <= 0) return 0;
        if (ev.type == NXW_EV_FRAME && ev.window == w) return 1;
        if (ev.type == NXW_EV_DISCONNECTED) return 0;
    }
}

static nxw_window_t *open_window(const char *title, uint32_t w, uint32_t h, uint32_t flags) {
    nxw_window_desc_t d;
    d.title = title;
    d.width = w;
    d.height = h;
    d.x = NXW_POS_AUTO;
    d.y = NXW_POS_AUTO;
    d.flags = flags;
    nxw_window_t *out = 0;
    int r = nxw_window_create(&d, &out);
    return r == 0 ? out : 0;
}

/* ============ 2. Errors ============ */

static void test_errors(void) {
    nxw_window_desc_t d;
    nxw_window_t *w = 0;
    char t64[65];
    for (int i = 0; i < 64; i++) t64[i] = 'a';
    t64[64] = '\0';

    d.title = t64; d.width = 100; d.height = 100; d.x = NXW_POS_AUTO; d.y = NXW_POS_AUTO; d.flags = 0;
    check(nxw_window_create(&d, &w) == -NXW_EINVAL, "a 64-byte title is refused", 0);
    d.title = "nxwtest";
    d.width = 0;
    int r = nxw_window_create(&d, &w);
    check(r == -NXW_EINVAL, "width 0 is refused", r);
    d.width = 4097;
    r = nxw_window_create(&d, &w);
    check(r == -NXW_EINVAL, "width 4097 is refused", r);
    d.width = 100;
    d.flags = 0x80;
    r = nxw_window_create(&d, &w);
    check(r == -NXW_EINVAL, "unknown flags are refused", r);

    /* Eight windows (never committed, so never shown), then the ninth */
    nxw_window_t *ws[8];
    int made = 0;
    for (int i = 0; i < 8; i++) {
        ws[i] = open_window("nxwtest limit", 50, 50, 0);
        if (ws[i]) made++;
    }
    check(made == 8, "eight windows can be created", made);
    d.flags = 0;
    r = nxw_window_create(&d, &w);
    check(r == -NXW_EMFILE, "the ninth window is -EMFILE", r);
    int destroyed = 0;
    for (int i = 0; i < 8; i++) if (ws[i] && nxw_window_destroy(ws[i]) == 0) destroyed++;
    check(destroyed == made, "all eight are destroyed", destroyed);

    nxw_frame_t f;
    r = ws[0] ? nxw_frame_begin(ws[0], &f) : -NXW_EBADF;
    check(r == -NXW_EBADF, "a destroyed window is -EBADF", r);
    r = ws[0] ? nxw_window_destroy(ws[0]) : -NXW_EBADF;
    check(r == -NXW_EBADF, "destroying it again is -EBADF", r);

    w = open_window("nxwtest", 40, 40, 0);
    r = w ? nxw_frame_commit(w, 0, 0) : 0;
    check(w && r == -NXW_EINVAL, "commit without a frame is -EINVAL", r);
    if (w) nxw_window_destroy(w);
}

/* ============ 3. The frame rule ============ */

static void test_frames(void) {
    nxw_window_t *w = open_window("nxwtest frames", 160, 100, 0);
    check(w != 0, "a 160x100 window opens", 0);
    if (!w) return;
    nxw_frame_t f;
    int r = nxw_frame_begin(w, &f);
    check(r == 0 && f.full == 1 && f.width == 160 && f.height == 100 && f.stride >= 640,
          "the first frame is a new 160x100 buffer", r);
    if (r != 0) { nxw_window_destroy(w); return; }
    int zero = 1;
    for (uint32_t y = 0; y < f.height; y++)
        for (uint32_t x = 0; x < f.width; x++)
            if (f.pixels[y * (f.stride / 4) + x] != 0) zero = 0;
    check(zero, "the new buffer is zeroed", 0);
    fill(&f, 0, 0, 160, 100, 0x00203040u);
    fill(&f, 10, 10, 20, 20, 0x00E0A020u);
    r = nxw_frame_commit(w, 0, 0);
    check(r == 0, "the whole first frame is committed", r);
    r = nxw_frame_begin(w, &f);
    check(r == -NXW_EBUSY, "the buffer is busy until FRAME_DONE", r);
    uint64_t t0 = nx_gettime();
    int got = wait_frame(w, FRAME_WAIT_MS);
    check(got, "NXW_EV_FRAME arrives", (int64_t)(nx_gettime() - t0));
    r = nxw_frame_begin(w, &f);
    check(r == 0 && f.full == 0 && f.pixels[15 * (f.stride / 4) + 15] == 0x00E0A020u,
          "the next frame is the same buffer, contents kept", r);
    if (r != 0) { nxw_window_destroy(w); return; }

    /* 30 frames with an 8x8 damage rectangle each */
    int ok = 0;
    t0 = nx_gettime();
    for (int i = 0; i < 30; i++) {
        if (i > 0 && nxw_frame_begin(w, &f) != 0) break;
        int32_t x = 4 + (i * 5) % 140;
        fill(&f, x, 60, 8, 8, 0x0040C040u + (uint32_t)i);
        nxw_rect_t d = { x, 60, 8, 8 };
        if (nxw_frame_commit(w, &d, 1) != 0) break;
        if (!wait_frame(w, FRAME_WAIT_MS)) break;
        ok++;
    }
    uint64_t ms = nx_gettime() - t0;
    check(ok == 30, "30 small-damage frames each get FRAME_DONE", ok);
    note("ms for 30 frames: ", (int64_t)ms);
    check(nxw_window_destroy(w) == 0, "the window is destroyed", 0);
}

/* ============ 4. Memory ============ */

/* Two windows with committed buffers, then destroyed. */
static int buffer_round(void) {
    nxw_window_t *a = open_window("nxwtest memory A", 400, 300, 0);
    nxw_window_t *b = open_window("nxwtest memory B", 300, 200, 0);
    int ok = a && b;
    nxw_frame_t f;
    if (a && nxw_frame_begin(a, &f) == 0) { fill(&f, 0, 0, 400, 300, 0x00305070u); nxw_frame_commit(a, 0, 0); }
    else ok = 0;
    if (b && nxw_frame_begin(b, &f) == 0) { fill(&f, 0, 0, 300, 200, 0x00703050u); nxw_frame_commit(b, 0, 0); }
    else ok = 0;
    if (a && !wait_frame(a, FRAME_WAIT_MS)) ok = 0;
    if (a) nxw_window_destroy(a);
    if (b) nxw_window_destroy(b);
    nx_sleep(1000);         /* the shell's close animation, then it drops the buffers */
    return ok;
}

static void test_memory(void) {
    check(buffer_round(), "warm-up: two windows shown and destroyed", 0);
    uint64_t before = free_memory();
    check(buffer_round(), "two windows shown and destroyed", 0);
    uint64_t after = free_memory();
    int64_t diff = (int64_t)before - (int64_t)after;
    check(before != 0 && diff <= (int64_t)LEAK_TOLERANCE, "all buffer memory comes back (bytes missing)", diff);
}

/* ============ 5. Interactive ============ */

#define SQ 64

typedef struct {
    nxw_window_t *w;
    uint32_t width, height;
    int need_full;
    int32_t sq_x, sq_y;         /* square A: last drawn position, -1 none */
    uint32_t ticks;             /* window B: bars drawn */
    uint32_t want_ticks;
    uint64_t frames;
} iwin_t;

static int g_ifailed;

static void icheck(int ok, const char *what, int64_t got) {
    check(ok, what, got);
    if (!ok) g_ifailed = 1;
}

static void draw_square(iwin_t *s, uint64_t now) {
    nxw_frame_t f;
    int r = nxw_frame_begin(s->w, &f);
    if (r == -NXW_EBUSY) return;
    if (r != 0) { icheck(0, "square: frame_begin", r); s->w = 0; return; }
    int resized = f.width != s->width || f.height != s->height;
    if (resized) {
        icheck(f.full == 1, "resize gives a new buffer (width)", f.width);
        s->width = f.width;
        s->height = f.height;
    }
    int32_t span_x = (int32_t)f.width > SQ ? (int32_t)f.width - SQ : 1;
    int32_t span_y = (int32_t)f.height > SQ ? (int32_t)f.height - SQ : 1;
    int32_t px = (int32_t)((now / 8) % (uint64_t)(2 * span_x));
    int32_t py = (int32_t)((now / 12) % (uint64_t)(2 * span_y));
    if (px >= span_x) px = 2 * span_x - px;
    if (py >= span_y) py = 2 * span_y - py;

    if (f.full || s->need_full) {
        fill(&f, 0, 0, (int32_t)f.width, (int32_t)f.height, 0x00182030u);
        fill(&f, px, py, SQ, SQ, 0x00F0A030u);
        nxw_frame_commit(s->w, 0, 0);
        s->need_full = 0;
    } else {
        nxw_rect_t d[2];
        uint32_t n = 0;
        if (s->sq_x >= 0) {
            fill(&f, s->sq_x, s->sq_y, SQ, SQ, 0x00182030u);
            d[n].x = s->sq_x; d[n].y = s->sq_y; d[n].w = SQ; d[n].h = SQ; n++;
        }
        fill(&f, px, py, SQ, SQ, 0x00F0A030u);
        d[n].x = px; d[n].y = py; d[n].w = SQ; d[n].h = SQ; n++;
        nxw_frame_commit(s->w, d, n);
    }
    s->sq_x = px;
    s->sq_y = py;
    s->frames++;
}

static void draw_ticks(iwin_t *s) {
    if (!s->need_full && s->ticks == s->want_ticks) return;
    nxw_frame_t f;
    int r = nxw_frame_begin(s->w, &f);
    if (r == -NXW_EBUSY) return;
    if (r != 0) { icheck(0, "ticks: frame_begin", r); s->w = 0; return; }
    uint32_t bars = f.width / 8;
    if (bars == 0) bars = 1;
    if (f.full || s->need_full) {
        fill(&f, 0, 0, (int32_t)f.width, (int32_t)f.height, 0x00202020u);
        for (uint32_t i = 0; i < s->ticks % bars; i++)
            fill(&f, (int32_t)(i * 8 + 2), 20, 5, (int32_t)f.height - 40, 0x0030C0F0u);
        nxw_frame_commit(s->w, 0, 0);
        s->need_full = 0;
    } else {
        /* One new bar (or a cleared row when the bars wrap) */
        s->ticks = s->want_ticks;
        uint32_t i = s->ticks % bars;
        if (i == 0) {
            fill(&f, 0, 20, (int32_t)f.width, (int32_t)f.height - 40, 0x00202020u);
            nxw_rect_t d = { 0, 20, f.width, f.height - 40 };
            nxw_frame_commit(s->w, &d, 1);
        } else {
            int32_t x = (int32_t)((i - 1) * 8 + 2);
            fill(&f, x, 20, 5, (int32_t)f.height - 40, 0x0030C0F0u);
            nxw_rect_t d = { x, 20, 5, f.height - 40 };
            nxw_frame_commit(s->w, &d, 1);
        }
    }
    s->ticks = s->want_ticks;
    s->frames++;
}

static iwin_t *which(iwin_t *a, iwin_t *b, nxw_window_t *w) {
    if (w && a->w == w) return a;
    if (w && b->w == w) return b;
    return 0;
}

static void interactive(void) {
    iwin_t A = { 0 }, B = { 0 };
    A.w = open_window("nxwtest: square", 360, 240, NXW_WIN_RESIZABLE);
    B.w = open_window("nxwtest: ticks", 200, 120, 0);
    icheck(A.w && B.w, "the interactive windows open", 0);
    if (!A.w || !B.w) return;
    A.width = 360; A.height = 240; A.sq_x = -1;
    B.width = 200; B.height = 120;
    out_str("[NXWTEST] interactive: two windows open; close both (or wait 120 s)\n");
    flush();

    uint64_t start = nx_gettime();
    uint64_t resizes = 0, keys = 0, clicks = 0;
    int closed_by_user = 0;
    while (A.w || B.w) {
        uint64_t now = nx_gettime();
        if (now - start >= INTERACTIVE_MS) break;
        B.want_ticks = (uint32_t)((now - start) / 1000);
        if (A.w) draw_square(&A, now);
        if (B.w) draw_ticks(&B);

        uint64_t next_tick = start + (uint64_t)(B.want_ticks + 1) * 1000;
        uint64_t timeout = next_tick > now ? next_tick - now : 1;
        nxw_event_t ev;
        int r = nxw_wait_event(&ev, timeout);
        while (r == 1) {
            iwin_t *s = which(&A, &B, ev.window);
            switch (ev.type) {
            case NXW_EV_CONFIGURE:
                if (s && (ev.configure.width != s->width || ev.configure.height != s->height)) {
                    resizes++;
                    note("CONFIGURE width ", ev.configure.width);
                    note("CONFIGURE height ", ev.configure.height);
                }
                if (s) s->need_full = 1;
                break;
            case NXW_EV_KEY:
                if (ev.key.pressed) { keys++; note("key ascii ", ev.key.ascii); }
                break;
            case NXW_EV_POINTER:
                if (ev.pointer.kind == NXW_PTR_BUTTON && ev.pointer.buttons) {
                    clicks++;
                    note("click x ", ev.pointer.x);
                    note("click y ", ev.pointer.y);
                }
                break;
            case NXW_EV_CLOSE:
                if (s) {
                    note(s == &A ? "close button: square window " : "close button: ticks window ", 0);
                    nxw_window_destroy(s->w);
                    s->w = 0;
                    closed_by_user++;
                }
                break;
            case NXW_EV_ERROR:
                icheck(0, "the server refused a request (code)", ev.error.code);
                break;
            case NXW_EV_DISCONNECTED:
                icheck(0, "the window server went away", 0);
                A.w = B.w = 0;
                break;
            default:
                break;
            }
            r = nxw_poll_event(&ev);
        }
        if (r < 0 && r != -NXW_ENOTCONN) { icheck(0, "event wait failed", r); break; }
    }
    if (A.w) nxw_window_destroy(A.w);
    if (B.w) nxw_window_destroy(B.w);
    note("interactive: square frames ", (int64_t)A.frames);
    note("interactive: ticks frames ", (int64_t)B.frames);
    note("interactive: resizes ", (int64_t)resizes);
    note("interactive: keys ", (int64_t)keys);
    note("interactive: clicks ", (int64_t)clicks);
    note(closed_by_user == 2 ? "interactive: closed by the user " : "interactive: ended after 120 s, windows closed by user: ",
         closed_by_user);
}

/* ============ Main ============ */

void _start(long argc, char **argv) {
    (void)argc; (void)argv;
    note("started, PID ", syscall0(SYS_GETPID));

    nxw_display_info_t info;
    int r = nxw_connect(&info);
    check(r == 0, "nxw_connect reaches the window server", r);
    if (r != 0) {
        out_str("[NXWTEST] RESULT: FAIL (no window server)\n");
        flush();
        finish(1);
    }
    check(info.proto_major == 1 && info.screen_w > 0 && info.screen_h > 0 &&
          info.max_windows == 8 && info.max_size == 4096, "WELCOME: protocol 1, screen and limits", info.screen_w);
    r = nxw_connect(0);
    check(r == -NXW_EISCONN, "a second nxw_connect is -EISCONN", r);

    test_errors();
    test_frames();
    test_memory();

    out_str(g_failed ? "[NXWTEST] RESULT: FAIL\n" : "[NXWTEST] RESULT: PASS\n");
    flush();
    int failed = g_failed;

    interactive();
    nxw_disconnect();
    out_str(g_ifailed ? "[NXWTEST] INTERACTIVE: FAIL\n" : "[NXWTEST] INTERACTIVE: PASS\n");
    flush();
    finish(failed || g_ifailed ? 1 : 0);
}
