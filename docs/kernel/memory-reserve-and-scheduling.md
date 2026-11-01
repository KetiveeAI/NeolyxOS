# Memory reserve and desktop-first scheduling (K0b)

Two kernel policies that keep the desktop and the kernel working when apps use all the memory or all the CPU. No syscall, ABI or public API changes: both use `PROC_FLAG_SYSTEM`, which the kernel already sets on the desktop.

## Memory reserve

At boot the PMM sets aside a reserve of free pages: 2% of usable RAM, at least 32 MiB, at most 128 MiB, and never more than a quarter of RAM. The boot log shows it:

```
[PMM] Reserve: 35 MB kept from user allocations (system processes may use half)
```

Every user page mapping (heap growth with `brk`, ELF segments and the stack at spawn and exec, and the page tables behind them) goes through `addrspace_map_alloc`, which asks `pmm_user_alloc_allowed()` before taking a frame:

| Who | May allocate down to |
|---|---|
| Ordinary processes | the full reserve (K2 part 2: background apps stop at 1.5 times the reserve, see `memory-policy.md`) |
| `PROC_FLAG_SYSTEM` processes (the desktop) | half the reserve |
| Kernel (kernel stacks, kernel heap, DMA buffers) | zero |

A refused mapping fails the request with `-ENOMEM` and logs the free memory, the reserve, and (since K2 part 2) the floor of the process's class:

```
[MEM] user mapping refused: 36824 KB free, reserve 36824 KB, foreground app floor 36824 KB
[BRK] PID 33: out of memory
```

Shared memory keeps its own, stricter 64 MiB floor (`SHM_SYSTEM_FLOOR` in `nx_shm.c`).

The process's class is recorded in `addrspace_t.reserve_class` when its image is built, and is updated when its app class changes (K2 part 2), so the check needs no process lookup.

## Scheduling

K1 later split the NORMAL queue into FOREGROUND and BACKGROUND apps: see `app-classes-and-memory-pressure.md`.

The single round-robin queue is replaced by two FIFO queues (`kernel/src/core/sched_queue.c`):

- **SYSTEM**: `PROC_FLAG_SYSTEM` processes (the desktop) and kernel threads.
- **NORMAL**: every other process.

Rules:

- When both queues have work, SYSTEM gets 3 turns in a row, then NORMAL gets 1. Neither class can starve the other.
- Each class is round robin.
- The streak resets when no NORMAL work is waiting, so a desktop that ran alone owes nothing later.
- **Wake-up preemption.** When a SYSTEM process becomes ready while a NORMAL process runs in ring 3, the next timer tick (≤ 1 ms) switches to it, instead of waiting for the end of the 10 ms slice.
- `process_t.priority` now reflects the class: 64 for SYSTEM, 128 for NORMAL.

Unchanged:

- Kernel code is still never preempted.
- One CPU.
- The idle thread.
- Sleep, block and wake semantics.
- The 10 ms slice.

## Tests

- **Host.** Build and run instructions are in each file header.
  - `tests/host/sched_queue_test.c`: 59 checks covering class mapping, queue add/remove, the exact 3:1 pattern, round robin within a class, no starvation, and no carried debt.
  - `tests/host/pmm_test.c`: reserve size, and the NORMAL and SYSTEM floors to the page.
- **QEMU.** Two test apps are installed in `/apps`:
  - `memstress` spawns copies of itself that each take up to 64 MiB until the kernel refuses. It prints `[MEMSTRESS] PASS` when the refusals came with the reserve still free and no memory was corrupted.
  - `cpuhog` runs two busy loops for 20 s and prints their loop rate. Compare the desktop's `[DESKTOP] Frames:` lines with and without it.

Measured in QEMU (2 GiB, 1 CPU, app drawer toggled 8 times while two cpuhog loops run):

| | Desktop time per drawn frame |
|---|---|
| Before (main) | 94 ms and 177 ms (two runs) |
| K0b | 40 ms and 43 ms (no-load runs: 26 to 48 ms) |

Over the same 20 s, the busy loops got about 15% fewer iterations than on main.
