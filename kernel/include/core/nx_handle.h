/*
 * NeolyxOS Kernel Objects and Handles (kernel-internal)
 *
 * Kernel objects (shared memory, channel endpoints, ports) are reached
 * from userspace only through handles: per-process integers that carry
 * rights. A handle value is (generation << 8) | slot, so a closed and reused
 * slot never matches an old value. Objects are reference-counted and are
 * destroyed when their last handle, mapping or queued reference is gone.
 *
 * Concurrency: one CPU, syscalls run with interrupts disabled and the
 * kernel is not preemptible, so no locks are needed today. Anything that
 * adds SMP or kernel preemption must add locking here first.
 *
 * Public ABI types: core/nx_ipc.h. Spec: docs/desktop/proposals/kernel-shm-ipc.md
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_HANDLE_H
#define NEOLYX_NX_HANDLE_H

#include <stdint.h>
#include "core/nx_ipc.h"

struct process;
struct nx_shm;

/* Object types */
#define NX_OBJ_SHM          1
#define NX_OBJ_CHAN         2   /* one channel endpoint */
#define NX_OBJ_PORT         3   /* named service port */
#define NX_OBJ_MEMPRESSURE  4   /* memory pressure notifier (core/nx_mem.c) */

#define NX_RIGHTS_ALL       (NX_RIGHT_READ | NX_RIGHT_WRITE | NX_RIGHT_MAP | \
                             NX_RIGHT_TRANSFER | NX_RIGHT_DUPLICATE)

typedef struct nx_object nx_object_t;

/* Called once, when the last reference is dropped */
typedef void (*nx_object_destroy_fn)(nx_object_t *obj);

struct nx_object {
    uint32_t type;                  /* NX_OBJ_* */
    uint32_t refs;                  /* handles + mappings + queued references */
    nx_object_destroy_fn destroy;
    nx_object_t *dnext;             /* pending-destroy list (see nx_object_unref) */
};

typedef struct {
    nx_object_t *obj;               /* NULL = free slot */
    uint32_t rights;
    uint16_t gen;                   /* bumped on every allocation, never 0 in use */
    uint16_t reserved;
} nx_handle_slot_t;

typedef struct {
    uint64_t va;                    /* base returned by shm.map */
    uint64_t span;                  /* reserved VA, 2 MiB multiple (includes the gap) */
    uint64_t pages;                 /* pages actually mapped */
    struct nx_shm *shm;             /* holds one reference */
} nx_shm_mapping_t;

/* Per-process IPC state, allocated on first use (process_t.ipc) */
typedef struct nx_proc_ipc {
    nx_handle_slot_t slots[NX_HANDLE_MAX];
    uint32_t handle_count;
    uint32_t map_count;
    nx_shm_mapping_t maps[NX_SHM_MAP_MAX];
    uint64_t shm_bytes;             /* bytes of live shm objects this process created */
    uint64_t queued_bytes;          /* message bytes it sent that are not yet received */
    uint32_t waiting;               /* non-zero while blocked in obj.wait */
} nx_proc_ipc_t;

/* ============ Objects ============ */

void nx_object_ref(nx_object_t *obj);

/**
 * Drop one reference; destroys the object when it was the last one.
 * Destruction is iterative: an object whose destroy drops further last
 * references (an endpoint queue holding endpoints holding endpoints...)
 * never recurses on the kernel stack.
 */
void nx_object_unref(nx_object_t *obj);

/* ============ Handles ============ */

/**
 * Per-process IPC state. With create != 0 it is allocated on first use.
 * @return state, or NULL (no process, or out of memory when creating)
 */
nx_proc_ipc_t *nx_ipc_state(struct process *proc, int create);

/**
 * Install obj in proc's handle table with the given rights. Takes a new
 * reference on obj (the caller keeps its own).
 * @return handle (> 0), -EMFILE (table full) or -ENOMEM
 */
int32_t nx_handle_alloc(struct process *proc, nx_object_t *obj, uint32_t rights);

/**
 * Look up a handle of the calling process.
 * @param type      required NX_OBJ_* type, or 0 for any
 * @param required  rights that must all be present
 * @return 0 and *obj / *rights set; -EBADF (not a live handle), -EINVAL
 *         (wrong type), -EACCES (missing rights)
 */
int nx_handle_get(struct process *proc, int32_t handle, uint32_t type,
                  uint32_t required, nx_object_t **obj, uint32_t *rights);

/** Close one handle. @return 0 or -EBADF */
int nx_handle_close(struct process *proc, int32_t handle);

/**
 * Remove a handle from proc's table WITHOUT dropping its reference: the
 * caller takes over that reference (used to move handles into a message).
 * @return 0, or -EBADF
 */
int nx_handle_take(struct process *proc, int32_t handle, nx_object_t **obj, uint32_t *rights);

/** Number of free handle slots of proc (NX_HANDLE_MAX when it has no state). */
uint32_t nx_handle_free_slots(struct process *proc);

/**
 * Wake every process blocked in obj.wait so it re-checks its handles.
 * Called whenever a channel or port changes state.
 */
void nx_ipc_wake_waiters(void);

/**
 * Release everything a process holds: unmap every shm mapping, close every
 * handle, stop charging it for shm it created, and free its IPC state.
 * Called on exit, kill, fault and exec. Safe when the process has none.
 */
void nx_ipc_release(struct process *proc);

/* ============ Shared memory (nx_shm.c) ============ */

/** Unmap every mapping of proc (used by nx_ipc_release). */
void nx_shm_unmap_all(struct process *proc, nx_proc_ipc_t *ipc);

/** Stop charging proc for objects it created that outlive it. */
void nx_shm_forget_creator(struct process *proc);

/** Bytes of all live shm objects (mem.stats). Syscall context (IRQs off). */
uint64_t nx_shm_total_bytes(void);

/* ============ Memory pressure objects (core/nx_mem.c, K1) ============ */

/** Signals of a pressure object for obj.wait (NX_SIGNAL_READABLE when the
 *  level changed since its handle last read it). */
uint32_t nx_mem_pressure_signals(nx_object_t *obj);

/** Timer tick hook: after a pressure level change, log it and wake
 *  obj.wait waiters. Cheap when nothing changed. */
void nx_mem_tick(void);

/** K2 part 3: the desktop's mapping was refused at its floor (R/2). Ends
 *  the largest BACKGROUND app if one may be ended now. Syscall context.
 *  @return 1 if an app was ended (retry the mapping), 0 otherwise */
int nx_mem_reclaim_for_system(void);

/** K2 part 3 timer hook: when free memory is below R/2 (the kernel used
 *  it), end the largest BACKGROUND app. Call only when the tick
 *  interrupted user mode. */
void nx_mem_reclaim_tick(void);

/* ============ Channels and ports (nx_chan.c) ============ */

/** Stop charging proc for messages it sent that are still queued. */
void nx_chan_forget_sender(struct process *proc);

/* ============ Syscall handlers ============ */

int64_t sys_handle_close_impl(uint64_t h, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_handle_dup_impl(uint64_t h, uint64_t rights, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_shm_create_impl(uint64_t size, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_shm_map_impl(uint64_t h, uint64_t prot, uint64_t out_size, uint64_t a4, uint64_t a5);
int64_t sys_shm_unmap_impl(uint64_t addr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_chan_create_impl(uint64_t out, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_chan_send_impl(uint64_t h, uint64_t msg, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_chan_recv_impl(uint64_t h, uint64_t msg, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_obj_wait_impl(uint64_t items, uint64_t count, uint64_t timeout_ms, uint64_t a4, uint64_t a5);
int64_t sys_port_listen_impl(uint64_t name, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_port_connect_impl(uint64_t name, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_port_accept_impl(uint64_t port, uint64_t out_pid, uint64_t a3, uint64_t a4, uint64_t a5);

#endif /* NEOLYX_NX_HANDLE_H */
