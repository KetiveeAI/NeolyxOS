/*
 * nxserver - the window server inside the desktop process (see nxserver.h)
 *
 * Life of a client window:
 *   WIN_CREATE   a slot here, no shell window yet (not shown)
 *   BUF_ATTACH   the client's shm is mapped read-only as the pending buffer
 *   COMMIT       the pending buffer becomes current; the first COMMIT creates
 *                the shell window (decorations, focus, tiling: all the shell's)
 *   frame        the shell draws the window and calls the render callback,
 *                which draws straight from the mapping; FRAME_DONE follows
 *   WIN_DESTROY  the shell window closes with its usual animation; the
 *                buffers stay mapped until the shell destroys the window
 *   disconnect   (exit, crash, protocol violation) every window of the
 *                client is destroyed and unmapped in the same frame
 *
 * The server never blocks on a client: replies and events go out with
 * chan.send, and a full client queue moves them to a 64-message queue per
 * client with coalescing (pointer motion, FRAME_DONE and CONFIGURE keep only
 * the newest per window); on overflow the oldest motion goes first.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nxsyscall.h"
#include "nxcanvas.h"
#include "nxui.h"
#include "desktop_shell.h"
#include "window_manager.h"
#include "nxserver.h"
#include "nxw_check.h"

/* ============ Limits ============ */

#define NXS_CLIENTS        NXW_CLIENTS_MAX
#define NXS_WINDOWS        NXW_WINDOWS_TOTAL
#define NXS_QUEUE          64          /* outbound messages queued per client */
#define NXS_MSGS_PER_POLL  64          /* requests handled per client per frame */
#define NXS_QMSG_MAX       40          /* largest event struct (36) rounded up */
#define NXS_DROP_LOG_MS    1000

/* Kernel errno values (kernel/include/core/syscall.h); own names so a host
 * build (desktop/tools/preview) never picks up the host's errno.h values. */
#define NXS_EAGAIN         11
#define NXS_ENOMEM         12
#define NXS_EPIPE          32
#define NXS_ETIMEDOUT      110

_Static_assert(sizeof(nxw_pointer_t) <= NXS_QMSG_MAX, "queue slot too small for POINTER");
_Static_assert(sizeof(nxw_welcome_t) <= NXS_QMSG_MAX, "queue slot too small for WELCOME");

/* ============ State ============ */

typedef struct {
    void    *map;           /* shm.map address, NULL when empty */
    uint64_t obj_size;
    uint32_t width, height, stride;
    uint64_t offset;
} nxs_buf_t;

typedef struct {
    uint32_t  id;           /* protocol window id, 0 = free slot */
    int       client;       /* index into g_clients */
    uint32_t  shell_id;     /* shell window, 0 until the first COMMIT */
    int       closing;      /* destroyed by the client or the server; waiting
                               for the shell to drop it (buffers still mapped) */
    uint32_t  width, height;    /* size granted at WIN_CREATE */
    int32_t   x, y;
    uint32_t  flags;
    char      title[NXW_TITLE_MAX];
    nxs_buf_t cur;          /* drawn by the shell */
    nxs_buf_t pending;      /* attached, becomes cur at the next COMMIT */
    int       frame_owed;   /* a COMMIT waits for FRAME_DONE */
    uint32_t  commit_seq;
    uint32_t  sent_w, sent_h, sent_state;   /* last CONFIGURE */
    uint8_t   keys_down[32];    /* scancodes whose press this window got */
} nxs_win_t;

typedef struct {
    uint16_t op;
    uint16_t len;
    uint32_t win;
    uint8_t  data[NXS_QMSG_MAX];
} nxs_qmsg_t;

typedef struct {
    int         used;
    nx_handle_t chan;
    uint32_t    pid;
    int         hello;
    int         dead;           /* chan.send saw the peer gone */
    nxs_qmsg_t  q[NXS_QUEUE];
    int         qhead, qcount;
    uint32_t    dropped;        /* events dropped since the last log line */
    uint64_t    drop_log_ms;
} nxs_client_t;

static int          g_ready;
static nx_handle_t  g_port;
static uint32_t     g_screen_w, g_screen_h;
static nxs_client_t g_clients[NXS_CLIENTS];
static nxs_win_t    g_wins[NXS_WINDOWS];
static uint32_t     g_next_win_id = 1;

/* Pointer routing */
static uint32_t g_hover_win;        /* protocol id under the pointer (ENTER sent) */
static uint32_t g_capture_win;      /* protocol id that got a button press */
static int32_t  g_last_mx = -1, g_last_my = -1;

/* Receive buffers (one request at a time) */
static uint8_t     g_rx[NX_CHAN_DATA_MAX];
static nx_handle_t g_rx_handles[NX_CHAN_HANDLES_MAX];

/* ============ Small helpers ============ */

static void s_copy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void s_zero(void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static uint32_t now_ms32(void) { return (uint32_t)nx_gettime(); }

/* ---- Log lines: "[NXSERVER] ..." built piece by piece ---- */

static char g_logbuf[192];
static int  g_loglen;

static void lg_str(const char *s) {
    while (s && *s && g_loglen < (int)sizeof(g_logbuf) - 2) g_logbuf[g_loglen++] = *s++;
}

static void lg_dec(int64_t v) {
    char b[24];
    int i = 23;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    b[i] = '\0';
    if (u == 0) b[--i] = '0';
    while (u > 0 && i > 1) { b[--i] = (char)('0' + (int)(u % 10u)); u /= 10u; }
    if (neg) b[--i] = '-';
    lg_str(&b[i]);
}

static void lg_hex16(uint16_t v) {
    char b[7];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 4; i++) {
        uint32_t d = ((uint32_t)v >> (12 - 4 * i)) & 0xFu;
        b[2 + i] = (char)(d < 10 ? '0' + (int)d : 'a' + (int)d - 10);
    }
    b[6] = '\0';
    lg_str(b);
}

static void lg_begin(void) { g_loglen = 0; lg_str("[NXSERVER] "); }

static void lg_end(void) {
    g_logbuf[g_loglen++] = '\n';
    g_logbuf[g_loglen] = '\0';
    nx_debug_print(g_logbuf);
    g_loglen = 0;
}

static void lg_client(const nxs_client_t *c) {
    lg_str("PID ");
    lg_dec(c ? (int64_t)c->pid : 0);
}

/* ============ Lookups ============ */

static nxs_win_t *win_by_id(uint32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NXS_WINDOWS; i++) if (g_wins[i].id == id) return &g_wins[i];
    return NULL;
}

static nxs_win_t *win_by_shell(uint32_t shell_id) {
    if (shell_id == 0) return NULL;
    for (int i = 0; i < NXS_WINDOWS; i++) {
        if (g_wins[i].id && g_wins[i].shell_id == shell_id) return &g_wins[i];
    }
    return NULL;
}

static int client_index(const nxs_client_t *c) { return (int)(c - g_clients); }

/* Live (not closing) windows of a client */
static int client_window_count(int ci) {
    int n = 0;
    for (int i = 0; i < NXS_WINDOWS; i++) {
        if (g_wins[i].id && g_wins[i].client == ci && !g_wins[i].closing) n++;
    }
    return n;
}

/* Windows using one of the shell's 16 slots, plus client windows that will
 * need one at their first COMMIT. */
static int total_window_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) if (desktop_window_slot(i)) n++;
    for (int i = 0; i < NXS_WINDOWS; i++) if (g_wins[i].id && g_wins[i].shell_id == 0) n++;
    return n;
}

/* ============ Outbound messages ============ */

static int is_state_event(uint16_t op) {
    return op == NXW_OP_FRAME_DONE || op == NXW_OP_CONFIGURE || op == NXW_OP_CLOSE_REQUEST ||
           op == NXW_OP_WIN_CREATED || op == NXW_OP_WELCOME;
}

static int is_motion(const nxs_qmsg_t *q) {
    if (q->op != NXW_OP_POINTER) return 0;
    const nxw_pointer_t *p = (const nxw_pointer_t *)(const void *)q->data;
    return p->kind == NXW_PTR_MOTION;
}

static nxs_qmsg_t *q_at(nxs_client_t *c, int k) { return &c->q[(c->qhead + k) % NXS_QUEUE]; }

static void q_remove(nxs_client_t *c, int k) {
    for (int i = k; i < c->qcount - 1; i++) *q_at(c, i) = *q_at(c, i + 1);
    c->qcount--;
}

static void note_drop(nxs_client_t *c) {
    c->dropped++;
    uint64_t now = nx_gettime();
    if (now - c->drop_log_ms >= NXS_DROP_LOG_MS) {
        lg_begin(); lg_client(c);
        lg_str(" is not reading its events: dropped "); lg_dec(c->dropped);
        lg_end();
        c->dropped = 0;
        c->drop_log_ms = now;
    }
}

/* Queue a message that chan.send could not deliver now. */
static void q_push(nxs_client_t *c, const void *msg, uint16_t len, uint16_t op, uint32_t win,
                   int coalesce) {
    if (coalesce) {
        for (int k = 0; k < c->qcount; k++) {
            nxs_qmsg_t *q = q_at(c, k);
            if (q->op == op && q->win == win && (op != NXW_OP_POINTER || is_motion(q))) {
                s_copy(q->data, msg, len);      /* keep the newest, same position */
                q->len = len;
                return;
            }
        }
    }
    if (c->qcount >= NXS_QUEUE) {
        int victim = -1;
        for (int k = 0; k < c->qcount && victim < 0; k++) if (is_motion(q_at(c, k))) victim = k;
        for (int k = 0; k < c->qcount && victim < 0; k++) if (!is_state_event(q_at(c, k)->op)) victim = k;
        if (victim < 0) { note_drop(c); return; }      /* only state events queued: drop the new one */
        q_remove(c, victim);
        note_drop(c);
    }
    nxs_qmsg_t *q = q_at(c, c->qcount);
    q->op = op;
    q->len = len;
    q->win = win;
    s_copy(q->data, msg, len);
    c->qcount++;
}

static int raw_send(nxs_client_t *c, const void *msg, uint32_t len) {
    nx_chan_msg_t m;
    m.data = (uint64_t)(uintptr_t)msg;
    m.handles = 0;
    m.data_len = len;
    m.handle_count = 0;
    m.sender_pid = 0;
    m.flags = 0;
    int r = nx_chan_send(c->chan, &m);
    if (r == -NXS_EPIPE) c->dead = 1;      /* handled at the next poll (PEER_CLOSED) */
    return r;
}

/* Send now, or queue (in order) when the client's queue is full. */
static void send_msg(nxs_client_t *c, void *msg, uint16_t len, uint32_t win, int coalesce) {
    if (!c || !c->used || c->dead) return;
    nxw_hdr_t *h = (nxw_hdr_t *)msg;
    h->len = len;
    if (c->qcount == 0) {
        int r = raw_send(c, msg, len);
        if (r == 0 || r == -NXS_EPIPE) return;
        if (r != -NXS_EAGAIN) {
            lg_begin(); lg_str("chan.send to "); lg_client(c); lg_str(" failed: "); lg_dec(r); lg_end();
            return;
        }
    }
    q_push(c, msg, len, h->op, win, coalesce);
}

static void flush_queue(nxs_client_t *c) {
    while (c->used && !c->dead && c->qcount > 0) {
        nxs_qmsg_t *q = q_at(c, 0);
        int r = raw_send(c, q->data, q->len);
        if (r == -NXS_EAGAIN) return;
        c->qhead = (c->qhead + 1) % NXS_QUEUE;   /* sent, or undeliverable: drop it */
        c->qcount--;
    }
}

static void send_error(nxs_client_t *c, uint32_t seq, uint16_t op, int32_t code, uint32_t win) {
    nxw_error_t e;
    s_zero(&e, sizeof(e));
    e.hdr.op = NXW_OP_ERROR;
    e.hdr.seq = seq;
    e.op = op;
    e.code = code;
    e.win = win;
    send_msg(c, &e, (uint16_t)sizeof(e), win, 0);
}

/* ============ Buffers and windows ============ */

static void buf_release(nxs_buf_t *b) {
    if (b->map) {
        int r = nx_shm_unmap(b->map);
        if (r != 0) {
            lg_begin(); lg_str("shm.unmap failed: "); lg_dec(r); lg_end();
        }
    }
    s_zero(b, sizeof(*b));
}

/* Forget a window everywhere and unmap its buffers. The shell window must
 * already be gone (or never have existed). */
static void win_free(nxs_win_t *w) {
    if (!w || !w->id) return;
    if (g_hover_win == w->id) g_hover_win = 0;
    if (g_capture_win == w->id) g_capture_win = 0;
    buf_release(&w->cur);
    buf_release(&w->pending);
    s_zero(w, sizeof(*w));
}

/* Remove a client's window from the screen at once (disconnect). */
static void win_destroy_now(nxs_win_t *w) {
    uint32_t sid = w->shell_id;
    w->closing = 1;
    if (sid && desktop_destroy_window(sid) == 0) return;   /* the shell hook freed it */
    win_free(w);
}

static void log_window(const char *what, const nxs_client_t *c, const nxs_win_t *w) {
    lg_begin(); lg_client(c); lg_str(what); lg_dec(w->id);
    lg_str(" ("); lg_dec(w->width); lg_str("x"); lg_dec(w->height); lg_str(")");
    lg_end();
}

/* ============ Shell callbacks for client windows ============ */

static void cb_render(desktop_window_t *sw, void *ctx) {
    nxs_win_t *w = win_by_id((uint32_t)(uintptr_t)ctx);
    if (!sw || !w || !w->cur.map) return;    /* no buffer yet: the window background shows */
    uint32_t dw = w->cur.width < sw->width ? w->cur.width : sw->width;
    uint32_t dh = w->cur.height < sw->height ? w->cur.height : sw->height;
    if (dw == 0 || dh == 0) return;
    const uint32_t *px = (const uint32_t *)(const void *)((const uint8_t *)w->cur.map + w->cur.offset);
    nxc_canvas_t *c = nxui_canvas();
    if (!c) return;
    /* Anchored top-left; a smaller buffer leaves the window background around it */
    nxc_draw_xrgb32(c, px, (int32_t)dw, (int32_t)dh, (int32_t)w->cur.stride,
                    nxc_rect((float)sw->x, (float)(sw->y + WM_TITLEBAR_H), (float)dw, (float)dh),
                    255, NXC_FILTER_NEAREST);
}

static nxs_client_t *client_of(const nxs_win_t *w) {
    if (!w || w->client < 0 || w->client >= NXS_CLIENTS) return NULL;
    nxs_client_t *c = &g_clients[w->client];
    return c->used ? c : NULL;
}

static void cb_key(desktop_window_t *sw, uint8_t scancode, uint8_t pressed, void *ctx) {
    (void)sw;
    nxs_win_t *w = win_by_id((uint32_t)(uintptr_t)ctx);
    if (!w || w->closing) return;
    /* A release whose press went to the shell (an NX chord) or to another
     * window is not this app's key: drop it. */
    uint8_t bit = (uint8_t)(1u << (scancode & 7u));
    if (pressed) {
        w->keys_down[scancode >> 3] |= bit;
    } else {
        if (!(w->keys_down[scancode >> 3] & bit)) return;
        w->keys_down[scancode >> 3] &= (uint8_t)~bit;
    }
    nxw_key_t k;
    s_zero(&k, sizeof(k));
    k.hdr.op = NXW_OP_KEY;
    k.win = w->id;
    k.scancode = scancode;
    k.ascii = (uint8_t)desktop_key_ascii();
    k.pressed = pressed ? 1 : 0;
    k.modifiers = desktop_key_modifiers();
    k.time_ms = now_ms32();
    send_msg(client_of(w), &k, (uint16_t)sizeof(k), w->id, 0);
}

static void send_pointer(nxs_win_t *w, uint32_t kind, int32_t x, int32_t y, uint32_t buttons,
                         int32_t wheel) {
    if (!w || w->closing) return;
    nxw_pointer_t p;
    s_zero(&p, sizeof(p));
    p.hdr.op = NXW_OP_POINTER;
    p.win = w->id;
    p.kind = kind;
    p.x = x;
    p.y = y;
    p.buttons = buttons;
    p.wheel = wheel;
    p.time_ms = now_ms32();
    send_msg(client_of(w), &p, (uint16_t)sizeof(p), w->id, kind == NXW_PTR_MOTION);
}

/* Buttons only: motion, enter and leave come from pointer_track(). */
static void cb_mouse(desktop_window_t *sw, int32_t lx, int32_t ly, uint8_t buttons,
                     uint8_t prev_buttons, void *ctx) {
    (void)sw;
    nxs_win_t *w = win_by_id((uint32_t)(uintptr_t)ctx);
    if (!w || w->closing) return;
    uint32_t b = buttons & 0x07u, pb = prev_buttons & 0x07u;
    if (b == pb) return;
    send_pointer(w, NXW_PTR_BUTTON, lx, ly, b, 0);
    g_capture_win = b ? w->id : 0;
}

static void cb_scroll(desktop_window_t *sw, int32_t lx, int32_t ly, int32_t notches, void *ctx) {
    (void)sw;
    nxs_win_t *w = win_by_id((uint32_t)(uintptr_t)ctx);
    if (!w || w->closing) return;
    send_pointer(w, NXW_PTR_WHEEL, lx, ly, 0, notches);
}

/* First COMMIT: create the shell window. 0, or a negative errno. */
static int win_show(nxs_win_t *w) {
    int32_t x = w->x, y = w->y;
    if (x == NXW_POS_AUTO || y == NXW_POS_AUTO) {
        x = ((int32_t)g_screen_w - (int32_t)w->width) / 2;
        y = WM_MENUBAR_H + 48;
    }
    uint32_t sid = desktop_create_window(w->title, x, y, w->width, w->height);
    if (sid == 0) return -NXW_EMFILE;
    void *ctx = (void *)(uintptr_t)w->id;
    desktop_set_window_render(sid, cb_render, ctx);
    desktop_set_window_key_handler(sid, cb_key, ctx);
    desktop_set_window_mouse_handler(sid, cb_mouse, ctx);
    desktop_set_window_scroll_handler(sid, cb_scroll, ctx);
    desktop_set_window_resizable(sid, (w->flags & NXW_WIN_RESIZABLE) != 0);
    w->shell_id = sid;
    w->sent_w = w->width;
    w->sent_h = w->height;
    w->sent_state = 0;          /* the shell focuses new windows: first CONFIGURE says ACTIVE */
    return 0;
}

/* ============ Connections ============ */

static void disconnect(nxs_client_t *c, const char *why) {
    if (!c || !c->used) return;
    int ci = client_index(c);
    int n = 0;
    for (int i = 0; i < NXS_WINDOWS; i++) {
        if (g_wins[i].id && g_wins[i].client == ci) { win_destroy_now(&g_wins[i]); n++; }
    }
    lg_begin(); lg_client(c); lg_str(" disconnected ("); lg_str(why); lg_str("), ");
    lg_dec(n); lg_str(" window(s) removed");
    lg_end();
    int r = nx_handle_close(c->chan);
    if (r != 0) { lg_begin(); lg_str("handle.close failed: "); lg_dec(r); lg_end(); }
    s_zero(c, sizeof(*c));
}

static void violation(nxs_client_t *c, uint16_t op, int v) {
    lg_begin(); lg_str("protocol violation by "); lg_client(c); lg_str(", op "); lg_hex16(op);
    lg_str(": "); lg_str(nxw_violation_str(v));
    lg_end();
    disconnect(c, "protocol violation");
}

static void accept_clients(void) {
    for (int guard = 0; guard < NX_PORT_BACKLOG; guard++) {
        uint32_t pid = 0;
        nx_handle_t h = nx_port_accept(g_port, &pid);
        if (h == -NXS_EAGAIN) return;
        if (h < 0) {
            lg_begin(); lg_str("port.accept failed: "); lg_dec(h); lg_end();
            return;
        }
        nxs_client_t *c = NULL;
        for (int i = 0; i < NXS_CLIENTS; i++) if (!g_clients[i].used) { c = &g_clients[i]; break; }
        if (!c) {
            lg_begin(); lg_str("refused PID "); lg_dec(pid); lg_str(": 16 clients already"); lg_end();
            nx_handle_close(h);
            continue;
        }
        s_zero(c, sizeof(*c));
        c->used = 1;
        c->chan = h;
        c->pid = pid;
        lg_begin(); lg_client(c); lg_str(" connected"); lg_end();
    }
}

/* Find the client's window for a request. NULL after sending the ERROR. */
static nxs_win_t *own_window(nxs_client_t *c, uint32_t seq, uint16_t op, uint32_t id) {
    nxs_win_t *w = win_by_id(id);
    if (!w || w->closing) {
        send_error(c, seq, op, -NXW_EBADF, id);
        return NULL;
    }
    if (w->client != client_index(c)) {
        send_error(c, seq, op, -NXW_EPERM, id);
        return NULL;
    }
    return w;
}

/* ============ Requests ============ */

static void req_hello(nxs_client_t *c, const nxw_hello_t *m) {
    if (c->hello) {                     /* a second HELLO: harmless, refused */
        send_error(c, m->hdr.seq, NXW_OP_HELLO, -NXW_EINVAL, 0);
        return;
    }
    if (m->major != NXW_PROTO_MAJOR || m->flags != 0) {
        lg_begin(); lg_client(c); lg_str(" speaks protocol "); lg_dec(m->major);
        lg_str(", refused"); lg_end();
        nxw_error_t e;
        s_zero(&e, sizeof(e));
        e.hdr.op = NXW_OP_ERROR;
        e.hdr.len = (uint16_t)sizeof(e);
        e.hdr.seq = m->hdr.seq;
        e.op = NXW_OP_HELLO;
        e.code = -NXW_EINVAL;
        raw_send(c, &e, sizeof(e));     /* stays readable after the close */
        disconnect(c, "unsupported protocol version");
        return;
    }
    c->hello = 1;
    nxw_welcome_t r;
    s_zero(&r, sizeof(r));
    r.hdr.op = NXW_OP_WELCOME;
    r.hdr.seq = m->hdr.seq;
    r.major = NXW_PROTO_MAJOR;
    r.minor = NXW_PROTO_MINOR;
    r.screen_w = g_screen_w;
    r.screen_h = g_screen_h;
    r.max_windows = NXW_WINDOWS_PER_CLIENT;
    r.max_size = NXW_SIZE_MAX;
    send_msg(c, &r, (uint16_t)sizeof(r), 0, 0);
}

static void req_win_create(nxs_client_t *c, const nxw_win_create_t *m) {
    uint32_t seq = m->hdr.seq;
    char title[NXW_TITLE_MAX];
    if (nxw_check_size(m->width, m->height) != 0 || (m->flags & ~NXW_WIN_RESIZABLE) != 0 ||
        nxw_title_sanitize(m->title, title) != 0) {
        send_error(c, seq, NXW_OP_WIN_CREATE, -NXW_EINVAL, 0);
        return;
    }
    int ci = client_index(c);
    if (client_window_count(ci) >= NXW_WINDOWS_PER_CLIENT || total_window_count() >= NXW_WINDOWS_TOTAL) {
        send_error(c, seq, NXW_OP_WIN_CREATE, -NXW_EMFILE, 0);
        return;
    }
    nxs_win_t *w = NULL;
    for (int i = 0; i < NXS_WINDOWS; i++) if (!g_wins[i].id) { w = &g_wins[i]; break; }
    if (!w) {
        send_error(c, seq, NXW_OP_WIN_CREATE, -NXW_EMFILE, 0);
        return;
    }
    s_zero(w, sizeof(*w));
    w->id = g_next_win_id++;
    if (g_next_win_id == 0) g_next_win_id = 1;
    w->client = ci;
    w->width = m->width;
    w->height = m->height;
    desktop_fit_window_size(&w->width, &w->height);
    w->x = m->x;
    w->y = m->y;
    w->flags = m->flags;
    s_copy(w->title, title, NXW_TITLE_MAX);
    log_window(" created window ", c, w);

    nxw_win_created_t r;
    s_zero(&r, sizeof(r));
    r.hdr.op = NXW_OP_WIN_CREATED;
    r.hdr.seq = seq;
    r.win = w->id;
    r.width = w->width;
    r.height = w->height;
    send_msg(c, &r, (uint16_t)sizeof(r), w->id, 0);
}

static void req_win_destroy(nxs_client_t *c, const nxw_win_destroy_t *m) {
    nxs_win_t *w = own_window(c, m->hdr.seq, NXW_OP_WIN_DESTROY, m->win);
    if (!w) return;
    log_window(" destroyed window ", c, w);
    w->closing = 1;
    if (w->shell_id) {
        /* Animated close; the buffers go when the shell drops the window */
        if (desktop_close_window(w->shell_id) == 0) return;
    }
    win_free(w);
}

static void req_set_title(nxs_client_t *c, const nxw_win_set_title_t *m) {
    nxs_win_t *w = own_window(c, m->hdr.seq, NXW_OP_WIN_SET_TITLE, m->win);
    if (!w) return;
    char title[NXW_TITLE_MAX];
    if (nxw_title_sanitize(m->title, title) != 0) {
        send_error(c, m->hdr.seq, NXW_OP_WIN_SET_TITLE, -NXW_EINVAL, w->id);
        return;
    }
    s_copy(w->title, title, NXW_TITLE_MAX);
    if (w->shell_id) desktop_set_window_title(w->shell_id, w->title);
}

/* Returns 0, or a violation (the caller closes the connection). The handle
 * is always closed here. */
static int req_buf_attach(nxs_client_t *c, const nxw_buf_attach_t *m, nx_handle_t h) {
    uint32_t seq = m->hdr.seq;
    nxs_win_t *w = own_window(c, seq, NXW_OP_BUF_ATTACH, m->win);
    if (!w) {
        nx_handle_close(h);
        return NXW_FRAME_OK;
    }
    uint64_t size = 0;
    int64_t addr = nx_shm_map(h, NX_SHM_PROT_READ, &size);
    nx_handle_close(h);                 /* the mapping keeps the memory alive */
    if (addr == -NXS_ENOMEM) {
        lg_begin(); lg_str("cannot map a buffer for "); lg_client(c); lg_str(": out of mappings"); lg_end();
        send_error(c, seq, NXW_OP_BUF_ATTACH, -NXW_ENOMEM, w->id);
        return NXW_FRAME_OK;
    }
    if (addr <= 0) return NXW_V_NOT_SHM;
    if (nxw_check_buffer(m->width, m->height, m->stride, m->offset, size) != 0) {
        int r = nx_shm_unmap((void *)(uintptr_t)addr);
        if (r != 0) { lg_begin(); lg_str("shm.unmap failed: "); lg_dec(r); lg_end(); }
        send_error(c, seq, NXW_OP_BUF_ATTACH, -NXW_EINVAL, w->id);
        return NXW_FRAME_OK;
    }
    buf_release(&w->pending);           /* a newer attach replaces an uncommitted one */
    w->pending.map = (void *)(uintptr_t)addr;
    w->pending.obj_size = size;
    w->pending.width = m->width;
    w->pending.height = m->height;
    w->pending.stride = m->stride;
    w->pending.offset = m->offset;
    return NXW_FRAME_OK;
}

static void req_commit(nxs_client_t *c, const nxw_commit_t *m) {
    uint32_t seq = m->hdr.seq;
    nxs_win_t *w = own_window(c, seq, NXW_OP_COMMIT, m->win);
    if (!w) return;
    if (m->count > NXW_DAMAGE_MAX || (!w->pending.map && !w->cur.map)) {
        send_error(c, seq, NXW_OP_COMMIT, -NXW_EINVAL, w->id);
        return;
    }
    int new_buffer = 0;
    if (w->pending.map) {
        buf_release(&w->cur);
        w->cur = w->pending;
        s_zero(&w->pending, sizeof(w->pending));
        new_buffer = 1;
    }
    if (!w->shell_id) {
        int r = win_show(w);
        if (r != 0) {
            send_error(c, seq, NXW_OP_COMMIT, r, w->id);
            return;
        }
        /* A new window damages its whole frame through the shell */
    }
    /* Damage, clipped to the buffer, then mapped to the screen by the shell
     * (which clips it to the window's content). A new buffer may differ in
     * size from the old one, so the whole content area is redrawn; so is
     * count 0 ("the whole buffer changed"). */
    uint32_t damaged = 0;
    if (new_buffer || m->count == 0) {
        desktop_damage_window_content(w->shell_id, 0, 0, NXW_SIZE_MAX, NXW_SIZE_MAX);
        damaged = 1;
    } else {
        for (uint32_t i = 0; i < m->count; i++) {
            nxw_rect_t r;
            if (!nxw_clip_rect(&m->rects[i], w->cur.width, w->cur.height, &r)) continue;
            desktop_damage_window_content(w->shell_id, r.x, r.y, r.w, r.h);
            damaged++;
        }
    }
#ifdef NXSERVER_TRACE
    lg_begin(); lg_client(c); lg_str(" commit window "); lg_dec(w->id); lg_str(", rects ");
    lg_dec(m->count); lg_str(" ("); lg_dec(damaged); lg_str(" inside the buffer)"); lg_end();
#else
    (void)damaged;
#endif
    w->frame_owed = 1;
    w->commit_seq = seq;
}

/* Handle every queued request of one client (bounded per frame). */
static void serve_client(nxs_client_t *c) {
    for (int n = 0; n < NXS_MSGS_PER_POLL && c->used; n++) {
        nx_chan_msg_t m;
        m.data = (uint64_t)(uintptr_t)g_rx;
        m.handles = (uint64_t)(uintptr_t)g_rx_handles;
        m.data_len = sizeof(g_rx);
        m.handle_count = NX_CHAN_HANDLES_MAX;
        m.sender_pid = 0;
        m.flags = 0;
        int r = nx_chan_recv(c->chan, &m);
        if (r == -NXS_EAGAIN) return;
        if (r == -NXS_EPIPE) { disconnect(c, "client closed its connection"); return; }
        if (r != 0) {
            lg_begin(); lg_str("chan.recv from "); lg_client(c); lg_str(" failed: "); lg_dec(r); lg_end();
            disconnect(c, "receive failed");
            return;
        }
        uint16_t op = 0;
        int v = nxw_check_frame(g_rx, m.data_len, m.handle_count, c->hello, &op);
        if (v != NXW_FRAME_OK) {
            for (uint32_t i = 0; i < m.handle_count && i < NX_CHAN_HANDLES_MAX; i++) {
                nx_handle_close(g_rx_handles[i]);
            }
            violation(c, m.data_len >= sizeof(nxw_hdr_t) ? ((const nxw_hdr_t *)(const void *)g_rx)->op : 0, v);
            return;
        }
        switch (op) {
            case NXW_OP_HELLO:         req_hello(c, (const nxw_hello_t *)(const void *)g_rx); break;
            case NXW_OP_WIN_CREATE:    req_win_create(c, (const nxw_win_create_t *)(const void *)g_rx); break;
            case NXW_OP_WIN_DESTROY:   req_win_destroy(c, (const nxw_win_destroy_t *)(const void *)g_rx); break;
            case NXW_OP_WIN_SET_TITLE: req_set_title(c, (const nxw_win_set_title_t *)(const void *)g_rx); break;
            case NXW_OP_BUF_ATTACH:
                v = req_buf_attach(c, (const nxw_buf_attach_t *)(const void *)g_rx, g_rx_handles[0]);
                if (v != NXW_FRAME_OK) { violation(c, op, v); return; }
                break;
            case NXW_OP_COMMIT:        req_commit(c, (const nxw_commit_t *)(const void *)g_rx); break;
            default:                   break;   /* nxw_check_frame admits requests only */
        }
    }
}

/* ============ Per-frame work ============ */

static uint32_t state_of(const desktop_window_info_t *i) {
    uint32_t s = 0;
    if (i->maximized) s |= NXW_STATE_MAXIMIZED;
    if (i->tiled) s |= NXW_STATE_TILED;
    if (i->focused) s |= NXW_STATE_ACTIVE;
    return s;
}

/* CONFIGURE when a settled window's size, tiling or focus changed. */
static void send_configures(void) {
    for (int i = 0; i < NXS_WINDOWS; i++) {
        nxs_win_t *w = &g_wins[i];
        if (!w->id || !w->shell_id || w->closing) continue;
        desktop_window_info_t info;
        if (desktop_window_info(w->shell_id, &info) != 0 || !info.settled) continue;
        uint32_t st = state_of(&info);
        if (info.width == w->sent_w && info.height == w->sent_h && st == w->sent_state) continue;
        w->sent_w = info.width;
        w->sent_h = info.height;
        w->sent_state = st;
        nxw_configure_t m;
        s_zero(&m, sizeof(m));
        m.hdr.op = NXW_OP_CONFIGURE;
        m.win = w->id;
        m.width = info.width;
        m.height = info.height;
        m.state = st;
        send_msg(client_of(w), &m, (uint16_t)sizeof(m), w->id, 1);
    }
}

/* ENTER/LEAVE for the window under the pointer, MOTION to it (or to the
 * window holding a button press, even outside it). */
static void pointer_track(void) {
    int32_t mx = 0, my = 0, lx = 0, ly = 0;
    uint32_t sid = desktop_pointer_window(&mx, &my, &lx, &ly);
    nxs_win_t *hover = win_by_shell(sid);
    if (hover && hover->closing) hover = NULL;

    nxs_win_t *cap = win_by_id(g_capture_win);
    if (cap && (cap->closing || (desktop_mouse_buttons() & 0x07u) == 0)) {
        g_capture_win = 0;
        cap = NULL;
    }
    if (!cap) {
        uint32_t hid = hover ? hover->id : 0;
        if (hid != g_hover_win) {
            nxs_win_t *old = win_by_id(g_hover_win);
            if (old) send_pointer(old, NXW_PTR_LEAVE, 0, 0, 0, 0);
            if (hover) send_pointer(hover, NXW_PTR_ENTER, lx, ly, desktop_mouse_buttons() & 0x07u, 0);
            g_hover_win = hid;
        }
    }
    if (mx == g_last_mx && my == g_last_my) return;
    g_last_mx = mx;
    g_last_my = my;
    nxs_win_t *target = cap ? cap : hover;
    if (!target) return;
    desktop_window_info_t info;
    if (desktop_window_info(target->shell_id, &info) != 0) return;
    send_pointer(target, NXW_PTR_MOTION, mx - info.content_x, my - info.content_y,
                 desktop_mouse_buttons() & 0x07u, 0);
}

/* ============ Public entry points ============ */

int nxserver_init(uint32_t screen_w, uint32_t screen_h) {
    if (g_ready) return 0;
    s_zero(g_clients, sizeof(g_clients));
    s_zero(g_wins, sizeof(g_wins));
    g_screen_w = screen_w;
    g_screen_h = screen_h;
    nx_handle_t p = nx_port_listen(NXW_PORT_NAME);
    if (p < 0) {
        lg_begin(); lg_str("cannot listen on " NXW_PORT_NAME ": "); lg_dec(p);
        lg_str("; apps cannot open windows"); lg_end();
        return p;
    }
    g_port = p;
    g_ready = 1;
    lg_begin(); lg_str("listening on " NXW_PORT_NAME ", protocol ");
    lg_dec(NXW_PROTO_MAJOR); lg_str("."); lg_dec(NXW_PROTO_MINOR); lg_end();
    return 0;
}

/* The port and every client channel, for obj.wait. who[k] is the client
 * index of items[k], or -1 for the port. Returns the item count. */
static uint32_t wait_items(nx_wait_item_t *items, int *who) {
    uint32_t n = 0;
    items[n].handle = g_port;
    items[n].signals = NX_SIGNAL_READABLE;
    items[n].observed = 0;
    items[n].reserved = 0;
    who[n++] = -1;
    for (int i = 0; i < NXS_CLIENTS; i++) {
        if (!g_clients[i].used) continue;
        items[n].handle = g_clients[i].chan;
        items[n].signals = NX_SIGNAL_READABLE | NX_SIGNAL_PEER_CLOSED;
        items[n].observed = 0;
        items[n].reserved = 0;
        who[n++] = i;
    }
    return n;
}

int nxserver_wait(uint32_t timeout_ms) {
    if (!g_ready) return -1;
    nx_wait_item_t items[1 + NXS_CLIENTS];
    int who[1 + NXS_CLIENTS];
    uint32_t n = wait_items(items, who);
    int ready = nx_obj_wait(items, n, timeout_ms);
    if (ready == -NXS_ETIMEDOUT) return 0;
    if (ready < 0) {
        static int s_logged;
        if (!s_logged) { s_logged = 1; lg_begin(); lg_str("obj.wait (pacing) failed: "); lg_dec(ready); lg_end(); }
        return -1;
    }
    return ready;
}

int nxserver_owns_window(uint32_t shell_id) {
    return g_ready && shell_id != 0 && win_by_shell(shell_id) != NULL;
}

void nxserver_poll(void) {
    if (!g_ready) return;
    nx_wait_item_t items[1 + NXS_CLIENTS];
    int who[1 + NXS_CLIENTS];
    uint32_t n = wait_items(items, who);
    int ready = nx_obj_wait(items, n, 0);
    if (ready == -NXS_ETIMEDOUT) ready = 0;     /* nothing ready: the usual case */
    if (ready < 0) {
        static int s_logged;
        if (!s_logged) { s_logged = 1; lg_begin(); lg_str("obj.wait failed: "); lg_dec(ready); lg_end(); }
        return;
    }
    if (ready > 0) {
        for (uint32_t k = 0; k < n; k++) {
            if (!(items[k].observed & items[k].signals)) continue;
            if (who[k] < 0) { accept_clients(); continue; }
            nxs_client_t *c = &g_clients[who[k]];
            if (!c->used || c->chan != items[k].handle) continue;
            serve_client(c);    /* drains messages first; PEER_CLOSED then shows as -EPIPE */
        }
    }
    for (int i = 0; i < NXS_CLIENTS; i++) {
        if (g_clients[i].used && g_clients[i].dead) disconnect(&g_clients[i], "client closed its connection");
    }
    send_configures();
    pointer_track();
}

void nxserver_frame_presented(void) {
    if (!g_ready) return;
    uint64_t now = nx_gettime();
    for (int i = 0; i < NXS_WINDOWS; i++) {
        nxs_win_t *w = &g_wins[i];
        if (!w->id || !w->frame_owed || w->closing) continue;
        w->frame_owed = 0;
        nxw_frame_done_t f;
        s_zero(&f, sizeof(f));
        f.hdr.op = NXW_OP_FRAME_DONE;
        f.win = w->id;
        f.commit_seq = w->commit_seq;
        f.time_ms = now;
        send_msg(client_of(w), &f, (uint16_t)sizeof(f), w->id, 1);
    }
    for (int i = 0; i < NXS_CLIENTS; i++) if (g_clients[i].used) flush_queue(&g_clients[i]);
}

int nxserver_close_requested(uint32_t shell_id) {
    if (!g_ready) return 0;
    nxs_win_t *w = win_by_shell(shell_id);
    if (!w || w->closing || !client_of(w)) return 0;
    nxw_close_request_t m;
    s_zero(&m, sizeof(m));
    m.hdr.op = NXW_OP_CLOSE_REQUEST;
    m.win = w->id;
    send_msg(client_of(w), &m, (uint16_t)sizeof(m), w->id, 1);
    return 1;
}

void nxserver_window_destroyed(uint32_t shell_id) {
    if (!g_ready) return;
    nxs_win_t *w = win_by_shell(shell_id);
    if (!w) return;
    if (!w->closing) {
        lg_begin(); lg_str("shell removed window "); lg_dec(w->id); lg_str(" of ");
        lg_client(client_of(w)); lg_end();
    }
    w->shell_id = 0;
    win_free(w);
}
