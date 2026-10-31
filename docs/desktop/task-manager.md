# Task Manager

The Task Manager is a shell-hosted app view (see `desktop/include/app_view.h`).
It opens from the dock, app drawer or search as "Task Manager", "Activity" or
"Tasks", in a 780 x 520 window.

| File | Role |
|------|------|
| `desktop/shell/views/taskmgr_view.c` | The window: tabs, tables, charts, End Task sheet |
| `desktop/include/nxproc.h`, `desktop/lib/nxproc.c` | Userspace process and system-statistics client |
| `docs/desktop/proposals/taskmgr.md` | Kernel syscalls the full feature needs |

## Tabs

- **Processes**: four stat tiles (CPU with a sparkline, Memory, Processes,
  Uptime), then a table: Name, PID, State, User, Threads, CPU, Memory. Click a
  header to sort, click again to reverse. CPU cells are tinted by load. The
  desktop's own process has a "This app" badge. Footer: selection summary,
  **Priority** (popover: High, Above normal, Normal, Below normal, Low) and
  **End Task**. Both are disabled for system processes, kernel threads and the
  desktop itself.
- **Performance**: CPU and Memory area charts over the last 60 seconds, a
  per-core bar chart and a System card (uptime, processes, threads, memory
  total/used/free, processors).
- **Apps**: the desktop's own windows (`desktop_window_slot`), with Active,
  Running or Minimized status, size and window id. **Switch To** calls
  `desktop_activate_window`, and **End Task** calls `desktop_close_window`.
  This works today with no kernel support.
- **Startup**: login items with path, Enabled or Disabled, and impact.
  Read-only.

## Ending a task

End Task opens a confirmation sheet with a destructive **End Task** button.
Press Enter to confirm or Esc to cancel. For a process:

1. `nxproc_end(pid, NXPROC_END_GRACEFUL, 3000)` asks the process to quit, and
   the footer shows "Asked X to quit...".
2. Each 1 Hz refresh checks whether the pid is still present. If it has gone,
   the footer shows "X has ended."
3. If the process is still running after the grace period, the view calls
   `nxproc_end(pid, NXPROC_END_FORCE, 0)` and says so.

The process is checked twice before any syscall is made:

- The view does not offer End Task for protected processes.
- `nxproc_end` refuses pid 0, the caller itself and any process flagged as
  system or kernel in the last snapshot.

The kernel is expected to enforce privilege again (see the proposal). Every
request and its outcome is logged with `nx_debug_print`.

## Data and refresh

- The view samples about once a second, using the `nx_gettime` deadline
  `TM_REFRESH_MS`. It never samples every frame. Between samples, frames only
  animate: the charts slide smoothly, and the sheet and popovers move.
- CPU % is computed in `nxproc` from the difference between two samples of
  cumulative CPU time, matched by pid and start time so a reused pid never
  inherits old time. The first sample reports 0 %.
- All storage is static and bounded: 256 processes (the kernel's
  `MAX_PROCESSES`), 16 CPUs, 62 history points and 32 startup items. Nothing
  is allocated.

## Real hardware today

| Shown | Source |
|-------|--------|
| Memory tile, memory chart, uptime | `SYS_INFO` (50), real |
| Apps tab, Switch To, End Task on windows | Desktop window list, real |
| Process table | "Process information unavailable" state |
| CPU tile and chart, per-core bars | "Not reported by this kernel yet" |
| Startup | "Startup items unavailable" state |
| Priority, End Task on processes | Disabled, because no process can be selected |

Each unsupported operation is logged once:

```
[NXPROC] unsupported by this kernel (no userspace syscall): process list (proposed proc.list)
```

The view never shows a number the kernel did not give it. For example, the
processor count shows "—" rather than an assumed 1.

## Host preview

`NX_HOST_PREVIEW` builds use a deterministic demo backend inside `nxproc.c`:
24 processes on 8 cores, with smoothly varying load and one process
("build-nxfs") that ignores a graceful quit, so it exercises the force path.
When the view opens, it fast-forwards the demo clock 60 seconds so the charts
start full. The footer says "Preview demo data". None of this code exists in a
NeolyxOS build.

To preview the honest hardware states, also define `NXPROC_NO_DEMO`. The real
backend then runs against the host harness, which answers ENOSYS:

```
make -C desktop/tools/preview OBJ=out/nodemo CFLAGS="<Makefile CFLAGS> -DNXPROC_NO_DEMO"
```

Script helper: `call taskmgr_preview_light` switches to the light appearance.

## Keyboard

| Key | Action |
|-----|--------|
| Tab / Shift+Tab | Next / previous tab |
| Up, Down, Page Up, Page Down, Home, End | Move selection |
| Delete | End Task (opens the sheet) |
| Enter | Apps tab: switch to the window. Sheet: confirm. |
| Esc | Close the sheet or popover, clear the search |
| Ctrl+F, or start typing | Search by name, or by exact PID if only digits are typed |

The global Ctrl+Shift+Esc shortcut belongs in `desktop_shell.c` and is not
wired by this view.
