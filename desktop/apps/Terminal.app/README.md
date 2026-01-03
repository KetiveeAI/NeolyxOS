# Terminal.app

**NeolyxOS Terminal Emulator**

A tabbed terminal emulator with PTY support, ANSI colors, and themes.

## Directory Structure

```
Terminal.app/
├── bin/
│   └── terminal            # Main binary
├── lib/
│   └── libterminal.so      # Shared library
├── resources/
│   ├── themes/
│   │   ├── dark.theme
│   │   └── light.theme
│   └── terminal.nxi        # App icon
├── manifest.npa            # App manifest
└── (source files)
```

## Features

| Feature | Description |
|---------|-------------|
| **Tabs** | Multiple terminal tabs |
| **PTY** | Full PTY support |
| **ANSI** | 256 color support |
| **Themes** | Dark and Light |
| **Scrollback** | 10,000 lines |
| **Selection** | Copy/paste |

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Cmd+T` | New tab |
| `Cmd+W` | Close tab |
| `Cmd+C` | Copy |
| `Cmd+V` | Paste |
| `Cmd+K` | Clear |
| `Cmd++` | Increase font |
| `Cmd+-` | Decrease font |

## Building

```bash
cd apps/Terminal.app

# Standalone build
make

# With NXRender
make real

# Install
make install
```

## Source Files

| File | Purpose |
|------|---------|
| `main.c` | App lifecycle, tabs |
| `terminal_view.c` | Text grid rendering |
| `context_menu.c` | Right-click menu |
| `nxrender.h` | Graphics wrapper |

## PTY Syscalls Used

| Syscall | Number |
|---------|--------|
| `SYS_PTY_OPEN` | 60 |
| `SYS_PTY_READ` | 61 |
| `SYS_PTY_WRITE` | 62 |
| `SYS_PTY_CLOSE` | 63 |
| `SYS_PTY_SPAWN` | 66 |

---
Copyright (c) 2025 KetiveeAI
