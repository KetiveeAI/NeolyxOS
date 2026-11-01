# NeolyxOS TODO

**Last updated:** 2026-11-01

What is finished today is recorded in [docs/STATUS.md](docs/STATUS.md),
checked against the code. This file lists open work only. The previous
version of this file (June 12, 2026) marked several items complete that the
code does not support (for example "Kernel core 100%", "USB 100%" and "60 FPS
with vsync"); it is in git history.

## Release blockers: make nxrender the real window server

Kernel items need the project owner's sign-off before code lands
(see CLAUDE.md, public API changes).

- [ ] Per-process file descriptor table (today: one global table of 16)
- [ ] Anonymous `mmap` / `munmap` (declared, not registered)
- [ ] Restrict `fb.map`, `fb.flip`, `cursor.set`, `input.poll` to the display server
- [ ] Shared memory objects (create, map, unmap, close)
- [ ] IPC channels that can carry handles, with a multi-handle wait
- [ ] Desktop becomes nxserver: client surfaces, z-order, damage, input routing
- [ ] `libnxwindow` client library (C) and an NxUI backend for REOX on it
- [ ] Ship apps in the disk image and run them out of process
- [ ] Damage-only composite and a rect-flip syscall
- [ ] NXGame rebuilt on surfaces, sessions owned per process, real present
- [ ] virtio-gpu 2D driver with hardware cursor
- [ ] USB HID keyboard and mouse
- [ ] Bring-up on two reference machines

## After the release

- [ ] Native GPU drivers (Intel first)
- [ ] 3D / Vulkan for games
- [ ] SMP and threads
- [ ] TLS in the network stack (needed by Zepra for HTTPS)

**Copyright (c) 2025-2026 KetiveeAI**
