/*
 * NeolyxOS ipc_test - channels, obj.wait and ports (ABI v1.1, step 2)
 *
 * Checks syscalls 144-147 and 150-152 against
 * docs/desktop/proposals/kernel-shm-ipc.md ("Kernel test (week 2 exit)"):
 *
 *   1. Two processes: the parent listens on "test.ipc" and spawns a child.
 *      The child connects, creates a 4 MiB shm object, fills it with a
 *      pattern and sends a READ|MAP|TRANSFER handle to it. The parent maps
 *      it read-only, checks the pattern and replies. The child then
 *      crashes on purpose (a NULL write) while still holding its channel,
 *      its shm handle and its mapping; the parent sees PEER_CLOSED, the
 *      buffer stays valid for the parent, and after the parent unmaps and
 *      closes, free memory is back to its value before the test.
 *      (There is no kill syscall for userspace; a crash goes through the
 *      same terminate() path as a kill and is the harder case.)
 *   2. Hostile cases give the documented error and leave the kernel
 *      running: stale handle, wrong handle type, oversized message, full
 *      queue, the 1 MiB unreceived-bytes budget, too-small receive buffer
 *      (then a retry succeeds), a full handle table on receive, sending a
 *      port handle, sending a handle without TRANSFER (nothing moves),
 *      sending a channel's own end or its peer, "nx." names from a
 *      non-system process, bad port names, a full backlog, the port table
 *      limit, obj.wait with 0 or 65 items, bad signal bits or a shm handle,
 *      and shm.create above the quota.
 *
 * Output: "[IPCTEST] PASS/FAIL ..." lines and one final
 * "[IPCTEST] RESULT: PASS" or "[IPCTEST] RESULT: FAIL". Exit code 0 or 1.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_ipc.h"

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
#define SYS_PORT_LISTEN   150
#define SYS_PORT_CONNECT  151
#define SYS_PORT_ACCEPT   152

#define SPAWN_WAITABLE    0x1

#define ENOENT            2
#define EBADF             9
#define EAGAIN            11
#define ENOMEM            12
#define EACCES            13
#define EFAULT            14
#define EEXIST            17
#define EINVAL            22
#define ENFILE            23
#define EMFILE            24
#define EPIPE             32
#define EPERM             1
#define EMSGSIZE          90
#define ETIMEDOUT         110

#define MIB               (1024ULL * 1024ULL)
#define PAGE              4096ULL
#define LEAK_TOLERANCE    (64ULL * 1024ULL)   /* kernel heap may grow a little */
#define PATTERN(i)        ((uint64_t)(i) * 0x9E3779B97F4A7C15ULL ^ 0xA5A5A5A5A5A5A5A5ULL)
#define FAULT_EXIT_PF     (128 + 14)          /* terminate() code for a page fault */

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
static int64_t chan_create(nx_handle_t *out, uint64_t flags) { return sc2(SYS_CHAN_CREATE, (int64_t)out, (int64_t)flags); }
static int64_t chan_send(int64_t h, nx_chan_msg_t *m) { return sc2(SYS_CHAN_SEND, h, (int64_t)m); }
static int64_t chan_recv(int64_t h, nx_chan_msg_t *m) { return sc2(SYS_CHAN_RECV, h, (int64_t)m); }
static int64_t obj_wait(nx_wait_item_t *it, uint64_t n, uint64_t ms) { return sc3(SYS_OBJ_WAIT, (int64_t)it, (int64_t)n, (int64_t)ms); }
static int64_t port_listen(const char *name) { return sc2(SYS_PORT_LISTEN, (int64_t)name, 0); }
static int64_t port_connect(const char *name) { return sc2(SYS_PORT_CONNECT, (int64_t)name, 0); }
static int64_t port_accept(int64_t h, uint32_t *pid) { return sc2(SYS_PORT_ACCEPT, h, (int64_t)pid); }

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
    out_str(ok ? "[IPCTEST] PASS " : "[IPCTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_sdec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

static void note(const char *what, int64_t v) {
    out_str("[IPCTEST] ");
    out_str(what);
    out_sdec(v);
    out_str("\n");
    flush();
}

static void fail_out(const char *why) {
    out_str("[IPCTEST] RESULT: FAIL (");
    out_str(why);
    out_str(")\n");
    flush();
    finish(1);
}

/* ============ Helpers ============ */

static uint64_t free_memory(void) {
    sys_info_t info;
    if (sc1(SYS_INFO, (int64_t)&info) != 0) return 0;
    return info.free_memory;
}

static uint64_t uptime_ms(void) {
    sys_info_t info;
    if (sc1(SYS_INFO, (int64_t)&info) != 0) return 0;
    return info.uptime_ms;
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int memeq(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static uint32_t slen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void check_memory_back(const char *what, uint64_t baseline) {
    uint64_t now = free_memory();
    int64_t diff = (int64_t)baseline - (int64_t)now;   /* > 0: memory missing */
    check(diff <= (int64_t)LEAK_TOLERANCE, what, diff);
}

static void msg_init(nx_chan_msg_t *m, void *data, uint32_t len, nx_handle_t *hs, uint32_t n) {
    m->data = (uint64_t)data;
    m->handles = (uint64_t)hs;
    m->data_len = len;
    m->handle_count = n;
    m->sender_pid = 0;
    m->flags = 0;
}

static int64_t send_str(int64_t h, const char *s) {
    nx_chan_msg_t m;
    msg_init(&m, (void *)s, slen(s), 0, 0);
    return chan_send(h, &m);
}

/* Wait for one signal on one handle; returns obj.wait's result, *observed set */
static int64_t wait_one(int64_t h, uint32_t signals, uint64_t ms, uint32_t *observed) {
    nx_wait_item_t it;
    it.handle = (nx_handle_t)h;
    it.signals = signals;
    it.observed = 0;
    it.reserved = 0;
    int64_t r = obj_wait(&it, 1, ms);
    if (observed) *observed = it.observed;
    return r;
}

/* Receive into buf (cap bytes) and hs (hcap handles) */
static int64_t recv_into(int64_t h, char *buf, uint32_t cap, nx_handle_t *hs, uint32_t hcap,
                         nx_chan_msg_t *m) {
    msg_init(m, buf, cap, hs, hcap);
    return chan_recv(h, m);
}

/* ============ Child: connect, share 4 MiB, then crash ============ */

static void child_main(void) {
    int64_t c = port_connect("test.ipc");
    if (c <= 0) finish(10);

    int64_t shm = shm_create(4 * MIB);
    if (shm <= 0) finish(11);
    int64_t a = shm_map(shm, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE);
    if (a <= 0) finish(12);
    volatile uint64_t *p = (volatile uint64_t *)a;
    for (uint64_t i = 0; i < (4 * MIB) / 8; i++) p[i] = PATTERN(i);

    int64_t ro = h_dup(shm, NX_RIGHT_READ | NX_RIGHT_MAP | NX_RIGHT_TRANSFER);
    if (ro <= 0) finish(13);
    nx_handle_t hs[1] = { (nx_handle_t)ro };
    nx_chan_msg_t m;
    msg_init(&m, "buf4m", 5, hs, 1);
    if (chan_send(c, &m) != 0) finish(14);

    if (wait_one(c, NX_SIGNAL_READABLE, 10000, 0) != 1) finish(15);
    char reply[16];
    if (recv_into(c, reply, sizeof(reply), 0, 0, &m) != 0) finish(16);
    if (m.data_len != 2 || !memeq(reply, "ok", 2)) finish(17);

    /* Crash on purpose while holding the channel, the shm handle and its
     * mapping: the kernel must clean all of it up. The address comes
     * through asm so the compiler cannot drop the store. */
    volatile uint64_t *bad;
    __asm__ volatile("xor %0, %0" : "=r"(bad));
    *bad = 1;
    finish(18);   /* not reached */
}

/* ============ Parent: single-process channel checks ============ */

static void test_channels(void) {
    nx_handle_t ch[2];
    nx_chan_msg_t m;
    char buf[NX_CHAN_DATA_MAX + 1];
    int64_t r;
    uint32_t obs = 0;

    check(chan_create(0, 0) == -EFAULT, "chan.create(NULL) is -EFAULT", chan_create(0, 0));
    r = chan_create(ch, 1);
    check(r == -EINVAL, "chan.create with flags != 0 is -EINVAL", r);
    r = chan_create(ch, 0);
    check(r == 0 && ch[0] > 0 && ch[1] > 0 && ch[0] != ch[1], "chan.create gives two handles", r);
    if (r != 0) fail_out("no channel");

    /* Empty channel: writable, not readable */
    r = wait_one(ch[1], NX_SIGNAL_READABLE, 0, &obs);
    check(r == -ETIMEDOUT && obs == NX_SIGNAL_WRITABLE, "obj.wait poll on an empty channel is -ETIMEDOUT, WRITABLE only", r);
    msg_init(&m, buf, sizeof(buf), 0, 0);
    r = chan_recv(ch[1], &m);
    check(r == -EAGAIN, "chan.recv on an empty channel is -EAGAIN", r);

    /* A timed wait really waits */
    uint64_t t0 = uptime_ms();
    r = wait_one(ch[1], NX_SIGNAL_READABLE, 50, 0);
    uint64_t dt = uptime_ms() - t0;
    check(r == -ETIMEDOUT && dt >= 40, "obj.wait with a 50 ms timeout blocks, then -ETIMEDOUT (ms waited)", (int64_t)dt);

    /* One message, too-small buffer first, then a retry */
    check(send_str(ch[0], "hello") == 0, "chan.send 5 bytes", 0);
    r = wait_one(ch[1], NX_SIGNAL_READABLE, NX_WAIT_FOREVER, &obs);
    check(r == 1 && (obs & NX_SIGNAL_READABLE) && (obs & NX_SIGNAL_WRITABLE), "obj.wait sees READABLE|WRITABLE", (int64_t)obs);
    r = recv_into(ch[1], buf, 2, 0, 0, &m);
    check(r == -EMSGSIZE && m.data_len == 5, "chan.recv with a 2-byte buffer is -EMSGSIZE and reports 5", r);
    r = recv_into(ch[1], buf, sizeof(buf), 0, 0, &m);
    check(r == 0 && m.data_len == 5 && memeq(buf, "hello", 5), "retry with a large buffer receives the message", r);
    check(m.sender_pid == (uint32_t)sc1(SYS_GETPID, 0), "sender_pid is filled in by the kernel", m.sender_pid);

    /* Malformed sends */
    msg_init(&m, buf, NX_CHAN_DATA_MAX + 1, 0, 0);
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send of 1025 bytes is -EINVAL", chan_send(ch[0], &m));
    nx_handle_t five[5] = { 1, 2, 3, 4, 5 };
    msg_init(&m, buf, 1, five, 5);
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send with 5 handles is -EINVAL", chan_send(ch[0], &m));
    msg_init(&m, buf, 1, 0, 0);
    m.sender_pid = 1;
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send with a forged sender_pid is -EINVAL", chan_send(ch[0], &m));
    msg_init(&m, buf, 1, 0, 0);
    m.flags = 1;
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send with flags != 0 is -EINVAL", chan_send(ch[0], &m));
    msg_init(&m, (void *)0x10, 4, 0, 0);
    check(chan_send(ch[0], &m) == -EFAULT, "chan.send from a bad data pointer is -EFAULT", chan_send(ch[0], &m));
    check(chan_send(ch[0], (nx_chan_msg_t *)0) == -EFAULT, "chan.send(NULL msg) is -EFAULT", chan_send(ch[0], 0));
    nx_handle_t self[1] = { ch[0] };
    msg_init(&m, buf, 1, self, 1);
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send carrying its own handle is -EINVAL", chan_send(ch[0], &m));
    nx_handle_t peer[1] = { ch[1] };
    msg_init(&m, buf, 1, peer, 1);
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send carrying its own peer is -EINVAL", chan_send(ch[0], &m));
    check(wait_one(ch[1], NX_SIGNAL_READABLE, 0, 0) == -ETIMEDOUT, "nothing was queued by the failed sends", 0);

    /* Wrong handle types */
    int64_t shm = shm_create(PAGE);
    msg_init(&m, buf, 1, 0, 0);
    check(chan_send(shm, &m) == -EINVAL, "chan.send on a shm handle is -EINVAL", chan_send(shm, &m));
    check(chan_recv(shm, &m) == -EINVAL, "chan.recv on a shm handle is -EINVAL", chan_recv(shm, &m));
    check(shm_map(ch[0], NX_SHM_PROT_READ) == -EINVAL, "shm.map on a channel handle is -EINVAL", shm_map(ch[0], NX_SHM_PROT_READ));

    /* Moving a handle: it leaves the sender and arrives with its rights */
    int64_t moved = h_dup(shm, NX_RIGHT_READ | NX_RIGHT_MAP | NX_RIGHT_TRANSFER);
    nx_handle_t mv[1] = { (nx_handle_t)moved };
    msg_init(&m, "h", 1, mv, 1);
    check(chan_send(ch[0], &m) == 0, "chan.send moving a shm handle", 0);
    check(h_close(moved) == -EBADF, "the moved handle is gone from the sender (-EBADF)", h_close(moved));
    nx_handle_t got[NX_CHAN_HANDLES_MAX];
    r = recv_into(ch[1], buf, sizeof(buf), got, 0, &m);
    check(r == -EMSGSIZE && m.handle_count == 1, "chan.recv with no handle room is -EMSGSIZE and reports 1", r);
    r = recv_into(ch[1], buf, sizeof(buf), got, NX_CHAN_HANDLES_MAX, &m);
    check(r == 0 && m.handle_count == 1 && got[0] > 0, "chan.recv installs the moved handle", r);
    r = shm_map(got[0], NX_SHM_PROT_READ | NX_SHM_PROT_WRITE);
    check(r == -EACCES, "the moved handle kept its rights (no WRITE)", r);
    r = shm_map(got[0], NX_SHM_PROT_READ);
    check(r > 0, "the moved handle maps read-only", r);
    if (r > 0) shm_unmap(r);
    h_close(got[0]);

    /* Atomic send: a handle without TRANSFER moves nothing */
    int64_t keep = h_dup(shm, NX_RIGHT_READ | NX_RIGHT_MAP | NX_RIGHT_TRANSFER);
    int64_t notx = h_dup(shm, NX_RIGHT_READ | NX_RIGHT_MAP);
    nx_handle_t two[2] = { (nx_handle_t)keep, (nx_handle_t)notx };
    msg_init(&m, buf, 1, two, 2);
    check(chan_send(ch[0], &m) == -EACCES, "chan.send with a handle lacking TRANSFER is -EACCES", chan_send(ch[0], &m));
    check(h_dup(keep, NX_RIGHTS_SAME) == -EACCES, "the valid handle in the failed send did not move", 0);
    check(h_close(keep) == 0 && h_close(notx) == 0, "both handles still open after the failed send", 0);
    nx_handle_t dupl[2] = { (nx_handle_t)shm, (nx_handle_t)shm };
    msg_init(&m, buf, 1, dupl, 2);
    check(chan_send(ch[0], &m) == -EINVAL, "chan.send listing a handle twice is -EINVAL", chan_send(ch[0], &m));
    nx_handle_t stale[1] = { (nx_handle_t)moved };
    msg_init(&m, buf, 1, stale, 1);
    check(chan_send(ch[0], &m) == -EBADF, "chan.send carrying a stale handle is -EBADF", chan_send(ch[0], &m));

    /* Full queue: 64 messages, then -EAGAIN; WRITABLE clears and returns */
    int sent = 0;
    while (sent < NX_CHAN_QUEUE_MAX + 4) {
        if (send_str(ch[0], "q") != 0) break;
        sent++;
    }
    check(sent == NX_CHAN_QUEUE_MAX && send_str(ch[0], "q") == -EAGAIN, "64 queued messages, then -EAGAIN", sent);
    r = wait_one(ch[0], NX_SIGNAL_WRITABLE, 0, &obs);
    check(r == -ETIMEDOUT && !(obs & NX_SIGNAL_WRITABLE), "a full peer queue clears WRITABLE", (int64_t)obs);
    recv_into(ch[1], buf, sizeof(buf), 0, 0, &m);
    r = wait_one(ch[0], NX_SIGNAL_WRITABLE, 0, &obs);
    check(r == 1, "one receive makes the sender WRITABLE again", r);
    while (recv_into(ch[1], buf, sizeof(buf), 0, 0, &m) == 0) { }

    /* Receive with a full handle table: -EMFILE, the message stays */
    int64_t carry = h_dup(shm, NX_RIGHTS_SAME);
    nx_handle_t c1[1] = { (nx_handle_t)carry };
    msg_init(&m, "x", 1, c1, 1);
    chan_send(ch[0], &m);
    static int64_t fill[NX_HANDLE_MAX];
    int nfill = 0;
    while (nfill < NX_HANDLE_MAX) {
        int64_t d = h_dup(shm, NX_RIGHTS_SAME);
        if (d < 0) break;
        fill[nfill++] = d;
    }
    r = recv_into(ch[1], buf, sizeof(buf), got, NX_CHAN_HANDLES_MAX, &m);
    check(r == -EMFILE, "chan.recv with a full handle table is -EMFILE", r);
    if (nfill > 0) h_close(fill[--nfill]);
    r = recv_into(ch[1], buf, sizeof(buf), got, NX_CHAN_HANDLES_MAX, &m);
    check(r == 0 && m.handle_count == 1, "after closing one handle the same message is received", r);
    if (r == 0) h_close(got[0]);
    while (nfill > 0) h_close(fill[--nfill]);

    /* Peer closed: queued messages still arrive, then -EPIPE */
    send_str(ch[1], "last");
    check(h_close(ch[1]) == 0, "close one end", 0);
    r = wait_one(ch[0], NX_SIGNAL_PEER_CLOSED, 0, &obs);
    check(r == 1 && (obs & NX_SIGNAL_PEER_CLOSED) && !(obs & NX_SIGNAL_WRITABLE), "the other end sees PEER_CLOSED", (int64_t)obs);
    check(send_str(ch[0], "x") == -EPIPE, "chan.send to a closed peer is -EPIPE", send_str(ch[0], "x"));
    r = recv_into(ch[0], buf, sizeof(buf), 0, 0, &m);
    check(r == 0 && m.data_len == 4 && memeq(buf, "last", 4), "a message sent before the close is still delivered", r);
    r = recv_into(ch[0], buf, sizeof(buf), 0, 0, &m);
    check(r == -EPIPE, "then chan.recv is -EPIPE", r);
    h_close(ch[0]);
    r = recv_into(ch[0], buf, sizeof(buf), 0, 0, &m);
    check(r == -EBADF, "chan.recv through a closed handle is -EBADF", r);

    /* Messages never received: their handles are closed with the endpoint */
    chan_create(ch, 0);
    int64_t lost = h_dup(shm, NX_RIGHTS_SAME);
    nx_handle_t lh[1] = { (nx_handle_t)lost };
    msg_init(&m, "l", 1, lh, 1);
    check(chan_send(ch[0], &m) == 0, "queue a handle nobody will receive", 0);
    check(h_close(ch[1]) == 0 && h_close(ch[0]) == 0, "close both ends with it queued", 0);
    h_close(shm);
}

/* The per-process budget: 1 MiB of sent, unreceived bytes */
static void test_budget(int measure) {
    enum { PAIRS = 17 };
    static char big[NX_CHAN_DATA_MAX];
    nx_handle_t ch[PAIRS][2];
    nx_chan_msg_t m;
    int pairs = 0;
    uint64_t queued = 0;
    int64_t r = 0;

    for (int i = 0; i < PAIRS; i++) {
        if (chan_create(ch[i], 0) != 0) break;
        pairs++;
    }
    for (int i = 0; i < pairs; i++) {
        for (int k = 0; k < NX_CHAN_QUEUE_MAX; k++) {
            msg_init(&m, big, NX_CHAN_DATA_MAX, 0, 0);
            r = chan_send(ch[i][0], &m);
            if (r != 0) break;
            queued += NX_CHAN_DATA_MAX;
        }
        if (r != 0) break;
    }
    if (measure) {
        check(queued == NX_CHAN_PROC_QUEUED && r == -EAGAIN,
              "1 MiB of unreceived bytes, then -EAGAIN on an empty queue (KiB queued)",
              (int64_t)(queued / 1024));
    }
    /* Closing the receivers drops the messages and returns the budget */
    for (int i = 0; i < pairs; i++) h_close(ch[i][1]);
    for (int i = 0; i < pairs; i++) h_close(ch[i][0]);
    if (measure) {
        nx_handle_t c[2];
        chan_create(c, 0);
        msg_init(&m, big, NX_CHAN_DATA_MAX, 0, 0);
        r = chan_send(c[0], &m);
        check(r == 0, "the budget is returned when the queued messages are dropped", r);
        h_close(c[0]);
        h_close(c[1]);
    }
}

/* ============ Parent: obj.wait argument checks ============ */

static void test_wait_args(void) {
    static nx_wait_item_t items[NX_WAIT_ITEMS_MAX + 1];
    nx_handle_t ch[2];
    chan_create(ch, 0);
    for (int i = 0; i <= NX_WAIT_ITEMS_MAX; i++) {
        items[i].handle = ch[0];
        items[i].signals = NX_SIGNAL_READABLE;
        items[i].observed = 0;
        items[i].reserved = 0;
    }
    int64_t r = obj_wait(items, NX_WAIT_ITEMS_MAX + 1, 0);
    check(r == -EINVAL, "obj.wait with 65 items is -EINVAL", r);
    r = obj_wait(items, 0, 0);
    check(r == -EINVAL, "obj.wait with 0 items is -EINVAL", r);
    r = obj_wait(items, NX_WAIT_ITEMS_MAX, 0);
    check(r == -ETIMEDOUT, "obj.wait with 64 items polls (-ETIMEDOUT)", r);
    items[0].signals = 0x8;
    r = obj_wait(items, 1, 0);
    check(r == -EINVAL, "obj.wait with an unknown signal bit is -EINVAL", r);
    items[0].signals = NX_SIGNAL_READABLE;
    items[0].reserved = 1;
    r = obj_wait(items, 1, 0);
    check(r == -EINVAL, "obj.wait with reserved != 0 is -EINVAL", r);
    items[0].reserved = 0;
    r = obj_wait((nx_wait_item_t *)0, 1, 0);
    check(r == -EFAULT, "obj.wait(NULL) is -EFAULT", r);
    items[0].handle = 0x7777;
    r = obj_wait(items, 1, 0);
    check(r == -EBADF, "obj.wait on a bogus handle is -EBADF", r);
    int64_t shm = shm_create(PAGE);
    items[0].handle = (nx_handle_t)shm;
    r = obj_wait(items, 1, 0);
    check(r == -EINVAL, "obj.wait on a shm handle is -EINVAL", r);
    int64_t wonly = h_dup(ch[0], NX_RIGHT_WRITE);
    check(wonly == -EACCES, "channel handles cannot be duplicated (no DUPLICATE right)", wonly);
    h_close(shm);
    h_close(ch[0]);
    h_close(ch[1]);
}

/* ============ Parent: ports ============ */

static void test_ports(void) {
    int64_t r;
    char name32[33];
    for (int i = 0; i < 32; i++) name32[i] = 'a';
    name32[32] = '\0';

    r = port_listen("nx.display");
    check(r == -EPERM, "port.listen(\"nx.display\") from an app is -EPERM", r);
    check(port_listen("") == -EINVAL, "port.listen(\"\") is -EINVAL", port_listen(""));
    check(port_listen("Test") == -EINVAL, "port.listen with an upper-case name is -EINVAL", port_listen("Test"));
    check(port_listen("a/b") == -EINVAL, "port.listen with '/' is -EINVAL", port_listen("a/b"));
    check(port_listen(name32) == -EINVAL, "port.listen with a 32-byte name is -EINVAL", port_listen(name32));
    check(port_listen((const char *)0) == -EFAULT, "port.listen(NULL) is -EFAULT", port_listen(0));
    check(sc2(SYS_PORT_LISTEN, (int64_t)"test.f", 1) == -EINVAL, "port.listen with flags != 0 is -EINVAL", 0);

    int64_t p = port_listen("test.self");
    check(p > 0, "port.listen(\"test.self\")", p);
    check(port_listen("test.self") == -EEXIST, "a second listen on the same name is -EEXIST", port_listen("test.self"));
    check(port_connect("test.nobody") == -ENOENT, "port.connect to an unknown name is -ENOENT", port_connect("test.nobody"));
    check(port_accept(p, 0) == -EAGAIN, "port.accept with nothing pending is -EAGAIN", port_accept(p, 0));
    check(wait_one(p, NX_SIGNAL_READABLE, 0, 0) == -ETIMEDOUT, "an idle port is not READABLE", 0);

    /* Sending a port handle: ports have no TRANSFER right */
    nx_handle_t ch[2];
    chan_create(ch, 0);
    nx_handle_t ph[1] = { (nx_handle_t)p };
    nx_chan_msg_t m;
    msg_init(&m, "p", 1, ph, 1);
    check(chan_send(ch[0], &m) == -EACCES, "chan.send carrying a port handle is -EACCES", chan_send(ch[0], &m));
    check(port_accept(ch[0], 0) == -EINVAL, "port.accept on a channel handle is -EINVAL", port_accept(ch[0], 0));
    h_close(ch[0]);
    h_close(ch[1]);

    /* Connect to ourselves: the client may send before the accept */
    int64_t cl = port_connect("test.self");
    check(cl > 0, "port.connect(\"test.self\")", cl);
    check(send_str(cl, "early") == 0, "the client sends before the server accepts", 0);
    check(wait_one(p, NX_SIGNAL_READABLE, 0, 0) == 1, "the port is READABLE with a connection pending", 0);
    uint32_t pid = 0;
    check(port_accept(p, (uint32_t *)0x10) == -EFAULT, "port.accept with a bad out_pid is -EFAULT", 0);
    int64_t sv = port_accept(p, &pid);
    check(sv > 0 && pid == (uint32_t)sc1(SYS_GETPID, 0), "port.accept returns the server end and the client PID", (int64_t)pid);
    char buf[16];
    r = recv_into(sv, buf, sizeof(buf), 0, 0, &m);
    check(r == 0 && m.data_len == 5 && memeq(buf, "early", 5), "the server receives the early message", r);
    h_close(sv);
    h_close(cl);

    /* Backlog: 16 pending, then -EAGAIN; closing the port closes them */
    int64_t pend[NX_PORT_BACKLOG + 1];
    int np = 0;
    while (np <= NX_PORT_BACKLOG) {
        r = port_connect("test.self");
        if (r < 0) break;
        pend[np++] = r;
    }
    check(np == NX_PORT_BACKLOG && r == -EAGAIN, "16 pending connections, then -EAGAIN", np);
    check(h_close(p) == 0, "close the port with 16 pending", 0);
    uint32_t obs = 0;
    r = wait_one(pend[0], NX_SIGNAL_PEER_CLOSED, 0, &obs);
    check(r == 1, "a pending client sees PEER_CLOSED when the port closes", (int64_t)obs);
    for (int i = 0; i < np; i++) h_close(pend[i]);
    check(port_connect("test.self") == -ENOENT, "the name is released when the port closes", 0);
    p = port_listen("test.self");
    check(p > 0, "the name can be registered again", p);
    h_close(p);

    /* Port table: 32 system-wide, then -ENFILE */
    static int64_t ports[NX_PORT_MAX + 1];
    char pname[] = "test.pNN";
    int n = 0;
    r = 0;
    while (n <= NX_PORT_MAX) {
        pname[6] = (char)('0' + n / 10);
        pname[7] = (char)('0' + n % 10);
        r = port_listen(pname);
        if (r < 0) break;
        ports[n++] = r;
    }
    check(n <= NX_PORT_MAX && n > 0 && r == -ENFILE, "the port table fills, then -ENFILE (ports this test got)", n);
    for (int i = 0; i < n; i++) h_close(ports[i]);
}

/* ============ Parent: two processes ============ */

static void test_two_processes(void) {
    /* Spawning costs memory the kernel keeps (file and heap caches), so an
     * idle child is spawned first and the baseline is taken after it. */
    const char *iargv[3] = { "ipc_test", "idle", 0 };
    int status = -1;
    int64_t pid = sc3(SYS_SPAWN, (int64_t)"/apps/ipc_test", (int64_t)iargv, SPAWN_WAITABLE);
    if (pid > 0) sc2(SYS_WAIT, pid, (int64_t)&status);
    check(pid > 0 && status == 0, "idle child (spawn warm-up) exited 0", status);
    uint64_t baseline = free_memory();
    note("free memory baseline (bytes): ", (int64_t)baseline);

    int64_t port = port_listen("test.ipc");
    check(port > 0, "port.listen(\"test.ipc\")", port);
    if (port <= 0) return;

    const char *cargv[3] = { "ipc_test", "child", 0 };
    pid = sc3(SYS_SPAWN, (int64_t)"/apps/ipc_test", (int64_t)cargv, SPAWN_WAITABLE);
    check(pid > 0, "spawn the child", pid);
    if (pid <= 0) { h_close(port); return; }

    int64_t r = wait_one(port, NX_SIGNAL_READABLE, 10000, 0);
    check(r == 1, "obj.wait wakes when the child connects", r);
    uint32_t cpid = 0;
    int64_t c = port_accept(port, &cpid);
    check(c > 0 && cpid == (uint32_t)pid, "port.accept gives the child's PID", (int64_t)cpid);
    h_close(port);

    r = wait_one(c, NX_SIGNAL_READABLE, 10000, 0);
    check(r == 1, "obj.wait wakes when the child's message arrives", r);
    char buf[16];
    nx_handle_t hs[NX_CHAN_HANDLES_MAX];
    nx_chan_msg_t m;
    r = recv_into(c, buf, sizeof(buf), hs, NX_CHAN_HANDLES_MAX, &m);
    check(r == 0 && m.handle_count == 1 && m.data_len == 5 && memeq(buf, "buf4m", 5) &&
          m.sender_pid == (uint32_t)pid, "receive the child's message with one shm handle", r);
    if (r != 0 || m.handle_count != 1) return;

    int64_t shm = hs[0];
    r = shm_map(shm, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE);
    check(r == -EACCES, "the child's read-only handle cannot be mapped writable", r);
    int64_t a = shm_map(shm, NX_SHM_PROT_READ);
    check(a > 0, "map the child's 4 MiB buffer read-only", a);
    if (a <= 0) return;
    volatile uint64_t *p = (volatile uint64_t *)a;
    int ok = 1;
    for (uint64_t i = 0; i < (4 * MIB) / 8; i++) if (p[i] != PATTERN(i)) { ok = 0; break; }
    check(ok, "the parent sees the child's 4 MiB pattern", ok);

    check(send_str(c, "ok") == 0, "reply to the child", 0);

    uint32_t obs = 0;
    r = wait_one(c, NX_SIGNAL_PEER_CLOSED, 10000, &obs);
    check(r == 1 && (obs & NX_SIGNAL_PEER_CLOSED), "the child crashed: its end is PEER_CLOSED", (int64_t)obs);
    status = -1;
    int64_t waited = sc2(SYS_WAIT, pid, (int64_t)&status);
    check(waited == pid && status == FAULT_EXIT_PF, "the child was terminated by its page fault (status)", status);
    r = recv_into(c, buf, sizeof(buf), 0, 0, &m);
    check(r == -EPIPE, "chan.recv after the crash is -EPIPE", r);

    ok = 1;
    for (uint64_t i = 0; i < (4 * MIB) / 8; i++) if (p[i] != PATTERN(i)) { ok = 0; break; }
    check(ok, "the buffer stays valid for the parent after the child is gone", ok);

    check(shm_unmap(a) == 0 && h_close(shm) == 0 && h_close(c) == 0, "unmap and close everything", 0);
    check_memory_back("all memory back after the exchange and the crash (bytes missing)", baseline);
}

/* ============ Main ============ */

void _start(long argc, char **argv) {
    if (argc >= 2 && argv && argv[1] && streq(argv[1], "child")) child_main();
    if (argc >= 2 && argv && argv[1] && streq(argv[1], "idle")) finish(0);

    out_str("[IPCTEST] PID ");
    out_sdec(sc1(SYS_GETPID, 0));
    out_str(" started\n");
    flush();

    abi_info_t abi;
    int64_t r = sc1(SYS_ABI_QUERY, (int64_t)&abi);
    uint32_t want = NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC;
    check(r == 0 && abi.minor >= 1, "abi.query reports ABI 1.1 or later (minor)", r == 0 ? abi.minor : r);
    check(r == 0 && (abi.flags & want) == want, "abi.query reports NX_ABI_FLAG_SHM | NX_ABI_FLAG_IPC", r == 0 ? abi.flags : r);
    if (r != 0 || (abi.flags & want) != want) fail_out("no IPC");

    /* Warm the kernel heap once so the memory checks measure leaks only */
    test_budget(0);

    test_channels();
    test_budget(1);
    test_wait_args();
    test_ports();

    /* Quota still holds with channels in use (5 x 64 MiB, the 5th fails) */
    int64_t q[5];
    int nq = 0;
    for (int i = 0; i < 5; i++) {
        q[i] = shm_create(NX_SHM_SIZE_MAX);
        if (q[i] > 0) nq++;
    }
    check(nq == 4 && q[4] == -ENOMEM, "shm.create above the 256 MiB quota is -ENOMEM (objects created)", nq);
    for (int i = 0; i < 5; i++) if (q[i] > 0) h_close(q[i]);

    test_two_processes();

    out_str(g_failed ? "[IPCTEST] RESULT: FAIL\n" : "[IPCTEST] RESULT: PASS\n");
    flush();
    finish(g_failed ? 1 : 0);
}
