# GEMINI.md - NeolyxOS AI Developer Guide

**Project:** NeolyxOS - Custom 64-bit Operating System  
**Status:** Kernel Boots ✅ | Architecture Needs Fix 🔴 | ~35% Complete  
**Last Updated:** December 27, 2025

---

## ⚠️ CRITICAL: Architecture Issue

**Desktop GUI code is currently INSIDE the kernel - this violates OS design principles!**

Real operating systems (macOS, Linux, Windows) run their desktop in **userspace**:
- macOS: WindowServer is a userspace daemon
- Linux: GNOME Shell / KDE run in userspace  
- Windows: Explorer.exe is a userspace process

**Current Problem:**
- `kernel/src/ui/desktop.c` (599 lines of GUI code in kernel)
- Should use: `desktop/shell/desktop_shell.c` (already exists, correct location)

**Fix Required:** Add framebuffer syscalls, move desktop to userspace process.

---

## 📊 Honest Reality Check (vs macOS/Windows/Linux)

| Feature | Real OSes | NeolyxOS | Status |
|---------|-----------|----------|--------|
| Multi-core (SMP) | Full | None | 🔴 Missing |
| USB Stack | Full | Stub | 🔴 Missing |
| GPU Drivers | Metal/DX/Mesa | Framebuffer | 🔴 Missing |
| Audio | Full audio stack | Stub | 🔴 Missing |
| WiFi/Bluetooth | Full | None | 🔴 Missing |
| Real App Loading | Works | Demo only | 🟡 Partial |
| Kernel/Userspace Split | Proper | Desktop in kernel! | 🔴 Wrong |

**Overall Progress: ~35% of a production OS**

---

## 🚀 Current Status

### ✅ Phase 1-4: Kernel Core - COMPLETE
- UEFI bootloader (v8.0) working
- 64-bit long mode, GDT/IDT/TSS
- PMM, VMM, kernel heap
- PIC remapped, interrupts enabled
- Process management (fork/exec/wait)
- Round-robin scheduler with preemption

### ✅ Phase 5: Drivers - COMPLETE
10 kernel drivers: nxgfx, nxaudio, nxnet, nxstor, nxusb, nxpci, nxsysinfo, nxdisplay, nxpen, nxtask

### ✅ Phase 6: Filesystem - COMPLETE
- NXFS (501 lines) with AES-256 encryption
- VFS layer (436 lines)
- Block cache with LRU (300+ lines)

### ✅ Phase 7: Terminal Shell - COMPLETE
- 25 commands (help, info, cd, list, stress, etc.)
- Keyboard event queue (IRQ-driven)
- Framebuffer console

### ✅ Phase 8: Stress Tests - COMPLETE
- Memory allocation stress
- Keyboard queue tests
- Block cache tests
- Uptime stability tests

### 🔄 Phase 9: Stability & Heavy Apps - IN PROGRESS
**Goal:** Boot protection, crash isolation, NXGame Bridge for games/creative apps

#### ✅ Boot Guard (`boot_guard.c`)
- Boot status tracking (BOOTING → OK or → Recovery)
- Failure counter (max 3 before recovery)
- Watchdog timer (30s timeout)

#### ✅ Session Manager (`session_manager.c`)
- Desktop as client of nxgfx (not owner)
- Desktop crash ≠ System crash
- Games can request exclusive GPU mode
- Auto-restart GUI on crash

#### ✅ NXGame Bridge (`nxgame_bridge.c`)
- Direct GPU access (bypass compositor)
- Low-latency audio (~5ms)
- Raw input polling
- Crash isolation (game crash ≠ system crash)
- 10 syscalls (110-119) for userspace access

#### ✅ System Integrity Protection (`sip.c`)
- Protected paths: /System/, /Kernel/, /Boot/, /Library/
- Violation logging with PID and operation
- SIP only disableable in Recovery Mode
- Admin-only paths: /Applications/

### ✅ Phase 9: Complete!
**Summary:** Kernel fully hardened with boot protection, crash isolation, game support, and system file protection.

### ✅ Phase 10: Desktop GUI - COMPLETE
**Goal:** Desktop shell with window compositor and widgets

#### ✅ Desktop Shell (`desktop_shell.c`)
- Menu bar at top (NeolyxOS branding, clock)
- Dock at bottom (macOS-style with icons)
- Window compositor with z-ordering
- Mouse cursor rendering and tracking

#### ✅ Widget System (`widgets.c`)
- Button widget with hover/press states
- Label widget with color/alignment
- Container widget for composition
- Themeable color palette
- Event propagation system

#### ✅ Mouse Integration
- PS/2 mouse driver integration
- Click detection for windows
- Window dragging by title bar

### ✅ Phase 11: Terminal Window - COMPLETE
**Goal:** Port terminal shell to desktop window
- Terminal window widget (80x24 text buffer)
- Cursor and scrolling support
- Keyboard input handling
- Command execution integration

### ✅ Phase 12: Security Hardening - COMPLETE
**Goal:** Kernel security and crash prevention

#### ✅ Security Module (`security.c`)
- Capability-based access control (12 capabilities)
- Privilege levels (USER → KERNEL)
- Security audit logging (64-entry ring buffer)
- Pointer validation

#### ✅ Safe Memory (`safe_mem.c`)
- Bounded memcpy/strcpy (overflow protection)
- safe_alloc (zero-initialized)
- safe_free (poison on free, NULL pointer set)

#### ✅ Panic Handler (`panic_handler.c`)
- kernel_panic_ex with type, file, line, message
- Stack guard canaries
- ASSERT/PANIC macros
- Crash handler callbacks


---

## 📁 Critical Files

| Component | File | Lines | Status |
|-----------|------|-------|--------|
| **Bootloader** | `boot/main.c` | 672 | ✅ |
| **Kernel Entry** | `kernel/kernel_main.c` | 540 | ✅ |
| **Process Mgmt** | `src/core/process.c` | 761 | ✅ |
| **Syscalls** | `src/core/syscall.c` | 414 | ✅ |
| **NXFS** | `src/fs/nxfs.c` | 501 | ✅ |
| **Block Cache** | `src/fs/bcache.c` | 300+ | ✅ |
| **Kbd Queue** | `src/io/kbd_queue.c` | 200+ | ✅ |
| **Terminal** | `src/shell/terminal.c` | 537 | ✅ |
| **Stress Tests** | `src/test/stress_test.c` | 350+ | ✅ |

---

## 🎯 Boot Flow

```
POWER ON
│
├── UEFI FIRMWARE
│   ├── Hardware Init (CPU, RAM, PCI)
│   ├── UEFI Boot Manager
│   └── Load EFI/BOOT/BOOTX64.EFI
│
├── NEOLYXOS BOOTLOADER (EFI APP)
│   ├── GOP Framebuffer Init
│   ├── Keyboard Init
│   ├── Mount ESP (FAT32)
│   ├── Locate kernel.bin
│   ├── Load Kernel to RAM
│   ├── Build BootInfo
│   ├── ExitBootServices()
│   └── Jump to Kernel Entry
│
├── NEOLYXOS KERNEL (Early Boot)
│   ├── GDT / IDT Setup
│   ├── Physical Memory Manager
│   ├── Framebuffer Ownership
│   ├── Disk Driver Init
│   ├── Syscall Table (33 syscalls)
│   └── Mount ESP
│
├── INSTALLATION CHECK
│   ├── neolyx.installed EXISTS?
│   │
│   ├── NO  ──► INSTALL MODE
│   │            │
│   │            ├── Installer Core
│   │            ├── Disk Detection
│   │            ├── User Selects Edition
│   │            │     ├── Desktop
│   │            │     └── Server
│   │            │
│   │            ├── Disk Selection
│   │            ├── Disk Formatting
│   │            │     ├── GPT + Protective MBR
│   │            │     ├── ESP (FAT32)
│   │            │     └── Root FS (NXFS)
│   │            │
│   │            ├── Copy Bootloader
│   │            ├── Copy kernel.bin
│   │            ├── Install Base System
│   │            ├── Write neolyx.installed
│   │            ├── Write install.log
│   │            └── Reboot System
│   │
│   └── YES ──► NORMAL BOOT
│                │
│                ├── Init Process
│                ├── Driver Stack
│                ├── Filesystem Mount
│                ├── Scheduler Start
│                │
│                ├── Edition Branch
│                │     ├── Desktop Edition
│                │     │     ├── NXRender
│                │     │     ├── Compositor
│                │     │     ├── Desktop Shell
│                │     │     └── Terminal / Apps
│                │     │
│                │     └── Server Edition
│                │           ├── No GUI
│                │           ├── System Services
│                │           └── Console Login
│                │
│                └── FULL NEOLYXOS RUNNING
│
└── USER SPACE
    ├── User Programs (NXFS)
    ├── Syscalls Interface
    ├── NXGame Bridge
    └── Applications
```
---

## 🔧 Build Commands

```bash
cd kernel && make
sudo mount -o loop neolyx.img /tmp/neolyx_mount
sudo cp kernel/kernel.bin /tmp/neolyx_mount/EFI/BOOT/kernel.bin
sudo umount /tmp/neolyx_mount
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
  -drive file=neolyx.img,format=raw -m 512M -serial stdio
```

---

### ✅ Phase 13: Storage Integration - COMPLETE
**Goal:** Real hardware disk access and filesystem mounting

#### ✅ AHCI Driver (`ahci.c`)
- PCI device discovery and MMIO mapping
- Port detection and drive identification
- Sector read/write operations (512 bytes)
- IDENTIFY command for drive info

#### ✅ Partition Parsing (`nucleus.c`)
- Real MBR parsing (no dummy data)
- Real GPT parsing with GUID recognition
- Partition type detection (EFI, NTFS, Linux, NXFS)
- UTF-16LE to ASCII label conversion

#### ✅ AHCI Block Adapter (`ahci_block.c`)
- Bridges 512-byte AHCI sectors to 4KB NXFS blocks
- DMA-safe buffers via PMM
- Block device abstraction for VFS

#### ✅ Filesystem Mounting
- VFS/NXFS initialization on boot
- Automatic NXFS partition detection
- Root mount attempt from AHCI disk

#### ✅ Interrupt-Driven Keyboard (`keyboard.c`)
- Ring buffer key input (64 keys)
- Shift/Ctrl/Alt/CapsLock support
- Legacy API compatibility wrappers
- Terminal shell integration

---

## ✅ Phase 14: Userspace - COMPLETE

### ✅ Phase 14A: VFS Syscalls - COMPLETE
- sys_open, sys_read, sys_write, sys_close, sys_stat implemented
- Registered in syscall_init()

### ✅ Phase 14B: Terminal Enhancements - COMPLETE
- `sys reboot` - Real x86 keyboard controller reset
- `sys shutdown` - Real ACPI S5 power off
- `clear` command - Calls console_clear()
- Fixed input buffer clearing (no command concatenation)
- **Real System Data** (NO more dummy/hardcoded values):
  - `info memory` → PMM stats (total/free/used MB)
  - `info disk` → AHCI data (model, size, port)
  - `info cpu` → CPUID (vendor, model brand)
  - `uptime` → Timer ticks (days/hours/minutes/seconds)

### ✅ Phase 14C: User Program Loading - COMPLETE
- `enter_user_mode` in `arch.S` fixed with proper IRET stack frame
- Created `ramfs.c` - in-memory filesystem with embedded hello ELF
- `/bin/hello` available at boot (embedded in kernel)
- Ring 3 transition with correct segment selectors (CS=0x1B, SS=0x23)
- TSS kernel stack setup for Ring 3→0 syscalls
- Run `exec /bin/hello` in terminal to test

### ✅ Phase 15: Userland Shell & Init - COMPLETE
- **Shell (`nxsh`):** Built-in `cd`, `pwd`, `exit`, `help` + external command execution
- **Init Process:** PID 1, spawns shell, reaps zombies
- **RamFS:** Embedded init (5.8KB) and nxsh (8KB) binaries
- Run `exec /bin/nxsh` to test shell

### ✅ Phase 16: System Security - COMPLETE
**Goal:** Kernel-level security, firewall, and isolation

#### ✅ Firewall (`firewall.c`)
- Rule-based packet filtering (INPUT/OUTPUT/FORWARD)
- Protocol and port range filtering
- Telemetry and statistics

#### ✅ App Sandbox (`app_sandbox.c`)
- Capability-based isolation (Network, FS, Camera, etc.)
- App signature verification
- Resource limits (CPU/Memory)

#### ✅ System Lock (`system_lock.c`)
- Screen locking with PIN authentication
- Auto-lock timeout
- Brute-force protection (lockout)

#### ✅ Audit (`audit.c`)
- Security event logging (Login, Firewall Deny, App Install)
- Circular log buffer

#### ✅ App-Level Firewall Control
- Per-process network permissions (INCOMING/OUTGOING)
- Port-based allow/deny lists per app
- Traffic tracking (bytes sent/received)
- Blocked attempt counter

#### ✅ Security Scanner (`security_scanner.c`)
- File source detection (System, AppStore, ThirdParty, Downloaded)
- Desktop warnings for unverified files
- Quarantine functionality
- Download protection with warnings
- Signature verification

### ✅ Phase 16: Complete!
**Summary:** Comprehensive security subsystem with firewall, app control, file scanning, and threat detection.

### 🔄 Phase 17: Networking Stack Integration - IN PROGRESS

#### ✅ Kernel Boot Integration
- `network_init()` and `tcpip_init()` called in kernel_main.c
- Network interface status logged on boot

#### ✅ Socket Syscalls (40-47)
- SYS_SOCKET, SYS_BIND, SYS_LISTEN, SYS_ACCEPT
- SYS_CONNECT, SYS_SEND, SYS_RECV, SYS_CLOSESOCK
- All handlers implemented and registered

#### ✅ Terminal Commands
- `net status` (ifconfig) - Shows interface info, MAC, IP
- `ping <ip>` - Sends ICMP echo requests
- `dhcp` - Request IP via DHCP protocol

#### ✅ Real DHCP Client
- DHCP DISCOVER packet building and broadcast
- DHCP OFFER parsing (yiaddr, options)
- DHCP REQUEST with requested IP
- DHCP ACK processing and interface configuration

#### ⏳ Remaining
- DNS resolver (hostname to IP)

### ✅ Phase 18: Applications - COMPLETE
**Goal:** Core system applications with native UI

#### ✅ Settings.app (25 Panels)
Full macOS-style Settings application with:

**Core Settings:**
- System Panel - OS version, hardware, uptime
- About Panel - Device rename, serial, system controls
- Network Panel - WiFi, Ethernet, VPN, Bluetooth tabs
- Bluetooth Panel - Device pairing, battery status
- Display Panel - Brightness, resolution, Night Shift, True Tone
- Display Manager Panel - External displays, arrangement, HDR
- Sound Panel - Volume, output devices, input selection
- Storage Panel - Disk space, drives, optimization

**Input & Accessibility:**
- Keyboard Panel - Layouts, shortcuts, NX key remapping
- Trackpad Panel - Multi-touch gestures (2/3/4-finger, edge swipes)

**Appearance & Personalization:**
- Appearance Panel - Theme, accent colors, fonts, icons
- Color Panel - Display calibration, ICC profiles, soft proofing

**Security & Privacy:**
- Accounts Panel - User management, login
- Security Panel - Firewall, encryption, app security
- Privacy Panel - Location, camera, microphone permissions

**System Management:**
- Apps Panel - Installed applications, categories, manifest parsing
- Devices Panel - Hardware device management, drivers
- Power Panel - Battery, sleep, energy saver
- Processes Panel - Running processes, CPU/memory usage
- Startup Panel - Apps that launch at login
- Extensions Panel - System extensions and plugins
- Scheduler Panel - Scheduled tasks, automation
- Updates Panel - System and app updates
- Bootloader Panel - Startup disk, boot options
- Developer Panel - Developer tools, debugging

**Features:**
- Split-view (2-3 panels side by side)
- Pop-out tabs to separate windows
- Global search across all panels
- Keyboard shortcut: search_keywords per panel

---

## 🏗️ Architecture (macOS-inspired)

┌───────────────────────────────────────────┐
│  Desktop Shell ✅                         │
│  - Window compositor with z-ordering      │
│  - macOS-style dock and menu bar          │
│  - Widget system with theming             │
├───────────────────────────────────────────┤
│  Terminal Shell ✅                        │
│  - Interrupt-driven keyboard (IRQ1)       │
│  - console.c (framebuffer)                │
│  - terminal.c (25 commands)               │
├───────────────────────────────────────────┤
│  Storage Stack ✅                         │
│  - AHCI driver (real hardware)            │
│  - MBR/GPT partition parsing              │
│  - NXFS with VFS mounting                 │
├───────────────────────────────────────────┤
│  Process Management ✅                    │
│  - fork/exec/wait                         │
│  - Scheduler (round-robin)                │
│  - Userland support (Ring 3)              │
├───────────────────────────────────────────┤
│  10 Kernel Drivers ✅                     │
├───────────────────────────────────────────┤
│  Core: GDT/IDT/PMM/VMM/Syscalls ✅        │
├───────────────────────────────────────────┤
│  UEFI Boot (ACPI, not GRUB) ✅            │
└───────────────────────────────────────────┘

---

**Copyright (c) 2025 KetiveeAI**
