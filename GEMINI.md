# GEMINI.md - NeolyxOS AI Developer Guide

**Project:** NeolyxOS - Custom 64-bit Operating System  
**Status:** Desktop 70% (Rendering Gaps) | Networking 60% | USB/Mouse ✅ | ~50% Complete  
**Last Updated:** January 21, 2026

---

## 📊 Project Summary

NeolyxOS is a custom 64-bit operating system with:

- **2.9MB+ kernel source** across 340+ files
- **2050+ desktop files** including 16 native applications
- **UEFI bootloader** with recovery menu
- **Userspace desktop** running in Ring 3
- **NXRender** custom GUI rendering engine
- **NXFS** native filesystem with AES-256 encryption

---

## 🏗️ Codebase Structure

```
neolyx-os/
├── kernel/                    # 387 files - Core OS kernel
│   ├── kernel_main.c          # Main entry (800 lines)
│   ├── Makefile               # Build system
│   ├── include/               # 133 headers
│   │   ├── arch/              # x86_64, aarch64 headers
│   │   ├── core/              # Process, syscall, boot headers
│   │   ├── drivers/           # 40+ driver headers
│   │   ├── fs/                # Filesystem headers
│   │   ├── net/               # Network headers
│   │   └── security/          # Security headers
│   └── src/                   # Implementation
│       ├── arch/              # Architecture code (GDT, IDT, paging)
│       ├── core/              # Core kernel (37 files)
│       │   ├── process.c      # Process management (23KB)
│       │   ├── syscall.c      # 120+ syscalls (38KB)
│       │   ├── boot_guard.c   # Boot protection (10KB)
│       │   ├── session_manager.c  # Desktop crash isolation
│       │   └── installer.c    # OS installer (79KB)
│       ├── drivers/           # 78 driver files
│       │   ├── acpi/          # ACPI driver (18 files)
│       │   ├── usb/           # USB stack
│       │   ├── storage/       # AHCI, NVMe
│       │   ├── graphics/      # nxgfx, nxrender kernel drivers
│       │   └── network/       # E1000, WiFi drivers
│       ├── fs/                # 32 filesystem files
│       │   ├── nxfs.c         # NXFS implementation (54KB)
│       │   ├── fat.c          # FAT32 support (42KB)
│       │   ├── vfs.c          # VFS layer (15KB)
│       │   └── ramfs.c        # In-memory FS (15KB)
│       ├── net/               # Network stack
│       │   ├── network.c      # Core networking (16KB)
│       │   ├── tcpip.c        # TCP/IP stack (29KB)
│       │   └── netinfra.c     # Network infrastructure (18KB)
│       └── security/          # 11 security files
│           ├── firewall.c     # Packet filtering
│           ├── app_sandbox.c  # App isolation
│           └── crypto.c       # AES-256, hashing
│
├── boot/                      # UEFI bootloader (23 files)
│   ├── main.c                 # Bootloader entry (24KB)
│   ├── boot_menu.c            # Recovery menu
│   ├── graphics.c             # Boot graphics
│   └── BOOTX64.EFI            # Compiled bootloader
│
├── desktop/                   # 2050 files - Desktop environment
│   ├── shell/                 # Desktop shell (28 files)
│   │   ├── desktop_shell.c    # Main shell (75KB)
│   │   ├── compositor.c       # Window compositing (15KB)
│   │   ├── dock.c             # macOS-style dock (19KB)
│   │   ├── window_manager.c   # Window management (20KB)
│   │   ├── anveshan.c         # Spotlight-like search (26KB)
│   │   ├── control_center.c   # System controls (21KB)
│   │   ├── notification_center.c  # Notifications
│   │   └── lock_screen.c      # Lock screen
│   ├── apps/                  # 16 native applications
│   │   ├── Settings.app/      # 58 files, 25+ panels
│   │   ├── Terminal.app/      # Terminal emulator
│   │   ├── Calculator.app/    # Calculator
│   │   ├── Calendar.app/      # Calendar with dark/light themes
│   │   ├── Path.app/          # File manager (58 files)
│   │   ├── IconLay.app/       # Icon designer (39 files)
│   │   ├── Rivee.app/         # Media app
│   │   ├── RoleCut.app/       # Video editing
│   │   ├── reolab.app/        # IDE (72 files)
│   │   └── zeprabrowser/      # Full web browser (1162 files)
│   ├── include/               # 21 desktop headers
│   │   ├── desktop_shell.h
│   │   ├── widgets.h
│   │   ├── window_manager.h
│   │   └── nxsyscall.h        # Desktop syscall interface
│   └── lib/                   # Desktop libraries
│       ├── nxi_render.c       # NXI icon rendering
│       ├── nxconfig.c         # Configuration system
│       └── nxevent.c          # Event handling
│
├── gui/                       # GUI rendering libraries
│   └── nxrender_c/            # NXRender C library (70 files)
│       ├── include/           # 32 headers
│       │   ├── widgets/       # Button, label, textfield, etc.
│       │   ├── nxrender/      # Compositor, window
│       │   └── nxgfx/         # Graphics primitives
│       └── src/               # 32 source files
│
├── nxaudio/                   # Audio library (46 files)
│   ├── src/                   # 33 source files
│   └── include/               # Audio API headers
│
├── lib/                       # System libraries
│   ├── zeprascript/           # JavaScript engine (10 files)
│   ├── nxzip/                 # Compression library (7 files)
│   └── vulkan/                # Vulkan bindings
│
├── docs/                      # Documentation (71 files)
│   ├── architecture/          # Architecture docs
│   ├── drivers/               # 15 driver docs
│   ├── kernel/                # Kernel internals
│   ├── network/               # Networking docs
│   ├── reox/                  # REOX language docs
│   └── security/              # Security documentation
│
├── tests/                     # Test suite
│   ├── kernel_tests.c         # Kernel unit tests
│   ├── run_tests.sh           # Test runner
│   └── userland/              # Userland tests (14 files)
│
├── services/                  # System services
│   ├── nxaudio-server/        # Audio daemon
│   ├── compositor/            # Compositor service
│   └── input/                 # Input service
│
└── config/                    # System configuration (8 files)
```

---

## 🚀 Current Status by Phase

### ✅ Phase 1-4: Kernel Core - COMPLETE

- UEFI bootloader (v8.0) with recovery menu
- 64-bit long mode, GDT/IDT/TSS
- PMM, VMM, kernel heap
- FPU/SSE initialization
- PIC remapped, interrupts enabled
- Process management (fork/exec/wait)
- Round-robin scheduler with preemption

### ✅ Phase 5: Drivers - COMPLETE

**14 Kernel Drivers Loaded:**

- nxgfx_kdrv (GPU/framebuffer)
- nxstor_kdrv (storage)
- nxdisplay_kdrv (display management)
- nxsysinfo_kdrv (system information)
- nxpci_kdrv (PCI bus)
- nxusb_kdrv (USB stack)
- nxtask_kdrv (task management)
- nxrender_kdrv (rendering)
- nxgame (game bridge)
- nxmouse_kdrv (mouse input)
- E1000 (Intel NIC)
- WiFi driver
- Bluetooth driver
- AHCI/NVMe storage

### ✅ Phase 6: Filesystem - COMPLETE

- **NXFS** (54KB) with AES-256 encryption
- **VFS** layer (15KB) with mount points
- **FAT32** support (42KB) for ESP
- **Block cache** with LRU
- **RamFS** for embedded binaries

### ✅ Phase 7-8: Terminal & Tests - COMPLETE

- 25+ shell commands
- Keyboard event queue (IRQ-driven)
- Framebuffer console
- Memory/disk/keyboard stress tests

### ✅ Phase 9-12: Stability & Security - COMPLETE

- Boot Guard (failure tracking, recovery)
- Session Manager (crash isolation)
- NXGame Bridge (direct GPU for games)
- System Integrity Protection
- Capability-based security
- Safe memory operations
- Panic handler with stack canaries

### ✅ Phase 13-15: Storage & Userspace - COMPLETE

- AHCI driver with real hardware access
- GPT/MBR partition parsing
- VFS syscalls (open, read, write, close, stat)
- ELF loader with Ring 3 transition
- Userland shell (nxsh)
- Init process (PID 1)

### ✅ Phase 16: System Security - COMPLETE

- Firewall with packet filtering
- App sandbox with capabilities
- System lock with PIN
- Security audit logging
- Security scanner for file verification

### 🔄 Phase 17: Networking - IN PROGRESS

- ✅ Network/TCP/IP initialization on boot
- ✅ Socket syscalls (40-47)
- ✅ DHCP client
- ✅ Terminal commands: net status, ping, dhcp
- ⏳ DNS resolver

### ✅ Phase 18: Applications - COMPLETE

**Settings.app** with 25 panels:

- System, About, Network, Bluetooth
- Display, Display Manager, Sound, Storage
- Keyboard, Trackpad
- Appearance, Color
- Accounts, Security, Privacy
- Apps, Devices, Power, Processes
- Startup, Extensions, Scheduler
- Updates, Bootloader, Developer

**Other Apps:**

- Terminal.app, Calculator.app, Calendar.app
- Path.app (file manager), IconLay.app (icon designer)
- zeprabrowser (full web browser)

---

## 🎯 Boot Flow

```
POWER ON
├── UEFI FIRMWARE
│   └── Load EFI/BOOT/BOOTX64.EFI
│
├── NEOLYXOS BOOTLOADER (boot/main.c)
│   ├── GOP Framebuffer Init
│   ├── Build NeolyxBootInfo structure
│   ├── Load kernel.bin
│   └── Jump to kernel_main()
│
├── KERNEL EARLY BOOT (kernel_main.c)
│   ├── Boot Guard initialization
│   ├── GDT/IDT with TSS
│   ├── FPU/SSE enable
│   ├── PMM/VMM/Heap init
│   ├── PIT timer (1000Hz)
│   ├── RTC (real-time clock)
│   └── Enable interrupts
│
├── DRIVER LOADING
│   ├── Process/Scheduler init
│   ├── Graphics drivers
│   ├── Storage drivers (AHCI/NVMe)
│   ├── Network stack + TCP/IP
│   ├── USB driver
│   ├── Session Manager
│   └── SIP (System Integrity Protection)
│
├── RECOVERY MENU CHECK
│   └── F2 pressed? → Show recovery options
│
├── BOOT SOURCE DETECTION
│   ├── NXFS found? → Normal boot
│   └── No NXFS? → Disk Utility
│
└── DESKTOP LAUNCH
    ├── ELF load /ramfs/bin/desktop
    ├── Create user process (PID)
    ├── Map userspace stack (0x500000)
    ├── Set TSS kernel stack
    └── enter_user_mode() → Ring 3
```

---

## 📁 Critical Files Reference

| Component         | File                               | Size | Description        |
| ----------------- | ---------------------------------- | ---- | ------------------ |
| **Kernel Entry**  | `kernel/kernel_main.c`             | 28KB | Main boot sequence |
| **Process**       | `kernel/src/core/process.c`        | 23KB | Process management |
| **Syscalls**      | `kernel/src/core/syscall.c`        | 38KB | 120+ system calls  |
| **Installer**     | `kernel/src/core/installer.c`      | 79KB | OS installer       |
| **NXFS**          | `kernel/src/fs/nxfs.c`             | 54KB | Native filesystem  |
| **TCP/IP**        | `kernel/src/net/tcpip.c`           | 29KB | Network stack      |
| **Desktop Shell** | `desktop/shell/desktop_shell.c`    | 75KB | Main desktop       |
| **Settings**      | `desktop/apps/Settings.app/main.c` | 39KB | Settings app       |
| **Bootloader**    | `boot/main.c`                      | 24KB | UEFI bootloader    |

---

## 🔧 Build Commands

### Full Build

```bash
cd /home/swana/Documents/NEOLYXOS/neolyx-os
./build_all.sh
```

### Quick Test

```bash
./boot_test.sh
```

### Kernel Only

```bash
cd kernel && make
```

### Run in QEMU

```bash
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
  -drive file=neolyx.img,format=raw -m 512M -serial stdio
```

---

## 🔴 Known Gaps vs Production OSes

| Feature           | Status     | Notes                                    |
| ----------------- | ---------- | ---------------------------------------- |
| Multi-core (SMP)  | 🔴 Missing | Single-core only                         |
| USB Mass Storage  | 🔴 Missing | USB init exists, mass storage incomplete |
| Real GPU Drivers  | 🔴 Missing | Framebuffer only                         |
| Real Audio Output | 🔴 Missing | Audio stubs exist                        |
| WiFi              | 🔴 Missing | Driver exists, no real hardware support  |
| Package Manager   | 🔴 Missing | Not implemented                          |
| Self-Hosting      | 🔴 Missing | Cannot compile itself                    |

---

## 🛣️ Next Steps

### Priority 1: Networking Completion

- [ ] DNS resolver implementation
- [ ] HTTP client for basic web requests

### Priority 2: Real Hardware Testing

- [ ] Test on real x86_64 hardware
- [ ] Debug any QEMU-hidden issues

### Priority 3: SMP Support

- [ ] AP startup code
- [ ] Per-CPU structures
- [ ] Multi-core scheduler

### Priority 4: USB Mass Storage

- [ ] XHCI controller driver
- [ ] USB storage class driver

---

## 📊 Progress Meter

```
KERNEL BASICS      [████████████████████] 100%
DRIVERS (BASIC)    [████████████░░░░░░░░]  60%
FILESYSTEM         [██████████████░░░░░░]  70%
PROCESS MGMT       [████████████████░░░░]  80%
DESKTOP            [████████████████████] 100%
NETWORKING         [████████████░░░░░░░░]  60%
SECURITY           [████████████████░░░░]  80%
APPS               [████████████████░░░░]  80%
USB                [████░░░░░░░░░░░░░░░░]  20%
SMP                [░░░░░░░░░░░░░░░░░░░░]   0%
---------------------------------------------------------
OVERALL COMPLETENESS: ~50% of production OS requirements
```

---

## 🏗️ Architecture Diagram

```
┌───────────────────────────────────────────────────────────┐
│                    USER SPACE (Ring 3)                     │
│  ┌────────────┬────────────┬────────────┬────────────────┐│
│  │  Settings  │  Terminal  │ Calculator │  zeprabrowser  ││
│  │    .app    │    .app    │    .app    │   (1162 files) ││
│  └────────────┴────────────┴────────────┴────────────────┘│
│  ┌────────────────────────────────────────────────────────┐│
│  │              Desktop Shell (desktop_shell.c)           ││
│  │  Window Manager │ Compositor │ Dock │ Control Center   ││
│  └────────────────────────────────────────────────────────┘│
│  ┌────────────────────────────────────────────────────────┐│
│  │              NXRender + NXGFX Engine                   ││
│  │     Widgets │ Animation │ Theme │ Event System         ││
│  └────────────────────────────────────────────────────────┘│
├───────────────────────────────────────────────────────────┤
│                    SYSCALL INTERFACE                       │
│         120+ syscalls  │  VFS ops  │  Graphics ops         │
├───────────────────────────────────────────────────────────┤
│                    KERNEL SPACE (Ring 0)                   │
│  ┌────────────┬─────────────┬────────────┬───────────────┐│
│  │   Core     │   Drivers   │ Filesystem │    Network    ││
│  │ GDT/IDT    │ nxgfx_kdrv  │   NXFS     │   TCP/IP      ││
│  │ PMM/VMM    │ nxmouse     │   FAT32    │   E1000       ││
│  │ Scheduler  │ AHCI/NVMe   │   VFS      │   WiFi        ││
│  └────────────┴─────────────┴────────────┴───────────────┘│
│  ┌────────────────────────────────────────────────────────┐│
│  │                     Security Layer                      ││
│  │  Firewall │ App Sandbox │ SIP │ Audit │ Crypto         ││
│  └────────────────────────────────────────────────────────┘│
├───────────────────────────────────────────────────────────┤
│                      UEFI FIRMWARE                         │
└───────────────────────────────────────────────────────────┘
```

---

## 📝 Development Guidelines

1. **Kernel Code**: Minimal, no dynamic allocation in hot paths
2. **Desktop Code**: Lives in `desktop/`, runs in Ring 3
3. **Drivers**: Use `kdrv` suffix, init in kernel_main.c
4. **Apps**: Use `.app` bundle format in `desktop/apps/`
5. **Headers**: Kernel headers in `kernel/include/`, desktop in `desktop/include/`
6. **Tests**: Add to `tests/` directory
7. **Documentation**: Markdown in `docs/`

---

**Copyright (c) 2025-2026 KetiveeAI**
