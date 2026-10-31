# Proposal: kernel syscalls for process management (Task Manager)

Status: **proposal, not implemented**. No kernel file has been changed for this.
Owner of the client side: `desktop/lib/nxproc.c` (Task Manager area).

## Why

The Task Manager (`desktop/shell/views/taskmgr_view.c`) can only show real data
the kernel gives to userspace. Today that is:

| Need | Syscall today | Result |
|------|---------------|--------|
| Memory total / free | `info` (50) | Real (`pmm_get_stats`) |
| Uptime | `info` (50) | Real (`pit_get_ticks`, PIT at 1000 Hz, so ticks = ms) |
| Own pid / ppid | `getpid` (5) / `getppid` (6) | Real |
| CPU count | `info` (50) | Hard-coded `1` |
| Process count | `info` (50) | Hard-coded `1` (`TODO` in `sys_info_impl`) |
| Process list, per-process CPU and memory | none | `NXPROC_ERR_UNSUPPORTED` |
| CPU load (overall, per core) | none | `NXPROC_ERR_UNSUPPORTED` |
| End a process | none | `NXPROC_ERR_UNSUPPORTED` |
| Change priority | none | `NXPROC_ERR_UNSUPPORTED` |

The kernel already holds everything needed: the PCB has `pid`, `ppid`, `name`,
`state`, `flags`, `priority`, `total_time` (ticks, incremented in
`scheduler_tick`) and `start_time`. `nxtask_kdrv` (`kernel/include/drivers/nxtask_kdrv.h`)
already walks the table in-kernel. What is missing is a small, audited,
read-mostly path to userspace.

## Alignment with `claude/project-thread-ng0gvk`

That branch (per-process address spaces, `spawn` = 13) is the base this proposal
assumes. It does not conflict with it:

- New numbers 52-56 are free on `main` and on that branch (it adds only 13).
- Resident memory comes from the branch's `addrspace_t.pages_mapped`
  (`* 4096`), plus the kernel stack. Kernel threads (`as == NULL`) report
  their kernel stack only.
- `proc.kill` FORCE uses the branch's `process_kill()`, which already logs,
  refuses `PROC_FLAG_SYSTEM`, the idle process and zombies, and calls
  `terminate()` under `irq_save()`.
- The graceful deadline reuses the pattern of the branch's `wake_tick`
  field: one more appended `process_t` field (`term_deadline`), so earlier
  offsets stay stable.
- Apps started with `spawn` are children of the desktop, so the privilege rule
  below lets the desktop end what it launched without new privilege.

## Syscall table entries

```c
/* Process information and control (52-56) - Task Manager */
NX_SYSCALL(52, "sys.stats",    sys_sys_stats_impl,    2, PRIV_USER, 0)
NX_SYSCALL(53, "proc.list",    sys_proc_list_impl,    3, PRIV_USER, 0)
NX_SYSCALL(54, "proc.info",    sys_proc_info_impl,    3, PRIV_USER, 0)
NX_SYSCALL(55, "proc.kill",    sys_proc_kill_impl,    3, PRIV_USER, NX_FLAG_AUDIT)
NX_SYSCALL(56, "proc.setprio", sys_proc_setprio_impl, 2, PRIV_USER, NX_FLAG_AUDIT)
```

`PRIV_USER` in the table is the minimum to call; each handler applies the finer
rules below. The reads are not audited (the Task Manager polls them at 1 Hz);
every kill and priority change is.

## Structures

All structures are copied with `syscall_copy_to_user` after
`syscall_validate_ptr`. The caller passes the size it was built against, so the
kernel can append fields later without breaking old clients (the kernel copies
`min(caller_size, sizeof(struct))` and rejects sizes below the v1 size).

```c
#define NX_PROC_NAME_MAX 64

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;           /* process_state_t */
    uint32_t flags;           /* PROC_FLAG_*; PROC_FLAG_TERM_PENDING (0x0010) new */
    uint32_t priority;        /* 0 highest .. 255 lowest */
    uint32_t threads;         /* 1 until threads share a process */
    uint32_t uid;             /* 0 until the kernel has users */
    uint32_t reserved;
    uint64_t cpu_time_ms;     /* total_time; PIT ticks are ms at 1000 Hz */
    uint64_t start_ms;        /* start_time */
    uint64_t resident_bytes;  /* as->pages_mapped * 4096 + kernel stack */
    char     name[NX_PROC_NAME_MAX];   /* NUL-terminated */
} nx_proc_entry_t;            /* v1: 120 bytes */

#define NX_SYS_STATS_MAX_CPUS 16

typedef struct {
    uint32_t size;            /* bytes the kernel filled */
    uint32_t version;         /* 1 */
    uint32_t cpu_count;       /* real count; 1 until SMP */
    uint32_t proc_count;      /* processes not DEAD */
    uint32_t thread_count;
    uint32_t reserved;
    uint64_t uptime_ms;
    uint64_t busy_ms;         /* cumulative non-idle core-ms, all cores */
    uint64_t idle_ms;         /* cumulative idle core-ms (idle_process->total_time) */
    uint64_t mem_total;
    uint64_t mem_free;
    uint64_t core_busy_ms[NX_SYS_STATS_MAX_CPUS];
} nx_sys_stats_t;             /* v1: 192 bytes */
```

Cumulative counters, not percentages: the client computes CPU % from the
difference between two samples (`nxproc_snapshot` / `nxproc_sys_stats`
already do this, and the preview exercises it). This keeps the kernel free of
averaging windows and floating point.

## Semantics

### `sys.stats(nx_sys_stats_t *buf, uint32_t size)` -> 0 or -errno
`-EFAULT` bad pointer, `-EINVAL` size below v1. `busy_ms` is
`uptime_ms * cpu_count - idle_ms` on a single CPU today.

### `proc.list(nx_proc_entry_t *buf, uint32_t max, uint32_t entry_size)` -> total or -errno
Copies up to `max` entries (`max <= MAX_PROCESSES`) and returns the total
number of live processes, which may be larger than `max` (the client reports
`NXPROC_ERR_TRUNCATED`). The table is read with interrupts saved so the
snapshot is consistent. `DEAD` entries are skipped; zombies are listed.

### `proc.info(uint32_t pid, nx_proc_entry_t *buf, uint32_t entry_size)` -> 0 or -errno
`-ESRCH` no such pid. A process can call it on itself to see
`PROC_FLAG_TERM_PENDING`.

### `proc.kill(uint32_t pid, uint32_t mode, uint32_t grace_ms)` -> 0 or -errno
- `mode 0 = TERM`: set `PROC_FLAG_TERM_PENDING` and
  `term_deadline = pit_get_ticks() + grace_ms` (`grace_ms` 1..60000, default
  3000). `scheduler_tick` terminates the process when the deadline passes.
  Runtimes that poll `proc.info(self)` can save state and `exit` first.
- `mode 1 = KILL`: `process_kill(pid, 9)` now.
- Errors: `-ESRCH` unknown or already exiting, `-EACCES` target has
  `PROC_FLAG_SYSTEM` or `PROC_FLAG_KERNEL`, is pid 1 or the idle process,
  `-EPERM` caller lacks privilege, `-EINVAL` bad mode.

### `proc.setprio(uint32_t pid, uint32_t priority)` -> 0 or -errno
`priority` 0..255. Lowering priority (a larger number) is allowed under the
kill rule below. Raising it above the default 128 needs `PRIV_SYSTEM`.

## Privilege rules

| Action | Caller with `PRIV_USER` | Caller with `PRIV_SYSTEM` |
|--------|-------------------------|---------------------------|
| `sys.stats`, `proc.list`, `proc.info` | Allowed | Allowed |
| `proc.kill` / lower priority | Only its own descendants (walk `ppid`) | Any non-system process |
| Raise priority above 128 | Refused (`-EPERM`) | Allowed |
| Target `PROC_FLAG_SYSTEM` / kernel thread / pid 1 / idle | Refused (`-EACCES`) | Refused (`-EACCES`) |

When the kernel gains user ids, "own descendants" becomes "same uid".

## Audit

`NX_FLAG_AUDIT` makes the dispatcher log every `proc.kill` and `proc.setprio`.
The handlers add one line with the outcome, for example:

```
[PROC] kill pid=28 mode=TERM grace=3000 by pid=14 -> 0
[PROC] kill pid=28 deadline passed, terminated (exit 137)
[PROC] setprio pid=20 128->192 by pid=14 -> 0
```

## Small fixes that need no new syscall

- `sys_info_impl`: fill `num_processes` with the count of non-DEAD processes
  instead of the hard-coded `1` (field already exists; no ABI change).
- `sys_info_impl`: `uptime_ms` is `pit_get_ticks()`; correct only while the
  PIT runs at 1000 Hz. Scale by `PIT_TICK_RATE` to keep it right.

## Client mapping

`nxproc.c` maps `-ENOSYS` today; the other rows are how the backend will map
the new syscalls' errors.

| Kernel | `nxproc_status_t` |
|--------|-------------------|
| `-ENOSYS` (38) | `NXPROC_ERR_UNSUPPORTED` |
| `-ESRCH` | `NXPROC_ERR_NOT_FOUND` |
| `-EPERM` | `NXPROC_ERR_PERMISSION` |
| `-EACCES` | `NXPROC_ERR_PROTECTED` |
| other negative | `NXPROC_ERR_KERNEL` |

When these syscalls land, only the NeolyxOS backend block of `nxproc.c`
(`be_list`, `be_sys`, `be_end`, `be_prio`, `be_caps`) changes; the API in
`nxproc.h` and the Task Manager stay as they are.
