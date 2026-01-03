# NeolyxOS Desktop Environment

Userspace desktop environment for NeolyxOS.

## Architecture

The desktop runs in **Ring 3 (userspace)**, communicating with the kernel via syscalls.
This ensures the **Stability Promise**: desktop crashes never affect the kernel.

```
┌─────────────────────────────────────────────────────┐
│              USERSPACE (Ring 3)                     │
│  ┌───────────────────────────────────────────────┐  │
│  │           NeoLyx Desktop Shell                │  │
│  │  • Window Manager    • Compositor             │  │
│  │  • Widgets           • Taskbar                │  │
│  │  • App Launcher      • Desktop Icons          │  │
│  └───────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────┤
│              KERNEL SPACE (Ring 0)                  │
│  ┌───────────────────────────────────────────────┐  │
│  │           nxgfx (GPU Driver)                  │  │
│  │  OWNER of framebuffer and GPU resources       │  │
│  │  Syscalls: 100-106 (graphics), 110-119 (game) │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

## Directory Structure

```
desktop/
├── shell/              # Desktop shell core
│   ├── main.c          # Userspace entry point
│   ├── desktop_shell.c # Window management  
│   └── compositor.c    # Compositing engine
├── widgets/            # Widget library
│   └── widgets.c       # Button, label, etc.
├── apps/               # Built-in desktop apps
│   └── terminal/       # Terminal emulator
├── include/            # Header files
└── Makefile            # Build to ELF binary
```

## Building

```bash
cd desktop
make
make install  # Copy to initramfs
```

## Stability Guarantee

- Desktop crash → kernel keeps running
- GPU fault → gracefully recovered  
- NeolyxOS **never** requires reboot for recovery
