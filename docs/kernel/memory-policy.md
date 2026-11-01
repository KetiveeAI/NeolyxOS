# Memory policy when memory runs low (K2)

K2 is what the kernel and the app libraries do when memory gets short. It builds on the reserve (K0b, `memory-reserve-and-scheduling.md`) and the app classes and pressure levels (K1, `app-classes-and-memory-pressure.md`). The owner approved all three parts on 2026-11-01, with reclaim option A for part 3. Each part ships as its own pull request.

| Part | What it does | ABI change |
|---|---|---|
| 1 | Freed app memory goes back to the system | none |
| 2 | Background apps hit the memory limit first | none |
| 3 | When even the desktop is short, the largest background app is ended | new exit code constant only |

Parts 1 and 2 are in the tree.

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

## Part 2: background apps hit the limit first

Before part 2, every app was refused at the reserve R. Now the limit depends on the app's class (K1):

| Who | Refused when free memory would fall below |
|---|---|
| BACKGROUND apps | 1.5R (the CRITICAL line) |
| FOREGROUND apps | R (as before) |
| The desktop (`PROC_FLAG_SYSTEM`) | R/2 (as before) |
| Kernel | 0 (as before) |

On the 2 GiB QEMU setup R is 35 MB, so background apps stop at 53 MB free. The app you are using can still take the 18 MB between them, and the desktop can go further.

- `pmm_user_alloc_allowed()` takes a class (`PMM_USER_NORMAL`, `PMM_USER_SYSTEM`, `PMM_USER_BACKGROUND`). An unknown value gets the background floor, the strictest. `pmm_user_floor_pages()` returns a class's floor.
- `addrspace_t.reserve_class` holds the class. It is set when the image is built: new apps start FOREGROUND, and exec keeps the process's class. `process_set_class()` updates it whenever the class changes, whether the window server changes it on focus or the app sends itself to the background.
- Refusals name the class and its floor:

```
[MEM] user mapping refused: 55236 KB free, reserve 36824 KB, background app floor 55236 KB
```

- Shared memory keeps its own 64 MiB floor, as before.
- No syscall or ABI change. `mem.stats` still counts every refusal.

### Tests

- **Host:** `tests/host/pmm_test.c` (56 checks). It covers each class's floor, the background floor to the page, foreground still allowed where background is refused, and unknown classes treated as background. If the background floor is set to R, 6 checks fail.
- **QEMU** (2 GiB, 1 CPU): after its first run, `/apps/memstress` starts a class run. 29 children move themselves to BACKGROUND and fill memory, then one FOREGROUND child starts.

```
[MEMSTRESS] class run: 4 background refused at 52-53 MB free (floor 53), foreground refused at 35 MB free (floor 35) (PASS)
```

  The background children were refused at the 1.5R line. The foreground child still got 15 MB below it, and was refused at R. The nxuitest window kept working during the run. Regressions: fdtest, shmtest, ipc_test and classtest print `RESULT: PASS`. wintest: 39 PASS.

