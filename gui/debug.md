# NeolyxOS GUI & Desktop Debug Guide

## Current Issues to Fix

### 1. Mouse Input Not Working
**Status**: 🔴 BROKEN  
**Location**: `kernel/src/drivers/input/mouse.c`, `kernel/src/desktop/desktop_shell.c`

**Symptoms**:
- Cursor does not move when mouse moves
- IRQ12 is registered but interrupts may not be firing

**Debug Steps**:
```bash
# Check if mouse IRQ is unmasked
# In mouse.c, line 331: pic_unmask_irq(MOUSE_IRQ) added
# Still not working - check packet processing
```

**Root Cause Analysis**:
1. [ ] Verify `mouse_interrupt_handler` is being called (add serial debug)
2. [ ] Check if `mouse_process_packet` receives valid data
3. [ ] Verify `mouse_get_movement` returns non-zero deltas
4. [ ] Check `desktop_handle_mouse` is called with valid dx/dy

---

### 2. Desktop in Kernel (Architecture Issue)
**Status**: 🟡 NEEDS REFACTOR  
**Problem**: Desktop shell runs in kernel space - crash = kernel panic

**Target Architecture**:
```
┌─────────────────────────────────────────────────────────────┐
│                        USERSPACE                             │
├──────────────────┬──────────────────┬───────────────────────┤
│  Desktop Shell   │   Applications   │   System Services     │
│  (nxdesktop)     │  Path.app, etc   │  (display-server)     │
└────────┬─────────┴────────┬─────────┴───────────┬───────────┘
         │                  │                     │
         ▼                  ▼                     ▼
┌────────────────────────────────────────────────────────────┐
│                    KERNEL DRIVERS                          │
├──────────────┬──────────────┬──────────────┬───────────────┤
│ nxdisplay.kdrv│ nxinput.kdrv │ nxaudio.kdrv │ nxnet.kdrv   │
└──────────────┴──────────────┴──────────────┴───────────────┘
```

**Migration Steps**:
1. [ ] Move desktop_shell.c to `/apps/desktop/`
2. [ ] Create syscall interface for framebuffer access
3. [ ] Kernel only provides drivers + syscalls
4. [ ] Desktop crash → restart desktop, not kernel

---

### 3. Font Manager - System Fonts
**Status**: 🔴 NOT IMPLEMENTED  
**Location**: `apps/FontManager.app/`, System fonts dir

**Required**:
- [ ] Create `/System/Library/Fonts/` directory structure
- [ ] Copy TTF fonts to system dir on install
- [ ] Font Manager reads from `/System/Library/Fonts/`
- [ ] Provide font API for other apps

**Font Directory Structure**:
```
/System/Library/Fonts/
├── Core/
│   ├── NeoLyxSans.ttf       # System UI font
│   ├── NeoLyxSans-Bold.ttf
│   └── NeoLyxMono.ttf       # Terminal font
├── User/
│   └── (user-installed fonts)
└── Supplemental/
    └── (optional fonts)
```

---

### 4. Settings App - Customization
**Status**: 🟡 PARTIAL  
**Location**: `apps/Settings.app/`

**Panels Needed**:
- [ ] **Theme Panel**: Light/Dark/Custom themes
- [ ] **Font Panel**: System font selection
- [ ] **Mouse Panel**: Cursor type, speed, scroll direction
- [ ] **Display Panel**: Resolution, scaling
- [ ] **Network Panel**: WiFi/Ethernet config

---

### 5. Driver Status Debugging
**Status**: 🔴 NEEDS IMPROVEMENT

**Current Boot Messages** (from logs):
```
✅ [KLOG] Ethernet driver initialized successfully
⚠️  [WIFI] WiFi disabled in config (Server mode)  <-- Expected in VM
✅ [KLOG] Mouse: 5-button Intellimouse detected
✅ [KLOG] Mouse driver initialized with IRQ12
❌ [NXAudio] No audio devices found  <-- Expected in VM (no audio passthrough)
✅ [NXGfx] Found Bochs VGA @ PCI 0:2.0
```

**Status Summary**:
| Driver | Status | Notes |
|--------|--------|-------|
| Keyboard | ✅ Working | PS/2 + USB support |
| Mouse | 🟡 IRQ OK, input issue | Registered IRQ12 |
| Ethernet | ✅ Working | E1000 emulated |
| WiFi | ⏭️ Skipped | Disabled in VM |
| Audio | ❌ No device | VM has no audio |
| Graphics | ✅ Working | Bochs VGA |
| Storage | ✅ Working | AHCI + FAT32 |

---

## NXRender & ReoxUI Integration

### NXRender Stack
```
/gui/nxrender_c/
├── include/
│   └── nxrender.h        # Unified API header
├── nxgfx/                # Low-level graphics
├── nxrender-core/        # Compositor, windows
├── nxrender-widgets/     # Buttons, labels, etc
├── nxrender-layout/      # Flexbox, grid
├── nxrender-theme/       # Light/dark themes
└── nxrender-input/       # Mouse, keyboard events
```

### ReoxUI Bridge
```
/REOX/reolab/runtime/
├── reox_nxrender_bridge.h  # NXRender integration
├── reox_widgets.h          # Widget types
├── reox_forms.h            # Form layouts
└── reox_theme.h            # Theme system
```

---

## Fix Priority Order

### Phase 1: Mouse Fix (Critical)
1. Add debug output to `mouse_interrupt_handler`
2. Verify packet data in `mouse_process_packet`
3. Add debug to `desktop_handle_mouse`
4. Confirm cursor position updates

### Phase 2: Font System (High)
1. Create system font directory structure
2. Install base TTF fonts
3. Update Font Manager to read system fonts
4. Expose font API

### Phase 3: Settings Panels (Medium)
1. Theme customization panel
2. Mouse settings panel
3. Font selection panel
4. Save/load preferences

### Phase 4: Desktop Isolation (Low - Architectural)
1. Move desktop to userspace
2. Create framebuffer syscalls
3. Implement display server
4. Crash isolation

---

## Quick Debug Commands

```bash
# Rebuild specific driver
cd kernel && make mouse.o && make kernel.bin

# Test in QEMU
./boot_test.sh

# Check serial output for mouse debug
# Look for: [MOUSE] IRQ, packet, position logs
```

---

## Files to Modify

| File | Purpose |
|------|---------|
| `kernel/src/drivers/input/mouse.c` | Add debug output to IRQ handler |
| `kernel/src/desktop/desktop_shell.c` | Add debug to handle_mouse |
| `apps/FontManager.app/main.c` | Read from /System/Library/Fonts |
| `apps/Settings.app/panels/` | Add theme, font, mouse panels |

---

**Last Updated**: 2025-12-26  
**Author**: AI Assistant