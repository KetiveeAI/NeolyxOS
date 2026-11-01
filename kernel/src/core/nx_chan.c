/*
 * NeolyxOS Channels, Waiting and Ports
 *
 * Syscalls: chan.create (144), chan.send (145), chan.recv (146),
 *           obj.wait (147), port.listen (150), port.connect (151),
 *           port.accept (152).
 * Spec: docs/desktop/proposals/kernel-shm-ipc.md (approved 2026-11-01).
 *
 * A channel is two endpoint objects that point at each other (without
 * holding references, so closing one end never keeps the other alive).
 * Each endpoint owns the queue of messages sent TO it. A message is copied
 * into the kernel at send time and carries up to 4 handles, moved out of
 * the sender's table; the message holds their references until it is
 * received or dropped. Sends never block. Only obj.wait blocks.
 *
 * Waiting: a process blocked in obj.wait sleeps with its deadline; any
 * channel or port state change wakes every waiter, which re-checks its
 * handles (level-triggered). With one CPU and few processes this is cheap
 * and cannot miss a wake-up.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_handle.h"
#include "core/process.h"
#include "core/syscall.h"
#include "mm/kheap.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

#define CHAN_RIGHTS     (NX_RIGHT_READ | NX_RIGHT_WRITE | NX_RIGHT_TRANSFER)
#define PORT_RIGHTS     (NX_RIGHT_READ)
#define NO_DEADLINE     0xFFFFFFFFFFFFFFFFULL
#define SIGNALS_ALL     (NX_SIGNAL_READABLE | NX_SIGNAL_WRITABLE | NX_SIGNAL_PEER_CLOSED)

typedef struct chan_msg {
    struct chan_msg *next;
    struct process *charged;        /* sender, until it exits */
    uint32_t sender_pid;
    uint32_t data_len;
    uint32_t handle_count;
    uint32_t reserved;
    nx_object_t *objs[NX_CHAN_HANDLES_MAX];     /* one reference each */
    uint32_t rights[NX_CHAN_HANDLES_MAX];
    uint8_t data[];
} chan_msg_t;

typedef struct nx_chan {
    nx_object_t hdr;                /* must be first */
    struct nx_chan *peer;           /* NULL once the other end is destroyed */
    chan_msg_t *head;               /* messages sent to this endpoint */
    chan_msg_t *tail;
    uint32_t count;
    struct nx_chan *all_next;       /* every live endpoint (sender bookkeeping) */
} nx_chan_t;

typedef struct {
    nx_chan_t *endpoint;            /* server side, one reference */
    uint32_t client_pid;
} port_pending_t;

typedef struct nx_port {
    nx_object_t hdr;                /* must be first */
    char name[NX_PORT_NAME_MAX + 1];
    uint32_t owner_pid;
    uint32_t count;
    port_pending_t backlog[NX_PORT_BACKLOG];   /* FIFO, oldest first */
} nx_port_t;

static nx_chan_t *g_chan_all = NULL;
static nx_port_t *g_ports[NX_PORT_MAX];

/* ============ Logging ============ */

static void c_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    serial_puts(&buf[i]);
}

static void c_log(const char *tag, struct process *proc, const char *msg, const char *name) {
    serial_puts(tag);
    serial_puts(" PID ");
    c_dec(proc ? proc->pid : 0);
    serial_puts(": ");
    serial_puts(msg);
    if (name) {
        serial_puts(" '");
        serial_puts(name);
        serial_puts("'");
    }
    serial_puts("\n");
}

/* ============ Messages ============ */

static void msg_uncharge(chan_msg_t *m) {
    if (!m->charged) return;
    nx_proc_ipc_t *ipc = nx_ipc_state(m->charged, 0);
    if (ipc && ipc->queued_bytes >= m->data_len) ipc->queued_bytes -= m->data_len;
    m->charged = NULL;
}

/* Drop a message that will never be received: release what it carries */
static void msg_drop(chan_msg_t *m) {
    msg_uncharge(m);
    for (uint32_t i = 0; i < m->handle_count; i++) {
        nx_object_unref(m->objs[i]);
    }
    kfree(m);
}

void nx_chan_forget_sender(struct process *proc) {
    for (nx_chan_t *c = g_chan_all; c; c = c->all_next) {
        for (chan_msg_t *m = c->head; m; m = m->next) {
            if (m->charged == proc) m->charged = NULL;
        }
    }
}

/* ============ Endpoints ============ */

static void chan_destroy(nx_object_t *obj) {
    nx_chan_t *c = (nx_chan_t *)obj;

    nx_chan_t **link = &g_chan_all;
    while (*link && *link != c) link = &(*link)->all_next;
    if (*link) *link = c->all_next;

    if (c->peer) c->peer->peer = NULL;
    c->peer = NULL;

    /* Messages nobody will read: their handles are closed (queued for
     * destruction, never recursively) */
    chan_msg_t *m = c->head;
    while (m) {
        chan_msg_t *next = m->next;
        msg_drop(m);
        m = next;
    }
    kfree(c);

    nx_ipc_wake_waiters();   /* the peer is now PEER_CLOSED */
}

static nx_chan_t *chan_alloc(void) {
    nx_chan_t *c = (nx_chan_t *)kzalloc(sizeof(nx_chan_t));
    if (!c) return NULL;
    c->hdr.type = NX_OBJ_CHAN;
    c->hdr.destroy = chan_destroy;
    c->all_next = g_chan_all;
    g_chan_all = c;
    return c;
}

/*
 * Create a connected pair with one reference each, owned by the caller.
 * @return 0 or -ENOMEM
 */
static int chan_pair(nx_chan_t **a, nx_chan_t **b) {
    nx_chan_t *x = chan_alloc();
    nx_chan_t *y = x ? chan_alloc() : NULL;
    if (!x || !y) {
        if (x) { x->hdr.refs = 1; nx_object_unref(&x->hdr); }
        return -ENOMEM;
    }
    x->peer = y;
    y->peer = x;
    x->hdr.refs = 1;
    y->hdr.refs = 1;
    *a = x;
    *b = y;
    return 0;
}

static uint32_t chan_signals(const nx_chan_t *c) {
    uint32_t s = 0;
    if (c->count > 0) s |= NX_SIGNAL_READABLE;
    if (!c->peer) s |= NX_SIGNAL_PEER_CLOSED;
    else if (c->peer->count < NX_CHAN_QUEUE_MAX) s |= NX_SIGNAL_WRITABLE;
    return s;
}

/* ============ Ports ============ */

static void port_destroy(nx_object_t *obj) {
    nx_port_t *p = (nx_port_t *)obj;

    for (uint32_t i = 0; i < NX_PORT_MAX; i++) {
        if (g_ports[i] == p) g_ports[i] = NULL;
    }

    serial_puts("[PORT] Closed '");
    serial_puts(p->name);
    serial_puts("' (owner PID ");
    c_dec(p->owner_pid);
    serial_puts("), dropping ");
    c_dec(p->count);
    serial_puts(" pending connection(s)\n");

    /* Pending clients see PEER_CLOSED when their server end goes */
    for (uint32_t i = 0; i < p->count; i++) {
        nx_object_unref(&p->backlog[i].endpoint->hdr);
    }
    kfree(p);
    nx_ipc_wake_waiters();
}

static nx_port_t *port_find(const char *name) {
    for (uint32_t i = 0; i < NX_PORT_MAX; i++) {
        nx_port_t *p = g_ports[i];
        if (!p) continue;
        const char *a = p->name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return p;
    }
    return NULL;
}

/*
 * Copy and check a port name from userspace: 1-31 bytes of a-z 0-9 . _ -
 * @return 0, -EFAULT or -EINVAL
 */
static int port_name_from_user(uint64_t uptr, char out[NX_PORT_NAME_MAX + 1]) {
    int len = syscall_strncpy_from_user(out, (const char *)uptr, NX_PORT_NAME_MAX + 1);
    if (len == -EFAULT) return -EFAULT;
    if (len <= 0) return -EINVAL;      /* empty or too long */
    for (int i = 0; i < len; i++) {
        char ch = out[i];
        int ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                 ch == '.' || ch == '_' || ch == '-';
        if (!ok) return -EINVAL;
    }
    return 0;
}

static int name_is_system(const char *name) {
    return name[0] == 'n' && name[1] == 'x' && name[2] == '.';
}

/* ============ Syscalls: channels ============ */

/* int sys_chan_create(nx_handle_t out[2], uint32_t flags) */
int64_t sys_chan_create_impl(uint64_t out, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;
    if (flags != 0) return -EINVAL;
    if (syscall_validate_ptr((void *)out, 2 * sizeof(nx_handle_t), 1) != 0) return -EFAULT;
    if (nx_handle_free_slots(proc) < 2) return -EMFILE;

    nx_chan_t *a, *b;
    if (chan_pair(&a, &b) != 0) {
        c_log("[CHAN]", proc, "chan.create refused: out of kernel heap", 0);
        return -ENOMEM;
    }

    nx_handle_t h[2];
    h[0] = nx_handle_alloc(proc, &a->hdr, CHAN_RIGHTS);
    h[1] = (h[0] > 0) ? nx_handle_alloc(proc, &b->hdr, CHAN_RIGHTS) : -EMFILE;
    /* Drop the creation references: the handles now hold the objects */
    nx_object_unref(&a->hdr);
    nx_object_unref(&b->hdr);
    if (h[0] <= 0 || h[1] <= 0) {
        if (h[0] > 0) nx_handle_close(proc, h[0]);
        return (h[0] <= 0) ? h[0] : h[1];
    }

    if (syscall_copy_to_user((void *)out, h, sizeof(h)) != 0) {
        nx_handle_close(proc, h[0]);
        nx_handle_close(proc, h[1]);
        return -EFAULT;
    }
    return 0;
}

/* int sys_chan_send(nx_handle_t h, const nx_chan_msg_t *msg) */
int64_t sys_chan_send_impl(uint64_t h, uint64_t umsg, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;
    if (h > 0x7FFFFFFFULL) return -EBADF;

    nx_object_t *obj = NULL;
    int rc = nx_handle_get(proc, (int32_t)h, NX_OBJ_CHAN, NX_RIGHT_WRITE, &obj, NULL);
    if (rc != 0) return rc;
    nx_chan_t *c = (nx_chan_t *)obj;

    nx_chan_msg_t msg;
    if (syscall_copy_from_user(&msg, (const void *)umsg, sizeof(msg)) != 0) return -EFAULT;
    if (msg.flags != 0 || msg.sender_pid != 0 ||
        msg.data_len > NX_CHAN_DATA_MAX || msg.handle_count > NX_CHAN_HANDLES_MAX) {
        return -EINVAL;
    }

    nx_handle_t hs[NX_CHAN_HANDLES_MAX];
    if (msg.handle_count &&
        syscall_copy_from_user(hs, (const void *)msg.handles,
                               msg.handle_count * sizeof(nx_handle_t)) != 0) {
        return -EFAULT;
    }

    /* Check every carried handle before anything moves (atomic send) */
    for (uint32_t i = 0; i < msg.handle_count; i++) {
        if (hs[i] == (nx_handle_t)h) return -EINVAL;          /* the channel itself */
        for (uint32_t j = 0; j < i; j++) {
            if (hs[j] == hs[i]) return -EINVAL;               /* listed twice */
        }
        nx_object_t *carried = NULL;
        rc = nx_handle_get(proc, hs[i], 0, NX_RIGHT_TRANSFER, &carried, NULL);
        if (rc != 0) return rc;                               /* -EBADF or -EACCES */
        if (carried == &c->hdr) return -EINVAL;               /* the channel itself */
        if (c->peer && carried == &c->peer->hdr) return -EINVAL; /* its own peer: would never be freed */
    }

    if (!c->peer) return -EPIPE;
    if (c->peer->count >= NX_CHAN_QUEUE_MAX) return -EAGAIN;
    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 1);
    if (!ipc) return -ENOMEM;
    if (ipc->queued_bytes + msg.data_len > NX_CHAN_PROC_QUEUED) {
        c_log("[CHAN]", proc, "chan.send refused: 1 MiB of unreceived messages", 0);
        return -EAGAIN;
    }

    chan_msg_t *m = (chan_msg_t *)kmalloc(sizeof(chan_msg_t) + msg.data_len);
    if (!m) {
        c_log("[CHAN]", proc, "chan.send refused: out of kernel heap", 0);
        return -ENOMEM;
    }
    if (msg.data_len &&
        syscall_copy_from_user(m->data, (const void *)msg.data, msg.data_len) != 0) {
        kfree(m);
        return -EFAULT;
    }

    /* Commit: move the handles (their references go to the message) */
    m->next = NULL;
    m->charged = proc;
    m->sender_pid = proc->pid;
    m->data_len = msg.data_len;
    m->handle_count = msg.handle_count;
    m->reserved = 0;
    for (uint32_t i = 0; i < msg.handle_count; i++) {
        nx_handle_take(proc, hs[i], &m->objs[i], &m->rights[i]);
    }

    nx_chan_t *dst = c->peer;
    if (dst->tail) dst->tail->next = m;
    else dst->head = m;
    dst->tail = m;
    dst->count++;
    ipc->queued_bytes += m->data_len;

    nx_ipc_wake_waiters();
    return 0;
}

/* int sys_chan_recv(nx_handle_t h, nx_chan_msg_t *msg) */
int64_t sys_chan_recv_impl(uint64_t h, uint64_t umsg, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;
    if (h > 0x7FFFFFFFULL) return -EBADF;

    nx_object_t *obj = NULL;
    int rc = nx_handle_get(proc, (int32_t)h, NX_OBJ_CHAN, NX_RIGHT_READ, &obj, NULL);
    if (rc != 0) return rc;
    nx_chan_t *c = (nx_chan_t *)obj;

    nx_chan_msg_t msg;
    if (syscall_copy_from_user(&msg, (const void *)umsg, sizeof(msg)) != 0) return -EFAULT;

    chan_msg_t *m = c->head;
    if (!m) return c->peer ? -EAGAIN : -EPIPE;

    if (m->data_len > msg.data_len || m->handle_count > msg.handle_count) {
        msg.data_len = m->data_len;
        msg.handle_count = m->handle_count;
        if (syscall_copy_to_user((void *)umsg, &msg, sizeof(msg)) != 0) return -EFAULT;
        return -EMSGSIZE;
    }
    if (nx_handle_free_slots(proc) < m->handle_count) return -EMFILE;
    if (syscall_validate_ptr((void *)umsg, sizeof(msg), 1) != 0) return -EFAULT;
    if (m->data_len &&
        syscall_validate_ptr((void *)msg.data, m->data_len, 1) != 0) return -EFAULT;
    if (m->handle_count &&
        syscall_validate_ptr((void *)msg.handles, m->handle_count * sizeof(nx_handle_t), 1) != 0) {
        return -EFAULT;
    }

    /* Everything checked: deliver. The copies below cannot fault (ranges
     * validated above, one CPU, the caller is not running). */
    if (m->data_len && syscall_copy_to_user((void *)msg.data, m->data, m->data_len) != 0) {
        return -EFAULT;
    }
    nx_handle_t hs[NX_CHAN_HANDLES_MAX];
    for (uint32_t i = 0; i < m->handle_count; i++) {
        hs[i] = nx_handle_alloc(proc, m->objs[i], m->rights[i]);
        if (hs[i] < 0) {
            /* Not expected (free slots and state checked): undo, keep the message */
            for (uint32_t j = 0; j < i; j++) nx_handle_close(proc, hs[j]);
            c_log("[CHAN]", proc, "chan.recv could not install a handle", 0);
            return hs[i];
        }
    }
    if (m->handle_count &&
        syscall_copy_to_user((void *)msg.handles, hs, m->handle_count * sizeof(nx_handle_t)) != 0) {
        for (uint32_t j = 0; j < m->handle_count; j++) nx_handle_close(proc, hs[j]);
        return -EFAULT;
    }

    msg.data_len = m->data_len;
    msg.handle_count = m->handle_count;
    msg.sender_pid = m->sender_pid;
    if (syscall_copy_to_user((void *)umsg, &msg, sizeof(msg)) != 0) {
        for (uint32_t j = 0; j < m->handle_count; j++) nx_handle_close(proc, hs[j]);
        return -EFAULT;
    }

    c->head = m->next;
    if (!c->head) c->tail = NULL;
    c->count--;
    msg_uncharge(m);
    for (uint32_t i = 0; i < m->handle_count; i++) {
        nx_object_unref(m->objs[i]);    /* the new handles hold their own reference */
    }
    kfree(m);

    nx_ipc_wake_waiters();   /* the sender may be waiting for WRITABLE */
    return 0;
}

/* ============ Syscalls: waiting ============ */

/*
 * Fill observed for every item.
 * @return number of items with a requested signal asserted, or -errno
 */
static int wait_poll(struct process *proc, nx_wait_item_t *items, uint32_t count) {
    int ready = 0;
    for (uint32_t i = 0; i < count; i++) {
        nx_object_t *obj = NULL;
        int rc = nx_handle_get(proc, items[i].handle, 0, NX_RIGHT_READ, &obj, NULL);
        if (rc != 0) return rc;
        uint32_t s;
        if (obj->type == NX_OBJ_CHAN) {
            s = chan_signals((nx_chan_t *)obj);
        } else if (obj->type == NX_OBJ_PORT) {
            s = (((nx_port_t *)obj)->count > 0) ? NX_SIGNAL_READABLE : 0;
        } else {
            return -EINVAL;     /* shm has no signals */
        }
        items[i].observed = s;
        if (s & items[i].signals) ready++;
    }
    return ready;
}

/* int sys_obj_wait(nx_wait_item_t *items, uint32_t count, uint64_t timeout_ms) */
int64_t sys_obj_wait_impl(uint64_t uitems, uint64_t count, uint64_t timeout_ms,
                          uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;
    if (count == 0 || count > NX_WAIT_ITEMS_MAX) return -EINVAL;

    nx_wait_item_t items[NX_WAIT_ITEMS_MAX];
    uint64_t bytes = count * sizeof(nx_wait_item_t);
    if (syscall_copy_from_user(items, (const void *)uitems, bytes) != 0) return -EFAULT;
    if (syscall_validate_ptr((void *)uitems, bytes, 1) != 0) return -EFAULT;
    for (uint32_t i = 0; i < count; i++) {
        if (items[i].reserved != 0 || (items[i].signals & ~SIGNALS_ALL)) return -EINVAL;
    }

    uint64_t deadline = NO_DEADLINE;
    if (timeout_ms != NX_WAIT_FOREVER) {
        uint64_t now = pit_get_ticks();
        deadline = (timeout_ms > NO_DEADLINE - now) ? NO_DEADLINE : now + timeout_ms;
    }

    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 1);
    if (!ipc) return -ENOMEM;

    int ready;
    for (;;) {
        ready = wait_poll(proc, items, (uint32_t)count);
        if (ready != 0) break;                     /* ready > 0, or an error */
        if (timeout_ms == 0 || pit_get_ticks() >= deadline) {
            ready = -ETIMEDOUT;
            break;
        }
        ipc->waiting = 1;
        int slept = process_sleep_until(deadline);
        ipc->waiting = 0;
        if (slept != 0) {
            /* Cannot block here: report a timeout rather than spin */
            c_log("[CHAN]", proc, "obj.wait cannot block outside the scheduler", 0);
            ready = -ETIMEDOUT;
            break;
        }
    }

    if (ready > 0 || ready == -ETIMEDOUT) {
        if (syscall_copy_to_user((void *)uitems, items, bytes) != 0) return -EFAULT;
    }
    return ready;
}

/* ============ Syscalls: ports ============ */

/* nx_handle_t sys_port_listen(const char *name, uint32_t flags) */
int64_t sys_port_listen_impl(uint64_t uname, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;

    char name[NX_PORT_NAME_MAX + 1];
    int rc = port_name_from_user(uname, name);
    if (rc != 0) return rc;
    if (flags != 0) return -EINVAL;

    if (name_is_system(name) && !(proc->flags & PROC_FLAG_SYSTEM)) {
        c_log("[PORT] DENIED", proc, "nx.* names need a system process (-EPERM):", name);
        return -EPERM;
    }
    if (port_find(name)) {
        c_log("[PORT]", proc, "name already registered:", name);
        return -EEXIST;
    }

    int slot = -1;
    for (int i = 0; i < NX_PORT_MAX; i++) {
        if (!g_ports[i]) { slot = i; break; }
    }
    if (slot < 0) {
        c_log("[PORT]", proc, "port table full (32), cannot register", name);
        return -ENFILE;
    }
    if (nx_handle_free_slots(proc) < 1) return -EMFILE;

    nx_port_t *p = (nx_port_t *)kzalloc(sizeof(nx_port_t));
    if (!p) return -ENOMEM;
    p->hdr.type = NX_OBJ_PORT;
    p->hdr.destroy = port_destroy;
    p->owner_pid = proc->pid;
    for (int i = 0; i <= NX_PORT_NAME_MAX; i++) {
        p->name[i] = name[i];
        if (!name[i]) break;
    }
    g_ports[slot] = p;

    int32_t hnd = nx_handle_alloc(proc, &p->hdr, PORT_RIGHTS);
    if (hnd < 0) {
        p->hdr.refs = 1;
        nx_object_unref(&p->hdr);
        return hnd;
    }
    c_log("[PORT]", proc, "listening on", name);
    return hnd;
}

/* nx_handle_t sys_port_connect(const char *name, uint32_t flags) */
int64_t sys_port_connect_impl(uint64_t uname, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;

    char name[NX_PORT_NAME_MAX + 1];
    int rc = port_name_from_user(uname, name);
    if (rc != 0) return rc;
    if (flags != 0) return -EINVAL;

    nx_port_t *p = port_find(name);
    if (!p) return -ENOENT;
    if (p->count >= NX_PORT_BACKLOG) {
        c_log("[PORT]", proc, "backlog full (16), connect refused for", name);
        return -EAGAIN;
    }
    if (nx_handle_free_slots(proc) < 1) return -EMFILE;

    nx_chan_t *client, *server;
    if (chan_pair(&client, &server) != 0) return -ENOMEM;

    int32_t hnd = nx_handle_alloc(proc, &client->hdr, CHAN_RIGHTS);
    nx_object_unref(&client->hdr);               /* the handle holds it now */
    if (hnd < 0) {
        nx_object_unref(&server->hdr);
        return hnd;
    }

    /* The backlog keeps the server end's creation reference */
    p->backlog[p->count].endpoint = server;
    p->backlog[p->count].client_pid = proc->pid;
    p->count++;

    c_log("[PORT]", proc, "connected to", name);
    nx_ipc_wake_waiters();
    return hnd;
}

/* nx_handle_t sys_port_accept(nx_handle_t port, uint32_t *out_pid) */
int64_t sys_port_accept_impl(uint64_t h, uint64_t out_pid, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc) return -EPERM;
    if (h > 0x7FFFFFFFULL) return -EBADF;

    nx_object_t *obj = NULL;
    int rc = nx_handle_get(proc, (int32_t)h, NX_OBJ_PORT, NX_RIGHT_READ, &obj, NULL);
    if (rc != 0) return rc;
    nx_port_t *p = (nx_port_t *)obj;

    if (out_pid && syscall_validate_ptr((void *)out_pid, sizeof(uint32_t), 1) != 0) return -EFAULT;
    if (p->count == 0) return -EAGAIN;
    if (nx_handle_free_slots(proc) < 1) return -EMFILE;

    port_pending_t pending = p->backlog[0];
    for (uint32_t i = 1; i < p->count; i++) p->backlog[i - 1] = p->backlog[i];
    p->count--;

    int32_t hnd = nx_handle_alloc(proc, &pending.endpoint->hdr, CHAN_RIGHTS);
    nx_object_unref(&pending.endpoint->hdr);     /* the handle holds it now */
    if (hnd < 0) return hnd;

    if (out_pid) {
        uint32_t pid = pending.client_pid;
        syscall_copy_to_user((void *)out_pid, &pid, sizeof(pid));
    }

    serial_puts("[PORT] '");
    serial_puts(p->name);
    serial_puts("' accepted PID ");
    c_dec(pending.client_pid);
    serial_puts("\n");
    return hnd;
}
