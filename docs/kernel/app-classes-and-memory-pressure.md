# App classes, memory pressure and memory stats (K1, ABI 1.3)

K1 builds on the memory reserve and the system scheduling class from K0b (`memory-reserve-and-scheduling.md`). It adds four syscalls, approved by the owner on 2026-11-01. Public types are in `kernel/include/core/nx_mem.h`, and userspace wrappers are in `desktop/include/nxsyscall.h`. `abi.query` reports ABI 1.3 and `NX_ABI_FLAG_MEMCLASS` (0x0008).

| # | Call | Returns |
|---|---|---|
| 153 | `proc.set_class(pid, cls)` | 0 or -errno |
| 154 | `mem.pressure_open(flags)` | handle or -errno |
| 155 | `mem.pressure_read(h)` | level (0 to 2) or -errno |
| 156 | `mem.stats(buf, size)` | 0 or -errno |

## App classes

Apps are in one of two classes. The desktop and kernel threads keep the SYSTEM class from K0b, which these calls cannot change.

| Class | Who | Share under full load | `priority` |
|---|---|---|---|
| SYSTEM | `PROC_FLAG_SYSTEM` processes, kernel threads | 9 of 12 turns | 64 |
| `NX_CLASS_FOREGROUND` (1) | new apps; the app with the focused window | 2 of 12 | 112 |
| `NX_CLASS_BACKGROUND` (2) | other apps with windows; apps that send themselves back | 1 of 12 | 144 |

How the turns are shared:
- When apps are waiting, SYSTEM gets 3 turns in a row, then one app turn.
- App turns go 2:1 to FOREGROUND over BACKGROUND. With no SYSTEM work, that is 2 of 3 turns.
- No class can starve.
- A wake-up of a more urgent class preempts a less urgent app at the next tick (≤ 1 ms).

**Who may call `proc.set_class`:**
- A SYSTEM process (nxserver in the desktop) may set any app.
- An app may only send itself to BACKGROUND.
- Every change is logged: `[SCHED] PID 35 (nxuitest): class FOREGROUND -> BACKGROUND by PID 1`.

**Errors:**
- `-EINVAL`: unknown class.
- `-ESRCH`: no such live process.
- `-EACCES`: the target is SYSTEM, a kernel thread or pid 0, or the caller isn't allowed. These refusals are logged.

**nxserver** (`desktop/shell/nxserver/nxserver.c`) checks every frame which client owns the focused window.
- That app is FOREGROUND, and every other client with a window is BACKGROUND.
- A client with no window yet keeps its class, so an app that is still starting isn't demoted.
- Only changes are sent. A refusal is logged once and not retried.
- Processes without a window (not nxserver clients) keep the class they started in.

## Memory pressure

The kernel keeps one pressure level, based on free memory against the reserve R. On 2 GiB, R is 35 MB.

| Level | Entered at free ≤ | Left above |
|---|---|---|
| `NX_MEM_NORMAL` (0) | n/a | n/a |
| `NX_MEM_WARN` (1) | 3R | 4R, back to NORMAL |
| `NX_MEM_CRITICAL` (2) | 1.5R | 2R, back to WARN |

The gaps stop the level flapping. The PMM updates the level under its lock on every allocation and free (`pmm_pressure_next`, host-tested). The next timer tick logs the change and wakes `obj.wait` waiters, so the PMM never calls the scheduler:

```
[MEM] pressure WARN: 106 MB free, reserve 35 MB
[MEM] pressure CRITICAL: 53 MB free, reserve 35 MB
```

How the handle works:
- `mem.pressure_open(0)` returns a handle with READ and DUPLICATE rights. It can't be transferred over a channel.
- In `obj.wait`, the handle is `NX_SIGNAL_READABLE` once the level has changed since this handle last read it.
- `mem.pressure_read(h)` returns the current level and clears READABLE.

Errors:
- `-EINVAL`: `flags` is not 0, or the handle isn't a pressure handle.
- `-EBADF`: a bad handle.
- `-EMFILE`: the handle table is full.

## `mem.stats`

This fills `nx_mem_stats_t` (v1, 80 bytes):
- `level`;
- total and free memory;
- the reserve;
- all private user pages;
- live shared memory;
- kernel heap in use;
- refusals at the reserve since boot;
- the caller's own private pages.

The kernel copies `min(size, 80)` bytes, so a newer, larger struct still works. A `size` below 80 is `-EINVAL`, and a bad buffer is `-EFAULT`.

## Not in K1

- **NxUI cache trimming.** The proposal had NxUI apps drop their glyph cache on WARN. The NXCanvas caches are fixed-size allocations made once at start. The user heap (`brk`) also keeps its pages mapped until exit. So trimming would free no physical memory today. It needs a heap that can return pages first, which fits K2. K2 part 1 added that, and NxUI now trims its heap on WARN: see `memory-policy.md`.
- **K2 policy**: what the kernel does at CRITICAL, such as ending or pausing background apps. See `memory-policy.md`.

## Tests

- **Host:**
  - `tests/host/sched_queue_test.c` (81 checks): exact 9:2:1 and 2:1 shares, no starvation, and class changes while queued.
  - `tests/host/pmm_test.c` (46 checks): every threshold, hysteresis over a 10,000-step random walk, and the live level and change flag while memory drains.
- **QEMU** (2 GiB, 1 CPU):
  - `/apps/classtest` covers every error path and permission rule, the pressure handle with `obj.wait`, `handle.dup` and `-EMFILE`, `mem.stats` consistency, and CPU share. One FOREGROUND and two BACKGROUND spinners got 221,912 vs 61,638 and 70,351 kloops (about 3.3:1; 4:1 in theory, minus the desktop's share). It prints `RESULT: PASS`.
  - `/apps/memstress` sees WARN at 106 MB free and CRITICAL at 53 MB, both before the first refusal at the reserve. It sees NORMAL again after its children exit, and `mem.stats` counts the refusals. It prints `PASS`.
  - Focus: opening nxuitest keeps it FOREGROUND. Opening wintest moves nxuitest to BACKGROUND, and the classes follow focus as windows come and go.
  - Regressions: shmtest, fdtest and ipc_test pass. wintest: 39 PASS, 0 FAIL.
