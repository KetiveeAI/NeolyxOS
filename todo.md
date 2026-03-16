# NeolyxOS Development TODO - Deep Architecture Analysis

**Last Updated:** January 21, 2026  
**Project Status:** Desktop 70% | Networking 60% | Overall ~50%

---

## 🔴 CRITICAL STATUS CORRECTIONS

Based on deep codebase analysis:

| Component      | Previous Status | Actual Status       | Notes                                     |
| -------------- | --------------- | ------------------- | ----------------------------------------- |
| USB            | 🔴 Missing      | ✅ Working          | `nxusb_kdrv` initialized in kernel_main.c |
| Mouse          | ✅ Working      | ✅ Working          | IRQ12, PS/2 driver functional             |
| Keyboard       | ✅ Working      | ✅ Working          | IRQ1, input events dispatched             |
| Desktop Shell  | ✅ Complete     | 🟡 Incomplete       | Renders but lacks OSD feedback components |
| Volume OSD     | Not listed      | 🔴 Missing          | No popup when changing volume             |
| Keyboard OSD   | Not listed      | 🔴 Missing          | No on-screen keyboard indicator           |
| Control Center | ✅ Complete     | 🟡 Rendering issues | Code exists but may not display properly  |

---

## 🏗️ DESKTOP ARCHITECTURE DEEP ANALYSIS

### What EXISTS (Code Present)

```
desktop/shell/
├── desktop_shell.c     (1975 lines) - Main compositor, window manager
├── control_center.c    (566 lines)  - Volume/brightness sliders, toggles
├── notification_center.c            - Notification system
├── dock.c              (19KB)       - App dock with hover animations
├── anveshan.c          (26KB)       - Spotlight-like search
├── window_manager.c    (20KB)       - Window states, animations
└── compositor.c        (15KB)       - Layer compositing
```

### What's BROKEN or INCOMPLETE

#### 1. **Control Center Rendering Issues**

**Location:** `desktop/shell/control_center.c`

- **Problem:** Control center may not fully render all sliders and toggles
- **Evidence:** Code has `render_slider()`, `render_toggle_tile()` but desktop shows incomplete UI
- **Root cause candidates:**
  - Glass morphism alpha blending too transparent
  - z-order not bringing control center to top
  - `control_center_is_visible()` returning false unexpectedly

#### 2. **Missing Volume OSD (On-Screen Display)**

**What macOS/Windows have:** When pressing volume keys, a floating popup shows current volume level
**What NeolyxOS has:**

- Control center has volume slider (line 545-549 in control_center.c)
- Navigation bar has volume icon (line 730-741 in desktop_shell.c)
  **What's MISSING:**
- No OSD popup when volume changes
- No keyboard shortcut for volume up/down
- No visual feedback outside control center

**Required implementation:**

```c
/* New file: desktop/shell/volume_osd.c */
typedef struct {
    int visible;
    int volume;           /* 0-100 */
    int fade_timer;       /* ms until hide */
    int x, y, w, h;       /* Position */
} volume_osd_t;

void volume_osd_show(int volume);  /* Called on volume change */
void volume_osd_render(...);       /* Draw floating volume bar */
void volume_osd_tick(...);         /* Handle fade out */
```

#### 3. **Missing Keyboard Input Feedback**

**What's missing:**

- No visual indicator when typing (e.g., Caps Lock on-screen icon)
- No keyboard shortcut OSD (e.g., when pressing Cmd+Space)
- Keyboard events dispatch but no visual acknowledgment

**Required implementation:**

```c
/* New file: desktop/shell/keyboard_osd.c */
void keyboard_osd_show_modifier(const char *text);  /* "Caps Lock On" */
void keyboard_osd_show_shortcut(const char *keys);  /* "⌘ + Space" */
```

#### 4. **Widget Rendering Not Connected**

**Problem:** Several UI components exist in code but aren't imported/rendered:

```c
/* In control_center.c - these functions exist: */
render_toggle_tile()      /* WiFi, Bluetooth toggles */
render_quick_action()     /* Focus, Dark Mode buttons */
render_slider()           /* Volume, Brightness bars */

/* But may not be called correctly in render loop */
```

---

## 📋 DETAILED DESKTOP GAPS

### Must Fix (Breaks User Experience)

1. **[x] Control Center visibility issue** ✅ Fixed (opacity increased)
   - File: `desktop/shell/control_center.c`
   - Alpha values increased from 25-75% to 50-91%
   - Debug logging added to toggle chain

2. **[x] Volume/Brightness sliders not rendering** ✅ Fixed (opacity increased)
   - File: `desktop/shell/control_center.c` lines 329-344
   - CC_SLIDER_BG increased from 18% to 38% opacity

3. **[x] Keyboard input not forwarded to windows** ✅ Fixed
   - File: `desktop/shell/desktop_shell.c`
   - Added `desktop_set_window_key_handler()` API
   - Apps can now register keyboard callbacks

### Should Implement (Usability)

4. **[x] Volume OSD popup** ✅ Already implemented
   - File: `desktop/shell/volume_osd.c` (192 lines)
   - Trigger: Volume change from control center
   - Design: Floating horizontal bar with speaker icon, 3-second fade

5. **[x] Caps Lock indicator** ✅ Already implemented
   - File: `desktop/shell/keyboard_osd.c` (211 lines)
   - Trigger: Caps Lock key press
   - Design: Floating indicator with icon and text

6. **[x] Keyboard shortcut help overlay** ✅ Connected to desktop shell
   - File: `desktop/shell/desktop_shell.c`
   - Trigger: Long-press NX key (500ms)
   - Design: Full-screen overlay with shortcut list
   - Quick tap still toggles App Drawer

### Nice to Have (Polish)

7. **[x] Dark mode toggle OSD** ✅ Implemented
   - File: `desktop/shell/theme_osd.c` (160 lines)
   - Trigger: Toggle Dark Mode in Control Center
   - Design: Floating pill with moon/sun icon, 2-second fade
8. **[x] WiFi connected notification** ✅ Implemented
   - File: `desktop/shell/control_center.c`
   - Trigger: Toggle WiFi in Control Center
   - Design: Green notification on connect, red on disconnect
9. **[x] Low battery warning popup** ✅ Implemented
   - File: `desktop/shell/battery_warning.c` (215 lines)
   - Trigger: Battery falls below 20%, 10%, or 5%
   - Design: Color-coded modal (yellow→orange→red), 8-second display
10. **[x] Archive-Pie.app** ✅ Native archive manager
    - Location: `desktop/apps/Archive-Pie.app/`
    - Features: Open/Extract/Add ZIP files, file list, status bar
    - Uses nxzip compression library with Dynamic Huffman

---

## ✅ WHAT'S ACTUALLY WORKING

### Kernel (Verified)

- [x] UEFI bootloader with recovery menu (F2)
- [x] 64-bit kernel with GDT/IDT/TSS
- [x] 14 kernel drivers loaded (including nxusb_kdrv)
- [x] PS/2 keyboard (IRQ1) and mouse (IRQ12)
- [x] Framebuffer syscalls (nx_fb_map, nx_fb_flip)
- [x] Input syscalls (nx_input_poll, nx_input_register)
- [x] Cursor syscall (nx_cursor_set)

### Desktop Shell (Verified)

- [x] Main loop at 60 FPS with vsync
- [x] Background gradient rendering
- [x] Navigation bar with clock
- [x] Globe clock widget with real RTC time
- [x] Anveshan search bar rendering
- [x] Dock with icons and hover
- [x] Window creation and z-ordering
- [x] Mouse tracking and click handling
- [x] App drawer initialization

### Desktop Modules (Code Exists, Rendering May Fail)

- [?] Control Center - `control_center_render()` called in main loop
- [?] Notification Center - `notification_center_render()` called
- [?] Quick Access Menu - renders when `qm->visible`

---

## 🧪 DEBUGGING STEPS

### Step 1: Verify Control Center Toggle

```c
/* In boot_test.sh QEMU output, look for: */
[DESKTOP] Control center initialized

/* Then click the grid icon in nav bar */
/* Serial should show: */
[CC] Toggle called
[CC] visible = 1
```

### Step 2: Check Alpha Blending

```c
/* In control_center.c, temporarily change line 131: */
/* From: */ fill_rounded_rect(x, y, w, h, 12, 0xC0202030);
/* To:   */ fill_rounded_rect(x, y, w, h, 12, 0xFF202030);
/* 0xC0 = 75% opacity, 0xFF = 100% opacity */
```

### Step 3: Force Show Control Center

```c
/* In desktop_shell.c around line 1763, add: */
control_center_show();  /* Force visible on boot */
```

---

## 📊 UPDATED PROGRESS METER

```
KERNEL CORE        [████████████████████] 100% ✅
USB DRIVER         [████████████████████] 100% ✅ (corrected)
MOUSE DRIVER       [████████████████████] 100% ✅
KEYBOARD DRIVER    [████████████████████] 100% ✅
FRAMEBUFFER        [████████████████████] 100% ✅
DESKTOP SHELL      [██████████████░░░░░░]  70% 🟡 (rendering gaps)
CONTROL CENTER     [████████░░░░░░░░░░░░]  40% 🔴 (may not display)
VOLUME OSD         [░░░░░░░░░░░░░░░░░░░░]   0% 🔴 (not implemented)
KEYBOARD OSD       [░░░░░░░░░░░░░░░░░░░░]   0% 🔴 (not implemented)
NETWORKING         [████████████░░░░░░░░]  60% 🔄
SMP                [░░░░░░░░░░░░░░░░░░░░]   0% ⏳
---------------------------------------------------------
OVERALL COMPLETENESS: ~50%
```

---

## 🎯 IMMEDIATE ACTION ITEMS

### This Week: Fix Desktop Rendering

1. **Debug control center visibility**
   - Add serial logging in `control_center_toggle()`
   - Test in QEMU, watch serial output
2. **Debug slider visibility**
   - Change slider alpha to 0xFF
   - Verify sliders appear
   - Tune alpha for glass effect

3. **Forward keyboard to focused window**
   - Implement TODO in `desktop_handle_key()` line 1701
   - Allow apps to receive keyboard input

### Next Week: Implement OSD Components

4. **Create volume_osd.c**
   - Floating popup when volume changes
   - 3-second fade out
   - Horizontal bar with percentage

5. **Create keyboard_osd.c**
   - Caps Lock indicator
   - Shortcut helper popup

---

## 🔧 KEY FILES FOR DESKTOP FIXES

| Purpose               | File                             | Lines to Check |
| --------------------- | -------------------------------- | -------------- |
| Control Center toggle | `desktop/shell/control_center.c` | 200-212        |
| Control Center render | `desktop/shell/control_center.c` | 348-431        |
| Slider render         | `desktop/shell/control_center.c` | 329-344        |
| Main render loop      | `desktop/shell/desktop_shell.c`  | 1169-1285      |
| Keyboard handler      | `desktop/shell/desktop_shell.c`  | 1685-1702      |
| Nav bar with icons    | `desktop/shell/desktop_shell.c`  | 674-764        |

---

## 📝 NOTES

- **USB works** - `nxusb_kdrv_init()` is called in kernel_main.c line 364-367
- **Mouse works** - `nxmouse_kdrv_init()` called in kernel_main.c line 487-492
- **Keyboard works** - `kbd_init()` called in kernel_main.c line 479-483
- **Desktop renders** - Main loop runs at 60 FPS, basic elements visible
- **Components not visible** - Control center, volume OSD, keyboard OSD need work

---

**Copyright (c) 2025-2026 KetiveeAI**
