/*
 * NeolyxOS wintest - window protocol 1.0 against the desktop's nxserver
 *
 * Checks docs/desktop/proposals/nxserver-window-protocol.md (week 3,
 * step 2) with the raw protocol (sdk/nxwindow/nxwin_proto.h), from a
 * separate process:
 *
 *   1. Hostile cases. Every protocol violation closes that connection
 *      (each case uses its own connection) and every recoverable mistake
 *      gets the documented ERROR while the connection stays usable:
 *      messages before HELLO, a wrong major version, bad lengths, unknown
 *      ops, wrong handle counts, a channel handle or an unmappable shm
 *      handle in BUF_ATTACH, bad sizes, flags and titles, unknown and
 *      foreign window ids, the 8-windows limit, COMMIT before a buffer,
 *      bad strides and offsets, too many damage rects, a second HELLO, and
 *      a client that stops reading its events (the server keeps running
 *      and drops events instead of blocking).
 *   2. Crash cleanup. A child opens a window, waits until it is on screen
 *      and crashes; the parent checks that all memory comes back (the
 *      server unmaps the window buffer when the connection dies). A first
 *      child warms the desktop up so the measurement sees leaks only.
 *   3. A real window. A 360x240 window animates a 64x64 square, sending
 *      only the old and new square as damage, redraws at the new size on
 *      CONFIGURE, logs keys and pointer events, and destroys its window on
 *      CLOSE_REQUEST (or after 120 s).
 *
 * Output: "[WINTEST] PASS/FAIL ..." lines, notes, and one final
 * "[WINTEST] RESULT: PASS" or "[WINTEST] RESULT: FAIL". Exit code 0 or 1.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_ipc.h"
#include "../../../sdk/nxwindow/nxwin_proto.h"

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_ABI_QUERY     0
#define SYS_EXIT          1
#define SYS_SLEEP         2
#define SYS_GETPID        5
#define SYS_WAIT          9
#define SYS_SPAWN         13
#define SYS_INFO          50
#define SYS_DEBUG_PRINT   127
#define SYS_HANDLE_CLOSE  140
#define SYS_SHM_CREATE    141
#define SYS_SHM_MAP       142
#define SYS_SHM_UNMAP     143
#define SYS_CHAN_CREATE   144
#define SYS_CHAN_SEND     145
#define SYS_CHAN_RECV     146
#define SYS_OBJ_WAIT      147
#define SYS_HANDLE_DUP    149
#define SYS_PORT_CONNECT  151

#define SPAWN_WAITABLE    0x1
#define EAGAIN            11
#define EPIPE             32
#define ETIMEDOUT         110
#define FAULT_EXIT_PF     (128 + 14)          /* terminate() code for a page fault */
#define LEAK_TOLERANCE    (64ULL * 1024ULL)
#define REPLY_MS          2000                /* the server answers within a frame or two */

typedef struct {            /* nx_abi_info_t */
    uint16_t major, minor, patch, flags;
    uint32_t syscall_count;
    uint32_t reserved[3];
} abi_info_t;

typedef struct {            /* sysinfo_t */
    char os_name[32];
    char os_version[32];
    char arch[16];
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t num_cpus;
    uint32_t num_processes;
} sys_info_t;

static inline int64_t sc1(int64_t n, int64_t a1) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t sc2(int64_t n, int64_t a1, int64_t a2) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t sc3(int64_t n, int64_t a1, int64_t a2, int64_t a3) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

static int64_t h_close(int64_t h) { return sc1(SYS_HANDLE_CLOSE, h); }
static int64_t h_dup(int64_t h, uint64_t rights) { return sc2(SYS_HANDLE_DUP, h, (int64_t)rights); }
static int64_t shm_create(uint64_t size) { return sc2(SYS_SHM_CREATE, (int64_t)size, 0); }
static int64_t shm_map(int64_t h, uint64_t prot) { return sc3(SYS_SHM_MAP, h, (int64_t)prot, 0); }
static int64_t shm_unmap(int64_t addr) { return sc1(SYS_SHM_UNMAP, addr); }
static int64_t chan_create(nx_handle_t *out) { return sc2(SYS_CHAN_CREATE, (int64_t)out, 0); }
static int64_t chan_send(int64_t h, nx_chan_msg_t *m) { return sc2(SYS_CHAN_SEND, h, (int64_t)m); }
static int64_t chan_recv(int64_t h, nx_chan_msg_t *m) { return sc2(SYS_CHAN_RECV, h, (int64_t)m); }
static int64_t obj_wait(nx_wait_item_t *it, uint64_t n, uint64_t ms) { return sc3(SYS_OBJ_WAIT, (int64_t)it, (int64_t)n, (int64_t)ms); }
static int64_t port_connect(const char *name) { return sc2(SYS_PORT_CONNECT, (int64_t)name, 0); }
static void sleep_ms(uint64_t ms) { sc1(SYS_SLEEP, (int64_t)ms); }

/* ============ Output ============ */

static char g_line[192];
static int g_len;
static int g_failed;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_sdec(int64_t v) {
    char buf[22];
    int i = 21;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 1) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) buf[--i] = '-';
    out_str(&buf[i]);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

static void check(int ok, const char *what, int64_t got) {
    out_str(ok ? "[WINTEST] PASS " : "[WINTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_sdec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

static void note(const char *what, int64_t v) {
    out_str("[WINTEST] ");
    out_str(what);
    out_sdec(v);
    out_str("\n");
    flush();
}

/* ============ Helpers ============ */

static uint64_t sysinfo_field(int which) {
    sys_info_t info;
    if (sc1(SYS_INFO, (int64_t)&info) != 0) return 0;
    return which ? info.uptime_ms : info.free_memory;
}
static uint64_t free_memory(void) { return sysinfo_field(0); }
static uint64_t uptime_ms(void) { return sysinfo_field(1); }

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void mzero(void *p, uint32_t n) {
    uint8_t *d = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void scopy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static uint32_t g_seq = 1;

static void hdr(void *m, uint16_t op, uint32_t len) {
    nxw_hdr_t *h = (nxw_hdr_t *)m;
    h->op = op;
    h->len = (uint16_t)len;
    h->seq = g_seq++;
}

static int64_t wait_sig(int64_t h, uint32_t signals, uint64_t ms, uint32_t *observed) {
    nx_wait_item_t it;
    it.handle = (nx_handle_t)h;
    it.signals = signals;
    it.observed = 0;
    it.reserved = 0;
    int64_t r = obj_wait(&it, 1, ms);
    if (observed) *observed = it.observed;
    return r;
}

/* Send len bytes (and handles), waiting for room when the server is busy. */
static int64_t send_raw(int64_t c, const void *data, uint32_t len, nx_handle_t *hs, uint32_t n) {
    nx_chan_msg_t m;
    m.data = (uint64_t)data;
    m.handles = (uint64_t)hs;
    m.data_len = len;
    m.handle_count = n;
    m.sender_pid = 0;
    m.flags = 0;
    for (int tries = 0; tries < 200; tries++) {
        int64_t r = chan_send(c, &m);
        if (r != -EAGAIN) return r;
        wait_sig(c, NX_SIGNAL_WRITABLE | NX_SIGNAL_PEER_CLOSED, 50, 0);
    }
    return -EAGAIN;
}

/* Receive one message into buf; 0, or a negative errno (-EAGAIN: none). */
static int64_t recv_msg(int64_t c, void *buf, uint32_t cap, uint32_t *len) {
    nx_chan_msg_t m;
    m.data = (uint64_t)buf;
    m.handles = 0;
    m.data_len = cap;
    m.handle_count = 0;
    m.sender_pid = 0;
    m.flags = 0;
    int64_t r = chan_recv(c, &m);
    if (r == 0 && len) *len = m.data_len;
    return r;
}

/* Wait for the next message (up to ms). 0 and the message, or -errno. */
static int64_t next_msg(int64_t c, void *buf, uint32_t cap, uint32_t *len, uint64_t ms) {
    uint32_t obs = 0;
    int64_t r = wait_sig(c, NX_SIGNAL_READABLE | NX_SIGNAL_PEER_CLOSED, ms, &obs);
    if (r < 0) return r;
    if (r == 0) return -ETIMEDOUT;
    return recv_msg(c, buf, cap, len);
}

/* The server closed this connection (PEER_CLOSED) within REPLY_MS.
 * Messages the server sent before closing are read and discarded. */
static int closed_by_server(int64_t c) {
    uint64_t end = uptime_ms() + REPLY_MS;
    while (uptime_ms() < end) {
        uint32_t obs = 0;
        int64_t r = wait_sig(c, NX_SIGNAL_PEER_CLOSED | NX_SIGNAL_READABLE, 100, &obs);
        if (r == -ETIMEDOUT) continue;      /* obj.wait reports a timeout as -ETIMEDOUT */
        if (r < 0) return 0;
        if (obs & NX_SIGNAL_READABLE) {
            uint8_t junk[64];
            recv_msg(c, junk, sizeof(junk), 0);
            continue;
        }
        if (obs & NX_SIGNAL_PEER_CLOSED) return 1;
    }
    return 0;
}

static int64_t connect_display(void) { return port_connect(NXW_PORT_NAME); }

/* HELLO and WELCOME. Returns 1 on success. */
static int do_hello(int64_t c, nxw_welcome_t *out) {
    nxw_hello_t h;
    mzero(&h, sizeof(h));
    hdr(&h, NXW_OP_HELLO, sizeof(h));
    h.major = NXW_PROTO_MAJOR;
    h.minor = NXW_PROTO_MINOR;
    if (send_raw(c, &h, sizeof(h), 0, 0) != 0) return 0;
    nxw_welcome_t w;
    uint32_t len = 0;
    if (next_msg(c, &w, sizeof(w), &len, REPLY_MS) != 0) return 0;
    if (len != sizeof(w) || w.hdr.op != NXW_OP_WELCOME || w.hdr.seq != h.hdr.seq) return 0;
    if (out) *out = w;
    return 1;
}

/* Wait for an ERROR for request op; returns its code (0 if none came). */
static int32_t expect_error(int64_t c, uint16_t op) {
    uint8_t buf[64];
    uint32_t len = 0;
    if (next_msg(c, buf, sizeof(buf), &len, REPLY_MS) != 0) return 0;
    const nxw_error_t *e = (const nxw_error_t *)buf;
    if (len != sizeof(*e) || e->hdr.op != NXW_OP_ERROR || e->op != op) return 0;
    return e->code;
}

static int64_t win_create(int64_t c, uint32_t w, uint32_t h, uint32_t flags, const char *title,
                          uint32_t *gw, uint32_t *gh) {
    nxw_win_create_t m;
    mzero(&m, sizeof(m));
    hdr(&m, NXW_OP_WIN_CREATE, sizeof(m));
    m.width = w;
    m.height = h;
    m.x = NXW_POS_AUTO;
    m.y = NXW_POS_AUTO;
    m.flags = flags;
    scopy(m.title, title, NXW_TITLE_MAX);
    if (send_raw(c, &m, sizeof(m), 0, 0) != 0) return -1;
    uint8_t buf[64];
    uint32_t len = 0;
    if (next_msg(c, buf, sizeof(buf), &len, REPLY_MS) != 0) return -1;
    const nxw_win_created_t *r = (const nxw_win_created_t *)buf;
    if (r->hdr.op == NXW_OP_ERROR) return ((const nxw_error_t *)buf)->code;
    if (len != sizeof(*r) || r->hdr.op != NXW_OP_WIN_CREATED || r->hdr.seq != m.hdr.seq) return -1;
    if (gw) *gw = r->width;
    if (gh) *gh = r->height;
    return r->win;
}

static int64_t win_destroy(int64_t c, uint32_t win) {
    nxw_win_destroy_t m;
    mzero(&m, sizeof(m));
    hdr(&m, NXW_OP_WIN_DESTROY, sizeof(m));
    m.win = win;
    return send_raw(c, &m, sizeof(m), 0, 0);
}

static int64_t buf_attach(int64_t c, uint32_t win, uint32_t w, uint32_t h, uint32_t stride,
                          uint64_t offset, nx_handle_t shm) {
    nxw_buf_attach_t m;
    mzero(&m, sizeof(m));
    hdr(&m, NXW_OP_BUF_ATTACH, sizeof(m));
    m.win = win;
    m.width = w;
    m.height = h;
    m.stride = stride;
    m.offset = offset;
    nx_handle_t hs[1] = { shm };
    return send_raw(c, &m, sizeof(m), hs, 1);
}

static uint32_t commit(int64_t c, uint32_t win, const nxw_rect_t *rects, uint32_t count) {
    nxw_commit_t m;
    mzero(&m, sizeof(m));
    hdr(&m, NXW_OP_COMMIT, sizeof(m));
    m.win = win;
    m.count = count;
    for (uint32_t i = 0; i < count && i < NXW_DAMAGE_MAX; i++) m.rects[i] = rects[i];
    if (send_raw(c, &m, sizeof(m), 0, 0) != 0) return 0;
    return m.hdr.seq;
}

/* ============ 1. Hostile cases ============ */

/* One violation per connection: send it, expect the server to hang up. */
static void hostile_close(const char *what, const void *msg, uint32_t len, nx_handle_t *hs,
                          uint32_t n, int hello_first) {
    int64_t c = connect_display();
    if (c <= 0) { check(0, what, c); return; }
    if (hello_first && !do_hello(c, 0)) { check(0, what, -1); h_close(c); return; }
    int64_t r = send_raw(c, msg, len, hs, n);
    int closed = (r == 0) && closed_by_server(c);
    check(closed, what, r);
    h_close(c);
}

static void test_hostile(void) {
    uint8_t big[96];

    /* -- Violations: the connection is closed -- */
    nxw_win_create_t wc;
    mzero(&wc, sizeof(wc));
    hdr(&wc, NXW_OP_WIN_CREATE, sizeof(wc));
    wc.width = 100; wc.height = 100;
    hostile_close("WIN_CREATE before HELLO closes the connection", &wc, sizeof(wc), 0, 0, 0);

    {   /* Wrong major: ERROR first, then closed */
        int64_t c = connect_display();
        nxw_hello_t h;
        mzero(&h, sizeof(h));
        hdr(&h, NXW_OP_HELLO, sizeof(h));
        h.major = 2;
        int64_t r = (c > 0) ? send_raw(c, &h, sizeof(h), 0, 0) : c;
        int32_t code = (r == 0) ? expect_error(c, NXW_OP_HELLO) : 0;
        check(code == -NXW_EINVAL, "HELLO with major 2 gets ERROR -EINVAL", code);
        check(c > 0 && closed_by_server(c), "... and the connection is closed", 0);
        if (c > 0) h_close(c);
    }

    nxw_hello_t hl;
    mzero(&hl, sizeof(hl));
    hdr(&hl, NXW_OP_HELLO, sizeof(hl) + 8);
    hl.major = 1;
    mzero(big, sizeof(big));
    for (uint32_t i = 0; i < sizeof(hl); i++) big[i] = ((uint8_t *)&hl)[i];
    hostile_close("header length differs from the message closes", big, sizeof(hl), 0, 0, 0);

    mzero(big, sizeof(big));
    hdr(big, 0x01FF, 16);
    hostile_close("unknown op 0x01FF closes", big, 16, 0, 0, 1);

    mzero(big, sizeof(big));
    hdr(big, NXW_OP_HELLO, 64);
    ((nxw_hello_t *)big)->major = 1;
    hostile_close("HELLO padded to 64 bytes (larger than its struct) closes", big, 64, 0, 0, 0);

    mzero(big, sizeof(big));
    hdr(big, NXW_OP_HELLO, 4);
    hostile_close("4-byte message (shorter than the header) closes", big, 4, 0, 0, 0);

    {
        int64_t shm = shm_create(4096);
        nx_handle_t hs[1] = { (nx_handle_t)shm };
        nxw_hello_t h;
        mzero(&h, sizeof(h));
        hdr(&h, NXW_OP_HELLO, sizeof(h));
        h.major = 1;
        hostile_close("HELLO carrying a handle closes", &h, sizeof(h), hs, 1, 0);
    }

    nxw_buf_attach_t ba;
    mzero(&ba, sizeof(ba));
    hdr(&ba, NXW_OP_BUF_ATTACH, sizeof(ba));
    ba.win = 1; ba.width = 16; ba.height = 16; ba.stride = 64;
    hostile_close("BUF_ATTACH without a handle closes", &ba, sizeof(ba), 0, 0, 1);

    {   /* A channel endpoint instead of shared memory */
        int64_t c = connect_display();
        nxw_welcome_t wel;
        int ok = c > 0 && do_hello(c, &wel);
        int64_t win = ok ? win_create(c, 64, 64, 0, "hostile", 0, 0) : -1;
        nx_handle_t pair[2] = { 0, 0 };
        int64_t r = chan_create(pair);
        nxw_buf_attach_t b2 = ba;
        hdr(&b2, NXW_OP_BUF_ATTACH, sizeof(b2));
        b2.win = (uint32_t)win;
        nx_handle_t hs[1] = { pair[0] };
        if (win > 0 && r == 0) r = send_raw(c, &b2, sizeof(b2), hs, 1);
        check(win > 0 && r == 0 && closed_by_server(c), "BUF_ATTACH with a channel handle closes", r);
        if (pair[1] > 0) h_close(pair[1]);
        if (c > 0) h_close(c);
    }

    {   /* Shared memory without the MAP right */
        int64_t c = connect_display();
        int ok = c > 0 && do_hello(c, 0);
        int64_t win = ok ? win_create(c, 64, 64, 0, "hostile", 0, 0) : -1;
        int64_t shm = shm_create(64 * 64 * 4);
        int64_t ro = (shm > 0) ? h_dup(shm, NX_RIGHT_READ | NX_RIGHT_TRANSFER) : -1;
        int64_t r = -1;
        if (win > 0 && ro > 0) r = buf_attach(c, (uint32_t)win, 64, 64, 256, 0, (nx_handle_t)ro);
        check(r == 0 && closed_by_server(c), "BUF_ATTACH with an unmappable shm handle closes", r);
        if (shm > 0) h_close(shm);
        if (c > 0) h_close(c);
    }

    /* -- Recoverable: ERROR, the connection stays open -- */
    int64_t c = connect_display();
    nxw_welcome_t wel;
    mzero(&wel, sizeof(wel));
    int ok = c > 0 && do_hello(c, &wel);
    check(ok && wel.major == 1 && wel.max_windows == 8 && wel.max_size == 4096 && wel.screen_w > 0,
          "HELLO gets WELCOME 1.x (8 windows, 4096 px)", wel.minor);
    if (!ok) return;

    check(win_create(c, 0, 100, 0, "bad", 0, 0) == -NXW_EINVAL, "WIN_CREATE width 0: -EINVAL", 0);
    check(win_create(c, 5000, 100, 0, "bad", 0, 0) == -NXW_EINVAL, "WIN_CREATE width 5000: -EINVAL", 0);
    check(win_create(c, 100, 100, 0x80, "bad", 0, 0) == -NXW_EINVAL, "WIN_CREATE unknown flag: -EINVAL", 0);
    {
        nxw_win_create_t m;
        mzero(&m, sizeof(m));
        hdr(&m, NXW_OP_WIN_CREATE, sizeof(m));
        m.width = 100; m.height = 100;
        for (int i = 0; i < NXW_TITLE_MAX; i++) m.title[i] = 'T';
        send_raw(c, &m, sizeof(m), 0, 0);
        check(expect_error(c, NXW_OP_WIN_CREATE) == -NXW_EINVAL, "WIN_CREATE title without a NUL: -EINVAL", 0);
    }
    win_destroy(c, 999999);
    check(expect_error(c, NXW_OP_WIN_DESTROY) == -NXW_EBADF, "WIN_DESTROY of an unknown id: -EBADF", 0);

    int64_t wins[9];
    int created = 0;
    for (int i = 0; i < 8; i++) {
        wins[i] = win_create(c, 64, 64, 0, "limit", 0, 0);
        if (wins[i] > 0) created++;
    }
    check(created == 8, "8 windows per client", created);
    wins[8] = win_create(c, 64, 64, 0, "limit", 0, 0);
    check(wins[8] == -NXW_EMFILE, "the 9th window: -EMFILE", wins[8]);
    for (int i = 1; i < 8; i++) if (wins[i] > 0) win_destroy(c, (uint32_t)wins[i]);
    uint32_t win = (uint32_t)wins[0];

    {   /* Another client may not touch this one's window */
        int64_t c2 = connect_display();
        int ok2 = c2 > 0 && do_hello(c2, 0);
        if (ok2) win_destroy(c2, win);
        check(ok2 && expect_error(c2, NXW_OP_WIN_DESTROY) == -NXW_EPERM, "WIN_DESTROY of another client's window: -EPERM", 0);
        if (c2 > 0) h_close(c2);
    }

    nxw_rect_t full;
    full.x = 0; full.y = 0; full.w = 64; full.h = 64;
    commit(c, win, &full, 1);
    check(expect_error(c, NXW_OP_COMMIT) == -NXW_EINVAL, "COMMIT before any buffer: -EINVAL", 0);

    int64_t shm = shm_create(64 * 64 * 4);
    check(shm > 0, "shm.create for the buffer tests", shm);
    if (shm <= 0) { h_close(c); return; }
    struct { uint32_t stride; uint64_t offset; const char *what; } bad[] = {
        { 252, 0, "stride below width * 4: -EINVAL" },
        { 258, 0, "stride not a multiple of 4: -EINVAL" },
        { 16388, 0, "stride above 16384: -EINVAL" },
        { 256, 4096, "buffer past the end of the object: -EINVAL" },
        { 256, 0xFFFFFFFFFFFFFFF0ull, "offset near 2^64: -EINVAL" },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int64_t d = h_dup(shm, NX_RIGHTS_SAME);
        int64_t r = buf_attach(c, win, 64, 64, bad[i].stride, bad[i].offset, (nx_handle_t)d);
        check(r == 0 && expect_error(c, NXW_OP_BUF_ATTACH) == -NXW_EINVAL, bad[i].what, r);
    }
    int64_t r = buf_attach(c, win, 64, 64, 256, 0, (nx_handle_t)shm);   /* moves the handle */
    {
        nxw_commit_t m;
        mzero(&m, sizeof(m));
        hdr(&m, NXW_OP_COMMIT, sizeof(m));
        m.win = win;
        m.count = 17;
        send_raw(c, &m, sizeof(m), 0, 0);
        check(r == 0 && expect_error(c, NXW_OP_COMMIT) == -NXW_EINVAL, "COMMIT with 17 rects: -EINVAL", 0);
    }
    {
        nxw_hello_t h;
        mzero(&h, sizeof(h));
        hdr(&h, NXW_OP_HELLO, sizeof(h));
        h.major = 1;
        send_raw(c, &h, sizeof(h), 0, 0);
        check(expect_error(c, NXW_OP_HELLO) == -NXW_EINVAL, "a second HELLO: -EINVAL, still connected", 0);
    }
    {
        nxw_win_set_title_t m;
        mzero(&m, sizeof(m));
        hdr(&m, NXW_OP_WIN_SET_TITLE, sizeof(m));
        m.win = win;
        scopy(m.title, "bad \xFF utf-8", NXW_TITLE_MAX);
        send_raw(c, &m, sizeof(m), 0, 0);
        uint8_t buf[64];
        uint32_t len = 0;
        int64_t got = next_msg(c, buf, sizeof(buf), &len, 300);
        check(got == -ETIMEDOUT, "a title with invalid UTF-8 is accepted (replaced when drawn)", got);
    }

    /* A client that stops reading: 200 requests that each get an ERROR */
    int sent = 0;
    for (int i = 0; i < 200; i++) if (win_destroy(c, 900000 + (uint32_t)i) == 0) sent++;
    sleep_ms(500);
    int errors = 0;
    for (;;) {
        uint8_t buf[64];
        uint32_t len = 0;
        int64_t rr = next_msg(c, buf, sizeof(buf), &len, 300);
        if (rr != 0) break;
        if (((nxw_hdr_t *)buf)->op == NXW_OP_ERROR) errors++;
    }
    check(sent == 200, "200 requests sent to a busy server", sent);
    check(errors >= 64 && errors < 200, "the server dropped events instead of blocking (errors received)", errors);
    uint32_t gw = 0, gh = 0;
    int64_t w2 = win_create(c, 64, 64, 0, "still alive", &gw, &gh);
    check(w2 > 0, "the connection still works after the flood", w2);
    if (w2 > 0) win_destroy(c, (uint32_t)w2);
    win_destroy(c, win);
    h_close(c);
    sleep_ms(100);
}

/* ============ 2. Crash cleanup ============ */

/* Child: show a window, wait until it is on screen, crash. Exit codes
 * 20+ say which step failed if it never got to crash. */
static void crash_child(void) {
    int64_t c = connect_display();
    if (c <= 0 || !do_hello(c, 0)) finish(20);
    uint32_t w = 0, h = 0;
    int64_t win = win_create(c, 360, 240, 0, "wintest crash", &w, &h);
    if (win <= 0) finish(21);
    uint32_t stride = w * 4;
    int64_t shm = shm_create((uint64_t)stride * h);
    int64_t addr = (shm > 0) ? shm_map(shm, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE) : -1;
    if (addr <= 0) finish(22);
    uint32_t *px = (uint32_t *)addr;
    for (uint32_t i = 0; i < w * h; i++) px[i] = 0x00C04040u;
    if (buf_attach(c, (uint32_t)win, w, h, stride, 0, (nx_handle_t)shm) != 0) finish(23);
    uint32_t seq = commit(c, (uint32_t)win, 0, 0);
    if (seq == 0) finish(24);
    uint64_t end = uptime_ms() + 5000;
    while (uptime_ms() < end) {
        uint8_t buf[64];
        uint32_t len = 0;
        if (next_msg(c, buf, sizeof(buf), &len, 200) != 0) continue;
        const nxw_frame_done_t *f = (const nxw_frame_done_t *)buf;
        if (f->hdr.op == NXW_OP_FRAME_DONE && f->commit_seq == seq) {
            volatile uint32_t *null_ptr = 0;
            *null_ptr = 1;               /* crash with the window, buffer and mapping alive */
        }
    }
    finish(25);
}

static int run_crash_child(void) {
    const char *argv[3] = { "wintest", "crash", 0 };
    int status = -1;
    int64_t pid = sc3(SYS_SPAWN, (int64_t)"/apps/wintest", (int64_t)argv, SPAWN_WAITABLE);
    if (pid <= 0) return (int)pid;
    sc2(SYS_WAIT, pid, (int64_t)&status);
    sleep_ms(500);                       /* the server cleans up at its next frame */
    return status;
}

static void test_crash(void) {
    int st = run_crash_child();
    check(st == FAULT_EXIT_PF, "warm-up child showed its window, then crashed (status)", st);
    uint64_t baseline = free_memory();
    note("free memory baseline (bytes): ", (int64_t)baseline);
    st = run_crash_child();
    check(st == FAULT_EXIT_PF, "child showed its window, then crashed (status)", st);
    int64_t diff = (int64_t)baseline - (int64_t)free_memory();
    check(diff <= (int64_t)LEAK_TOLERANCE, "all memory back after the crash (bytes missing)", diff);
}

/* ============ 3. A real window ============ */

typedef struct {
    int64_t   shm;
    uint32_t *px;
    uint32_t  w, h, stride;
} buffer_t;

static int buffer_new(buffer_t *b, uint32_t w, uint32_t h) {
    b->w = w;
    b->h = h;
    b->stride = w * 4;
    b->shm = shm_create((uint64_t)b->stride * h);
    if (b->shm <= 0) return 0;
    int64_t a = shm_map(b->shm, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE);
    if (a <= 0) { h_close(b->shm); b->shm = 0; return 0; }
    b->px = (uint32_t *)a;
    return 1;
}

static void buffer_free(buffer_t *b) {
    if (b->px) shm_unmap((int64_t)b->px);
    if (b->shm > 0) h_close(b->shm);
    mzero(b, sizeof(*b));
}

static uint32_t bg_at(const buffer_t *b, uint32_t x, uint32_t y) {
    uint32_t r = 0x20 + (x * 0x30) / (b->w ? b->w : 1);
    uint32_t g = 0x28 + (y * 0x40) / (b->h ? b->h : 1);
    return (r << 16) | (g << 8) | 0x60u;
}

static void fill_rect(buffer_t *b, int32_t x0, int32_t y0, uint32_t w, uint32_t h, int square) {
    for (int32_t y = y0; y < y0 + (int32_t)h; y++) {
        if (y < 0 || y >= (int32_t)b->h) continue;
        for (int32_t x = x0; x < x0 + (int32_t)w; x++) {
            if (x < 0 || x >= (int32_t)b->w) continue;
            b->px[(uint32_t)y * (b->stride / 4) + (uint32_t)x] =
                square ? 0x00F0A030u : bg_at(b, (uint32_t)x, (uint32_t)y);
        }
    }
}

static void test_window(void) {
    int64_t c = connect_display();
    if (c <= 0 || !do_hello(c, 0)) { check(0, "connect for the window test", c); return; }
    uint32_t w = 0, h = 0;
    int64_t win = win_create(c, 360, 240, NXW_WIN_RESIZABLE, "wintest", &w, &h);
    check(win > 0 && w >= 120 && h >= 60, "WIN_CREATE 360x240 resizable (granted width)", w);
    if (win <= 0) { h_close(c); return; }

    buffer_t buf;
    mzero(&buf, sizeof(buf));
    if (!buffer_new(&buf, w, h)) { check(0, "window buffer", 0); h_close(c); return; }
    fill_rect(&buf, 0, 0, w, h, 0);
    int32_t sx = 20, sy = 20, vx = 3, vy = 2;
    fill_rect(&buf, sx, sy, 64, 64, 1);
    int64_t dup = h_dup(buf.shm, NX_RIGHTS_SAME);    /* keep our own handle */
    buf_attach(c, (uint32_t)win, w, h, buf.stride, 0, (nx_handle_t)dup);
    uint32_t pending = commit(c, (uint32_t)win, 0, 0);
    note("window open: drag it, tile it, type, click and scroll in it, then close it; window id ", win);

    uint32_t frames = 0, keys = 0, buttons = 0, motions = 0, enters = 0, leaves = 0, wheels = 0;
    uint32_t configures = 0, errors = 0;
    int closed_by_user = 0, lost = 0;
    uint64_t end = uptime_ms() + 120000;
    while (uptime_ms() < end && !closed_by_user && !lost) {
        uint8_t m[64];
        uint32_t len = 0;
        int64_t r = next_msg(c, m, sizeof(m), &len, 50);
        if (r == -EPIPE) { lost = 1; break; }
        if (r != 0) continue;
        uint16_t op = ((nxw_hdr_t *)m)->op;
        if (op == NXW_OP_FRAME_DONE) {
            const nxw_frame_done_t *f = (const nxw_frame_done_t *)m;
            if (f->commit_seq != pending) continue;
            frames++;
            /* Our turn to draw: move the square, damage old + new rects */
            nxw_rect_t d[2];
            d[0].x = sx; d[0].y = sy; d[0].w = 64; d[0].h = 64;
            fill_rect(&buf, sx, sy, 64, 64, 0);
            sx += vx; sy += vy;
            if (sx < 0 || sx + 64 > (int32_t)buf.w) { vx = -vx; sx += 2 * vx; }
            if (sy < 0 || sy + 64 > (int32_t)buf.h) { vy = -vy; sy += 2 * vy; }
            fill_rect(&buf, sx, sy, 64, 64, 1);
            d[1].x = sx; d[1].y = sy; d[1].w = 64; d[1].h = 64;
            pending = commit(c, (uint32_t)win, d, 2);
        } else if (op == NXW_OP_CONFIGURE) {
            const nxw_configure_t *cf = (const nxw_configure_t *)m;
            configures++;
            out_str("[WINTEST] CONFIGURE ");
            out_sdec(cf->width); out_str("x"); out_sdec(cf->height);
            out_str(" state "); out_sdec(cf->state); out_str("\n");
            flush();
            if ((cf->width != buf.w || cf->height != buf.h) && cf->width >= 1 && cf->height >= 1 &&
                cf->width <= NXW_SIZE_MAX && cf->height <= NXW_SIZE_MAX) {
                buffer_t nb;
                mzero(&nb, sizeof(nb));
                if (buffer_new(&nb, cf->width, cf->height)) {
                    buffer_free(&buf);           /* the server keeps its own mapping until COMMIT */
                    buf = nb;
                    fill_rect(&buf, 0, 0, buf.w, buf.h, 0);
                    if (sx + 64 > (int32_t)buf.w) sx = 0;
                    if (sy + 64 > (int32_t)buf.h) sy = 0;
                    fill_rect(&buf, sx, sy, 64, 64, 1);
                    int64_t d2 = h_dup(buf.shm, NX_RIGHTS_SAME);
                    buf_attach(c, (uint32_t)win, buf.w, buf.h, buf.stride, 0, (nx_handle_t)d2);
                    pending = commit(c, (uint32_t)win, 0, 0);
                }
            }
        } else if (op == NXW_OP_KEY) {
            const nxw_key_t *k = (const nxw_key_t *)m;
            keys++;
            if (keys <= 12) {
                out_str("[WINTEST] KEY scancode "); out_sdec(k->scancode);
                out_str(" ascii "); out_sdec(k->ascii);
                out_str(k->pressed ? " down" : " up");
                out_str(" modifiers "); out_sdec(k->modifiers); out_str("\n");
                flush();
            }
        } else if (op == NXW_OP_POINTER) {
            const nxw_pointer_t *p = (const nxw_pointer_t *)m;
            if (p->kind == NXW_PTR_MOTION) motions++;
            else if (p->kind == NXW_PTR_ENTER) enters++;
            else if (p->kind == NXW_PTR_LEAVE) leaves++;
            else if (p->kind == NXW_PTR_WHEEL) wheels++;
            else if (p->kind == NXW_PTR_BUTTON) buttons++;
            if (p->kind != NXW_PTR_MOTION) {
                static const char *const names[] = { "?", "MOTION", "BUTTON", "ENTER", "LEAVE", "WHEEL" };
                out_str("[WINTEST] POINTER ");
                out_str(p->kind <= 5 ? names[p->kind] : "?");
                out_str(" at "); out_sdec(p->x); out_str(","); out_sdec(p->y);
                out_str(" buttons "); out_sdec(p->buttons);
                if (p->kind == NXW_PTR_WHEEL) { out_str(" wheel "); out_sdec(p->wheel); }
                out_str("\n");
                flush();
            }
        } else if (op == NXW_OP_CLOSE_REQUEST) {
            note("CLOSE_REQUEST for window ", ((const nxw_close_request_t *)m)->win);
            closed_by_user = 1;
        } else if (op == NXW_OP_ERROR) {
            const nxw_error_t *e = (const nxw_error_t *)m;
            errors++;
            out_str("[WINTEST] unexpected ERROR op "); out_sdec(e->op);
            out_str(" code "); out_sdec(e->code); out_str("\n");
            flush();
        }
    }
    check(!lost, "the server kept the connection open", lost);
    check(errors == 0, "no ERROR for valid requests", errors);
    check(frames > 0, "FRAME_DONE arrived for commits (frames animated)", frames);
    note("events: CONFIGURE ", configures);
    note("events: KEY ", keys);
    note("events: BUTTON ", buttons);
    note("events: MOTION ", motions);
    note("events: ENTER ", enters);
    note("events: LEAVE ", leaves);
    note("events: WHEEL ", wheels);
    note(closed_by_user ? "closed by the user (CLOSE_REQUEST), frames " : "closed after 120 s, frames ", frames);
    win_destroy(c, (uint32_t)win);
    sleep_ms(100);
    buffer_free(&buf);
    h_close(c);
}

/* ============ Main ============ */

void _start(long argc, char **argv) {
    if (argc >= 2 && argv && argv[1] && streq(argv[1], "crash")) crash_child();

    out_str("[WINTEST] PID ");
    out_sdec(sc1(SYS_GETPID, 0));
    out_str(" started\n");
    flush();

    abi_info_t abi;
    int64_t r = sc1(SYS_ABI_QUERY, (int64_t)&abi);
    check(r == 0 && (abi.flags & (NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC)) == (NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC),
          "abi.query reports shared memory and IPC", r == 0 ? abi.flags : r);
    int64_t c = connect_display();
    check(c > 0, "port.connect(\"nx.display\") reaches the window server", c);
    if (c <= 0) {
        out_str("[WINTEST] RESULT: FAIL (no window server)\n");
        flush();
        finish(1);
    }
    h_close(c);

    test_hostile();
    test_crash();
    test_window();

    out_str(g_failed ? "[WINTEST] RESULT: FAIL\n" : "[WINTEST] RESULT: PASS\n");
    flush();
    finish(g_failed ? 1 : 0);
}
