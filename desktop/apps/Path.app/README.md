# Path.app

**NeolyxOS File Manager**

A Finder-like file browser for NeolyxOS with tabs, Quick Look, drag-and-drop, and full VFS integration.

## Features

| Feature | Description |
|---------|-------------|
| **Tabs** | Multi-folder tabs (Ctrl+T/W) |
| **Quick Look** | Space key preview |
| **Get Info** | Cmd+I file details |
| **Views** | Icons, List, Columns, Gallery |
| **Sidebar** | Favorites, Locations, Volumes |
| **Themes** | Dark and Light themes |
| **Drag & Drop** | Move/copy files with mouse |
| **Search** | Incremental file search |
| **Sorting** | By name, size, date, type |

## Directory Structure

```
Path.app/
├── bin/
│   └── path                 # Main binary
├── lib/
│   └── libpath.so           # Shared library
├── resources/
│   ├── icons/
│   │   ├── close.nxi        # Window controls
│   │   ├── minimize.nxi
│   │   ├── expand.nxi
│   │   ├── folder.nxi       # File type icons
│   │   ├── file.nxi
│   │   └── image.nxi
│   ├── themes/
│   │   ├── dark.theme
│   │   └── light.theme
│   └── path.nxi             # App icon
├── manifest.npa             # App manifest
└── (source files)
```

## Keyboard Shortcuts

| Category | Shortcut | Action |
|----------|----------|--------|
| **Tabs** | `Ctrl+T` | New tab |
| | `Ctrl+W` | Close tab |
| | `Ctrl+Tab` | Next tab |
| **Files** | `Cmd+C` | Copy |
| | `Cmd+X` | Cut |
| | `Cmd+V` | Paste |
| | `Delete` | Trash |
| **View** | `Space` | Quick Look |
| | `Cmd+I` | Get Info |
| | `1` | Icons view |
| | `2` | List view |
| | `3` | Columns view |
| | `4` | Gallery view |
| **Nav** | `Cmd+↑` | Parent folder |
| | `Cmd+←` | Back |
| | `Cmd+→` | Forward |
| **Other** | `Cmd+A` | Select all |
| | `Cmd+N` | New folder |
| | `Cmd+Shift+N` | New file |
| | `Cmd+H` | Toggle hidden |
| | `Cmd+R` | Refresh |

## Source Files

| File | Purpose | Lines |
|------|---------|-------|
| `main.c` | App lifecycle, event handling | ~900 |
| `file_view.c` | View rendering (icons/list/columns/gallery) | ~240 |
| `sidebar.c` | Favorites & volumes panel | ~215 |
| `toolbar.c` | Navigation & controls | ~225 |
| `context_menu.c` | Right-click menu | ~240 |
| `tab_bar.c` | Multi-tab support | ~410 |
| `quick_look.c` | Space preview | ~350 |
| `get_info.c` | File info panel | ~300 |
| `vfs_ops.c` | VFS syscall wrappers | ~320 |
| `status_bar.c` | Bottom status bar | ~210 |
| `drag_drop.c` | Drag and drop support | ~280 |
| `search.c` | File search | ~250 |

## Building

```bash
cd apps/Path.app

# Standalone build (uses NXRender stubs)
make

# With real NXRender
make real

# Clean
make clean

# Install to /Applications
make install
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      Path.app                           │
├─────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────┐   │
│  │               Tab Bar (if multiple)             │   │
│  └─────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────┐   │
│  │  ← → ↑  │  Path Bar  │  View Buttons  │ Search  │   │
│  └─────────────────────────────────────────────────┘   │
│  ┌──────────┬──────────────────────────────────────┐   │
│  │          │                                      │   │
│  │ Sidebar  │         File View                    │   │
│  │          │    (Icons/List/Columns/Gallery)      │   │
│  │ Favorites│                                      │   │
│  │ ────────│                                      │   │
│  │ Locations│                                      │   │
│  │          │                                      │   │
│  └──────────┴──────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────┐   │
│  │  X items  │  Message  │  Total Size            │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────┐
│                     VFS Layer                           │
│  opendir/readdir/stat/mkdir/unlink/rename/copy/move    │
└─────────────────────────────────────────────────────────┘
```

## VFS Operations

Path.app uses NeolyxOS VFS syscalls for file operations:

| Operation | Syscall | Description |
|-----------|---------|-------------|
| List directory | `SYS_OPENDIR/READDIR` | Read folder contents |
| Get info | `SYS_STAT` | File metadata |
| Create folder | `SYS_MKDIR` | New directory |
| Create file | `SYS_OPEN` | Empty file |
| Delete | `SYS_UNLINK/RMDIR` | Remove file/folder |
| Rename | `SYS_RENAME` | Rename/move |
| Copy | `SYS_READ/WRITE` | File copy |

---
Copyright (c) 2025 KetiveeAI
