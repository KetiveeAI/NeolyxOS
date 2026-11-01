# Memory policy when memory runs low (K2)

K2 is what the kernel and the app libraries do when memory gets short. It builds on the reserve (K0b, `memory-reserve-and-scheduling.md`) and the app classes and pressure levels (K1, `app-classes-and-memory-pressure.md`). The owner approved all three parts on 2026-11-01, with reclaim option A for part 3. Each part ships as its own pull request.

| Part | What it does | ABI change |
|---|---|---|
| 1 | Freed app memory goes back to the system | none |
| 2 | Background apps hit the memory limit first | none |
| 3 | When even the desktop is short, the largest background app is ended | new exit code constant only |

Only part 1 is in the tree so far.

## Part 1: freed memory goes back to the system

Before K2, an app that freed memory kept the physical pages until it exited, so an app that freed its caches on WARN freed nothing system-wide.

### Kernel: `brk` shrinking

- Shrinking the break with `brk` now unmaps the whole pages above the new break and returns their frames to the PMM. Page tables left empty are freed as well.
- The `brk` contract is unchanged. It returns the new page-aligned break, and memory below the break is untouched. Growing again maps fresh zero-filled pages, never the old contents.
- The work is done by `addrspace_unmap_range()` (`kernel/src/mm/addrspace.c`). It only accepts page-aligned ranges inside the private user window. It never frees a shared-memory frame: such a page would be logged and left mapped. When the address space is live, each page is flushed with `invlpg`, and CR3 is reloaded if a page table was freed.
- A shrink that frees 1 MiB or more is logged:

```
[BRK] PID 3: returned 32768 KB to the system
```

### `nx_malloc` (desktop/lib)

- When a free makes the free space at the top of the heap reach 1 MiB plus the 256 KiB growth step, `nx_malloc` lowers the break and keeps 256 KiB of slack. Smaller frees are not trimmed, so a free/malloc loop does not call `brk` every time.
- `nx_malloc_trim()` (declared in `nxsyscall.h`) gives back everything above the last used block, with no slack. It returns the number of bytes released. Apps can call it when memory runs low.
- If `brk` refuses a trim, the allocator logs it and keeps the memory. The heap stays valid either way.
- The desktop uses `nx_malloc` too, so it also gives memory back. In QEMU it returned about 7 MB after its startup work.

### NxUI

- At start, NxUI opens a memory pressure handle (when the kernel reports `NX_ABI_FLAG_MEMCLASS`). It waits on that handle next to its window server connection, so an idle app reacts as well.
- When the level rises to WARN or CRITICAL, NxUI calls `nx_malloc_trim()` and logs what it returned:

```
[NXUI] memory WARN: returned 96 KB of heap to the system
```

- If the kernel has no pressure notices, or the handle cannot be opened or read, the app runs as before and the reason is logged once.
- The NXCanvas glyph caches are not dropped. They are fixed allocations of about 1.3 MiB made once at start.

### Tests

- **Host:** `tests/host/nx_malloc_test.c` (39 checks). It covers trimming after freeing the top 8 MiB, the slack that is kept, lower blocks keeping their contents, small frees not being trimmed, `nx_malloc_trim()`, and growing again after a trim. Its fake `brk` poisons released pages, so use after trim would show.
- **QEMU** (2 GiB, 1 CPU):
  - `/apps/memstress` first grows its own heap by 32 MB and shrinks it back. `mem.stats` must show its own pages down by exactly 32 MB and free memory up by 32 MB, and regrown pages must read as zero. Result: `trim: ... own pages -32 MB, free memory +32 MB, regrown pages zeroed (PASS)`, then the K0b and K1 checks, then `PASS`.
  - With nxuitest open during memstress, NxUI logged its WARN trim and the window kept working afterwards.
  - Regressions: fdtest, shmtest, ipc_test and classtest print `RESULT: PASS`. wintest: 39 PASS, 0 FAIL.
  - classtest's CPU share check (FOREGROUND:BACKGROUND between 2.5:1 and 6:1) is close to its lower limit while another app window is open: 1 run in 4 failed with this change, and 1 in 3 failed with the old NxUI. With no other window open it measured 3.1:1 and 3.3:1, as in K1. Part 1 does not change the scheduler.
