/*
 * libnxwindow - client side of window protocol 1.0 (see nxwindow.h)
 *
 * Spec: docs/desktop/proposals/libnxwindow.md.
 *
 * State is static: one connection, NXW_WINDOWS_PER_CLIENT window slots and
 * a ring of NXWL_QUEUE events. Every server message is read by pump(),
 * validated (exact length for its op, no handles) and either consumed by
 * the call that waits for it (WELCOME, WIN_CREATED, an ERROR echoing that
 * request's seq) or turned into an event. The server is the desktop, a
 * system process, but its messages are still checked: a malformed one is
 * logged and dropped, never trusted.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxsyscall.h"
#include "nxwindow.h"

/* ============ Limits and constants ============ */

#define NXWL_QUEUE          64          /* queued events */
#define NXWL_REPLY_MS       2000        /* HELLO and WIN_CREATE replies */
#define NXWL_SEND_MS        1000        /* waiting for room in the server's queue */
#define NXWL_DROP_LOG_MS    1000
#define NXWL_RX_MAX         NX_CHAN_DATA_MAX
#define NXWL_EAGAIN         11          /* kernel errno value */

/* Window tokens: tag | generation << 8 | (slot + 1). Never 0, never a real
 * pointer; checked against the slot's generation on every call. */
#define NXWL_TAG            0x6E78770000000000ULL       /* "nxw" */
#define NXWL_TAG_MASK       0xFFFFFF0000000000ULL
#define NXWL_GEN_MASK       0xFFFFFFFFULL

_Static_assert(NXW_WINDOWS_PER_CLIENT < 255, "slot must fit in the token's low byte");

/* ============ State ============ */

typedef struct {
    int       used;
    uint32_t  gen;              /* bumped on every reuse of the slot */
    uint32_t  id;               /* server window id */
    uint32_t  width, height;    /* current content size */
    uint32_t  state;            /* NXW_STATE_* */
    /* buffer (our read-write mapping; the server has its own) */
    uint32_t *map;
    uint32_t  buf_w, buf_h, buf_stride;
    int       fresh;            /* attached, not committed yet: frame.full */
    int       begun;            /* nxw_frame_begin since the last commit */
    int       busy;             /* commit sent, FRAME_DONE not seen */
    uint32_t  commit_seq;
    int       merge_logged;
} nxwl_win_t;

typedef struct {
    int         connected;      /* nxw_connect succeeded */
    int         dead;           /* the server went away */
    int         dead_delivered; /* NXW_EV_DISCONNECTED was returned */
    nx_handle_t chan;
    uint32_t    seq;
    nxw_display_info_t info;
    nxwl_win_t  wins[NXW_WINDOWS_PER_CLIENT];
    /* event ring */
    nxw_event_t q[NXWL_QUEUE];
    uint32_t    q_head, q_count;
    uint64_t    drops, drops_logged, last_drop_log;
    /* the reply a synchronous call is waiting for */
    uint32_t    await_seq;
    uint16_t    await_op;
    int         await_done;
    int32_t     await_code;     /* 0, or the ERROR's code */
    union {
        nxw_welcome_t     welcome;
        nxw_win_created_t created;
    } await_msg;
} nxwl_state_t;

static nxwl_state_t g;
/* Outside g so that tokens from an earlier connection stay invalid after a
 * reconnect (g is cleared on every connect and disconnect). */
static uint32_t g_next_gen = 1;
static uint8_t g_rx[NXWL_RX_MAX] __attribute__((aligned(8)));

/* ============ Small helpers (freestanding) ============ */

static void z_mem(void *p, size_t n) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (n--) *b++ = 0;
}

static void c_mem(void *d, const void *s, size_t n) {
    uint8_t *o = (uint8_t *)d;
    const uint8_t *i = (const uint8_t *)s;
    while (n--) *o++ = *i++;
}

/* ============ Logging ============ */

static char g_line[160];
static int g_len;

static void lg_begin(void) {
    const char *p = "[NXWIN] ";
    g_len = 0;
    while (*p) g_line[g_len++] = *p++;
}

static void lg_str(const char *s) {
    if (!s) s = "(null)";
    while (*s && g_len < (int)sizeof(g_line) - 2) g_line[g_len++] = *s++;
}

static void lg_dec(int64_t v) {
    char b[24];
    int i = 23;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    b[i] = '\0';
    if (u == 0) b[--i] = '0';
    while (u && i > 1) { b[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) b[--i] = '-';
    lg_str(&b[i]);
}

static void lg_hex16(uint32_t v) {
    char b[7] = { '0', 'x', 0, 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        uint32_t d = (v >> (12 - 4 * i)) & 0xF;
        b[2 + i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    lg_str(b);
}

static void lg_end(void) {
    g_line[g_len++] = '\n';
    g_line[g_len] = '\0';
    nx_debug_print(g_line);
}

/* Log a failure returned to the app; returns code for "return fail(...)". */
static int fail(const char *call, const char *why, int code) {
    lg_begin(); lg_str(call); lg_str(": "); lg_str(why); lg_str(" ("); lg_dec(code); lg_str(")"); lg_end();
    return code;
}

/* ============ Window tokens ============ */

static nxw_window_t *token_of(int slot) {
    uint64_t t = NXWL_TAG | ((uint64_t)(g.wins[slot].gen & NXWL_GEN_MASK) << 8) | (uint64_t)(slot + 1);
    return (nxw_window_t *)(uintptr_t)t;
}

/* The slot of a live window, or NULL. */
static nxwl_win_t *resolve(const nxw_window_t *w) {
    uint64_t t = (uint64_t)(uintptr_t)w;
    if ((t & NXWL_TAG_MASK) != NXWL_TAG) return NULL;
    uint64_t slot = (t & 0xFF);
    uint64_t gen = (t >> 8) & NXWL_GEN_MASK;
    if (slot == 0 || slot > NXW_WINDOWS_PER_CLIENT) return NULL;
    nxwl_win_t *s = &g.wins[slot - 1];
    if (!s->used || s->gen != (uint32_t)gen) return NULL;
    return s;
}

static int slot_index(const nxwl_win_t *s) { return (int)(s - g.wins); }

static nxwl_win_t *win_by_id(uint32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NXW_WINDOWS_PER_CLIENT; i++)
        if (g.wins[i].used && g.wins[i].id == id) return &g.wins[i];
    return NULL;
}

/* ============ Buffers ============ */

static void buf_drop(nxwl_win_t *s) {
    if (s->map) {
        int r = nx_shm_unmap(s->map);
        if (r != 0) { lg_begin(); lg_str("shm.unmap failed: "); lg_dec(r); lg_end(); }
    }
    s->map = NULL;
    s->buf_w = s->buf_h = s->buf_stride = 0;
    s->fresh = 0;
    s->begun = 0;
}

/* ============ Event queue ============ */

static void q_drop_log(void) {
    uint64_t now = nx_gettime();
    if (g.drops != g.drops_logged && now - g.last_drop_log >= NXWL_DROP_LOG_MS) {
        lg_begin(); lg_str("event queue full: dropped "); lg_dec((int64_t)(g.drops - g.drops_logged));
        lg_str(" event(s)"); lg_end();
        g.drops_logged = g.drops;
        g.last_drop_log = now;
    }
}

/* Remove entry i (0 = oldest) keeping the order of the rest. */
static void q_remove(uint32_t i) {
    for (uint32_t k = i; k + 1 < g.q_count; k++)
        g.q[(g.q_head + k) % NXWL_QUEUE] = g.q[(g.q_head + k + 1) % NXWL_QUEUE];
    g.q_count--;
}

static int is_motion(const nxw_event_t *e) {
    return e->type == NXW_EV_POINTER && e->pointer.kind == NXW_PTR_MOTION;
}

static void q_push(const nxw_event_t *e) {
    /* Coalesce motion with a motion for the same window at the tail: the
     * newest position wins and the order of other events is kept. */
    if (is_motion(e) && g.q_count > 0) {
        nxw_event_t *tail = &g.q[(g.q_head + g.q_count - 1) % NXWL_QUEUE];
        if (is_motion(tail) && tail->window == e->window) {
            *tail = *e;
            return;
        }
    }
    if (g.q_count == NXWL_QUEUE) {
        /* Full: the oldest motion goes first, else the oldest event */
        uint32_t victim = 0;
        for (uint32_t k = 0; k < g.q_count; k++) {
            if (is_motion(&g.q[(g.q_head + k) % NXWL_QUEUE])) { victim = k; break; }
        }
        q_remove(victim);
        g.drops++;
        q_drop_log();
    }
    g.q[(g.q_head + g.q_count) % NXWL_QUEUE] = *e;
    g.q_count++;
}

static int q_pop(nxw_event_t *out) {
    if (g.q_count == 0) return 0;
    *out = g.q[g.q_head];
    g.q_head = (g.q_head + 1) % NXWL_QUEUE;
    g.q_count--;
    return 1;
}

/* Forget queued events of a window that is gone. */
static void q_purge(const nxw_window_t *w) {
    for (uint32_t k = 0; k < g.q_count;) {
        if (g.q[(g.q_head + k) % NXWL_QUEUE].window == w) q_remove(k);
        else k++;
    }
}

/* ============ Connection loss ============ */

static void go_dead(const char *why) {
    if (g.dead) return;
    g.dead = 1;
    lg_begin(); lg_str("connection lost: "); lg_str(why); lg_end();
    for (int i = 0; i < NXW_WINDOWS_PER_CLIENT; i++) {
        if (!g.wins[i].used) continue;
        buf_drop(&g.wins[i]);
        g.wins[i].busy = 0;
    }
    /* Only the DISCONNECTED event matters now */
    g.q_count = 0;
    nxw_event_t e;
    z_mem(&e, sizeof(e));
    e.type = NXW_EV_DISCONNECTED;
    q_push(&e);
}

/* ============ Receiving ============ */

static uint16_t expected_len(uint16_t op) {
    switch (op) {
        case NXW_OP_WELCOME:       return (uint16_t)sizeof(nxw_welcome_t);
        case NXW_OP_WIN_CREATED:   return (uint16_t)sizeof(nxw_win_created_t);
        case NXW_OP_CONFIGURE:     return (uint16_t)sizeof(nxw_configure_t);
        case NXW_OP_FRAME_DONE:    return (uint16_t)sizeof(nxw_frame_done_t);
        case NXW_OP_CLOSE_REQUEST: return (uint16_t)sizeof(nxw_close_request_t);
        case NXW_OP_KEY:           return (uint16_t)sizeof(nxw_key_t);
        case NXW_OP_POINTER:       return (uint16_t)sizeof(nxw_pointer_t);
        case NXW_OP_ERROR:         return (uint16_t)sizeof(nxw_error_t);
        default:                   return 0;
    }
}

/* A WIN_CREATED nobody waits for (its call timed out): the window exists
 * on the server, so destroy it instead of leaking it. */
static void destroy_orphan(uint32_t id);

static void handle_msg(const uint8_t *data, uint32_t len) {
    if (len < sizeof(nxw_hdr_t)) {
        lg_begin(); lg_str("dropped a "); lg_dec(len); lg_str("-byte message from the server"); lg_end();
        return;
    }
    nxw_hdr_t h;
    c_mem(&h, data, sizeof(h));
    uint16_t want = expected_len(h.op);
    if (want == 0 || h.len != want || len != want) {
        lg_begin(); lg_str("dropped a malformed server message, op "); lg_hex16(h.op);
        lg_str(", len "); lg_dec(len); lg_end();
        return;
    }

    nxw_event_t e;
    z_mem(&e, sizeof(e));

    switch (h.op) {
    case NXW_OP_WELCOME: {
        if (g.await_op == NXW_OP_HELLO && !g.await_done && h.seq == g.await_seq) {
            c_mem(&g.await_msg.welcome, data, sizeof(nxw_welcome_t));
            g.await_done = 1;
            g.await_code = 0;
        } else {
            lg_begin(); lg_str("unexpected WELCOME dropped"); lg_end();
        }
        return;
    }
    case NXW_OP_WIN_CREATED: {
        nxw_win_created_t m;
        c_mem(&m, data, sizeof(m));
        if (g.await_op == NXW_OP_WIN_CREATE && !g.await_done && h.seq == g.await_seq) {
            g.await_msg.created = m;
            g.await_done = 1;
            g.await_code = 0;
        } else {
            lg_begin(); lg_str("late WIN_CREATED for window "); lg_dec(m.win); lg_str(": destroying it"); lg_end();
            destroy_orphan(m.win);
        }
        return;
    }
    case NXW_OP_ERROR: {
        nxw_error_t m;
        c_mem(&m, data, sizeof(m));
        if (g.await_op != 0 && !g.await_done && h.seq == g.await_seq && m.op == g.await_op) {
            g.await_done = 1;
            g.await_code = m.code < 0 ? m.code : -NXW_EINVAL;
            return;
        }
        nxwl_win_t *s = win_by_id(m.win);
        lg_begin(); lg_str("server refused op "); lg_hex16(m.op); lg_str(" (seq "); lg_dec(h.seq);
        lg_str(", window "); lg_dec(m.win); lg_str("): "); lg_dec(m.code); lg_end();
        if (s && m.op == NXW_OP_COMMIT && s->busy && s->commit_seq == h.seq) {
            s->busy = 0;            /* no FRAME_DONE will come for it */
        }
        if (s && m.op == NXW_OP_BUF_ATTACH) {
            buf_drop(s);            /* not attached: the next frame makes a new one */
            s->busy = 0;
        }
        e.type = NXW_EV_ERROR;
        e.window = s ? token_of(slot_index(s)) : NULL;
        e.error.op = m.op;
        e.error.code = m.code;
        q_push(&e);
        return;
    }
    default:
        break;
    }

    /* Window events: every one names a window; others' or stale ids are dropped */
    uint32_t win;
    c_mem(&win, data + sizeof(nxw_hdr_t), sizeof(win));
    nxwl_win_t *s = win_by_id(win);
    if (!s) {
#ifdef NXWIN_TRACE
        lg_begin(); lg_str("event "); lg_hex16(h.op); lg_str(" for unknown window "); lg_dec(win); lg_end();
#endif
        return;
    }
    e.window = token_of(slot_index(s));

    switch (h.op) {
    case NXW_OP_CONFIGURE: {
        nxw_configure_t m;
        c_mem(&m, data, sizeof(m));
        if (m.width < 1 || m.height < 1 || m.width > NXW_SIZE_MAX || m.height > NXW_SIZE_MAX) {
            lg_begin(); lg_str("CONFIGURE with a bad size dropped"); lg_end();
            return;
        }
        s->width = m.width;
        s->height = m.height;
        s->state = m.state;
        e.type = NXW_EV_CONFIGURE;
        e.configure.width = m.width;
        e.configure.height = m.height;
        e.configure.state = m.state;
        break;
    }
    case NXW_OP_FRAME_DONE: {
        nxw_frame_done_t m;
        c_mem(&m, data, sizeof(m));
        if (!s->busy || m.commit_seq != s->commit_seq) {
#ifdef NXWIN_TRACE
            lg_begin(); lg_str("stale FRAME_DONE for window "); lg_dec(win); lg_end();
#endif
            return;
        }
        s->busy = 0;
        e.type = NXW_EV_FRAME;
        e.frame.commit_seq = m.commit_seq;
        e.frame.time_ms = m.time_ms;
        break;
    }
    case NXW_OP_CLOSE_REQUEST:
        e.type = NXW_EV_CLOSE;
        break;
    case NXW_OP_KEY: {
        nxw_key_t m;
        c_mem(&m, data, sizeof(m));
        e.type = NXW_EV_KEY;
        e.key.scancode = m.scancode;
        e.key.ascii = m.ascii;
        e.key.pressed = m.pressed ? 1 : 0;
        e.key.modifiers = m.modifiers;
        e.key.time_ms = m.time_ms;
        break;
    }
    case NXW_OP_POINTER: {
        nxw_pointer_t m;
        c_mem(&m, data, sizeof(m));
        if (m.kind < NXW_PTR_MOTION || m.kind > NXW_PTR_WHEEL) {
            lg_begin(); lg_str("POINTER with unknown kind "); lg_dec(m.kind); lg_str(" dropped"); lg_end();
            return;
        }
        e.type = NXW_EV_POINTER;
        e.pointer.kind = m.kind;
        e.pointer.x = m.x;
        e.pointer.y = m.y;
        e.pointer.buttons = m.buttons;
        e.pointer.wheel = m.wheel;
        e.pointer.time_ms = m.time_ms;
        break;
    }
    default:
        return;
    }
    q_push(&e);
}

/* Read every message waiting on the channel. 0, or -NXW_ENOTCONN once the
 * connection is gone. */
static int pump(void) {
    if (!g.connected) return -NXW_ENOTCONN;
    if (g.dead) return -NXW_ENOTCONN;
    for (;;) {
        nx_handle_t hs[NX_CHAN_HANDLES_MAX];
        nx_chan_msg_t m;
        m.data = (uint64_t)(uintptr_t)g_rx;
        m.handles = (uint64_t)(uintptr_t)hs;
        m.data_len = sizeof(g_rx);
        m.handle_count = NX_CHAN_HANDLES_MAX;
        m.sender_pid = 0;
        m.flags = 0;
        int r = nx_chan_recv(g.chan, &m);
        if (r == -NXWL_EAGAIN) return 0;
        if (r == -NXW_EPIPE) { go_dead("the window server closed the connection"); return -NXW_ENOTCONN; }
        if (r != 0) {
            lg_begin(); lg_str("chan.recv failed: "); lg_dec(r); lg_end();
            go_dead("receive failed");
            return -NXW_ENOTCONN;
        }
        if (m.handle_count > 0) {
            /* The protocol sends no handles to clients */
            for (uint32_t i = 0; i < m.handle_count && i < NX_CHAN_HANDLES_MAX; i++) nx_handle_close(hs[i]);
            lg_begin(); lg_str("dropped a server message carrying handles"); lg_end();
            continue;
        }
        handle_msg(g_rx, m.data_len);
    }
}

/* Wait for the channel to become readable (or closed), up to ms.
 * 1 ready, 0 timeout, -NXW_ENOTCONN. */
static int wait_readable(uint64_t ms) {
    nx_wait_item_t it;
    it.handle = g.chan;
    it.signals = NX_SIGNAL_READABLE | NX_SIGNAL_PEER_CLOSED;
    it.observed = 0;
    it.reserved = 0;
    int r = nx_obj_wait(&it, 1, ms);
    if (r > 0) return 1;
    if (r == -NXW_ETIMEDOUT) return 0;
    lg_begin(); lg_str("obj.wait failed: "); lg_dec(r); lg_end();
    go_dead("wait failed");
    return -NXW_ENOTCONN;
}

/* ============ Sending ============ */

/* Send one message (and at most one handle, which moves to the server on
 * success). Waits up to NXWL_SEND_MS for room, reading events meanwhile.
 * 0, -NXW_ETIMEDOUT, -NXW_EPIPE, or another negative errno. */
static int send_msg(const void *msg, uint16_t len, nx_handle_t handle) {
    if (!g.connected || g.dead) return -NXW_ENOTCONN;
    nx_handle_t hs[1] = { handle };
    nx_chan_msg_t m;
    m.data = (uint64_t)(uintptr_t)msg;
    m.handles = (uint64_t)(uintptr_t)hs;
    m.data_len = len;
    m.handle_count = handle ? 1u : 0u;
    m.sender_pid = 0;
    m.flags = 0;
    uint64_t start = nx_gettime();
    for (;;) {
        int r = nx_chan_send(g.chan, &m);
        if (r == 0) return 0;
        if (r == -NXW_EPIPE) { go_dead("the window server closed the connection"); return -NXW_EPIPE; }
        if (r != -NXWL_EAGAIN) {
            lg_begin(); lg_str("chan.send failed: "); lg_dec(r); lg_end();
            return r;
        }
        uint64_t spent = nx_gettime() - start;
        if (spent >= NXWL_SEND_MS) return -NXW_ETIMEDOUT;
        nx_wait_item_t it;
        it.handle = g.chan;
        it.signals = NX_SIGNAL_WRITABLE | NX_SIGNAL_READABLE | NX_SIGNAL_PEER_CLOSED;
        it.observed = 0;
        it.reserved = 0;
        int w = nx_obj_wait(&it, 1, NXWL_SEND_MS - spent);
        if (w < 0 && w != -NXW_ETIMEDOUT) {
            lg_begin(); lg_str("obj.wait failed: "); lg_dec(w); lg_end();
            return w;
        }
        if (w > 0 && (it.observed & (NX_SIGNAL_READABLE | NX_SIGNAL_PEER_CLOSED))) {
            if (pump() != 0) return -NXW_EPIPE;
        }
    }
}

static uint32_t next_seq(void) {
    g.seq++;
    if (g.seq == 0) g.seq = 1;
    return g.seq;
}

static void hdr(nxw_hdr_t *h, uint16_t op, uint16_t len, uint32_t seq) {
    h->op = op;
    h->len = len;
    h->seq = seq;
}

/* Send a request and wait for its reply (or an ERROR with its seq).
 * 0 with the reply in g.await_msg, the server's negative code, or an
 * errno from sending or waiting. */
static int request_reply(const void *msg, uint16_t len, uint16_t op, uint32_t seq) {
    g.await_op = op;
    g.await_seq = seq;
    g.await_done = 0;
    g.await_code = 0;
    int r = send_msg(msg, len, 0);
    if (r == 0) {
        uint64_t start = nx_gettime();
        for (;;) {
            /* A reply read just before the server closed still counts
             * (HELLO refused: ERROR, then the close) */
            int p = pump();
            if (g.await_done) { r = g.await_code; break; }
            if (p != 0) { r = -NXW_EPIPE; break; }
            uint64_t spent = nx_gettime() - start;
            if (spent >= NXWL_REPLY_MS) { r = -NXW_ETIMEDOUT; break; }
            int w = wait_readable(NXWL_REPLY_MS - spent);
            if (w < 0) { r = -NXW_EPIPE; break; }
        }
    }
    g.await_op = 0;
    g.await_seq = 0;
    return r;
}

static void destroy_orphan(uint32_t id) {
    nxw_win_destroy_t m;
    z_mem(&m, sizeof(m));
    hdr(&m.hdr, NXW_OP_WIN_DESTROY, (uint16_t)sizeof(m), next_seq());
    m.win = id;
    nx_handle_t hs[1] = { 0 };
    nx_chan_msg_t cm;
    cm.data = (uint64_t)(uintptr_t)&m;
    cm.handles = (uint64_t)(uintptr_t)hs;
    cm.data_len = sizeof(m);
    cm.handle_count = 0;
    cm.sender_pid = 0;
    cm.flags = 0;
    /* Called from inside pump(): never wait here, a failure is only logged */
    int r = nx_chan_send(g.chan, &cm);
    if (r != 0) { lg_begin(); lg_str("could not destroy orphan window: "); lg_dec(r); lg_end(); }
}

/* ============ Validation ============ */

/* 0 when title is NUL-terminated within NXW_TITLE_MAX bytes. */
static int title_ok(const char *t) {
    if (!t) return 0;
    for (int i = 0; i < NXW_TITLE_MAX; i++) if (t[i] == '\0') return 1;
    return 0;
}

static void title_copy(char out[NXW_TITLE_MAX], const char *t) {
    int i = 0;
    for (; i < NXW_TITLE_MAX - 1 && t[i]; i++) out[i] = t[i];
    for (; i < NXW_TITLE_MAX; i++) out[i] = '\0';
}

/* Live window for a call, or NULL with *err set (and logged). */
static nxwl_win_t *live(const char *call, nxw_window_t *w, int *err) {
    if (!g.connected) { *err = fail(call, "not connected", -NXW_ENOTCONN); return NULL; }
    nxwl_win_t *s = resolve(w);
    if (!s) { *err = fail(call, "not a live window", -NXW_EBADF); return NULL; }
    if (g.dead) { *err = fail(call, "the window server is gone", -NXW_ENOTCONN); return NULL; }
    return s;
}

/* ============ Connection ============ */

int nxw_connect(nxw_display_info_t *info) {
    if (g.connected) return fail("nxw_connect", "already connected", -NXW_EISCONN);

    nx_abi_info_t abi;
    z_mem(&abi, sizeof(abi));
    int r = nx_abi_query(&abi);
    if (r != 0) return fail("nxw_connect", "abi.query failed", -NXW_ENOSYS);
    if ((abi.flags & (NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC)) != (NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC))
        return fail("nxw_connect", "the kernel has no shared memory or IPC", -NXW_ENOSYS);

    nx_handle_t ch = nx_port_connect(NXW_PORT_NAME);
    if (ch == -NXW_ENOENT) return fail("nxw_connect", "no window server (port nx.display)", -NXW_ENOENT);
    if (ch <= 0) return fail("nxw_connect", "port.connect failed", ch < 0 ? ch : -NXW_EPIPE);

    z_mem(&g, sizeof(g));
    g.connected = 1;
    g.chan = ch;

    nxw_hello_t m;
    z_mem(&m, sizeof(m));
    uint32_t seq = next_seq();
    hdr(&m.hdr, NXW_OP_HELLO, (uint16_t)sizeof(m), seq);
    m.major = NXW_PROTO_MAJOR;
    m.minor = NXW_PROTO_MINOR;
    m.flags = 0;
    r = request_reply(&m, (uint16_t)sizeof(m), NXW_OP_HELLO, seq);
    if (r != 0) {
        int code = r;
        const char *why = "HELLO not sent";
        if (g.await_done) { why = "the server refused protocol 1.0"; code = -NXW_EPROTO; }
        else if (r == -NXW_ETIMEDOUT) why = "no WELCOME within 2 s";
        else if (r == -NXW_EPIPE) why = "the server closed the connection";
        nx_handle_close(ch);
        z_mem(&g, sizeof(g));
        return fail("nxw_connect", why, code);
    }
    const nxw_welcome_t *w = &g.await_msg.welcome;
    if (w->major != NXW_PROTO_MAJOR || w->max_windows == 0 || w->max_size == 0 ||
        w->screen_w == 0 || w->screen_h == 0) {
        nx_handle_close(ch);
        z_mem(&g, sizeof(g));
        return fail("nxw_connect", "bad WELCOME", -NXW_EPROTO);
    }
    g.info.proto_major = w->major;
    g.info.proto_minor = w->minor;
    g.info.screen_w = w->screen_w;
    g.info.screen_h = w->screen_h;
    g.info.max_windows = w->max_windows < NXW_WINDOWS_PER_CLIENT ? w->max_windows : NXW_WINDOWS_PER_CLIENT;
    g.info.max_size = w->max_size < NXW_SIZE_MAX ? w->max_size : NXW_SIZE_MAX;
    if (info) *info = g.info;

    lg_begin(); lg_str("connected, protocol "); lg_dec(w->major); lg_str("."); lg_dec(w->minor);
    lg_str(", screen "); lg_dec(w->screen_w); lg_str("x"); lg_dec(w->screen_h); lg_end();
    return 0;
}

void nxw_disconnect(void) {
    if (!g.connected) return;
    for (int i = 0; i < NXW_WINDOWS_PER_CLIENT; i++) {
        if (!g.wins[i].used) continue;
        nxw_window_destroy(token_of(i));
    }
    nx_handle_close(g.chan);
    lg_begin(); lg_str("disconnected"); lg_end();
    z_mem(&g, sizeof(g));
}

nx_handle_t nxw_connection_handle(void) {
    return g.connected && !g.dead ? g.chan : NX_HANDLE_INVALID;
}

/* ============ Windows ============ */

int nxw_window_create(const nxw_window_desc_t *d, nxw_window_t **out) {
    const char *call = "nxw_window_create";
    if (!d || !out) return fail(call, "NULL argument", -NXW_EINVAL);
    if (!g.connected || g.dead) return fail(call, "not connected", -NXW_ENOTCONN);
    if (!title_ok(d->title)) return fail(call, "title missing or longer than 63 bytes", -NXW_EINVAL);
    if (d->width < 1 || d->height < 1 || d->width > g.info.max_size || d->height > g.info.max_size)
        return fail(call, "size out of range", -NXW_EINVAL);
    if ((d->flags & ~NXW_WIN_RESIZABLE) != 0) return fail(call, "unknown flags", -NXW_EINVAL);

    int slot = -1;
    for (int i = 0; i < (int)g.info.max_windows; i++) if (!g.wins[i].used) { slot = i; break; }
    if (slot < 0) return fail(call, "too many windows", -NXW_EMFILE);

    nxw_win_create_t m;
    z_mem(&m, sizeof(m));
    uint32_t seq = next_seq();
    hdr(&m.hdr, NXW_OP_WIN_CREATE, (uint16_t)sizeof(m), seq);
    m.width = d->width;
    m.height = d->height;
    m.x = d->x;
    m.y = d->y;
    m.flags = d->flags;
    title_copy(m.title, d->title);
    int r = request_reply(&m, (uint16_t)sizeof(m), NXW_OP_WIN_CREATE, seq);
    if (r != 0) return fail(call, "the server refused or did not answer", r);

    const nxw_win_created_t *c = &g.await_msg.created;
    if (c->win == 0 || win_by_id(c->win)) {
        /* Never destroy by this id: it is 0 or one of our live windows */
        return fail(call, "bad window id in WIN_CREATED", -NXW_EPROTO);
    }
    if (c->width < 1 || c->height < 1 || c->width > NXW_SIZE_MAX || c->height > NXW_SIZE_MAX) {
        destroy_orphan(c->win);
        return fail(call, "bad size in WIN_CREATED", -NXW_EPROTO);
    }
    nxwl_win_t *s = &g.wins[slot];
    uint32_t gen = g_next_gen++;
    if (g_next_gen == 0) g_next_gen = 1;
    z_mem(s, sizeof(*s));
    s->used = 1;
    s->gen = gen;
    s->id = c->win;
    s->width = c->width;
    s->height = c->height;
    *out = token_of(slot);
    lg_begin(); lg_str("window "); lg_dec(s->id); lg_str(" created, "); lg_dec(s->width);
    lg_str("x"); lg_dec(s->height); lg_end();
    return 0;
}

int nxw_window_destroy(nxw_window_t *w) {
    nxwl_win_t *s = resolve(w);
    if (!g.connected || !s) return fail("nxw_window_destroy", "not a live window", -NXW_EBADF);
    int r = 0;
    if (!g.dead) {
        nxw_win_destroy_t m;
        z_mem(&m, sizeof(m));
        hdr(&m.hdr, NXW_OP_WIN_DESTROY, (uint16_t)sizeof(m), next_seq());
        m.win = s->id;
        r = send_msg(&m, (uint16_t)sizeof(m), 0);
        /* -NXW_EPIPE: the server is gone and took the window with it */
        if (r == -NXW_EPIPE) r = 0;
        if (r != 0) fail("nxw_window_destroy", "WIN_DESTROY not sent; freed locally", r);
    }
    lg_begin(); lg_str("window "); lg_dec(s->id); lg_str(" destroyed"); lg_end();
    buf_drop(s);
    q_purge(w);
    uint32_t gen = s->gen;
    z_mem(s, sizeof(*s));
    s->gen = gen;           /* kept so the old token stays invalid */
    return r;
}

int nxw_window_set_title(nxw_window_t *w, const char *title) {
    int err = 0;
    nxwl_win_t *s = live("nxw_window_set_title", w, &err);
    if (!s) return err;
    if (!title_ok(title)) return fail("nxw_window_set_title", "title missing or longer than 63 bytes", -NXW_EINVAL);
    nxw_win_set_title_t m;
    z_mem(&m, sizeof(m));
    hdr(&m.hdr, NXW_OP_WIN_SET_TITLE, (uint16_t)sizeof(m), next_seq());
    m.win = s->id;
    title_copy(m.title, title);
    int r = send_msg(&m, (uint16_t)sizeof(m), 0);
    if (r != 0) return fail("nxw_window_set_title", "send failed", r);
    return 0;
}

int nxw_window_get_size(nxw_window_t *w, uint32_t *width, uint32_t *height, uint32_t *state) {
    nxwl_win_t *s = g.connected ? resolve(w) : NULL;
    if (!s) return fail("nxw_window_get_size", "not a live window", -NXW_EBADF);
    if (width) *width = s->width;
    if (height) *height = s->height;
    if (state) *state = s->state;
    return 0;
}

/* ============ Frames ============ */

/* Create, map and attach a buffer of the window's current size. */
static int buf_new(nxwl_win_t *s) {
    const char *call = "nxw_frame_begin";
    uint32_t w = s->width, h = s->height;
    uint32_t stride = w * 4u;                               /* w <= 4096: fits */
    uint64_t size = (uint64_t)stride * (uint64_t)h;         /* <= 64 MiB */
    nx_handle_t shm = nx_shm_create(size, 0);
    if (shm <= 0) return fail(call, "shm.create failed", shm < 0 ? shm : -NXW_ENOMEM);
    uint64_t got = 0;
    int64_t addr = nx_shm_map(shm, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE, &got);
    if (addr <= 0 || got < size) {
        if (addr > 0) nx_shm_unmap((void *)(uintptr_t)addr);
        nx_handle_close(shm);
        return fail(call, "shm.map failed", addr < 0 ? (int)addr : -NXW_ENOMEM);
    }
    /* The server gets read and map rights only */
    nx_handle_t ro = nx_handle_dup(shm, NX_RIGHT_READ | NX_RIGHT_MAP | NX_RIGHT_TRANSFER);
    nx_handle_close(shm);               /* our mapping keeps the memory alive */
    if (ro <= 0) {
        nx_shm_unmap((void *)(uintptr_t)addr);
        return fail(call, "handle.dup failed", ro < 0 ? ro : -NXW_ENOMEM);
    }
    nxw_buf_attach_t m;
    z_mem(&m, sizeof(m));
    hdr(&m.hdr, NXW_OP_BUF_ATTACH, (uint16_t)sizeof(m), next_seq());
    m.win = s->id;
    m.width = w;
    m.height = h;
    m.stride = stride;
    m.offset = 0;
    int r = send_msg(&m, (uint16_t)sizeof(m), ro);
    if (r != 0) {
        nx_handle_close(ro);            /* a failed send moves nothing */
        nx_shm_unmap((void *)(uintptr_t)addr);
        return fail(call, "BUF_ATTACH not sent", r);
    }
    /* The old buffer stays on screen (the server's own mapping) until the
     * next commit; ours is no longer needed. */
    buf_drop(s);
    s->map = (uint32_t *)(uintptr_t)addr;
    s->buf_w = w;
    s->buf_h = h;
    s->buf_stride = stride;
    s->fresh = 1;
    s->merge_logged = 0;
    lg_begin(); lg_str("window "); lg_dec(s->id); lg_str(" buffer "); lg_dec(w); lg_str("x");
    lg_dec(h); lg_str(", "); lg_dec((int64_t)size); lg_str(" bytes"); lg_end();
    return 0;
}

int nxw_frame_begin(nxw_window_t *w, nxw_frame_t *f) {
    int err = 0;
    nxwl_win_t *s = live("nxw_frame_begin", w, &err);
    if (!s) return err;
    if (!f) return fail("nxw_frame_begin", "frame is NULL", -NXW_EINVAL);
    /* Pick up a FRAME_DONE or CONFIGURE that is already waiting */
    if (pump() != 0) return fail("nxw_frame_begin", "the window server is gone", -NXW_ENOTCONN);
    if (s->busy) return -NXW_EBUSY;     /* expected while waiting: not logged */
    if (!s->map || s->buf_w != s->width || s->buf_h != s->height) {
        int r = buf_new(s);
        if (r != 0) return r;
    }
    s->begun = 1;
    f->pixels = s->map;
    f->width = s->buf_w;
    f->height = s->buf_h;
    f->stride = s->buf_stride;
    f->full = s->fresh;
    return 0;
}

/* Clip r to w x h; 0 when nothing is left. 64-bit, so any input is safe. */
static int clip(const nxw_rect_t *r, uint32_t w, uint32_t h, nxw_rect_t *out) {
    int64_t x0 = r->x, y0 = r->y;
    int64_t x1 = x0 + (int64_t)r->w, y1 = y0 + (int64_t)r->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)w) x1 = (int64_t)w;
    if (y1 > (int64_t)h) y1 = (int64_t)h;
    if (x0 >= x1 || y0 >= y1) return 0;
    out->x = (int32_t)x0;
    out->y = (int32_t)y0;
    out->w = (uint32_t)(x1 - x0);
    out->h = (uint32_t)(y1 - y0);
    return 1;
}

int nxw_frame_commit(nxw_window_t *w, const nxw_rect_t *rects, uint32_t count) {
    const char *call = "nxw_frame_commit";
    int err = 0;
    nxwl_win_t *s = live(call, w, &err);
    if (!s) return err;
    if (!s->begun || !s->map) return fail(call, "no nxw_frame_begin before it", -NXW_EINVAL);
    if (count > 0 && !rects) return fail(call, "rects is NULL", -NXW_EINVAL);

    nxw_commit_t m;
    z_mem(&m, sizeof(m));
    uint32_t seq = next_seq();
    hdr(&m.hdr, NXW_OP_COMMIT, (uint16_t)sizeof(m), seq);
    m.win = s->id;
    if (count > 0 && !s->fresh) {
        uint32_t n = 0;
        nxw_rect_t box = { 0, 0, 0, 0 };
        int have_box = 0;
        for (uint32_t i = 0; i < count; i++) {
            nxw_rect_t c;
            if (!clip(&rects[i], s->buf_w, s->buf_h, &c)) continue;
            if (n < NXW_DAMAGE_MAX) m.rects[n] = c;
            n++;
            if (!have_box) { box = c; have_box = 1; continue; }
            int64_t x0 = box.x < c.x ? box.x : c.x, y0 = box.y < c.y ? box.y : c.y;
            int64_t bx1 = (int64_t)box.x + box.w, cx1 = (int64_t)c.x + c.w;
            int64_t by1 = (int64_t)box.y + box.h, cy1 = (int64_t)c.y + c.h;
            int64_t x1 = bx1 > cx1 ? bx1 : cx1, y1 = by1 > cy1 ? by1 : cy1;
            box.x = (int32_t)x0; box.y = (int32_t)y0;
            box.w = (uint32_t)(x1 - x0); box.h = (uint32_t)(y1 - y0);
        }
        if (n > NXW_DAMAGE_MAX) {
            if (!s->merge_logged) {
                lg_begin(); lg_str("window "); lg_dec(s->id); lg_str(": "); lg_dec(n);
                lg_str(" damage rects merged into one"); lg_end();
                s->merge_logged = 1;
            }
            m.rects[0] = box;
            n = 1;
        }
        if (n == 0) {
            /* Nothing visible changed. One empty rectangle still asks for a
             * FRAME_DONE (the server clips it away), so the app's frame
             * loop keeps going; count 0 would mean "everything". */
            n = 1;
        }
        m.count = n;
    } else {
        m.count = 0;                    /* the whole buffer (always for a new one) */
    }
    int r = send_msg(&m, (uint16_t)sizeof(m), 0);
    if (r != 0) return fail(call, "COMMIT not sent", r);
    s->busy = 1;
    s->begun = 0;
    s->fresh = 0;
    s->commit_seq = seq;
#ifdef NXWIN_TRACE
    lg_begin(); lg_str("window "); lg_dec(s->id); lg_str(" commit "); lg_dec(seq); lg_str(", rects ");
    lg_dec(m.count); lg_end();
#endif
    return 0;
}

/* ============ Events ============ */

int nxw_poll_event(nxw_event_t *ev) {
    if (!ev) return fail("nxw_poll_event", "event is NULL", -NXW_EINVAL);
    if (!g.connected) return -NXW_ENOTCONN;
    if (g.q_count == 0 && !g.dead) pump();
    if (q_pop(ev)) {
        if (ev->type == NXW_EV_DISCONNECTED) g.dead_delivered = 1;
        return 1;
    }
    return g.dead ? -NXW_ENOTCONN : 0;
}

int nxw_wait_event(nxw_event_t *ev, uint64_t timeout_ms) {
    if (!ev) return fail("nxw_wait_event", "event is NULL", -NXW_EINVAL);
    if (!g.connected) return -NXW_ENOTCONN;
    uint64_t start = nx_gettime();
    for (;;) {
        int r = nxw_poll_event(ev);
        if (r != 0) return r;
        uint64_t left = NX_WAIT_FOREVER;
        if (timeout_ms != NX_WAIT_FOREVER) {
            uint64_t spent = nx_gettime() - start;
            if (spent >= timeout_ms) return 0;
            left = timeout_ms - spent;
        }
        int w = wait_readable(left);
        if (w == 0) return 0;
        /* readable, closed or failed: the next poll reports it */
    }
}
