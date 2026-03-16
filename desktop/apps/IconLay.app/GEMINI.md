# GEMINI.md - IconLay AI Engineering Guide

**Project:** IconLay - NeolyxOS Native Icon FX Compositor  
**Language:** C (NOT TypeScript/web technologies)  
**Status:** Core Complete ✅ | Effects System Complete ✅ | Export Needs Enhancement 🟡  
**Last Updated:** January 16, 2026

---

## 🎯 Project Overview

IconLay is a **native NeolyxOS application** for designing and compositing icons with visual effects. It imports flat geometry (SVG), applies effects (Glass, Frost, Blur, Glow, Edge), and exports to the `.nxi` format for all NeolyxOS platforms.

### Technology Stack

| Component | Technology | File |
|-----------|------------|------|
| **Main App** | C | `src/main_nxrender.c` (1113 lines) |
| **Rendering** | nxrender_backend + SDL2 | `src/nxrender_backend.c` |
| **Format** | NXI (Custom Binary) | `src/nxi_format.c` |
| **SVG Import** | nxsvg | `src/svg_import.c` |
| **Layers** | Custom layer system | `src/layer.c` |

### Core Features

- ✅ 1024×1024 base canvas with Figma-style zoom/pan
- ✅ Multi-layer composition with z-ordering
- ✅ SVG import with path preservation
- ✅ 6 visual effects (None, Glass, Frost, Blur, Edge, Glow)
- ✅ HSV color picker with live preview
- ✅ Platform previews (Desktop, Mobile, Watch, Tablet)
- ✅ NXI format save/load
- ✅ Multi-size export presets
- ✅ PNG export with alpha transparency
- ✅ Linear/Radial gradient fills

---

## 📁 Directory Structure

```
IconLay.app/
├── GEMINI.md              # THIS FILE - AI Engineering Guide
├── Makefile               # Build configuration
├── note.ai                # Original requirements doc
│
├── include/               # Header files
│   ├── nxi_format.h       # NXI format definitions
│   ├── layer.h            # Layer system
│   ├── svg_import.h       # SVG import API
│   └── nxrender_backend.h # Rendering abstraction
│
├── src/                   # Source files
│   ├── main_nxrender.c    # Main application (1113 lines)
│   ├── nxrender_backend.c # NXRender backend
│   ├── nxi_format.c       # NXI save/load
│   ├── svg_import.c       # SVG parsing
│   ├── layer.c            # Layer management
│   ├── iconlay_effects.c  # Effect implementations
│   └── colorpicker.c      # Color picker widget
│
└── resource/              # Assets
    └── icons/             # UI icons
```

---

## 🔧 Architecture

### Application State (`app_state_t`)

```c
typedef struct {
    SDL_Window* window;
    SDL_Surface* surface;
    
    nxi_icon_t* icon;           // Current icon project
    nxsvg_image_t** layer_svgs; // Cached SVG renders
    int selected_layer;         // -1 = base, 0+ = layer index
    
    int running;
    int dragging_layer;
    float drag_offset_x, drag_offset_y;
    
    /* View */
    int show_grid;
    int show_base;
    bg_mode_t bg_mode;          // BG_DARK, BG_LIGHT, BG_COLOR
    platform_t preview_platform;
    float canvas_zoom;          // 0.1 to 256.0
    float canvas_pan_x, canvas_pan_y;
    
    /* FX */
    fx_type_t current_fx;
    float fx_intensity;         // 0.0 to 1.0
    float fx_blur;              // 0.0 to 1.0
    
    /* Color picker */
    int picker_open;
    float picker_h, picker_s, picker_v;  // HSV values
    
    int drag_slider;            // Which slider is being dragged
} app_state_t;
```

### Render Pipeline

```
┌─────────────────────────────────────────────────────────────┐
│                        Frame Loop                            │
├─────────────────────────────────────────────────────────────┤
│  1. nxrender_backend_clear(COL_BG)                          │
│  2. render_toolbar()      - Top bar with tools              │
│  3. render_layers()       - Left sidebar layer list         │
│  4. render_canvas()       - Main editing area               │
│  5. render_fx_panel()     - Right sidebar effects           │
│  6. render_previews()     - Bottom platform previews        │
│  7. render_color_picker() - Modal overlay (if open)         │
│  8. nxrender_backend_present()                              │
│  9. SDL_UpdateWindowSurface()                               │
└─────────────────────────────────────────────────────────────┘
```

### Effect Rendering (in `render_canvas`)

Effects are applied per-layer during render:

```c
/* Effect flags from nxi_format.h */
#define NXI_EFFECT_NONE    0x00
#define NXI_EFFECT_GLASS   0x01  // Highlight overlay
#define NXI_EFFECT_FROST   0x02  // Noise blur pattern
#define NXI_EFFECT_BLUR    0x04  // Gaussian blur simulation
#define NXI_EFFECT_EDGE    0x08  // White outline
#define NXI_EFFECT_GLOW    0x10  // Outer glow

/* Applied in render_canvas() lines 484-550 */
if (layer->effect_flags & NXI_EFFECT_GLOW) { ... }
if (layer->effect_flags & NXI_EFFECT_BLUR) { ... }
if (layer->effect_flags & NXI_EFFECT_GLASS) { ... }
if (layer->effect_flags & NXI_EFFECT_FROST) { ... }
if (layer->effect_flags & NXI_EFFECT_EDGE) { ... }
```

---

## 🎨 UI Layout Constants

```c
#define WINDOW_WIDTH   1280
#define WINDOW_HEIGHT  860
#define TOOLBAR_HEIGHT 56
#define SIDEBAR_WIDTH  260
#define PREVIEW_BAR_HEIGHT 110
#define CANVAS_SIZE    480

/* Colors (NeolyxOS dark theme) */
#define COL_BG          0x18181BFF
#define COL_SIDEBAR     0x27272AFF
#define COL_TOOLBAR     0x1F1F23FF
#define COL_PANEL       0x3F3F46FF
#define COL_BORDER      0x52525BFF
#define COL_ACCENT      0x6366F1FF  /* Indigo */
#define COL_TEXT        0xFAFAFAFF
#define COL_TEXT_DIM    0xA1A1AAFF
```

---

## ⌨️ Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `S` | Save to output.nxi |
| `E` | Export all size presets (NXI + PNG) |
| `P` | Export single PNG (512px) |
| `G` | Toggle grid overlay |
| `B` | Toggle base shape |
| `C` | Open/close color picker |
| `1` | Dark background mode |
| `2` | Light background mode |
| `Delete` | Remove selected layer |
| `Escape` | Close picker / Quit |
| `Ctrl+Scroll` | Zoom canvas |
| `Shift+Scroll` | Pan horizontal |
| `Scroll` | Pan vertical |

---

## 📋 NXI Format Specification

### Header (nxi_header_t)

```c
typedef struct {
    char magic[4];          // "NXIC"
    uint16_t version;       // Format version (1)
    uint16_t base_size;     // 1024
    uint16_t export_size;   // Target size for this export
    uint8_t color_depth;    // NXI_DEPTH_RGBA32
    uint8_t layer_count;
    uint8_t has_effects;    // Global effects flag
    uint8_t reserved;
} nxi_header_t;
```

### Layer Header (nxi_layer_header_t)

```c
typedef struct {
    char name[32];
    uint8_t visible;
    uint8_t opacity;        // 0-255
    float pos_x, pos_y;     // 0.0-1.0 normalized
    float scale;            // 0.1-2.0
    uint32_t fill_color;    // RGBA
    uint8_t effect_flags;
    uint32_t path_count;
} nxi_layer_header_t;
```

### Export Presets

| Preset | Sizes |
|--------|-------|
| Desktop | 16, 24, 32, 48, 64, 128, 256, 512 |
| Mobile | 48, 72, 96, 120, 144, 192 |
| Watch | 24, 38, 42, 48 |
| All | 16, 24, 32, 38, 42, 48, 64, 72, 96, 120, 128, 144, 192, 256, 512 |

---

## 🟡 Improvements in Progress

### ✅ PNG Export (IMPLEMENTED)

**Status:** Implemented in January 2026.

**Usage:**
- Press `P` to export single 512px PNG
- Press `E` to export all presets (NXI + PNG)

**API:**
```c
/* Single PNG export */
int nxi_export_png(const nxi_icon_t* icon, uint32_t size, 
                   const char* path, void* render_ctx);

/* Preset-based PNG export */
int nxi_export_png_preset(const nxi_icon_t* icon, nxi_preset_t preset, 
                          const char* output_dir, void* render_ctx);

/* Render to RGBA buffer (for custom processing) */
uint8_t* nxi_render_to_buffer(const nxi_icon_t* icon, uint32_t size, 
                               void* render_ctx);
```

---

### ✅ Gradient Support (IMPLEMENTED)

**Status:** Implemented in January 2026.

**Features:**
- Linear and radial gradient types
- Up to 8 color stops
- Angle control for linear gradients
- Center/radius control for radial gradients
- Toggleable per-layer via FX panel

**API:**
```c
typedef struct {
    uint8_t type;            /* NXI_GRADIENT_LINEAR or NXI_GRADIENT_RADIAL */
    float angle;             /* For linear: 0-360 degrees */
    float cx, cy, radius;    /* For radial */
    uint8_t stop_count;      /* 2-8 */
    nxi_gradient_stop_t stops[8];
} nxi_gradient_t;
```

---

## 🔴 Known Gaps and Improvements Needed

### 1. Undo/Redo Stack (Priority: MEDIUM)

**Current State:** No undo functionality.

**Required:** Command pattern with undo stack.

```c
typedef struct {
    undo_action_t type;     // UNDO_MOVE, UNDO_COLOR, UNDO_ADD, UNDO_DELETE
    int layer_index;
    union {
        struct { float x, y; } position;
        uint32_t color;
        nxi_layer_t* layer_copy;
    } data;
} undo_entry_t;

#define UNDO_STACK_SIZE 50
typedef struct {
    undo_entry_t entries[UNDO_STACK_SIZE];
    int current;
    int count;
} undo_stack_t;
```

---

### 4. Layer Scale Slider (Priority: LOW)

**Current State:** Scale slider exists but may not update cached SVG.

**Fix needed in `main_nxrender.c`:** Invalidate `layer_svgs[i]` when scale changes.

---

### 5. Import Button Functionality (Priority: HIGH)

**Current State:** Import button rendered but may not open file dialog.

**Required:** Implement file dialog using system API or zenity fallback.

```c
/* In toolbar click handler (line ~259) */
if (mx >= 64 && mx < 144 && my >= 10 && my < 46) {
    // Open file dialog
    char filepath[256];
    if (open_file_dialog(filepath, "*.svg") == 0) {
        svg_import_opts_t opts = svg_import_defaults();
        svg_import_multilayer(filepath, app->icon, &opts);
    }
}
```

---

### 6. Export Button with Format Selection (Priority: HIGH)

**Current State:** Export button exists, only exports NXI.

**Required:** Modal dialog to choose format (NXI, PNG) and sizes.

---

## 🛠️ Code Quality Guidelines

### Memory Management

```c
/* Always check allocations */
layer->paths = realloc(layer->paths, new_size);
if (!layer->paths) {
    /* Handle error - never ignore */
    return -1;
}

/* Free in reverse order of allocation */
nxsvg_free(app->layer_svgs[i]);
app->layer_svgs[i] = NULL;  /* Prevent double-free */
```

### SVG Cache Invalidation

**CRITICAL:** When modifying layer properties that affect rendering, invalidate the cached SVG:

```c
/* After changing fill_color, scale, path data, etc. */
if (app->layer_svgs && app->layer_svgs[layer_index]) {
    nxsvg_free(app->layer_svgs[layer_index]);
    app->layer_svgs[layer_index] = NULL;
}
```

### Bounds Checking

```c
/* Always validate indices */
if (app->selected_layer >= 0 && 
    app->selected_layer < (int)app->icon->layer_count) {
    nxi_layer_t* layer = app->icon->layers[app->selected_layer];
    /* Safe to use layer */
}
```

### Event Handling Order

In `SDL_MOUSEBUTTONDOWN` handler (lines 797-993):

1. Check modal overlays first (picker_open)
2. Check toolbar area
3. Check sidebars
4. Check canvas last

---

## 🧪 Testing Checklist

### Manual Tests

- [ ] Import SVG file via command line
- [ ] Add/remove layers
- [ ] Drag layers on canvas
- [ ] Apply each effect type
- [ ] Adjust sliders (opacity, blur, intensity)
- [ ] Use color picker
- [ ] Zoom in/out with Ctrl+scroll
- [ ] Pan with scroll/Shift+scroll
- [ ] Save to NXI
- [ ] Load NXI file
- [ ] Export presets
- [ ] Switch platforms in preview bar
- [ ] Toggle grid/base overlays

### Edge Cases

- [ ] Zero layers
- [ ] Maximum layers (test with 32)
- [ ] Very small zoom (0.1x)
- [ ] Very large zoom (256x)
- [ ] Invalid SVG files
- [ ] Missing file paths

---

## 🔨 Build Commands

```bash
# Full build
cd /home/swana/Documents/NEOLYXOS/neolyx-os/desktop/apps/IconLay.app
make clean && make

# Run with SVG argument
./iconlay /path/to/icon.svg

# Run without argument (empty canvas)
./iconlay
```

### Dependencies

- SDL2
- SDL2_image
- Math library (libm)
- POSIX file I/O

---

## 📝 Adding New Features

### Adding a New Effect

1. **Define flag in `include/nxi_format.h`:**
   ```c
   #define NXI_EFFECT_NEWEFFECT 0x20
   ```

2. **Add enum in `main_nxrender.c`:**
   ```c
   typedef enum {
       FX_NONE = 0,
       // ...
       FX_NEWEFFECT
   } fx_type_t;
   ```

3. **Add button in `render_fx_panel()`** (line ~371)

4. **Implement rendering in `render_canvas()`** (line ~484):
   ```c
   if (layer->effect_flags & NXI_EFFECT_NEWEFFECT) {
       /* Render effect */
   }
   ```

5. **Add button click handler** (line ~891)

### Adding a New Export Format

1. **Create function in `nxi_format.c`:**
   ```c
   int nxi_export_FORMAT(const nxi_icon_t* icon, uint32_t size, const char* path);
   ```

2. **Declare in `include/nxi_format.h`**

3. **Add UI in `render_toolbar()` or create export dialog**

4. **Add keyboard shortcut if needed**

---

## ⚠️ Critical Rules

1. **Never include web technologies** - IconLay is pure C
2. **Use nxrender_backend for all drawing** - No direct SDL rendering
3. **Invalidate SVG cache on layer changes** - Prevents stale renders
4. **Check null pointers** - Especially `app->icon` and `app->layer_svgs`
5. **Normalize coordinates** - Layer positions are 0.0-1.0 range
6. **Respect layer visibility** - Skip invisible layers in rendering and hit tests

---

## 📚 Related Files

| File | Purpose |
|------|---------|
| `gui/nxrender_c/GEMINI.md` | NXRender C library guide |
| `neolyx-os/GEMINI.md` | Overall OS guide |
| `note.ai` | Original requirements document |

---

**Copyright (c) 2025-2026 KetiveeAI / NeolyxOS**
