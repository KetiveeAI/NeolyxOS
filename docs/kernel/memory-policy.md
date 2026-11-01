# Memory policy when memory runs low (K2)

K2 is what the kernel and the app libraries do when memory gets short. It builds on the reserve (K0b, `memory-reserve-and-scheduling.md`) and the app classes and pressure levels (K1, `app-classes-and-memory-pressure.md`). The owner approved all three parts on 2026-11-01, with reclaim option A for part 3. Each part ships as its own pull request.

| Part | What it does | ABI change |
|---|---|---|
| 1 | Freed app memory goes back to the system | none |
| 2 | Background apps hit the memory limit first | none |
| 3 | When even the desktop is short, the largest background app is ended | new exit code constant only |

All three parts are in the tree.

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

## Part 3: reclaim when even the desktop is short

The owner chose option A: when the desktop or the kernel would take free memory below R/2, the kernel ends the BACKGROUND app holding the most private pages.

- **Who can be ended.** Only live BACKGROUND apps that are not running at that moment. FOREGROUND apps, the desktop and kernel threads are never chosen. The app with the most private pages goes first. On a tie, the lowest PID goes first, so the choice is deterministic.
- **How often.** At most one app every 2 s (`NX_RECLAIM_INTERVAL_MS`), so one spike cannot wipe out every app. The rule is the same whatever caused the shortage.
- **When it runs.**
  - *Desktop.* If a desktop mapping is refused at its floor (R/2), the kernel ends one app right there, inside the desktop's own system call, and tries the mapping again. If that frees enough, the desktop's request succeeds.
  - *Kernel.* If the kernel itself used memory below R/2, the timer tick ends one app. This runs only when the tick interrupted user mode, so no kernel work is cut in half.
- **What the app sees.** Nothing: it is ended like a killed process. Its files are closed and its handles, shared memory and address space are released. Its windows close because its window server connection goes away. Its parent sees exit status `NX_EXIT_RECLAIMED` (251, in `nx_mem.h`).
- **Log.**

```
[MEM] reclaimed PID 31 (memstress): 64 MB, free was 17 MB (desktop short)
```

  When memory is short but no background app can be ended, this is logged instead, at most once every 2 s:
  `[MEM] desktop short of memory (17 MB free) and no background app to reclaim`.
- **Code.** The policy is in `kernel/include/core/mem_reclaim.h`. The process scan and logging are in `kernel/src/core/nx_mem.c`, and ending the process is `process_end()` in `process.c`. The hooks are in `as_reserve_allows()` (`addrspace.c`) and `scheduler_tick_irq()`.
- **ABI.** The only addition is the `NX_EXIT_RECLAIMED` constant. There is no new call and no layout change, so the ABI version stays 1.3.
- **Not in part 3.** Option C, warning an app and giving it time to save before ending it, needs a "please exit" signal. That belongs with Task Manager's `proc.kill` TERM work.

### Tests

- **Host:** `tests/host/mem_reclaim_test.c` (21 checks). It covers the choice (largest eligible app, never an ineligible one even if larger, ties to the lowest PID), the 2 s limit (to the millisecond, and when the clock goes back), and 20 simulated seconds of shortage ending at most one app per interval, largest first. If ties go to the last candidate instead, a check fails.
- **QEMU.** The desktop cannot be made short on demand in a normal build, so a test build adds an F12 key. It is compiled only with `EXTRA_CFLAGS=-DDESKTOP_MEM_SELFTEST ./build_all.sh` and never in release images. F12 makes the desktop take up to 32 MB, touching every page, and then free it.
  1. Open nxuitest, then start memstress. Wait for its class run line `foreground child PID n: ... refused at 35 MB free`. Memory is now at R, with about 29 background apps holding 64 MB each.
  2. Press F12. Result:

```
[DESKTOP] mem selftest: start, free MB 35
[MEM] reclaimed PID 31 (memstress): 64 MB, free was 17 MB (desktop short)
[DESKTOP] mem selftest: holding MB 30, free MB 70
[MEMSTRESS] class run: PID 31 (background) was reclaimed by the kernel
[MEMSTRESS] class run: 1 background app(s) reclaimed
[MEMSTRESS] PASS
```

  The desktop took 18 MB down to R/2. The kernel then ended one background app and the desktop got 12 MB more, stopping only at its own 64 MB heap limit. Only one app was ended, and nxuitest (FOREGROUND) kept working.
  - **Release build.** No reclaim happens in the normal memstress run, because the desktop never runs short. F12 does nothing. memstress, fdtest, shmtest, ipc_test and classtest print PASS. wintest: 39 PASS.

