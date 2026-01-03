# Neolyx OS – Project Structure

## Top-Level Layout

```
neolyx-os/
├── boot/             # UEFI bootloader (C, UEFI spec)
├── kernel/           # 64-bit C kernel (core, memory, scheduler, syscalls)
├── drivers/          # Device drivers (GPU, input, storage, network)
├── fs/               # NXFS file system implementation
├── net/              # Network stack and manager
├── gui/              # Graphics subsystem, compositor, window manager
├── apps/             # System apps (file explorer, terminal, etc.)
├── pkg/              # nxpkg package manager, .app installer
├── icons/            # Custom icon sets
├── include/          # Shared headers (kernel, drivers, fs, etc.)
├── build/            # Build artifacts (binaries, ISOs)
├── tools/            # Build scripts, utilities
├── docs/             # Design docs, API, architecture
└── tests/            # Unit and integration tests
```

## Component Details

- **boot/**: UEFI bootloader in C, loads kernel in 64-bit long mode.
- **kernel/**: Core OS logic (init, memory, multitasking, syscalls, user mgmt).
- **drivers/**: Modular drivers for GPU (NVIDIA, Intel, AMD), input, storage, network.
- **fs/**: NXFS file system (format, mount, permissions, API).
- **net/**: TCP/IP stack, DHCP, DNS, network manager UI.
- **gui/**: Framebuffer, compositor, window manager, widgets, file explorer, icons.
- **apps/**: System apps (file explorer, terminal, settings, installer, etc.).
- **pkg/**: nxpkg manager, .app bundle support, app sandboxing.
- **icons/**: Custom icon themes for GUI and file explorer.
- **include/**: Shared C headers for kernel, drivers, fs, etc.
- **build/**: Output binaries, ISOs, logs.
- **tools/**: Scripts for building, flashing, debugging.
- **docs/**: Architecture, API, design, and developer guides.
- **tests/**: Automated tests for all major subsystems.

## Build System
- Use Makefile or CMake for cross-platform builds.
- Support for x86_64-elf-gcc toolchain.
- Scripts for UEFI image creation and QEMU/VM testing.

## Next Steps
- Scaffold folders and minimal README in each.
- Begin with UEFI bootloader and kernel entry in C.
- Add build scripts and documentation templates. 