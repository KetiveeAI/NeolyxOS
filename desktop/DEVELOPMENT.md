# NeolyxOS Desktop Shell - Modular Architecture Development Plan

## Vision
Transform the desktop shell from hardcoded to fully modular, dynamic, and customizable - matching macOS quality with Settings.app integration.

---

## Phase 1: Core Modular Architecture

### 1.1 Configuration System
**Uses existing `.nlc` format** (INI-style) from `/System/Config/user_defaults.nlc`:

```ini
[desktop]
theme = "dark"
accent_color = "0x0000B4D8"
wallpaper = "/System/Library/Wallpapers/default.nix"
dock_position = "bottom"
menubar_transparency = "true"

[dock]
icon_size = "48"
minimize_effect = "scale"
bounce_on_launch = "true"

[window]
animations_enabled = "true"
animation_speed = "normal"
snap_enabled = "true"
```

**Files: ✅ Created**
- `desktop/include/nxconfig.h` - Config API (.nlc parser)
- `desktop/lib/nxconfig.c` - Config loader/saver

### 1.2 Event Bus System
Decouple components with message passing:

```c
// Components subscribe to events
nxevent_subscribe(NX_EVENT_THEME_CHANGED, on_theme_change, NULL);
nxevent_subscribe(NX_EVENT_DOCK_RESIZE, on_dock_resize, NULL);

// Settings.app publishes changes
nxevent_publish_theme("dark", 0x0000B4D8);
```

**Files: ✅ Created**
- `desktop/include/nxevent.h` - Event types and API
- `desktop/lib/nxevent.c` - Publish/subscribe implementation

---

## Phase 2: Window Manager (Stage Manager)

### 2.1 Window State Machine
```
WINDOW_MINIMIZED → WINDOW_NORMAL → WINDOW_MAXIMIZED
                       ↓
                 WINDOW_STAGED (multi-app tier)
```

### 2.2 Stage Manager Features
- Left sidebar showing app tiles (like macOS Ventura)
- Drag windows between stages
- Auto-arrange on stage switch
- Smooth animations (200ms ease-out)

**Files:**
- `desktop/shell/window_manager.c`
- `desktop/shell/stage_manager.c`
- `desktop/include/window_manager.h`

---

## Phase 3: Menu Bar (macOS-style)

### 3.1 Structure
```
┌─────────────────────────────────────────────────────────┐
│ 🍎 │ App Name │ File Edit View │ ··· │ WiFi 🔋 Clock │
└─────────────────────────────────────────────────────────┘
  │       │           │                    │
  │       │           │                    └── System tray
  │       │           └── App menus (dynamic)
  │       └── Focused app name
  └── NeolyxOS icon (click for system menu)
```

### 3.2 NeolyxOS Menu (click logo)
- About NeolyxOS
- System Preferences...
- App Store...
- Recent Items →
- Force Quit...
- Sleep / Restart / Shut Down...

---

## Phase 4: Dock Customization

### 4.1 Configurable Properties
| Property | Range | Default |
|----------|-------|---------|
| Position | bottom/left/right | bottom |
| Size | 32-128px | 64px |
| Icon Size | 24-96px | 48px |
| Magnification | off/1.0-2.0x | 1.5x |
| Auto-hide | on/off | off |

### 4.2 Animation
- Bounce on launch
- Genie minimize effect
- Magnification on hover

---

## Phase 5: Settings.app Integration

Connect existing panels in `Settings.app/panels/`:
- `appearance_panel.c` - Theme, colors, transparency
- `display_panel.c` - Resolution, HDR
- `dock_panel.c` [NEW] - Dock customization

---

## Phase 6: Visual Effects

### 6.1 Transparency/Blur
- Menu bar: 80% opacity + blur
- Dock: 70% opacity + blur
- Windows: Optional vibrancy

### 6.2 Animations
| Action | Duration | Easing |
|--------|----------|--------|
| Window open | 200ms | ease-out |
| Window close | 150ms | ease-in |
| Minimize | 300ms | genie |
| Maximize | 200ms | linear |

---

## Phase 7: App Management

### 7.1 App Discovery
Scan `desktop/apps/` for `.app` bundles containing `manifest.npa`:

```json
{
    "name": "Path",
    "bundle_id": "com.neolyx.path",
    "binary": "bin/path",
    "icon": "resources/path.nxi",
    "permissions": ["filesystem.read", "filesystem.write"],
    "menu": { ... }  // App-specific menus
}
```

### 7.2 Always-Running Apps
| App | Bundle ID | Purpose |
|-----|-----------|---------|
| **Path.app** | `com.neolyx.path` | File manager (like Finder) |
| SystemUIServer | `com.neolyx.systemui` | Menu bar extras (clock, WiFi, battery) |
| Dock | `com.neolyx.dock` | Application launcher dock |

### 7.3 Path.app Features
The file manager (`Path.app`) includes:
- **Views**: Icons, List, Columns, Gallery (`Cmd+1/2/3/4`)
- **Navigation**: Tabs, sidebar with favorites
- **Operations**: Cut/Copy/Paste, Drag & Drop, Quick Look
- **Context Menu**: Get Info, Rename, Move to Trash

---

## Implementation Priority

### Week 1-2: Foundation ✅ COMPLETE
- [x] `nxconfig.h/c` - Config system (.nlc parser, get/set for all types)
- [x] `nxevent.h/c` - Event bus (subscribe/publish, queued processing)
- [x] Extract dock to `dock.h/c` - Modular dock with magnification, bounce, autohide

### Week 3-4: Window Manager
- [ ] `window_manager.c` - Window states
- [ ] `stage_manager.c` - Multi-app tiers

### Week 5-6: Menu Bar
- [ ] `menu_bar.c` - Top bar rendering
- [ ] `system_menu.c` - NeolyxOS menu

### Week 7-8: Settings Integration
- [ ] Connect appearance_panel.c
- [ ] Live preview system

---

## Current Status

### Completed Files
| File | Lines | Purpose |
|------|-------|---------|
| `include/nxconfig.h` | 198 | Config API (.nlc parser) |
| `lib/nxconfig.c` | 376 | Config implementation with default values |
| `include/nxevent.h` | 195 | Event bus API |
| `lib/nxevent.c` | 264 | Event bus implementation |
| `include/dock.h` | 142 | Dock module API |
| `shell/dock.c` | 420 | Dock rendering, magnification, events |

### Settings.app Integration
The dock module uses settings from `nxappearance.h`:
- `dock_icon_size` (32-80px, default 48)
- `dock_magnification` (bool)
- `dock_magnification_amount` (1.0-2.0x)
- `dock_auto_hide` (bool)
- `glass_opacity` (0-255)

### Next Steps (Phase 2)

1. **Integrate Dock in desktop_shell.c**
   - Replace inline `render_dock()` with `dock_render()` call
   - Call `dock_init()` in `desktop_init()`
   - Call `dock_tick()` in main loop

2. **Create Window Manager** (`shell/window_manager.c`)
   - Window state machine (normal ↔ minimized ↔ maximized)
   - Focus tracking
   - Z-order management

3. **Integrate Event Bus**
   - Call `nxevent_init()` in desktop startup
   - Call `nxevent_process()` in main loop
   - Settings.app publishes changes via `nxevent_publish_theme()`

---

## File Structure After Refactor

```
desktop/
├── config/
│   ├── desktop.conf
│   ├── theme.conf
│   └── dock.conf
├── include/
│   ├── nxconfig.h
│   ├── nxevent.h
│   ├── window_manager.h
│   ├── dock.h
│   └── menu_bar.h
├── lib/
│   ├── nxconfig.c
│   ├── nxevent.c
│   └── nxanimation.c
├── shell/
│   ├── desktop_shell.c (coordinator)
│   ├── window_manager.c
│   ├── stage_manager.c
│   ├── dock.c
│   ├── menu_bar.c
│   └── system_menu.c
└── apps/
    └── Settings.app/
```
