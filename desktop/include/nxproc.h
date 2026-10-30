/*
 * NeolyxOS - NXProc: userspace process and system statistics client
 *
 * The one place desktop code asks the kernel about processes: a bounded
 * snapshot of the process table, system-wide CPU/memory/uptime figures,
 * ending a process and changing its priority.
 *
 * What works today (kernel at the time of writing):
 *   - System memory total/free and uptime: SYS_INFO (50), real data.
 *   - The caller's own pid/ppid: SYS_GETPID (5) / SYS_GETPPID (6).
 *   - Everything else (process list, per-process CPU and memory, CPU
 *     load, end process, set priority) has no userspace path yet and
 *     returns NXPROC_ERR_UNSUPPORTED (logged once per operation). The
 *     syscalls that would provide it are proposed in
 *     docs/desktop/proposals/taskmgr.md.
 *   - Host preview builds (NX_HOST_PREVIEW) answer from a deterministic
 *     demo backend inside nxproc.c so the Task Manager can be designed;
 *     that data never exists in a NeolyxOS build.
 *
 * Rules:
 *   - No allocation: every buffer is caller-owned and bounded by the
 *     NXPROC_MAX_* constants below.
 *   - CPU percentages are computed from the difference between two
 *     samples taken through the same nxproc_sampler_t, so the first
 *     sample after nxproc_sampler_init() reports 0 % CPU.
 *   - Not thread-safe; the desktop shell is single-threaded.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXPROC_H
#define NXPROC_H

#include <stdint.h>

/* ============ Limits ============ */

#define NXPROC_MAX_PROCS     256   /* matches kernel MAX_PROCESSES */
#define NXPROC_NAME_MAX      64    /* matches kernel PROCESS_NAME_MAX (incl. NUL) */
#define NXPROC_OWNER_MAX     16
#define NXPROC_MAX_CPUS      16

/* Default grace period between a graceful end request and a forced one. */
#define NXPROC_GRACE_MS      3000u

/* ============ Status codes ============ */

typedef enum {
    NXPROC_OK               =  0,
    NXPROC_ERR_INVALID_ARG  = -1,  /* NULL pointer, bad pid or out-of-range value */
    NXPROC_ERR_UNSUPPORTED  = -2,  /* the kernel has no userspace path for this yet */
    NXPROC_ERR_NOT_FOUND    = -3,  /* no such process (it may already have exited) */
    NXPROC_ERR_PERMISSION   = -4,  /* caller lacks the privilege for this process */
    NXPROC_ERR_PROTECTED    = -5,  /* system/critical process, or the caller itself */
    NXPROC_ERR_KERNEL       = -6,  /* kernel returned an error or inconsistent data */
    NXPROC_ERR_TRUNCATED    = -7   /* more processes than fit; the snapshot is still valid */
} nxproc_status_t;

/* ============ Processes ============ */

typedef enum {
    NXPROC_STATE_RUNNING = 0,
    NXPROC_STATE_READY,
    NXPROC_STATE_BLOCKED,
    NXPROC_STATE_SLEEPING,
    NXPROC_STATE_ZOMBIE,
    NXPROC_STATE_UNKNOWN,
    NXPROC_STATE_COUNT
} nxproc_state_t;

/* Process flags (mirror the kernel PROC_FLAG_* meanings) */
#define NXPROC_F_KERNEL     0x0001u   /* kernel thread (ring 0) */
#define NXPROC_F_USER       0x0002u   /* user process (ring 3) */
#define NXPROC_F_SYSTEM     0x0004u   /* system process: cannot be ended */
#define NXPROC_F_SELF       0x0100u   /* set by nxproc: this is the calling process */

/* Priority uses the kernel scale: 0 = highest, 255 = lowest, 128 = default.
 * The named classes are what the UI offers. */
#define NXPROC_PRIO_HIGH        64u
#define NXPROC_PRIO_ABOVE       96u
#define NXPROC_PRIO_NORMAL      128u
#define NXPROC_PRIO_BELOW       160u
#define NXPROC_PRIO_LOW         192u

typedef struct {
    uint32_t       pid;
    uint32_t       ppid;
    char           name[NXPROC_NAME_MAX];      /* always NUL-terminated */
    nxproc_state_t state;
    uint8_t        priority;                   /* 0 highest .. 255 lowest */
    uint32_t       flags;                      /* NXPROC_F_* */
    uint64_t       cpu_time_ms;                /* total CPU time consumed */
    uint16_t       cpu_pct_x10;                /* share of total machine CPU, 0..1000 */
    uint64_t       resident_bytes;             /* physical memory owned */
    uint32_t       threads;
    uint64_t       start_ms;                   /* ms since boot when created */
    char           owner[NXPROC_OWNER_MAX];    /* "system" / "user" until the kernel has uids */
} nxproc_info_t;

typedef struct {
    uint64_t      taken_ms;                    /* ms since boot of this sample */
    uint32_t      count;                       /* valid entries in procs[] */
    uint32_t      kernel_count;                /* processes the kernel reported (>= count) */
    nxproc_info_t procs[NXPROC_MAX_PROCS];
} nxproc_snapshot_t;

/* ============ System statistics ============ */

/* Which fields of nxproc_sys_stats_t hold real data */
#define NXPROC_SYS_MEMORY     0x0001u   /* mem_total / mem_used / mem_free */
#define NXPROC_SYS_UPTIME     0x0002u   /* uptime_ms */
#define NXPROC_SYS_CPU        0x0004u   /* cpu_pct_x10 */
#define NXPROC_SYS_PER_CORE   0x0008u   /* core_pct_x10[0..cpu_count) */
#define NXPROC_SYS_PROCS      0x0010u   /* proc_count / thread_count */

typedef struct {
    uint32_t valid;                            /* NXPROC_SYS_* */
    uint64_t taken_ms;
    uint32_t cpu_count;                        /* as reported by the kernel (>= 1) */
    uint16_t cpu_pct_x10;                      /* overall busy, 0..1000 */
    uint16_t core_pct_x10[NXPROC_MAX_CPUS];
    uint64_t mem_total;                        /* bytes */
    uint64_t mem_used;
    uint64_t mem_free;
    uint64_t uptime_ms;
    uint32_t proc_count;
    uint32_t thread_count;
} nxproc_sys_stats_t;

/* ============ Sampler (delta state between samples) ============ */

typedef struct {
    uint32_t pid;
    uint64_t start_ms;      /* distinguishes a reused pid */
    uint64_t cpu_time_ms;
} nxproc_prev_t;

typedef struct {
    uint32_t      magic;                       /* set by nxproc_sampler_init */
    uint64_t      prev_taken_ms;
    uint32_t      prev_count;
    nxproc_prev_t prev[NXPROC_MAX_PROCS];
    /* system-wide CPU counters from the previous sys sample */
    uint64_t      prev_sys_ms;
    uint64_t      prev_busy_ms;
    uint64_t      prev_core_busy_ms[NXPROC_MAX_CPUS];
    uint32_t      have_sys_prev;
} nxproc_sampler_t;

/* ============ Capabilities ============ */

#define NXPROC_CAP_LIST       0x0001u   /* nxproc_snapshot works */
#define NXPROC_CAP_SYS_MEM    0x0002u   /* memory figures in nxproc_sys_stats */
#define NXPROC_CAP_SYS_CPU    0x0004u   /* CPU load in nxproc_sys_stats */
#define NXPROC_CAP_END        0x0008u   /* nxproc_end works */
#define NXPROC_CAP_PRIORITY   0x0010u   /* nxproc_set_priority works */
#define NXPROC_CAP_STARTUP    0x0020u   /* nxproc_startup_list works */
#define NXPROC_CAP_DEMO       0x8000u   /* data is preview demo data (host only) */

/* ============ Startup items ============ */

#define NXPROC_MAX_STARTUP   32
#define NXPROC_PATH_MAX      96

typedef struct {
    char    name[NXPROC_NAME_MAX];
    char    path[NXPROC_PATH_MAX];
    uint8_t enabled;
    uint8_t impact;          /* 0 none/unknown, 1 low, 2 medium, 3 high */
} nxproc_startup_t;

/* ============ API ============ */

/* Bitmask of NXPROC_CAP_* this build/kernel supports. Cheap; no syscalls
 * beyond the first call. */
uint32_t nxproc_capabilities(void);

/* Reset a sampler. Must be called before its first use. */
nxproc_status_t nxproc_sampler_init(nxproc_sampler_t *s);

/* Take a bounded snapshot of the process table. On NXPROC_ERR_TRUNCATED
 * out->count == NXPROC_MAX_PROCS and the data is valid. CPU % is relative
 * to the previous snapshot taken with the same sampler. */
nxproc_status_t nxproc_snapshot(nxproc_sampler_t *s, nxproc_snapshot_t *out);

/* System-wide statistics. Fields not marked in out->valid are zero.
 * Returns NXPROC_OK when at least one group is valid. */
nxproc_status_t nxproc_sys_stats(nxproc_sampler_t *s, nxproc_sys_stats_t *out);

/* The calling process (real syscalls on every build). */
nxproc_status_t nxproc_self(uint32_t *pid, uint32_t *ppid);

typedef enum {
    NXPROC_END_GRACEFUL = 0,   /* ask it to quit; kernel forces after the grace period */
    NXPROC_END_FORCE    = 1    /* terminate now */
} nxproc_end_mode_t;

/* End a process. Refuses pid 0, the caller itself and processes flagged
 * NXPROC_F_SYSTEM in the last snapshot (NXPROC_ERR_PROTECTED) before any
 * syscall; the kernel enforces privilege again. Every request is logged. */
nxproc_status_t nxproc_end(uint32_t pid, nxproc_end_mode_t mode, uint32_t grace_ms);

/* Set scheduling priority (0 highest .. 255 lowest). Raising priority
 * above NXPROC_PRIO_NORMAL needs system privilege in the kernel. */
nxproc_status_t nxproc_set_priority(uint32_t pid, uint8_t priority);

/* Items the desktop launches at login. Writes at most cap entries. */
nxproc_status_t nxproc_startup_list(nxproc_startup_t *out, uint32_t cap, uint32_t *count);

/* Human-readable names (never NULL). */
const char *nxproc_status_str(nxproc_status_t st);
const char *nxproc_state_name(nxproc_state_t st);
const char *nxproc_priority_name(uint8_t priority);

#endif /* NXPROC_H */
