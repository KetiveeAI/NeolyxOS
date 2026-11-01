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
| Shared memory | Missing | No syscalls. |
| IPC | Missing | No pipes, ports or channels. PTY syscalls 60 to 67 are declared in `syscall.h` but not registered. |
| File descriptors | Partial, unsafe | One global table of 16 entries in `syscall.c`, shared by every process. |
| Framebuffer syscalls | Partial | `fb.map` gives any user process the raw framebuffer. `fb.flip` copies the full frame. No vsync. |
| Input syscalls | Partial | One global event queue: the first process to poll takes each event. `input.register` is a no-op. |
| Syscall dispatch, ABI query, filter, audit | Real | `nx_syscall.c`, `syscall_table.def`. |

## Drivers

| Area | State | What the code shows |
| --- | --- | --- |
| PS/2 keyboard and mouse | Real | IRQ1 and IRQ12, press/release event queue. |
| USB | Partial | xHCI controllers and devices enumerate. No USB HID keyboard or mouse input, so USB-only machines have no input. |
| Storage | Partial | AHCI, ATA and NVMe driver files present. The desktop reads and writes the FAT boot volume, including delete and rename. |
| Network | Partial | e1000, kernel TCP/IP, socket syscalls. No TLS. |
| GPU (`nxgfx_kdrv`) | Mock | Detects Intel, AMD, NVIDIA and Bochs by PCI ID. `nxgfx_kdrv_flip` and `nxgfx_kdrv_wait_vblank` return 0 and do nothing. No modesetting driver. Display is the UEFI GOP framebuffer only. |
| `nxrender_kdrv` | Mock | Initialised at boot, never called. |
| NXGame syscalls 110 to 119 | Mock | Registered, but `gpu_present` calls the no-op flip. Sessions live in one global table of 4, not per process. |
| Wi-Fi, Bluetooth, camera, sensors, biometrics | Not verified | Driver files exist; not exercised by any test or app. |

## Graphics and desktop

| Area | State | What the code shows |
| --- | --- | --- |
| Desktop shell | Real | One userspace process (`desktop/`) draws everything with NXCanvas on the CPU: menu bar, dock, windows, overlays. |
| Shell-hosted apps | Real | Settings, Path, Shell, Task Manager, Fonts and Setup are views inside the desktop process (`desktop/shell/views/`). |
| Window manager | Real, single process | Windows, z-order, snap, Overview, Stage, switcher, all inside the desktop process. |
| nxrender (`gui/nxrender_c`) | Not used | About 12k lines. `desktop/shell/nxrender_bridge.c` has empty draw functions. `nxserver_compositor.c` is designed for shared-memory windows but there is no kernel shared memory. `gpu_driver.c` reports a fake GPU and needs hosted libc. |
| Out-of-process GUI apps | Missing | No way for a second process to get a window: no shared memory, no IPC, single input queue. |
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
