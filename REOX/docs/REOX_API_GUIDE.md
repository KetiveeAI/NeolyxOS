# REOX Language & API Guide

## Overview

REOX (Reactive/Extensible Objects) is a **custom programming language** for NeolyxOS that compiles to C. It provides a SwiftUI-like declarative syntax for building native GUI applications.

---

## Two Ways to Use ReoxUI

### Option 1: Write in .rx (REOX Language)
```
app.rx  →  [REOX Compiler]  →  app.c  →  [GCC]  →  app executable
```

### Option 2: Write Directly in C
```
app.c  →  [GCC + libreox_runtime.a]  →  app executable
```

The Settings app uses **Option 2** (direct C) because it's compiled with the kernel.

---

## REOX Language Syntax (.rx files)

### File Structure
```rust
// 1. Type definitions (structs)
struct Color { r: int, g: int, b: int, a: int }
struct View { handle: int }

// 2. External function declarations (from runtime)
extern fn println(s: string) -> void;
extern fn vstack(gap: float) -> View;

// 3. Your functions
fn helper() -> void { }

// 4. Main entry point
fn main() -> int {
    // Your code
    return 0;
}
```

### Key Syntax Elements

| Feature | REOX Syntax | C Equivalent |
|---------|-------------|--------------|
| Variable | `let x: int = 5;` | `int x = 5;` |
| Function | `fn foo(a: int) -> int { }` | `int foo(int a) { }` |
| Struct | `struct Point { x: float, y: float }` | `typedef struct { float x; float y; } Point;` |
| Extern | `extern fn name() -> Type;` | `extern Type name(void);` |
| Comment | `// comment` | `// comment` |

---

## C API (libreox_runtime)

When writing in C (like the Settings app), use these functions:

### App & Window

```c
// Create application
rx_app* app = app_new("App Name");

// Create window
rx_window* window = app_create_window(app, "Title", 800, 600);

// Set root view
window->root_view = root_view;

// Run event loop
app_run(app);
```

### Layout Containers

```c
// Vertical stack (items top to bottom)
rx_view* column = vstack_new(16.0f);  // 16px gap between items

// Horizontal stack (items left to right)
rx_view* row = hstack_new(8.0f);  // 8px gap

// Add children
view_add_child(column, child_view);
```

### Views

```c
// Text
rx_text_view* label = text_view_new("Hello World");
text_view_set_font_size(label, 18.0f);
label->font_weight = 600;
label->color = (rx_color){ 255, 255, 255, 255 };

// Button
rx_button_view* btn = button_view_new("Click Me");
btn->normal_color = (rx_color){ 0, 122, 255, 255 };
btn->hover_color = (rx_color){ 30, 144, 255, 255 };
btn->pressed_color = (rx_color){ 0, 100, 220, 255 };
```

### Styling (Box Model)

```c
rx_view* card = vstack_new(12.0f);

// Background color
card->box.background = (rx_color){ 44, 44, 46, 255 };

// Padding (all sides)
card->box.padding = insets_all(16.0f);

// Corner radius
card->box.corner_radius = corners_all(12.0f);

// Shadow
card->box.shadow = shadow(0.0f, 4.0f, 16.0f, (rx_color){ 0, 0, 0, 80 });

// Size
card->box.width = 200;
card->box.height = 100;
```

### Colors

```c
// Define a color
rx_color red = { 255, 0, 0, 255 };  // R, G, B, A

// Predefined colors (from settings.h or theme)
#define NX_COLOR_PRIMARY     ((rx_color){ 0, 122, 255, 255 })
#define NX_COLOR_BACKGROUND  ((rx_color){ 28, 28, 30, 255 })
#define NX_COLOR_SURFACE     ((rx_color){ 44, 44, 46, 255 })
#define NX_COLOR_TEXT        ((rx_color){ 255, 255, 255, 255 })
```

---

## Complete C Example

```c
#include "reox_runtime.h"
#include "reox_ui.h"

int main(void) {
    // 1. Create app
    rx_app* app = app_new("My App");
    rx_window* win = app_create_window(app, "My Window", 640, 480);
    
    // 2. Build UI
    rx_view* root = vstack_new(16.0f);
    root->box.background = (rx_color){ 28, 28, 30, 255 };
    root->box.padding = insets_all(24.0f);
    
    // Title
    rx_text_view* title = text_view_new("Welcome!");
    text_view_set_font_size(title, 28.0f);
    title->font_weight = 700;
    title->color = (rx_color){ 255, 255, 255, 255 };
    view_add_child(root, (rx_view*)title);
    
    // Button
    rx_button_view* btn = button_view_new("Get Started");
    btn->normal_color = (rx_color){ 0, 122, 255, 255 };
    view_add_child(root, (rx_view*)btn);
    
    // 3. Set root and run
    win->root_view = root;
    app_run(app);
    
    return 0;
}
```

### Build Command
```bash
gcc -o myapp myapp.c -I/path/to/REOX/reolab/runtime \
    /path/to/REOX/reolab/runtime/libreox_runtime.a
```

---

## Settings App Pattern

The Settings app uses a **panel architecture**:

```c
// settings.h - defines shared types
typedef struct settings_ctx {
    rx_app* app;
    rx_window* window;
    panel_id_t active_panel;
} settings_ctx_t;

// Each panel is a separate C file
rx_view* system_panel_create(settings_ctx_t* ctx);
rx_view* network_panel_create(settings_ctx_t* ctx);
```

### Helper Functions (from settings.h)

```c
// Create a styled card
rx_view* card = settings_card("Card Title");

// Create a label-value row
rx_view* row = settings_row("CPU", "Intel Core i7");

// Create a dim label
rx_text_view* note = settings_label("Some note", true);
```

---

## When to Use .rx vs C

| Use .rx (REOX Language) | Use C Directly |
|-------------------------|----------------|
| Standalone desktop apps | Kernel/boot apps |
| Quick prototypes | Performance-critical |
| Learning ReoxUI | Integration with C code |
| Simple UIs | Complex state management |

---

## Key Files Reference

| File | Purpose |
|------|---------|
| `libreox_runtime.a` | Static library (link with GCC) |
| `reox_runtime.h` | Core runtime types |
| `reox_ui.h` | UI view types and functions |
| `reox_theme.h` | Theming system |
| `reox_animation.h` | Animation system |
| `reox_wrappers.c` | Bridge functions for REOX language |

---

This is how ReoxUI works for building NeolyxOS applications!
