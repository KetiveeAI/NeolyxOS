/*
 * Host unit test for libnxwindow (sdk/nxwindow/nxwindow.c).
 *
 * The library is built with NX_HOST_PREVIEW, so its syscalls come here: a
 * small fake kernel (handles with rights, shared memory, one channel, a
 * fake clock for obj.wait timeouts) and a scripted window server on the
 * other end of the channel that follows protocol 1.0 and records what it
 * received. The test checks the library's behaviour from the approved spec
 * (docs/desktop/proposals/libnxwindow.md):
 *
 *   connect errors (no IPC, no server, version refused, no answer), reply
 *   matching with events queued while waiting, window validation and
 *   limits, the single-buffer rule (-EBUSY until FRAME_DONE), damage
 *   clipping and merging, resize to a new zeroed buffer, read-only buffer
 *   handles, asynchronous ERRORs, malformed server messages, motion
 *   coalescing and queue overflow, a full server queue, stale and foreign
 *   window tokens, server loss, late replies, and that disconnecting
 *   returns every handle and mapping.
 *
 * Build and run on the development machine:
 *   gcc -std=c11 -O1 -Wall -Wextra -DNX_HOST_PREVIEW -I../../sdk/nxwindow \
 *       -I../../desktop/include -I../../kernel/include/core \
 *       -o /tmp/nxwindow_test nxwindow_test.c
 *   /tmp/nxwindow_test          (-v prints the library's log)
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../sdk/nxwindow/nxwindow.c"

static int g_fail, g_checks, g_verbose;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail = 1;
    }
}

/* ============ Fake kernel ============ */

#define K_EINVAL     22
#define K_ENOENT     2
#define K_EAGAIN     11
#define K_ENOMEM     12
#define K_EPIPE      32
#define K_EBADF      9
#define K_ETIMEDOUT  110

#define MAX_HANDLES  64
#define MAX_SHM      32
#define MAX_MSGS     256
#define CHAN_HANDLE  7          /* the client's end of the channel */

typedef struct { int used; size_t size; uint8_t *mem; int maps; int refs; } shm_t;
typedef struct { int used; int shm; uint32_t rights; } handle_t;     /* client handles (shm only) */
typedef struct { uint8_t data[NX_CHAN_DATA_MAX]; uint32_t len; int handle_shm; } msg_t;

static shm_t    k_shm[MAX_SHM];
static handle_t k_h[MAX_HANDLES];
static uint64_t k_now;                  /* fake clock, ms */
static uint16_t k_abi_flags = NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC;
static int      k_port_up = 1;
static int      k_chan_open;            /* client holds its end */
static int      k_server_closed;
static int      k_send_eagain;          /* next sends fail with -EAGAIN; -1 = always */
static int      k_shm_fail;
static int      k_forever_waits;        /* obj.wait forever with nothing ready (a hang) */
static msg_t    k_inbox[MAX_MSGS];      /* server -> client */
static int      k_in_head, k_in_count;
static char     k_log[65536];
static size_t   k_log_len;

static void log_clear(void) { k_log_len = 0; k_log[0] = 0; }
static int log_has(const char *s) { return strstr(k_log, s) != NULL; }

static int h_new(int shm, uint32_t rights) {
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (i == CHAN_HANDLE || k_h[i].used) continue;
        k_h[i].used = 1;
        k_h[i].shm = shm;
        k_h[i].rights = rights;
        k_shm[shm].refs++;
        return i + 100;                 /* handles are 101.. so 7 stays the channel */
    }
    return -K_ENOMEM;
}

static handle_t *h_get(int64_t h) {
    if (h < 101 || h >= 100 + MAX_HANDLES) return NULL;
    return k_h[h - 100].used ? &k_h[h - 100] : NULL;
}

static void shm_unref(int s) {
    if (--k_shm[s].refs == 0) {
        free(k_shm[s].mem);
        k_shm[s].used = 0;
        k_shm[s].mem = NULL;
    }
}

static int open_handles(void) {
    int n = 0;
    for (int i = 0; i < MAX_HANDLES; i++) n += k_h[i].used;
    return n;
}

static int client_maps(void) {
    int n = 0;
    for (int i = 0; i < MAX_SHM; i++) if (k_shm[i].used) n += k_shm[i].maps;
    return n;
}

static void inbox_push(const void *data, uint32_t len, int handle_shm) {
    if (k_in_count == MAX_MSGS) { printf("FAIL test inbox overflow\n"); exit(2); }
    msg_t *m = &k_inbox[(k_in_head + k_in_count) % MAX_MSGS];
    memcpy(m->data, data, len);
    m->len = len;
    m->handle_shm = handle_shm;
    k_in_count++;
}

/* ============ Scripted server ============ */

typedef struct { int used; uint32_t id; int buf_shm; uint32_t bw, bh; int owed; uint32_t commit_seq; } srv_win_t;

static struct {
    int      refuse_version;
    int      silent_hello;
    int      silent_create;     /* no WIN_CREATED reply */
    uint32_t key_before_reply;  /* a KEY for this window first, then the reply */
    int      error_attach;      /* BUF_ATTACH gets ERROR -ENOMEM */
    int      error_commit;
    uint32_t grant_w, grant_h;  /* 0: grant what was asked */
    uint32_t next_id;
    srv_win_t win[16];
    /* records */
    uint32_t n_ops[0x200];
    nxw_commit_t last_commit;
    nxw_win_create_t last_create;
    uint32_t last_attach_rights, last_attach_stride, last_attach_w, last_attach_h;
    uint32_t last_destroy;
    char     last_title[NXW_TITLE_MAX];
    uint32_t destroyed[64];
    int      n_destroyed;
} S;

static srv_win_t *srv_win(uint32_t id) {
    for (int i = 0; i < 16; i++) if (S.win[i].used && S.win[i].id == id) return &S.win[i];
    return NULL;
}

static void srv_send(const void *m, uint16_t len) { inbox_push(m, len, -1); }

static void srv_error(uint32_t seq, uint16_t op, int32_t code, uint32_t win) {
    nxw_error_t e;
    memset(&e, 0, sizeof(e));
    e.hdr.op = NXW_OP_ERROR; e.hdr.len = sizeof(e); e.hdr.seq = seq;
    e.op = op; e.code = code; e.win = win;
    srv_send(&e, sizeof(e));
}

static void srv_key(uint32_t win, uint8_t ascii) {
    nxw_key_t k;
    memset(&k, 0, sizeof(k));
    k.hdr.op = NXW_OP_KEY; k.hdr.len = sizeof(k);
    k.win = win; k.scancode = 0x1E; k.ascii = ascii; k.pressed = 1;
    srv_send(&k, sizeof(k));
}

static void srv_pointer(uint32_t win, uint32_t kind, int32_t x, int32_t y, uint32_t buttons) {
    nxw_pointer_t p;
    memset(&p, 0, sizeof(p));
    p.hdr.op = NXW_OP_POINTER; p.hdr.len = sizeof(p);
    p.win = win; p.kind = kind; p.x = x; p.y = y; p.buttons = buttons;
    srv_send(&p, sizeof(p));
}

static void srv_configure(uint32_t win, uint32_t w, uint32_t h, uint32_t state) {
    nxw_configure_t c;
    memset(&c, 0, sizeof(c));
    c.hdr.op = NXW_OP_CONFIGURE; c.hdr.len = sizeof(c);
    c.win = win; c.width = w; c.height = h; c.state = state;
    srv_send(&c, sizeof(c));
}

static void srv_close_request(uint32_t win) {
    nxw_close_request_t c;
    memset(&c, 0, sizeof(c));
    c.hdr.op = NXW_OP_CLOSE_REQUEST; c.hdr.len = sizeof(c); c.win = win;
    srv_send(&c, sizeof(c));
}

/* The server's frame tick: FRAME_DONE for every commit owed. */
static void srv_present(void) {
    for (int i = 0; i < 16; i++) {
        srv_win_t *w = &S.win[i];
        if (!w->used || !w->owed) continue;
        nxw_frame_done_t f;
        memset(&f, 0, sizeof(f));
        f.hdr.op = NXW_OP_FRAME_DONE; f.hdr.len = sizeof(f);
        f.win = w->id; f.commit_seq = w->commit_seq; f.time_ms = k_now;
        srv_send(&f, sizeof(f));
        w->owed = 0;
    }
}

/* One request from the client. handle_shm: the shm moved with it, or -1. */
static void srv_handle(const uint8_t *data, uint32_t len, int handle_shm) {
    nxw_hdr_t h;
    memcpy(&h, data, sizeof(h));
    if (h.len != len) { printf("FAIL client sent len %u for %u bytes\n", h.len, len); g_fail = 1; }
    if (h.op < 0x200) S.n_ops[h.op]++;
    switch (h.op) {
    case NXW_OP_HELLO: {
        if (S.silent_hello) return;
        if (S.refuse_version) {
            srv_error(h.seq, NXW_OP_HELLO, -NXW_EINVAL, 0);
            k_server_closed = 1;
            return;
        }
        nxw_welcome_t w;
        memset(&w, 0, sizeof(w));
        w.hdr.op = NXW_OP_WELCOME; w.hdr.len = sizeof(w); w.hdr.seq = h.seq;
        w.major = 1; w.minor = 0; w.screen_w = 1280; w.screen_h = 800;
        w.max_windows = 8; w.max_size = 4096;
        srv_send(&w, sizeof(w));
        return;
    }
    case NXW_OP_WIN_CREATE: {
        nxw_win_create_t m;
        memcpy(&m, data, sizeof(m));
        S.last_create = m;
        int n = 0;
        for (int i = 0; i < 16; i++) n += S.win[i].used;
        if (n >= 8) { srv_error(h.seq, NXW_OP_WIN_CREATE, -NXW_EMFILE, 0); return; }
        srv_win_t *w = NULL;
        for (int i = 0; i < 16; i++) if (!S.win[i].used) { w = &S.win[i]; break; }
        if (!w) { srv_error(h.seq, NXW_OP_WIN_CREATE, -NXW_EMFILE, 0); return; }
        memset(w, 0, sizeof(*w));
        w->used = 1;
        w->id = ++S.next_id;
        w->buf_shm = -1;
        if (S.key_before_reply) srv_key(S.key_before_reply, 'k');
        if (S.silent_create) return;
        nxw_win_created_t r;
        memset(&r, 0, sizeof(r));
        r.hdr.op = NXW_OP_WIN_CREATED; r.hdr.len = sizeof(r); r.hdr.seq = h.seq;
        r.win = w->id;
        r.width = S.grant_w ? S.grant_w : m.width;
        r.height = S.grant_h ? S.grant_h : m.height;
        srv_send(&r, sizeof(r));
        return;
    }
    case NXW_OP_WIN_DESTROY: {
        nxw_win_destroy_t m;
        memcpy(&m, data, sizeof(m));
        S.last_destroy = m.win;
        if (S.n_destroyed < 64) S.destroyed[S.n_destroyed++] = m.win;
        srv_win_t *w = srv_win(m.win);
        if (w) {
            if (w->buf_shm >= 0) shm_unref(w->buf_shm);
            w->used = 0;
        }
        return;
    }
    case NXW_OP_WIN_SET_TITLE: {
        nxw_win_set_title_t m;
        memcpy(&m, data, sizeof(m));
        memcpy(S.last_title, m.title, NXW_TITLE_MAX);
        return;
    }
    case NXW_OP_BUF_ATTACH: {
        nxw_buf_attach_t m;
        memcpy(&m, data, sizeof(m));
        S.last_attach_stride = m.stride;
        S.last_attach_w = m.width;
        S.last_attach_h = m.height;
        srv_win_t *w = srv_win(m.win);
        if (handle_shm < 0) { printf("FAIL BUF_ATTACH without a handle\n"); g_fail = 1; return; }
        if (S.error_attach || !w) {
            shm_unref(handle_shm);
            srv_error(h.seq, NXW_OP_BUF_ATTACH, -NXW_ENOMEM, m.win);
            return;
        }
        if (w->buf_shm >= 0) shm_unref(w->buf_shm);
        w->buf_shm = handle_shm;        /* the server's "mapping" keeps it alive */
        w->bw = m.width;
        w->bh = m.height;
        return;
    }
    case NXW_OP_COMMIT: {
        nxw_commit_t m;
        memcpy(&m, data, sizeof(m));
        S.last_commit = m;
        srv_win_t *w = srv_win(m.win);
        if (S.error_commit || !w || m.count > NXW_DAMAGE_MAX) {
            srv_error(h.seq, NXW_OP_COMMIT, -NXW_EINVAL, m.win);
            return;
        }
        w->owed = 1;
        w->commit_seq = h.seq;
        return;
    }
    default:
        printf("FAIL client sent unknown op 0x%x\n", h.op);
        g_fail = 1;
    }
}

/* ============ Syscall dispatcher ============ */

int64_t nx_host_syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    (void)a4;
    return nx_host_syscall(num, a1, a2, a3);
}

int64_t nx_host_syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    switch (num) {
    case SYS_TIME:
        return (int64_t)k_now;
    case 127: {                                 /* debug.print */
        const char *s = (const char *)(uintptr_t)a1;
        size_t n = strlen(s);
        if (k_log_len + n < sizeof(k_log)) { memcpy(k_log + k_log_len, s, n + 1); k_log_len += n; }
        if (g_verbose) fputs(s, stdout);
        return 0;
    }
    case SYS_ABI_QUERY: {
        nx_abi_info_t *i = (nx_abi_info_t *)(uintptr_t)a1;
        memset(i, 0, sizeof(*i));
        i->major = 1; i->minor = 2; i->flags = k_abi_flags;
        return 0;
    }
    case SYS_PORT_CONNECT:
        if (strcmp((const char *)(uintptr_t)a1, "nx.display") != 0) return -K_ENOENT;
        if (!k_port_up) return -K_ENOENT;
        k_chan_open = 1;
        k_server_closed = 0;
        k_in_head = k_in_count = 0;
        return CHAN_HANDLE;
    case SYS_HANDLE_CLOSE: {
        if (a1 == CHAN_HANDLE) {
            if (!k_chan_open) return -K_EBADF;
            k_chan_open = 0;
            return 0;
        }
        handle_t *h = h_get(a1);
        if (!h) { printf("FAIL close of unknown handle %ld\n", (long)a1); g_fail = 1; return -K_EBADF; }
        h->used = 0;
        shm_unref(h->shm);
        return 0;
    }
    case SYS_SHM_CREATE: {
        if (k_shm_fail) return -K_ENOMEM;
        if (a1 <= 0 || a1 > (int64_t)NX_SHM_SIZE_MAX) return -K_EINVAL;
        for (int s = 0; s < MAX_SHM; s++) {
            if (k_shm[s].used) continue;
            k_shm[s].used = 1;
            k_shm[s].size = (size_t)a1;
            k_shm[s].mem = calloc(1, (size_t)a1);
            k_shm[s].maps = 0;
            k_shm[s].refs = 0;
            return h_new(s, NX_RIGHT_READ | NX_RIGHT_WRITE | NX_RIGHT_MAP | NX_RIGHT_TRANSFER | NX_RIGHT_DUPLICATE);
        }
        return -K_ENOMEM;
    }
    case SYS_SHM_MAP: {
        handle_t *h = h_get(a1);
        if (!h) return -K_EBADF;
        uint32_t need = NX_RIGHT_MAP | NX_RIGHT_READ | ((a2 & NX_SHM_PROT_WRITE) ? NX_RIGHT_WRITE : 0);
        if ((h->rights & need) != need) return -1;      /* EPERM */
        k_shm[h->shm].maps++;
        k_shm[h->shm].refs++;
        if (a3) *(uint64_t *)(uintptr_t)a3 = k_shm[h->shm].size;
        return (int64_t)(uintptr_t)k_shm[h->shm].mem;
    }
    case SYS_SHM_UNMAP: {
        for (int s = 0; s < MAX_SHM; s++) {
            if (k_shm[s].used && k_shm[s].maps > 0 && (int64_t)(uintptr_t)k_shm[s].mem == a1) {
                k_shm[s].maps--;
                shm_unref(s);
                return 0;
            }
        }
        printf("FAIL unmap of an address that is not mapped\n");
        g_fail = 1;
        return -K_EINVAL;
    }
    case SYS_HANDLE_DUP: {
        handle_t *h = h_get(a1);
        if (!h) return -K_EBADF;
        uint32_t r = (uint32_t)a2 == NX_RIGHTS_SAME ? h->rights : (uint32_t)a2;
        if ((r & ~h->rights) != 0) return -1;
        return h_new(h->shm, r);
    }
    case SYS_CHAN_SEND: {
        if (a1 != CHAN_HANDLE || !k_chan_open) return -K_EBADF;
        if (k_server_closed) return -K_EPIPE;
        if (k_send_eagain != 0) {
            if (k_send_eagain > 0) k_send_eagain--;
            return -K_EAGAIN;
        }
        const nx_chan_msg_t *m = (const nx_chan_msg_t *)(uintptr_t)a2;
        if (m->data_len > NX_CHAN_DATA_MAX || m->handle_count > 1 || m->flags || m->sender_pid) return -K_EINVAL;
        int moved = -1;
        if (m->handle_count == 1) {
            nx_handle_t hv = ((const nx_handle_t *)(uintptr_t)m->handles)[0];
            handle_t *h = h_get(hv);
            if (!h || !(h->rights & NX_RIGHT_TRANSFER)) return -K_EBADF;
            S.last_attach_rights = h->rights;
            moved = h->shm;
            h->used = 0;                /* moved: the reference goes with it */
        }
        srv_handle((const uint8_t *)(uintptr_t)m->data, m->data_len, moved);
        return 0;
    }
    case SYS_CHAN_RECV: {
        if (a1 != CHAN_HANDLE || !k_chan_open) return -K_EBADF;
        nx_chan_msg_t *m = (nx_chan_msg_t *)(uintptr_t)a2;
        if (k_in_count == 0) return k_server_closed ? -K_EPIPE : -K_EAGAIN;
        msg_t *in = &k_inbox[k_in_head];
        if (in->len > m->data_len) return -90;
        memcpy((void *)(uintptr_t)m->data, in->data, in->len);
        m->data_len = in->len;
        m->handle_count = 0;
        if (in->handle_shm >= 0) {
            ((nx_handle_t *)(uintptr_t)m->handles)[0] = (nx_handle_t)h_new(in->handle_shm, NX_RIGHT_READ | NX_RIGHT_MAP);
            m->handle_count = 1;
        }
        k_in_head = (k_in_head + 1) % MAX_MSGS;
        k_in_count--;
        return 0;
    }
    case SYS_OBJ_WAIT: {
        nx_wait_item_t *it = (nx_wait_item_t *)(uintptr_t)a1;
        if (a2 != 1 || it->handle != CHAN_HANDLE) return -K_EINVAL;
        uint32_t obs = 0;
        if (k_in_count > 0) obs |= NX_SIGNAL_READABLE;
        if (k_server_closed) obs |= NX_SIGNAL_PEER_CLOSED;
        if (k_send_eagain >= 0) obs |= NX_SIGNAL_WRITABLE;     /* -1: the server never reads */
        it->observed = obs;
        if (obs & it->signals) return 1;
        if ((uint64_t)a3 == NX_WAIT_FOREVER) {
            k_forever_waits++;
            return -K_ETIMEDOUT;        /* would hang: counted as a failure by the test */
        }
        k_now += (uint64_t)a3;          /* nothing happens: the whole timeout passes */
        return -K_ETIMEDOUT;
    }
    default:
        printf("FAIL unexpected syscall %ld\n", (long)num);
        g_fail = 1;
        return -38;
    }
}

/* ============ Helpers ============ */

static void reset_all(void) {
    nxw_disconnect();
    for (int s = 0; s < MAX_SHM; s++) { free(k_shm[s].mem); k_shm[s].mem = NULL; k_shm[s].used = 0; }
    memset(k_h, 0, sizeof(k_h));
    memset(&S, 0, sizeof(S));
    k_abi_flags = NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC;
    k_port_up = 1;
    k_chan_open = 0;
    k_server_closed = 0;
    k_send_eagain = 0;
    k_shm_fail = 0;
    k_in_head = k_in_count = 0;
    log_clear();
}

static nxw_window_t *mkwin(const char *title, uint32_t w, uint32_t h) {
    nxw_window_desc_t d = { title, w, h, NXW_POS_AUTO, NXW_POS_AUTO, 0 };
    nxw_window_t *out = NULL;
    int r = nxw_window_create(&d, &out);
    return r == 0 ? out : NULL;
}

static uint32_t sid(nxw_window_t *w) {       /* the server's id of a window */
    nxwl_win_t *s = resolve(w);
    return s ? s->id : 0;
}

static int next_ev(nxw_event_t *e) { return nxw_poll_event(e); }

static int drain(void) {
    nxw_event_t e;
    int n = 0;
    while (nxw_poll_event(&e) == 1) n++;
    return n;
}

/* ============ Tests ============ */

static void test_connect(void) {
    nxw_display_info_t info;

    reset_all();
    k_abi_flags = NX_ABI_FLAG_SHM;
    check(nxw_connect(&info) == -NXW_ENOSYS, "connect: kernel without IPC is -ENOSYS");

    reset_all();
    k_port_up = 0;
    check(nxw_connect(&info) == -NXW_ENOENT, "connect: no window server is -ENOENT");

    reset_all();
    S.refuse_version = 1;
    check(nxw_connect(&info) == -NXW_EPROTO, "connect: refused version is -EPROTO");
    check(!k_chan_open, "connect: refused connection closes the channel");
    check(nxw_connection_handle() == NX_HANDLE_INVALID, "connect: no handle after a refusal");

    reset_all();
    S.silent_hello = 1;
    uint64_t t0 = k_now;
    check(nxw_connect(&info) == -NXW_ETIMEDOUT, "connect: no WELCOME is -ETIMEDOUT");
    check(k_now - t0 >= 2000 && k_now - t0 < 2100, "connect: waits about 2 s for WELCOME");
    check(!k_chan_open, "connect: timed-out connection closes the channel");

    reset_all();
    memset(&info, 0, sizeof(info));
    check(nxw_connect(&info) == 0, "connect: succeeds");
    check(info.proto_major == 1 && info.screen_w == 1280 && info.screen_h == 800 &&
          info.max_windows == 8 && info.max_size == 4096, "connect: display info from WELCOME");
    check(S.n_ops[NXW_OP_HELLO] == 1, "connect: one HELLO");
    check(nxw_connection_handle() == CHAN_HANDLE, "connect: connection handle");
    check(nxw_connect(NULL) == -NXW_EISCONN, "connect: second connect is -EISCONN");
    check(log_has("[NXWIN] connected, protocol 1.0"), "connect: logged");
    nxw_disconnect();
    check(!k_chan_open && nxw_connection_handle() == NX_HANDLE_INVALID, "disconnect closes the channel");
    nxw_disconnect();   /* again: harmless */
    nxw_event_t e;
    check(nxw_poll_event(&e) == -NXW_ENOTCONN, "poll when not connected is -ENOTCONN");
    check(mkwin("x", 10, 10) == NULL, "create when not connected fails");
}

static void test_create(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *w = NULL;
    nxw_window_desc_t d = { "Hello", 320, 200, NXW_POS_AUTO, 40, NXW_WIN_RESIZABLE };

    check(nxw_window_create(NULL, &w) == -NXW_EINVAL, "create: NULL desc");
    check(nxw_window_create(&d, NULL) == -NXW_EINVAL, "create: NULL out");
    char t64[65];
    memset(t64, 'a', 64); t64[64] = 0;
    d.title = t64;
    check(nxw_window_create(&d, &w) == -NXW_EINVAL, "create: 64-byte title refused");
    t64[63] = 0;
    d.title = NULL;
    check(nxw_window_create(&d, &w) == -NXW_EINVAL, "create: NULL title refused");
    d.title = "Hello";
    d.width = 0;
    check(nxw_window_create(&d, &w) == -NXW_EINVAL, "create: width 0 refused");
    d.width = 4097;
    check(nxw_window_create(&d, &w) == -NXW_EINVAL, "create: width 4097 refused");
    d.width = 320;
    d.flags = 2;
    check(nxw_window_create(&d, &w) == -NXW_EINVAL, "create: unknown flag refused");
    d.flags = NXW_WIN_RESIZABLE;
    check(S.n_ops[NXW_OP_WIN_CREATE] == 0, "create: invalid requests never reach the server");

    d.title = t64;      /* 63 bytes */
    w = NULL;
    check(nxw_window_create(&d, &w) == 0 && w != NULL, "create: 63-byte title accepted");
    check(S.last_create.width == 320 && S.last_create.height == 200 && S.last_create.x == NXW_POS_AUTO &&
          S.last_create.y == 40 && S.last_create.flags == NXW_WIN_RESIZABLE &&
          memcmp(S.last_create.title, t64, 64) == 0, "create: request carries the description");
    uint32_t ww = 0, hh = 0, st = 9;
    check(nxw_window_get_size(w, &ww, &hh, &st) == 0 && ww == 320 && hh == 200 && st == 0, "create: size");

    S.grant_w = 100; S.grant_h = 50;
    nxw_window_t *g2 = mkwin("small", 3000, 3000);
    check(g2 && nxw_window_get_size(g2, &ww, &hh, NULL) == 0 && ww == 100 && hh == 50,
          "create: granted size is used");
    S.grant_w = S.grant_h = 0;

    /* 8 per client: six more fit, the ninth is refused locally */
    int ok = 1;
    for (int i = 0; i < 6; i++) if (!mkwin("w", 10, 10)) ok = 0;
    check(ok, "create: eight windows");
    uint32_t before = S.n_ops[NXW_OP_WIN_CREATE];
    check(nxw_window_create(&d, &w) == -NXW_EMFILE, "create: ninth window is -EMFILE");
    check(S.n_ops[NXW_OP_WIN_CREATE] == before, "create: ninth refused without a round trip");

    /* The server's own refusal comes back as its code */
    reset_all();
    nxw_connect(NULL);
    for (int i = 0; i < 8; i++) S.win[i].used = 1, S.win[i].id = 900 + (uint32_t)i;
    check(nxw_window_create(&d, &w) == -NXW_EMFILE, "create: server ERROR code is returned");

    /* Events that arrive while waiting for the reply are kept */
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *k = mkwin("keys", 50, 50);
    S.key_before_reply = sid(k);
    nxw_window_t *k2 = mkwin("second", 50, 50);
    nxw_event_t e;
    check(k && k2 && next_ev(&e) == 1 && e.type == NXW_EV_KEY && e.window == k && e.key.ascii == 'k',
          "create: event queued during the wait is delivered");

    /* No reply: -ETIMEDOUT, and the late WIN_CREATED is destroyed */
    reset_all();
    nxw_connect(NULL);
    S.silent_create = 1;
    uint64_t t0 = k_now;
    check(nxw_window_create(&d, &w) == -NXW_ETIMEDOUT, "create: no reply is -ETIMEDOUT");
    check(k_now - t0 >= 2000, "create: waits 2 s");
    nxw_win_created_t late;
    memset(&late, 0, sizeof(late));
    late.hdr.op = NXW_OP_WIN_CREATED; late.hdr.len = sizeof(late); late.hdr.seq = 2;
    late.win = S.next_id; late.width = 10; late.height = 10;
    srv_send(&late, sizeof(late));
    drain();
    check(S.last_destroy == late.win, "create: a late WIN_CREATED is destroyed, not leaked");
}

static void test_frames(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *w = mkwin("frames", 64, 32);
    nxw_frame_t f;
    memset(&f, 0, sizeof(f));

    check(nxw_frame_commit(w, NULL, 0) == -NXW_EINVAL, "commit before begin is -EINVAL");
    check(nxw_frame_begin(w, NULL) == -NXW_EINVAL, "begin with NULL frame");

    check(nxw_frame_begin(w, &f) == 0, "begin: first frame");
    check(f.pixels && f.width == 64 && f.height == 32 && f.stride == 256 && f.full == 1, "begin: new buffer");
    check(S.n_ops[NXW_OP_BUF_ATTACH] == 1 && S.last_attach_w == 64 && S.last_attach_h == 32 &&
          S.last_attach_stride == 256, "begin: BUF_ATTACH sent");
    check((S.last_attach_rights & NX_RIGHT_WRITE) == 0 &&
          (S.last_attach_rights & (NX_RIGHT_READ | NX_RIGHT_MAP)) == (NX_RIGHT_READ | NX_RIGHT_MAP),
          "begin: the server gets a read-only handle");
    check(open_handles() == 0, "begin: the library keeps no shm handle");
    check(client_maps() == 1, "begin: one client mapping");
    int zero = 1;
    for (uint32_t i = 0; i < 64 * 32; i++) if (f.pixels[i]) zero = 0;
    check(zero, "begin: new buffer is zeroed");
    for (uint32_t i = 0; i < 64 * 32; i++) f.pixels[i] = 0x00FF00FFu;

    nxw_rect_t r1 = { 1, 1, 4, 4 };
    check(nxw_frame_commit(w, &r1, 1) == 0, "commit: first frame");
    check(S.last_commit.count == 0, "commit: a new buffer always commits whole");
    check(nxw_frame_begin(w, &f) == -NXW_EBUSY, "begin while waiting for FRAME_DONE is -EBUSY");
    check(nxw_frame_commit(w, NULL, 0) == -NXW_EINVAL, "commit while busy (no begin) is -EINVAL");

    srv_present();
    nxw_event_t e;
    check(next_ev(&e) == 1 && e.type == NXW_EV_FRAME && e.window == w &&
          e.frame.commit_seq == S.last_commit.hdr.seq, "FRAME_DONE gives NXW_EV_FRAME");
    uint32_t *p0 = f.pixels;
    check(nxw_frame_begin(w, &f) == 0 && f.full == 0 && f.pixels == p0 && f.pixels[5] == 0x00FF00FFu,
          "begin: same buffer, contents kept");
    check(S.n_ops[NXW_OP_BUF_ATTACH] == 1, "begin: no second attach");

    nxw_rect_t rs[3] = { { 2, 3, 5, 6 }, { 60, 30, 10, 10 }, { 100, 100, 5, 5 } };
    check(nxw_frame_commit(w, rs, 3) == 0, "commit: three rects");
    check(S.last_commit.count == 2 && S.last_commit.rects[0].x == 2 && S.last_commit.rects[0].w == 5 &&
          S.last_commit.rects[1].x == 60 && S.last_commit.rects[1].w == 4 && S.last_commit.rects[1].h == 2,
          "commit: rects clipped, off-buffer ones dropped");
    srv_present();
    drain();

    nxw_rect_t many[20];
    for (int i = 0; i < 20; i++) { many[i].x = i * 3; many[i].y = i; many[i].w = 2; many[i].h = 1; }
    nxw_frame_begin(w, &f);
    check(nxw_frame_commit(w, many, 20) == 0, "commit: twenty rects");
    check(S.last_commit.count == 1 && S.last_commit.rects[0].x == 0 && S.last_commit.rects[0].y == 0 &&
          S.last_commit.rects[0].w == 59 && S.last_commit.rects[0].h == 20,
          "commit: more than 16 merge into the bounding box");
    check(log_has("damage rects merged into one"), "commit: merge logged");
    srv_present();
    drain();

    nxw_frame_begin(w, &f);
    nxw_rect_t off = { -50, -50, 10, 10 };
    check(nxw_frame_commit(w, &off, 1) == 0 && S.last_commit.count == 1 && S.last_commit.rects[0].w == 0,
          "commit: no visible damage still asks for a frame");
    srv_present();
    check(next_ev(&e) == 1 && e.type == NXW_EV_FRAME, "commit: ... and gets FRAME_DONE");

    nxw_frame_begin(w, &f);
    check(nxw_frame_commit(w, NULL, 3) == -NXW_EINVAL, "commit: NULL rects with count is -EINVAL");
    check(nxw_frame_commit(w, NULL, 0) == 0 && S.last_commit.count == 0, "commit: count 0 is the whole buffer");

    /* A FRAME_DONE for an older commit does not free the buffer */
    nxw_frame_done_t old;
    memset(&old, 0, sizeof(old));
    old.hdr.op = NXW_OP_FRAME_DONE; old.hdr.len = sizeof(old); old.win = sid(w); old.commit_seq = 1;
    srv_send(&old, sizeof(old));
    drain();
    check(nxw_frame_begin(w, &f) == -NXW_EBUSY, "a stale FRAME_DONE is ignored");
    srv_present();
    drain();

    /* Resize: a new zeroed buffer of the new size, the old mapping goes */
    srv_configure(sid(w), 100, 40, NXW_STATE_ACTIVE | NXW_STATE_TILED);
    check(next_ev(&e) == 1 && e.type == NXW_EV_CONFIGURE && e.configure.width == 100 &&
          e.configure.height == 40 && e.configure.state == (NXW_STATE_ACTIVE | NXW_STATE_TILED),
          "CONFIGURE event");
    uint32_t ww, hh, st;
    nxw_window_get_size(w, &ww, &hh, &st);
    check(ww == 100 && hh == 40 && st == (NXW_STATE_ACTIVE | NXW_STATE_TILED), "CONFIGURE updates the size");
    check(nxw_frame_begin(w, &f) == 0 && f.width == 100 && f.height == 40 && f.stride == 400 && f.full == 1,
          "resize: new buffer");
    check(f.pixels[0] == 0 && f.pixels[100 * 40 - 1] == 0, "resize: new buffer is zeroed");
    check(client_maps() == 1 && S.n_ops[NXW_OP_BUF_ATTACH] == 2, "resize: old mapping released, new attached");
    check(nxw_frame_commit(w, rs, 1) == 0 && S.last_commit.count == 0, "resize: first commit is whole");
    srv_present();
    drain();

    /* Same-size CONFIGURE (focus only): no new buffer */
    srv_configure(sid(w), 100, 40, 0);
    drain();
    check(nxw_frame_begin(w, &f) == 0 && f.full == 0 && S.n_ops[NXW_OP_BUF_ATTACH] == 2,
          "focus-only CONFIGURE keeps the buffer");
    nxw_frame_commit(w, NULL, 0);
    srv_present();
    drain();

    /* shm failure: -ENOMEM, logged, old buffer still usable later */
    srv_configure(sid(w), 120, 40, 0);
    drain();
    k_shm_fail = 1;
    log_clear();
    check(nxw_frame_begin(w, &f) == -NXW_ENOMEM, "begin: shm.create failure is -ENOMEM");
    check(log_has("shm.create failed"), "begin: shm failure logged");
    check(client_maps() == 1, "begin: failure keeps the old buffer mapped");
    k_shm_fail = 0;
    check(nxw_frame_begin(w, &f) == 0 && f.width == 120, "begin: works again after the failure");
    nxw_frame_commit(w, NULL, 0);
    srv_present();
    drain();

    /* Asynchronous ERROR for a COMMIT: event, and the buffer is free again */
    S.error_commit = 1;
    nxw_frame_begin(w, &f);
    nxw_frame_commit(w, NULL, 0);
    check(next_ev(&e) == 1 && e.type == NXW_EV_ERROR && e.window == w && e.error.op == NXW_OP_COMMIT &&
          e.error.code == -NXW_EINVAL, "COMMIT refused gives NXW_EV_ERROR");
    check(nxw_frame_begin(w, &f) == 0, "a refused COMMIT frees the buffer");
    S.error_commit = 0;
    nxw_frame_commit(w, NULL, 0);
    srv_present();
    drain();

    /* Asynchronous ERROR for BUF_ATTACH: event, and the next frame makes a new buffer */
    S.error_attach = 1;
    srv_configure(sid(w), 30, 30, 0);
    drain();
    nxw_frame_begin(w, &f);
    nxw_frame_commit(w, NULL, 0);
    int saw = 0;
    while (next_ev(&e) == 1) if (e.type == NXW_EV_ERROR && e.error.op == NXW_OP_BUF_ATTACH) saw = 1;
    check(saw, "BUF_ATTACH refused gives NXW_EV_ERROR");
    S.error_attach = 0;
    uint32_t attaches = S.n_ops[NXW_OP_BUF_ATTACH];
    check(nxw_frame_begin(w, &f) == 0 && f.full == 1 && S.n_ops[NXW_OP_BUF_ATTACH] == attaches + 1,
          "after a refused attach the next frame attaches a new buffer");
    nxw_frame_commit(w, NULL, 0);
}

static void test_events(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *a = mkwin("a", 40, 40);
    nxw_window_t *b = mkwin("b", 40, 40);
    nxw_event_t e;

    check(nxw_poll_event(NULL) == -NXW_EINVAL, "poll with NULL");
    check(nxw_poll_event(&e) == 0, "poll: nothing");
    uint64_t t0 = k_now;
    check(nxw_wait_event(&e, 250) == 0 && k_now - t0 >= 250, "wait: times out");

    for (int i = 0; i < 5; i++) srv_pointer(sid(a), NXW_PTR_MOTION, i, i * 2, 0);
    check(next_ev(&e) == 1 && e.type == NXW_EV_POINTER && e.pointer.kind == NXW_PTR_MOTION &&
          e.pointer.x == 4 && e.pointer.y == 8 && e.window == a, "motion coalesces to the newest");
    check(next_ev(&e) == 0, "... into one event");

    srv_pointer(sid(a), NXW_PTR_MOTION, 1, 1, 0);
    srv_pointer(sid(a), NXW_PTR_BUTTON, 1, 1, NXW_BUTTON_LEFT);
    srv_pointer(sid(a), NXW_PTR_MOTION, 2, 2, NXW_BUTTON_LEFT);
    srv_pointer(sid(b), NXW_PTR_MOTION, 3, 3, 0);
    srv_pointer(sid(a), NXW_PTR_MOTION, 4, 4, NXW_BUTTON_LEFT);
    check(drain() == 5, "motion around a button or another window keeps its order");

    srv_key(sid(b), 'q');
    srv_close_request(sid(b));
    check(next_ev(&e) == 1 && e.type == NXW_EV_KEY && e.window == b && e.key.ascii == 'q' && e.key.pressed == 1,
          "KEY event");
    check(next_ev(&e) == 1 && e.type == NXW_EV_CLOSE && e.window == b, "CLOSE event");

    /* Overflow: the oldest motion goes first, then the oldest events */
    log_clear();
    srv_pointer(sid(a), NXW_PTR_MOTION, 9, 9, 0);
    for (int i = 0; i < 64; i++) srv_key(sid(a), (uint8_t)('0' + (i % 10)));
    int keys = 0, motions = 0, n = 0;
    while (next_ev(&e) == 1) { n++; if (e.type == NXW_EV_KEY) keys++; if (is_motion(&e)) motions++; }
    check(n == 64 && keys == 64 && motions == 0, "overflow drops the motion before any key");
    check(log_has("event queue full: dropped 1 event"), "overflow logged");
    for (int i = 0; i < 70; i++) srv_key(sid(a), 'x');
    check(drain() == 64, "overflow keeps 64 events");

    /* Malformed server messages are dropped and logged */
    log_clear();
    uint8_t junk[16] = { 0 };
    srv_send(junk, 4);
    nxw_key_t k;
    memset(&k, 0, sizeof(k));
    k.hdr.op = NXW_OP_KEY; k.hdr.len = 12; k.win = sid(a);
    srv_send(&k, sizeof(k));                 /* len field disagrees */
    nxw_hdr_t unk = { 0x0299, 8, 0 };
    srv_send(&unk, sizeof(unk));
    srv_configure(sid(a), 0, 10, 0);
    srv_pointer(sid(a), 9, 0, 0, 0);
    srv_key(4242, 'z');                      /* not our window */
    inbox_push(&unk, 8, 0);                  /* carries a handle */
    k_shm[0].used = 1; k_shm[0].refs = 0; k_shm[0].mem = calloc(1, 16); k_shm[0].size = 16;
    check(drain() == 0, "malformed and foreign messages give no events");
    check(log_has("dropped a 4-byte message") && log_has("malformed server message") &&
          log_has("CONFIGURE with a bad size") && log_has("unknown kind") &&
          log_has("carrying handles"), "malformed messages logged");
    check(open_handles() == 0, "a handle from the server is closed");

    /* Queued events of a destroyed window are dropped with it */
    srv_key(sid(a), 'a');
    srv_key(sid(b), 'b');
    nxw_poll_event(&e);                      /* reads both into the queue, returns a's */
    srv_key(sid(a), 'a');
    nxw_poll_event(&e);
    check(e.window == b || e.window == a, "queued");
    nxw_window_destroy(a);
    int foreign = 0;
    while (next_ev(&e) == 1) if (e.window == a) foreign = 1;
    check(!foreign, "destroy drops the window's queued events");
}

static void test_tokens(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *a = mkwin("a", 20, 20);
    nxw_frame_t f;
    check(nxw_window_destroy(a) == 0, "destroy");
    check(S.last_destroy == 1, "destroy: WIN_DESTROY sent");
    check(nxw_window_destroy(a) == -NXW_EBADF, "destroy twice is -EBADF");
    check(nxw_frame_begin(a, &f) == -NXW_EBADF, "stale token: begin is -EBADF");
    check(nxw_window_set_title(a, "x") == -NXW_EBADF, "stale token: set_title is -EBADF");
    nxw_window_t *b = mkwin("b", 20, 20);    /* reuses the slot */
    check(b != a, "a reused slot gets a new token");
    check(nxw_frame_begin(a, &f) == -NXW_EBADF, "the old token stays invalid after reuse");
    int dummy;
    check(nxw_frame_begin((nxw_window_t *)&dummy, &f) == -NXW_EBADF, "a real pointer is not a window");
    check(nxw_frame_begin(NULL, &f) == -NXW_EBADF, "NULL is not a window");
    check(nxw_window_get_size((nxw_window_t *)(uintptr_t)0x6E78770000000109ULL, NULL, NULL, NULL) == -NXW_EBADF,
          "a forged token is not a window");
    check(nxw_window_set_title(b, "New title") == 0 && strcmp(S.last_title, "New title") == 0, "set_title");
    char t64[65];
    memset(t64, 'a', 64); t64[64] = 0;
    check(nxw_window_set_title(b, t64) == -NXW_EINVAL && nxw_window_set_title(b, NULL) == -NXW_EINVAL,
          "set_title validates");

    /* Tokens from an earlier connection stay invalid */
    nxw_disconnect();
    nxw_connect(NULL);
    nxw_window_t *c = mkwin("c", 20, 20);
    check(c != b && nxw_frame_begin(b, &f) == -NXW_EBADF, "tokens do not survive a reconnect");
}

static void test_send_full(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *a = mkwin("a", 20, 20);
    k_send_eagain = 3;
    check(nxw_window_set_title(a, "later") == 0 && strcmp(S.last_title, "later") == 0,
          "a full server queue: send waits for room");
    k_send_eagain = -1;
    uint64_t t0 = k_now;
    check(nxw_window_set_title(a, "never") == -NXW_ETIMEDOUT, "a server that never reads: -ETIMEDOUT");
    check(k_now - t0 >= 1000 && k_now - t0 < 1100, "... after about 1 s");
    k_send_eagain = 0;
}

static void test_server_gone(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *a = mkwin("a", 20, 20);
    nxw_frame_t f;
    nxw_frame_begin(a, &f);
    nxw_frame_commit(a, NULL, 0);
    srv_key(sid(a), 'x');
    k_server_closed = 1;
    nxw_event_t e;
    int got_key = 0, got_disc = 0;
    while (nxw_poll_event(&e) == 1) {
        if (e.type == NXW_EV_KEY) got_key = 1;
        if (e.type == NXW_EV_DISCONNECTED && e.window == NULL) got_disc++;
    }
    check(got_disc == 1, "server gone: one NXW_EV_DISCONNECTED");
    check(!got_key, "server gone: older events are dropped");
    check(nxw_poll_event(&e) == -NXW_ENOTCONN && nxw_wait_event(&e, 10) == -NXW_ENOTCONN,
          "server gone: event calls are -ENOTCONN afterwards");
    check(client_maps() == 0, "server gone: buffers unmapped");
    check(nxw_frame_begin(a, &f) == -NXW_ENOTCONN, "server gone: begin is -ENOTCONN");
    check(nxw_window_set_title(a, "x") == -NXW_ENOTCONN, "server gone: set_title is -ENOTCONN");
    check(mkwin("b", 10, 10) == NULL, "server gone: create fails");
    check(nxw_window_destroy(a) == 0, "server gone: destroy frees locally");
    nxw_disconnect();
    check(!k_chan_open, "server gone: disconnect closes the channel");

    /* The server closing while the app waits */
    reset_all();
    nxw_connect(NULL);
    k_server_closed = 1;
    check(nxw_wait_event(&e, NX_WAIT_FOREVER) == 1 && e.type == NXW_EV_DISCONNECTED,
          "wait_event wakes for a closed server");
}

static void test_cleanup(void) {
    reset_all();
    nxw_connect(NULL);
    nxw_window_t *w[8];
    nxw_frame_t f;
    for (int i = 0; i < 8; i++) {
        w[i] = mkwin("w", 16 + (uint32_t)i, 16);
        nxw_frame_begin(w[i], &f);
        nxw_frame_commit(w[i], NULL, 0);
    }
    check(client_maps() == 8, "cleanup: eight buffers mapped");
    nxw_disconnect();
    check(S.n_destroyed == 8, "cleanup: disconnect destroys every window");
    check(client_maps() == 0 && open_handles() == 0 && !k_chan_open,
          "cleanup: no mapping, handle or channel left");
    int live = 0;
    for (int s = 0; s < MAX_SHM; s++) if (k_shm[s].used) live++;
    check(live == 0, "cleanup: every shm object freed once the server drops it too");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-v") == 0) g_verbose = 1;
    test_connect();
    test_create();
    test_frames();
    test_events();
    test_tokens();
    test_send_full();
    test_server_gone();
    test_cleanup();
    reset_all();
    check(k_forever_waits == 0, "no wait was left to hang forever");
    printf("%d checks\n", g_checks);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail;
}
