# NeolyxOS Project Structure

**Last Updated:** January 21, 2026

## Directory Overview

| Directory   | Files | Purpose                       |
| ----------- | ----- | ----------------------------- |
| `kernel/`   | 387   | Core OS kernel                |
| `desktop/`  | 2050  | Desktop environment + 16 apps |
| `boot/`     | 23    | UEFI bootloader               |
| `gui/`      | 76    | NXRender graphics library     |
| `nxaudio/`  | 46    | Audio library                 |
| `docs/`     | 71    | Documentation                 |
| `lib/`      | 21    | System libraries              |
| `tests/`    | 17    | Test suite                    |
| `services/` | 5     | System services               |
| `config/`   | 8     | Configuration files           |

## Kernel (`kernel/`)

```
kernel/
├── kernel_main.c          # Boot sequence (800 lines)
├── Makefile               # Build system
├── include/               # 133 header files
│   ├── arch/              # x86_64, aarch64
│   ├── core/              # Process, syscall, boot
│   ├── drivers/           # 40+ driver headers
│   ├── fs/                # Filesystem headers
│   ├── net/               # Network headers
│   └── security/          # Security headers
└── src/                   # 230 source files
    ├── arch/              # GDT, IDT, paging
    ├── core/              # 37 core files
    ├── drivers/           # 78 driver files
    ├── fs/                # 32 filesystem files
    ├── net/               # Network stack
    └── security/          # 11 security files
```

## Desktop (`desktop/`)

```
desktop/
├── shell/                 # 28 shell files
│   ├── desktop_shell.c    # Main desktop (75KB)
│   ├── compositor.c       # Window compositing
│   ├── dock.c             # macOS-style dock
│   └── window_manager.c   # Window management
├── apps/                  # 16 applications
│   ├── Settings.app/      # 58 files
│   ├── Terminal.app/
│   ├── Calculator.app/
│   ├── Calendar.app/
│   ├── Path.app/          # File manager
│   ├── IconLay.app/       # Icon designer
│   └── zeprabrowser/      # Browser (1162 files)
├── include/               # 21 headers
└── lib/                   # Libraries
```

## Boot (`boot/`)

```
boot/
├── main.c                 # Bootloader entry (24KB)
├── boot_menu.c            # Recovery menu
├── graphics.c             # Boot graphics
├── config.c               # Boot configuration
└── BOOTX64.EFI            # Compiled bootloader
```

## GUI (`gui/`)

```
gui/
└── nxrender_c/            # C rendering library
    ├── include/           # 32 headers
    │   ├── widgets/       # UI widgets
    │   ├── nxrender/      # Compositor
    │   └── nxgfx/         # Graphics primitives
    ├── src/               # 32 source files
    └── libnxrender.a      # Static library
```

## Key Files Quick Reference

| Purpose       | File                               |
| ------------- | ---------------------------------- |
| Kernel Entry  | `kernel/kernel_main.c`             |
| Syscalls      | `kernel/src/core/syscall.c`        |
| Process Mgmt  | `kernel/src/core/process.c`        |
| NXFS          | `kernel/src/fs/nxfs.c`             |
| TCP/IP        | `kernel/src/net/tcpip.c`           |
| Desktop Shell | `desktop/shell/desktop_shell.c`    |
| Bootloader    | `boot/main.c`                      |
| Settings App  | `desktop/apps/Settings.app/main.c` |

## Build Output

```
kernel/kernel.bin          # Compiled kernel
boot/BOOTX64.EFI           # UEFI bootloader
neolyx.img                 # Bootable disk image
```
