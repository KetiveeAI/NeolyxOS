# NXRender C

**Pure C GUI framework for NeolyxOS kernel integration.**

Converted from Rust (14,263 lines) to C (~3,000 lines) with zero dependencies.

## Quick Start

```c
#include <nxrender.h>

void on_init(nx_application_t* app, void* data) {
    nx_window_t* win = nx_window_create("Hello", nx_rect(100, 100, 400, 300), 0);
    
    nx_vbox_t* vbox = nx_vbox_create();
    nx_vbox_set_gap(vbox, 12);
    
    nx_label_t* label = nx_label_create("Welcome to NeolyxOS!");
    nx_button_t* btn = nx_button_create("Click Me");
    
    nx_widget_add_child((nx_widget_t*)vbox, (nx_widget_t*)label);
    nx_widget_add_child((nx_widget_t*)vbox, (nx_widget_t*)btn);
    
    nx_window_set_root_widget(win, (nx_widget_t*)vbox);
    nx_wm_add_window(nx_app_window_manager(app), win);
}

int main() {
    nx_app_config_t cfg = nx_app_default_config();
    cfg.title = "My App";
    cfg.on_init = on_init;
    
    nx_application_t* app = nx_app_create(cfg);
    return nx_app_run(app, framebuffer, pitch);
}
```

## Build

```bash
make              # Build libnxrender.a
make clean        # Clean
```

## Components

| Module | Description |
|--------|-------------|
| `nxgfx` | Framebuffer graphics, primitives, text |
| `nxrender` | Compositor, window manager, app framework |
| `widgets` | Button, Label, TextField, VBox, HBox, ScrollView |
| `layout` | Flexbox layout engine |
| `theme` | Light/dark themes with color palette |
| `animation` | Easing functions, spring physics |

## API Summary

### Graphics
- `nxgfx_init()`, `nxgfx_destroy()` - Context management
- `nxgfx_fill_rect()`, `nxgfx_draw_rect()` - Rectangles
- `nxgfx_fill_circle()`, `nxgfx_draw_line()` - Shapes
- `nxgfx_draw_text()`, `nxgfx_measure_text()` - Text

### Windows
- `nx_window_create()`, `nx_window_destroy()` - Lifecycle
- `nx_wm_create()`, `nx_wm_add_window()` - Manager
- `nx_wm_render()`, `nx_wm_handle_event()` - Rendering

### Widgets
- All widgets implement `nx_widget_t` base with vtable
- `nx_widget_add_child()` - Parent owns child (memory rule)
- `nx_widget_destroy()` - Recursively frees children

### Layout
- `nx_flex_row()`, `nx_flex_column()` - Flexbox presets
- `nx_layout_flex()` - Apply flexbox to children

### Theme
- `nx_theme_dark()`, `nx_theme_light()` - Built-in themes
- `nx_set_theme()`, `nx_get_theme()` - Global theme

### Animation
- `nx_anim_init()`, `nx_anim_start()` - Keyframe animation
- `nx_spring_init()`, `nx_spring_update()` - Spring physics
- 10 easing functions: linear, quad, cubic, back, elastic, bounce

## License

Copyright (c) 2025 KetiveeAI
