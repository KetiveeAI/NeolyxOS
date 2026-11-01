# NeolyxOS Status

**Last audited:** 2026-11-01, against `main` at 4406591 (PR #17).

This file is the single source of truth for what is finished. Every row was
checked against the code, not against older notes. When a feature lands,
update its row in the same pull request. Older status pages
(`docs/project_status.html`, `gui/nxrender_c/TODO.md`) are kept for history
only.

Legend:

- **Real**: works end to end in QEMU/OVMF.
- **Partial**: works, with the limits listed.
- **Mock**: the API exists but does nothing real underneath.
- **Missing**: not implemented.

None of the rows below has been verified on real hardware yet.

## Kernel

| Area | State | What the code shows |
| --- | --- | --- |
| UEFI loader, boot | Real | Boots to the desktop in QEMU/OVMF. The desktop is loaded from `\EFI\BOOT\desktop.elf` (`kernel/src/core/boot_image.c`). |
| Processes | Partial | Per-process address spaces; `spawn`, `exec`, `fork`, `wait`, `exit`. No threads. |
| Scheduler | Partial | Round-robin; the timer preempts ring 3 only, kernel code is not preemptible. One CPU, no SMP. |
| Memory syscalls | Partial | Only `brk` is registered. `mmap`/`munmap` are declared in `syscall.h` but are not in `syscall_table.def`, so they return `ENOSYS`. |
| Shared memory | Partial | Handles (`handle.close` 140, `handle.dup` 149) and `shm.create`/`shm.map`/`shm.unmap` (141 to 143) per `docs/desktop/proposals/kernel-shm-ipc.md`, mapped in a per-process shared region at `0x4000_0000_0000`. Pages are zeroed, returned to the PMM when the last handle and mapping go, and released on exit, kill, fault and `exec`. Verified by `tests/userland/shmtest`. Handles cross processes over channels (see IPC). |
| IPC | Working | Channels (`chan.create`/`chan.send`/`chan.recv` 144 to 146), `obj.wait` (147) and named ports (`port.listen`/`port.connect`/`port.accept` 150 to 152) per `docs/desktop/proposals/kernel-shm-ipc.md`; ABI 1.1 with `NX_ABI_FLAG_IPC`. Messages of up to 1 KiB carry up to 4 handles; sends never block; `nx.*` port names are reserved for system processes. Verified by `tests/userland/ipc_test` (two processes share a 4 MiB buffer, the client crashes, all memory returns). No pipes; PTY syscalls 60 to 67 are declared in `syscall.h` but not registered. |
| File descriptors | Partial | Per process: 16 descriptors in each PCB (`fd_table`), kept across `exec`, closed when the process exits, is killed or faults. Socket numbers are still one global table in the network stack, shared by every process. Verified by `tests/userland/fdtest`. |
| Framebuffer syscalls | Partial | `fb.map` gives any user process the raw framebuffer. `fb.flip` copies the full frame. `fb.flip_rects` (148, ABI 1.2) copies only given rectangles and is limited to system processes; the desktop does not use it yet (week 3, step 3). Verified by `tests/userland/fbtest` and `tests/host/nx_fb_clip_test.c`. No vsync. |
| Input syscalls | Partial | One global event queue: the first process to poll takes each event. `input.register` is a no-op. |
| Syscall dispatch, ABI query, filter, audit | Real | `nx_syscall.c`, `syscall_table.def`. |
| Boot log and diagnostics | Real | All kernel log output is kept in a 64 KiB ring (`kernel/src/video/bootlog.c`). A kernel exception or panic draws the last lines on screen. With `\EFI\BOOT\diagnose.flg` on the boot volume the loader shows firmware facts, the kernel shows its log and a boot summary on screen, and every disk driver refuses writes (`write_guard.c`). Hardware test procedure: `docs/hardware/smoke-test.md`. Host test `tests/host/bootlog_test.c`. |

## Drivers

| Area | State | What the code shows |
| --- | --- | --- |
| PS/2 keyboard and mouse | Real | IRQ1 and IRQ12, press/release event queue. The controller is detected first (`ps2_controller_present`); without one the kernel logs that the machine has no input. |
| USB | Partial | xHCI controllers are found. The driver leaves a controller alone when the firmware still owns it, its registers are above 4 GB, or it needs scratchpad buffers (the QEMU q35 xHCI is above 4 GB, so it is left alone). No USB HID and no USB storage, so USB-only machines have no input and a USB boot stick is not readable by the kernel. |
| Storage | Partial | AHCI, ATA and NVMe driver files present. The desktop reads and writes the FAT boot volume, including delete and rename. |
| Network | Partial | e1000, kernel TCP/IP, socket syscalls. No TLS. |
| GPU (`nxgfx_kdrv`) | Mock | Detects Intel, AMD, NVIDIA and Bochs by PCI ID. `nxgfx_kdrv_flip` and `nxgfx_kdrv_wait_vblank` return 0 and do nothing. No modesetting driver. Display is the UEFI GOP framebuffer only. |
| `nxrender_kdrv` | Mock | Initialised at boot, never called. |
| NXGame syscalls 110 to 119 | Mock | Registered, but `gpu_present` calls the no-op flip. Sessions live in one global table of 4, not per process. |
| Wi-Fi, Bluetooth, camera, sensors, biometrics | Not verified | Driver files exist; not exercised by any test or app. |

## Graphics and desktop

| Area | State | What the code shows |
| --- | --- | --- |
| Desktop shell | Real | One userspace process (`desktop/`) draws everything with NXCanvas on the CPU: menu bar, dock, windows, overlays. Frames are damage-driven: an idle desktop draws only the clock and menu bar once a second (about 5 of every 300 ticks), a client window's commit redraws only its damaged rectangles, and the screen is updated with `fb.flip_rects`; any shell activity still redraws the whole screen. A debug build (`make EXTRA_CFLAGS=-DDESKTOP_DAMAGE_VERIFY`) checks every frame against a full redraw. Its heap (`desktop/lib/nx_malloc.c`) reuses and merges freed blocks and the panel cache is capped at 16 MiB, so memory stays flat across app launches (host test `tests/host/nx_malloc_test.c`). |
| Shell-hosted apps | Real | Settings, Path, Shell, Task Manager, Fonts and Setup are views inside the desktop process (`desktop/shell/views/`). |
| Window manager | Real, single process | Windows, z-order, snap, Overview, Stage, switcher, all inside the desktop process. App windows from other processes are ordinary shell windows (same decorations, tiling, minimize, Overview). |
| nxrender (`gui/nxrender_c`) | Not used | About 12k lines. `desktop/shell/nxrender_bridge.c` has empty draw functions. `nxserver_compositor.c` is a sketch and is not used: the window server was built inside the desktop instead (`desktop/shell/nxserver`). `gpu_driver.c` reports a fake GPU and needs hosted libc. |
| Out-of-process GUI apps | Partial | The desktop's window server (`desktop/shell/nxserver`) listens on port `nx.display` and speaks window protocol 1.0 (`sdk/nxwindow/nxwin_proto.h`, spec `docs/desktop/proposals/nxserver-window-protocol.md`): a separate process opens a window, draws into shared memory that the server maps read-only and draws without a copy, and gets keys, pointer, `CONFIGURE` and `CLOSE_REQUEST`. Verified by `tests/userland/wintest` (hostile cases, crash cleanup, a live window) and `tests/host/nxw_check_test.c`. Commits redraw only their damaged rectangles (host test `tests/host/nxs_damage_test.c`); `libnxwindow` is week 4; the built-in apps still run inside the desktop (week 5). |
| GPU acceleration | Missing | `gpu.c` and `nxgfx_accel.c` are CPU loops. |
| Vulkan (`lib/vulkan`) | Missing | Future work. |

## Apps and SDK

| Area | State | What the code shows |
| --- | --- | --- |
| `desktop/apps/*.app` | Not shipped | 14 app folders with Makefiles. None is installed in the disk image; `build_all.sh` installs only `tests/userland/ticker`. |
| REOX SDK (`sdk/`) | Partial | `reoxc --target neolyx` builds freestanding ELFs (Calculator). The NxUI host maps the whole framebuffer and polls global input, so a REOX app would fight the desktop for the screen until a window server exists. |
| Zepra browser | Not in this repository | Separate project; `kernel/browser_session.c` and `nxrt` reference it. No native NeolyxOS build. |

## Next

The plan to make nxrender the real window server (shared memory, IPC,
client surfaces, `libnxwindow`, apps out of process, games, virtio-gpu) is
under review with the project owner. Kernel ABI changes in that plan need
the owner's sign-off before any code lands.
