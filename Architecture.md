# NeoLyx Desktop Environment - Architecture Integration Guide

**Integrating Modern Desktop UI with NeolyxOS Kernel**

---

## Overview

NeoLyx Desktop is the graphical user interface layer for NeolyxOS. Unlike traditional desktop environments that rely on X11/Wayland, NeoLyx uses **NXGFX** (custom GPU backend) for direct hardware rendering.

---

## Architecture Position

```
┌─────────────────────────────────────────────────────────────────────┐
│                         USER SPACE APPLICATIONS                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐ │
│  │ Terminal │  │  ReoLab  │  │  Files   │  │    Calculator        │ │
│  │   .nxt   │  │   IDE    │  │ Manager  │  │    Calendar, etc.    │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│                    ✨ NEOLYX DESKTOP LAYER ✨                       │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                  NeoLyx Desktop Shell                          │ │
│  │  • Window Manager        • Dock System                         │ │
│  │  • App Launcher          • Settings Panel                      │ │
│  │  • System Tray           • Notification Center                 │ │
│  │  • Desktop Icons         • Top Menu Bar                        │ │
│  └────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│                         GUI RENDERING ENGINE                         │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                  NXRender + NXGFX Engine                       │ │
│  │  • Custom GPU Backend       • Widget System                    │ │
│  │  • NXGFX (No OpenGL/Vulkan) • Animation System                 │ │
│  │  • Window Compositor        • Theme Engine                     │ │
│  │  • Text Renderer            • Event System                     │ │
│  └────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│                         KERNEL SPACE                                 │
│  ┌───────────────┬───────────────┬───────────────┬────────────────┐ │
│  │ Core Kernel   │ Device Drivers│  File System  │   Memory Mgmt  │ │
│  │  • Syscalls   │  • nxgfx_kdrv │  • VFS Layer  │  • Heap        │ │
│  │  • Scheduler  │  • nxdisplay  │  • NXFS       │  • Paging      │ │
│  │  • IPC        │  • nxinput    │  • FAT32      │  • Virtual Mem │ │
│  └───────────────┴───────────────┴───────────────┴────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Directory Structure for Desktop Integration

### Current NeoLyxOS Structure (Actual)

```
neolyx-os/kernel/                 # Main kernel directory
├── include/                      # Header files
│   ├── arch/                     # Architecture-specific headers
│   │   ├── aarch64/              # ARM64 headers (arm64.h, fdt.h)
│   │   ├── x86_64/               # x86_64 headers (idt.h)
│   │   └── io.h
│   │
│   ├── core/                     # Core kernel headers
│   │   ├── boot.h, elf.h, kernel.h
│   │   ├── process.h, syscall.h
│   │   ├── power.h, security.h
│   │   └── ... (21 core headers)
│   │
│   ├── drivers/                  # Driver headers
│   │   ├── nxgfx_kdrv.h          # GPU driver
│   │   ├── nxdisplay_kdrv.h      # Display driver
│   │   ├── nxrender_kdrv.h       # Render driver
│   │   ├── nxaudio_kdrv.h        # Audio driver
│   │   ├── nxtouch_kdrv.h        # Touch input
│   │   ├── nxpen_kdrv.h          # Pen/stylus
│   │   └── ... (40+ driver headers)
│   │
│   │
│   ├── video/                    # Video subsystem
│   │   ├── console.h             # Console
│   │   ├── terminal.h            # Terminal
│   │   ├── splash.h              # Boot splash
│   │   └── nx3d.h                # 3D graphics
│   │
│   └── ... (mm/, fs/, net/, io/, etc.)
│
├── src/                          # Source implementation
│   ├── arch/                     # Architecture code
│   │   ├── aarch64/              # ARM64: boot.S, fdt.c, mmu.c
│   │   └── x86_64/               # x86_64: gdt.c, idt.c, paging.c
│   │
│   ├── core/                     # Core kernel (284KB)
│   │   ├── boot.c, kernel.c      # Boot and kernel init
│   │   ├── process.c, syscall.c  # Process and syscall handling
│   │   ├── elf.c                 # ELF loader
│   │   └── ... (20+ core files)
│   │
│   │
│   ├── drivers/                  # Device drivers (612KB)
│   │   ├── graphics/             # Graphics drivers
│   │   │   ├── nxgfx_kdrv.c      # GPU kernel driver
│   │   │   ├── nxgfx_accel.c     # GPU acceleration
│   │   │   └── nxrender_kdrv.c   # Render driver
│   │   ├── display/              # Display management
│   │   │   └── nxdisplay_kdrv.c  # Multi-monitor (24KB)
│   │   ├── input/                # Input devices
│   │   │   ├── mouse.c           # Mouse driver
│   │   │   ├── nxtouch_kdrv.c    # Touch input
│   │   │   └── nxpen_kdrv.c      # Stylus/pen
│   │   ├── audio/                # Audio drivers
│   │   │   ├── nxaudio_kdrv.c    # Audio driver
│   │   │   └── nxaudio_spatial.c # Spatial audio
│   │   └── ... (network, storage, USB, etc.)
│   │
│   │
│   ├── video/                    # Video subsystem (62KB)
│   │   ├── console.c             # Console rendering
│   │   ├── terminal.c            # Terminal emulator (17KB)
│   │   ├── splash.c              # Boot splash screen
│   │   ├── nx3d.c                # 3D graphics (11KB)
│   │   └── video.c               # Video core
│   │
│   ├── shell/                    # Shell/Terminal (73KB)
│   │   ├── terminal.c            # Full terminal (59KB)
│   │   └── terminal_shell.c      # Shell interpreter
│   │
│   ├── disk/                     # Disk management UI (63KB)
│   │   ├── disk_manager.c        # Disk manager
│   │   ├── disk_ui.c             # Disk UI
│   │   └── disk_ops.c            # Disk operations
│   │
│   └── ... (fs/, mm/, net/, io/, etc.)
│
├── kernel_main.c                 # Kernel entry point (21KB)
├── Makefile                      # Build system (16KB)
├── linker.ld / link.ld           # Linker scripts
└── README.md

Total: ~2.9MB source code in 340 files
```

### Where Desktop Code Lives

**Desktop is ALREADY in the kernel source tree:**
- `include/desktop/` - Desktop headers (18KB)
- `src/desktop/` - Desktop implementation (51KB)
- `include/ui/` - UI headers (83KB)
- `src/ui/` - UI implementation (48KB)
- `src/video/` - Video/terminal (62KB)

**Desktop drivers in kernel:**
- `src/drivers/graphics/` - nxgfx_kdrv, nxrender_kdrv
- `src/drivers/display/` - nxdisplay_kdrv (multi-monitor)
- `src/drivers/input/` - mouse, touch, pen drivers

### Recommended: Separate Desktop Apps Directory

To keep user-space apps separate from kernel, create:

```
neolyx-os/
├── kernel/                       # Kernel (current structure)
│   ├── src/desktop/              # Desktop shell (core, runs in kernel)
│   ├── src/drivers/graphics/     # Graphics drivers
│   └── ...
│
├── desktop-apps/                 # 🆕 User-space desktop applications
│   ├── settings/                 # Settings App
│   │   ├── main.c                # App entry point
│   │   ├── panels/               # Setting panels
│   │   │   ├── system.c          # System info
│   │   │   ├── network.c         # Network config
│   │   │   ├── display.c         # Display settings
│   │   │   └── sound.c           # Audio settings
│   │   └── Makefile
│   │
│   ├── files/                    # File Manager
│   │   ├── main.c
│   │   ├── browser.c             # File browser
│   │   ├── sidebar.c             # Sidebar navigation
│   │   └── Makefile
│   │
│   ├── calculator/               # Calculator App
│   │   ├── main.c
│   │   └── Makefile
│   │
│   ├── calendar/                 # Calendar App
│   │   ├── main.c
│   │   └── Makefile
│   │
│   └── dock/                     # Dock/Taskbar (user-space)
│       ├── main.c                # Dock main
│       ├── app_launcher.c        # App launcher
│       └── Makefile
│
├── gui-toolkit/                  # 🆕 GUI Library (user-space)
│   ├── nxrender/                 # NXRender high-level API
│   │   ├── window.c              # Window management
│   │   ├── widgets/              # Widget library
│   │   │   ├── button.c
│   │   │   ├── label.c
│   │   │   ├── textbox.c
│   │   │   └── panel.c
│   │   ├── renderer.c            # 2D renderer
│   │   └── events.c              # Event system
│   │
│   └── nxgfx/                    # NXGFX user-space library
│       ├── gpu_interface.c       # GPU syscall wrapper
│       └── framebuffer.c         # Framebuffer access
│
└── resources/                    # 🆕 System resources
    ├── themes/
    │   ├── neolyx-default/
    │   ├── neolyx-dark/
    │   └── neolyx-light/
    ├── icons/                    # .nix icon files
    ├── fonts/                    # TrueType fonts
    └── wallpapers/               # Desktop backgrounds
```

### 1. **Desktop Shell** (`desktop/shell/`)

**Purpose**: Main desktop process that manages the entire UI

**Components**:

#### Window Manager (`window_manager.c`)
- Creates and manages application windows
- Handles window decorations (title bar, buttons)
- Implements minimize, maximize, close operations
- Window stacking and Z-order management

#### Compositor (`compositor.c`)
- Composites all windows into final framebuffer
- Applies transparency effects
- Handles window shadows and blur
- VSync and frame timing

#### Dock (`dock.c`)
- Bottom/side panel with app icons
- Running application indicators
- App launcher integration
- Quick access shortcuts

#### App Launcher (`launcher.c`)
- Grid view of all applications
- Search functionality
- App categories
- Recent apps tracking

#### System Tray (`tray.c`)
- Network status
- Volume control
- Battery indicator
- Clock/calendar
- Notification icons

---

### 2. **NXRender Engine** (`gui/nxrender/`)

**Purpose**: High-level rendering API for desktop and apps

**Key Features**:
```c
// Initialize renderer
nxrender_init(framebuffer_info);

// Create window
NXWindow* window = nxrender_create_window(x, y, width, height, "Title");

// Render widgets
nxrender_draw_button(window, x, y, "Click Me");
nxrender_draw_text(window, x, y, "Hello World", font);

// Handle events
NXEvent event;
while (nxrender_poll_event(&event)) {
    // Process mouse, keyboard, etc.
}

// Present frame
nxrender_present(window);
```

**Text Rendering**:
- TrueType font support
- Anti-aliased rendering
- Unicode support (UTF-8)
- Font caching

**Animation System**:
- CSS-like transitions
- Easing functions
- 144 FPS target
- Hardware acceleration via NXGFX

---

### 3. **NXGFX Backend** (`gui/nxgfx/`)

**Purpose**: Direct GPU hardware access (no OpenGL/Vulkan)

**Architecture**:
```
Desktop App → NXRender API → NXGFX → nxgfx_kdrv (kernel) → GPU Hardware
```

**Key Functions**:
```c
// Low-level GPU operations
nxgfx_allocate_buffer(width, height);
nxgfx_blit(src, dst, x, y, width, height);
nxgfx_fill_rect(x, y, width, height, color);
nxgfx_draw_line(x1, y1, x2, y2, color);
nxgfx_copy_to_screen(buffer);
```

**Hardware Acceleration**:
- 2D blitter operations
- Hardware cursor
- DMA transfers
- Multiple framebuffer support

---

### 4. **Settings App** (`desktop/apps/settings/`)

**Purpose**: System-wide configuration interface

**Panels**:

#### System Panel
- OS version and build info
- Hardware information (CPU, RAM, GPU)
- System updates
- About NeolyxOS

#### Network Panel
- WiFi on/off toggle
- Network list and connection
- IP address configuration
- VPN settings

#### Display Panel
- Resolution selection
- Brightness control
- Night light toggle
- Multiple monitor setup

#### Sound Panel
- Volume sliders (output/input)
- Device selection
- Sound effects toggle
- Audio profiles

#### Accounts Panel
- User management
- Login options
- Profile picture

#### Security Panel
- Firewall settings
- Privacy controls
- Password management

---

## Communication Flow

### Desktop → Kernel Communication

```c
// Desktop app wants to draw
desktop_app() {
    // 1. Use NXRender high-level API
    NXWindow* win = nxrender_create_window(...);
    nxrender_draw_button(win, ...);
    
    // 2. NXRender calls NXGFX
    nxgfx_draw_rect(...);
    
    // 3. NXGFX makes syscall to kernel
    syscall(SYS_GFX_DRAW, ...);
    
    // 4. Kernel nxgfx_kdrv handles request
    // 5. GPU hardware executes operation
}
```

### System Calls for Desktop

| Syscall | Number | Purpose |
|---------|--------|---------|
| `SYS_GFX_INIT` | 100 | Initialize graphics |
| `SYS_GFX_DRAW` | 101 | Draw operation |
| `SYS_GFX_BLIT` | 102 | Blit buffer |
| `SYS_GFX_FLIP` | 103 | Flip framebuffer |
| `SYS_INPUT_POLL` | 104 | Poll input events |
| `SYS_WINDOW_CREATE` | 105 | Create window |
| `SYS_WINDOW_DESTROY` | 106 | Destroy window |

---

## Installation & File Locations (Runtime)

### At Runtime - Installed System

```
/                           # Root filesystem (NXFS)
├── boot/
│   ├── kernel.elf          # NeoLyxOS kernel
│   ├── config/             # Boot configuration
│   └── splash.nix          # Boot splash image
│
├── bin/                    # System binaries
│   ├── init                # Init process
│   ├── sh                  # Shell
│   └── ...
│
├── usr/
│   ├── bin/                # User applications
│   │   ├── neolyx-shell    # Desktop shell (if user-space)
│   │   ├── settings        # Settings app
│   │   ├── files           # File manager
│   │   ├── terminal        # Terminal emulator
│   │   ├── calculator      # Calculator
│   │   └── calendar        # Calendar
│   │
│   ├── lib/                # Shared libraries
│   │   ├── libnxrender.so  # NXRender library
│   │   ├── libnxgfx.so     # NXGFX library
│   │   ├── libc.so         # C library
│   │   └── ...
│   │
│   └── share/
│       ├── neolyx/         # NeoLyx resources
│       │   ├── themes/     # UI themes
│       │   │   ├── default/
│       │   │   ├── dark/
│       │   │   └── light/
│       │   ├── icons/      # System icons (.nix)
│       │   ├── fonts/      # System fonts
│       │   │   ├── NeoLyxSans.ttf
│       │   │   ├── NeoLyxMono.ttf
│       │   │   └── ...
│       │   ├── wallpapers/ # Desktop backgrounds
│       │   │   ├── default.jpg
│       │   │   └── ...
│       │   └── sounds/     # System sounds
│       │
│       └── applications/   # .desktop files (app metadata)
│           ├── settings.desktop
│           ├── files.desktop
│           ├── terminal.desktop
│           └── ...
│
├── etc/                    # System configuration
│   ├── neolyx/
│   │   ├── desktop.conf    # Desktop settings
│   │   ├── theme.conf      # Active theme
│   │   ├── display.conf    # Display config
│   │   └── keybindings.conf
│   ├── fstab               # Filesystem table
│   └── passwd              # User accounts
│
├── var/                    # Variable data
│   ├── log/                # System logs
│   └── cache/              # Cache data
│
├── home/                   # User home directories
│   └── user/
│       ├── Desktop/        # Desktop files
│       ├── Documents/
│       ├── Downloads/
│       └── .config/        # User config
│           └── neolyx/     # User desktop preferences
│               ├── desktop.conf
│               └── shortcuts.conf
│
└── dev/                    # Device files
    ├── fb0                 # Framebuffer
    ├── input/              # Input devices
    │   ├── mouse0
    │   └── keyboard0
    └── ...
```

### Build-time vs Runtime Locations

| Component | Build Location (Source) | Runtime Location (Installed) |
|-----------|------------------------|------------------------------|
| Kernel | `kernel/kernel_main.c` | `/boot/kernel.elf` |
| Desktop Shell | `kernel/src/desktop/desktop_shell.c` | Embedded in kernel OR `/usr/bin/neolyx-shell` |
| Settings App | `desktop-apps/settings/` | `/usr/bin/settings` |
| File Manager | `desktop-apps/files/` | `/usr/bin/files` |
| NXRender Lib | `gui-toolkit/nxrender/` | `/usr/lib/libnxrender.so` |
| NXGFX Lib | `gui-toolkit/nxgfx/` | `/usr/lib/libnxgfx.so` |
| Icons | `resources/icons/` | `/usr/share/neolyx/icons/` |
| Fonts | `resources/fonts/` | `/usr/share/neolyx/fonts/` |
| Themes | `resources/themes/` | `/usr/share/neolyx/themes/` |

---

## Boot Sequence with Desktop

```
1. UEFI Bootloader (boot/BOOTX64.EFI)
   ↓
2. Kernel Init (kernel/kernel_main.c)
   • Initialize drivers (nxgfx_kdrv, nxdisplay_kdrv)
   • Mount root filesystem
   • Start init process
   ↓
3. Init Process (/sbin/init)
   • Load system services
   • Initialize NXRender
   • Start desktop shell
   ↓
4. Desktop Shell (/usr/bin/neolyx-shell)
   • Initialize window manager
   • Start compositor
   • Draw desktop background
   • Show dock and system tray
   • Display splash screen → Desktop
   ↓
5. User Desktop Ready
   • User can launch apps
   • Apps use NXRender API
   • Events flow: Hardware → Kernel → Desktop → Apps
```

---

## Building Desktop Components

### Build Kernel with Desktop (Current Setup)

```bash
# Desktop is already integrated in kernel
cd neolyx-os/kernel

# Build entire kernel (includes desktop)
make clean && make

# Output: build/kernel.elf (contains desktop_shell.c, widgets.c, etc.)
```

### Build Separate Desktop Apps (Recommended)

```bash
# Create desktop-apps directory (if not exists)
mkdir -p ../desktop-apps/{settings,files,calculator,calendar,dock}

# Build Settings App
cd ../desktop-apps/settings
cat > Makefile << 'EOF'
CC = gcc
CFLAGS = -I../../kernel/include -Wall -Wextra
LDFLAGS = -lnxrender -lnxgfx

SOURCES = main.c panels/system.c panels/network.c panels/display.c
TARGET = settings

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)
EOF

# Write main.c
cat > main.c << 'EOF'
#include <nxrender/nxrender.h>
#include <desktop/widgets.h>

int main() {
    nxrender_init();
    NXWindow* win = nxrender_create_window(100, 100, 900, 600, "Settings");
    
    // Main loop
    NXEvent event;
    while (nxrender_poll_event(&event)) {
        if (event.type == NX_EVENT_CLOSE) break;
        
        // Render settings UI
        nxrender_clear(win, NX_COLOR_WHITE);
        // ... draw panels
        nxrender_present(win);
    }
    
    nxrender_destroy_window(win);
    nxrender_shutdown();
    return 0;
}
EOF

make

# Output: settings (executable)
```

### Build GUI Toolkit Library

```bash
# Build NXRender library
cd ../gui-toolkit/nxrender

cat > Makefile << 'EOF'
CC = gcc
CFLAGS = -fPIC -I../../kernel/include -Wall
LDFLAGS = -shared

SOURCES = window.c renderer.c events.c widgets/*.c
TARGET = libnxrender.so

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

install:
	cp $(TARGET) /usr/lib/
	ldconfig

clean:
	rm -f $(TARGET)
EOF

make
sudo make install
```

### Build Everything - Master Build Script

```bash
# Create build_all.sh in neolyx-os/
cat > build_all.sh << 'EOF'
#!/bin/bash
set -e

echo "=== Building NeoLyxOS ==="

# 1. Build Kernel
echo "[1/4] Building kernel..."
cd kernel
make clean && make
cd ..

# 2. Build GUI Toolkit
echo "[2/4] Building GUI toolkit..."
cd gui-toolkit/nxrender
make clean && make
cd ../../

# 3. Build Desktop Apps
echo "[3/4] Building desktop apps..."
for app in settings files calculator calendar dock; do
    if [ -d "desktop-apps/$app" ]; then
        echo "  Building $app..."
        cd desktop-apps/$app
        make clean && make
        cd ../../
    fi
done

# 4. Create ISO/Disk Image
echo "[4/4] Creating bootable image..."
./create_iso.sh

echo "=== Build Complete ==="
echo "Kernel: kernel/build/kernel.elf"
echo "ISO: neolyx-os.iso"
EOF

chmod +x build_all.sh
./build_all.sh
```

### Install Desktop to ISO/Image

```bash
# Create installation script
cat > install_desktop.sh << 'EOF'
#!/bin/bash

STAGING_DIR="iso_staging"
mkdir -p $STAGING_DIR/{boot,usr/{bin,lib,share/neolyx/{themes,icons,fonts,wallpapers}},etc/neolyx}

# 1. Copy kernel
cp kernel/build/kernel.elf $STAGING_DIR/boot/

# 2. Copy desktop apps
cp desktop-apps/settings/settings $STAGING_DIR/usr/bin/
cp desktop-apps/files/files $STAGING_DIR/usr/bin/
cp desktop-apps/calculator/calculator $STAGING_DIR/usr/bin/
cp desktop-apps/calendar/calendar $STAGING_DIR/usr/bin/

# 3. Copy libraries
cp gui-toolkit/nxrender/libnxrender.so $STAGING_DIR/usr/lib/
cp gui-toolkit/nxgfx/libnxgfx.so $STAGING_DIR/usr/lib/

# 4. Copy resources
cp -r resources/themes/* $STAGING_DIR/usr/share/neolyx/themes/
cp -r resources/icons/* $STAGING_DIR/usr/share/neolyx/icons/
cp -r resources/fonts/* $STAGING_DIR/usr/share/neolyx/fonts/
cp -r resources/wallpapers/* $STAGING_DIR/usr/share/neolyx/wallpapers/

# 5. Create ISO
mkisofs -o neolyx-os.iso \
        -b boot/grub/stage2_eltorito \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        $STAGING_DIR/

echo "ISO created: neolyx-os.iso"
EOF

chmod +x install_desktop.sh
./install_desktop.sh
```

### Makefile Integration

Update your existing `kernel/Makefile` to include desktop build:

```makefile
# Add to kernel/Makefile

# Desktop sources (already in your tree)
DESKTOP_SRC = src/desktop/desktop_shell.c \
              src/desktop/terminal_window.c \
              src/desktop/widgets.c

DESKTOP_OBJ = $(DESKTOP_SRC:.c=.o)

# Add to main build
kernel.elf: $(KERNEL_OBJ) $(DESKTOP_OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

# Desktop apps target (separate user-space apps)
.PHONY: desktop-apps
desktop-apps:
	@echo "Building desktop applications..."
	@cd ../desktop-apps && for d in */; do \
		echo "  Building $d..."; \
		make -C $d; \
	done

# Full system build
.PHONY: all
all: kernel.elf desktop-apps
	@echo "Full system built successfully"
```

---

## Key Differences from Linux Desktop

| Feature | Linux (GNOME/KDE) | NeoLyx Desktop |
|---------|-------------------|----------------|
| Display Server | X11 / Wayland | **None** (direct NXGFX) |
| GPU API | OpenGL / Vulkan | **NXGFX** (custom) |
| Window Manager | Mutter / KWin | Built into shell |
| Toolkit | GTK / Qt | **NXRender widgets** |
| Language | C / C++ / Python | **C + REOX** |
| IPC | D-Bus | **Kernel IPC** |

**Advantages**:
- ✅ Lower latency (no X11/Wayland overhead)
- ✅ Tighter kernel integration
- ✅ Custom GPU optimizations
- ✅ Smaller memory footprint
- ✅ 144 FPS target for UI

---

## Development Guidelines

### Creating a Desktop App

```c
// myapp.c
#include <nxrender/nxrender.h>

int main() {
    // Initialize NXRender
    nxrender_init();
    
    // Create window
    NXWindow* window = nxrender_create_window(
        100, 100,      // x, y position
        800, 600,      // width, height
        "My App"       // title
    );
    
    // Main loop
    NXEvent event;
    bool running = true;
    
    while (running) {
        // Poll events
        while (nxrender_poll_event(&event)) {
            if (event.type == NX_EVENT_CLOSE) {
                running = false;
            }
        }
        
        // Clear window
        nxrender_clear(window, NX_COLOR_WHITE);
        
        // Draw UI
        nxrender_draw_button(window, 10, 10, 200, 40, "Click Me");
        nxrender_draw_text(window, 10, 60, "Hello NeoLyx!", NULL);
        
        // Present frame
        nxrender_present(window);
    }
    
    // Cleanup
    nxrender_destroy_window(window);
    nxrender_shutdown();
    
    return 0;
}
```

### Compile and Link
```bash
gcc myapp.c -o myapp -lnxrender -I/usr/include/neolyx
```

---

## Performance Targets

| Metric | Target | Current |
|--------|--------|---------|
| Desktop FPS | 144 FPS | TBD |
| Window Open | < 50ms | TBD |
| App Launch | < 500ms | TBD |
| Memory Usage | < 256 MB | TBD |
| Boot to Desktop | < 10s | TBD |

---

## Future Enhancements

### Phase 13: Advanced Desktop Features
- [ ] Multi-monitor hotplug support
- [ ] Desktop effects (blur, shadows, animations)
- [ ] Screen recording
- [ ] Screenshot tool
- [ ] Virtual desktops/workspaces

### Phase 14: Accessibility
- [ ] Screen reader
- [ ] High contrast themes
- [ ] Keyboard-only navigation
- [ ] Magnifier

### Phase 15: Desktop Widgets
- [ ] Clock widget
- [ ] Calendar widget
- [ ] System monitor widget
- [ ] Weather widget

---

## Conclusion

The NeoLyx Desktop sits **above the kernel** in user space, communicating through:
1. **NXRender** (high-level API)
2. **NXGFX** (low-level GPU backend)
3. **Kernel syscalls** (to nxgfx_kdrv, nxdisplay_kdrv)

**NOT part of kernel** - it's a separate layer that can be updated independently while kernel remains stable.

---

**Last Updated**: December 2025  
**Version**: NeoLyx Desktop 1.0 Alpha  
**Copyright**: © 2025 KetiveeAI. All rights reserved.