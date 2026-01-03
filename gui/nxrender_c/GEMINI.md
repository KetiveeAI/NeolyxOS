# GEMINI.md - AI Developer Guide for NXRENDER

**Project:** NXRENDER - Custom Rendering System for ZepraBrowser  
**Purpose:** Guide for AI assistants to understand, implement, and maintain NXRENDER  
**Last Updated:** 2024-12-13

---

## 🎯 Project Overview

NXRENDER is a **custom rendering engine** designed to replace third-party dependencies (SDL) in ZepraBrowser. It will later be extracted for NeolyxOS as its native rendering system.

### Goals
1. **Independence**: Remove SDL dependency from ZepraBrowser
2. **Performance**: 60 FPS with 100+ widgets
3. **Cross-platform**: Works on Linux, Windows, macOS, and later NeolyxOS
4. **Reusability**: Extract for NeolyxOS desktop environment

### Architecture Summary
```
┌─────────────────────────────────────────────────────────┐
│                   ZepraBrowser                          │
│  ┌─────────────────────────────────────────────────┐   │
│  │            Browser UI (NXRender)                │   │
│  │  • Tab Bar        • Address Bar                 │   │
│  │  • Navigation     • Bookmarks                   │   │
│  └─────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────┐   │
│  │         Web Content (WebCore + ZebraScript)     │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│                     NXRender                            │
├─────────────────────────────────────────────────────────┤
│  nxrender-widgets/  │  nxrender-layout/  │  nxrender-theme/
│  Button, TextField  │  Flexbox, Grid     │  Light/Dark
│  Label, ListView    │  Stack, Absolute   │  Theming
├─────────────────────────────────────────────────────────┤
│  nxrender-core/     │  nxrender-input/   │  nxrender-animation/
│  Compositor         │  Mouse, Keyboard   │  Easing, Spring
│  Surface, Layer     │  Touch, Gestures   │  Keyframes
├─────────────────────────────────────────────────────────┤
│                      NXGFX                              │
│  Graphics Backend: OpenGL → Vulkan → Custom (NeolyxOS) │
│  Text: FreeType + HarfBuzz                             │
│  Images: PNG, JPEG, WebP, SVG                          │
└─────────────────────────────────────────────────────────┘
```

---

## 📅 Development Schedule

### Weekly Development Plan

| Week | Focus Area | Key Deliverables |
|------|------------|------------------|
| **1-2** | NXGFX Graphics Backend | GPU context, primitives, text rendering |
| **3-4** | Compositor | Surface management, layers, damage tracking |
| **5-6** | Core Widgets | Button, TextField, Label, ListView, etc. |
| **7-8** | Layout Engine | Flexbox, Grid, Stack layouts |
| **9-10** | Theme System | Light/dark themes, color palettes |
| **11-12** | Input Handling | Mouse, keyboard, touch, gestures |
| **13-14** | Animation | Easing, spring physics, keyframes |
| **15-16** | Integration | Browser UI, examples, documentation |

### Daily Task Breakdown

#### Week 1: Graphics Foundation
```
Day 1: Project setup, Cargo workspace initialization
Day 2: GpuContext struct, device initialization
Day 3: Rectangle drawing (fill, stroke)
Day 4: Circle, line, path primitives
Day 5: Gradient support (linear, radial)
Day 6: Shadow rendering
Day 7: Integration testing
```

#### Week 2: Text & Images
```
Day 1: FreeType integration, font loading
Day 2: Glyph rasterization
Day 3: HarfBuzz integration, text shaping
Day 4: Text layout and measurement
Day 5: PNG, JPEG image loading
Day 6: WebP, SVG support
Day 7: Texture caching, optimization
```

#### Week 3: Compositor Core
```
Day 1: Surface struct, buffer management
Day 2: Layer system, z-ordering
Day 3: Opacity blending, transforms
Day 4: Damage tracking algorithm
Day 5: Compositor render loop
Day 6: VSync support
Day 7: Performance testing
```

#### Week 4: Window Management
```
Day 1: Window creation, platform abstraction
Day 2: Window focus handling
Day 3: Window stacking order
Day 4: Multi-window support
Day 5: Modal windows, popups
Day 6: Full-screen support
Day 7: Integration testing
```

#### Week 5: Container Widgets
```
Day 1: Widget trait, base implementation
Day 2: View container widget
Day 3: Window top-level widget
Day 4: ScrollView with scrollbars
Day 5: StackView layered container
Day 6: SplitView resizable panes
Day 7: Widget tree testing
```

#### Week 6: Control Widgets
```
Day 1: Button with states (hover, pressed)
Day 2: Label text display
Day 3: TextField text input
Day 4: Checkbox toggle
Day 5: Switch on/off
Day 6: Slider value input
Day 7: Dropdown menu
```

#### Week 7: Flexbox Layout
```
Day 1: Layout trait, constraints
Day 2: Flex direction (row/column)
Day 3: Justify content
Day 4: Align items
Day 5: Gap spacing, wrap
Day 6: Row/Column helpers
Day 7: Layout testing
```

#### Week 8: Grid & Stack Layout
```
Day 1: Grid track system
Day 2: Fixed, fraction, auto sizing
Day 3: Grid gaps, areas
Day 4: Stack layout z-axis
Day 5: Absolute positioning
Day 6: Layout helpers
Day 7: Performance optimization
```

#### Week 9: Color & Typography
```
Day 1: Theme struct definition
Day 2: Color palette system
Day 3: Semantic colors
Day 4: Typography system
Day 5: Font scales
Day 6: Spacing scale
Day 7: Theme serialization
```

#### Week 10: Theme Runtime
```
Day 1: Light theme implementation
Day 2: Dark theme implementation
Day 3: High contrast theme
Day 4: Runtime theme switching
Day 5: Animated transitions
Day 6: System preference detection
Day 7: Custom theme builder
```

#### Week 11: Mouse & Keyboard
```
Day 1: Event system architecture
Day 2: Mouse move, enter, leave
Day 3: Mouse buttons, click
Day 4: Scroll wheel
Day 5: Keyboard press/release
Day 6: Text input, modifiers
Day 7: Keyboard shortcuts
```

#### Week 12: Touch & Gestures
```
Day 1: Touch event system
Day 2: Multi-touch support
Day 3: Tap gesture
Day 4: Swipe gesture
Day 5: Pinch/zoom gesture
Day 6: Rotate gesture
Day 7: Focus management
```

#### Week 13: Animation Core
```
Day 1: Animator struct
Day 2: Animation properties
Day 3: Linear, ease easing
Day 4: Cubic bezier
Day 5: Spring physics
Day 6: Bounce, elastic
Day 7: Animation scheduler
```

#### Week 14: Advanced Animation
```
Day 1: Keyframe animations
Day 2: Animation chaining
Day 3: Parallel animations
Day 4: Staggered animations
Day 5: View transitions
Day 6: High-level API
Day 7: Animation debugging
```

#### Week 15: Browser Integration
```
Day 1: Application framework
Day 2: Tab bar component
Day 3: Address bar component
Day 4: Navigation buttons
Day 5: Bookmark bar
Day 6: Status bar
Day 7: Context menus
```

#### Week 16: Final Polish
```
Day 1: Replace SDL backend
Day 2: Browser UI migration
Day 3: Example apps
Day 4: Widget gallery
Day 5: API documentation
Day 6: Performance tuning
Day 7: Release preparation
```

---

## 📁 Directory Structure

```
source/nxrender/
├── Cargo.toml                    # Workspace configuration
│
├── nxgfx/                        # Graphics backend (Week 1-2)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── context.rs            # GPU context management
│       ├── buffer.rs             # GPU buffer management
│       ├── primitives/
│       │   ├── mod.rs
│       │   ├── rect.rs           # Rectangle drawing
│       │   ├── circle.rs         # Circle/ellipse
│       │   ├── path.rs           # Bezier paths
│       │   └── line.rs           # Line rendering
│       ├── text/
│       │   ├── mod.rs
│       │   ├── font.rs           # FreeType font loading
│       │   ├── shaper.rs         # HarfBuzz text shaping
│       │   ├── layout.rs         # Text layout
│       │   └── rasterizer.rs     # Glyph rasterization
│       ├── image/
│       │   ├── mod.rs
│       │   ├── loader.rs         # Image loading
│       │   ├── png.rs            # PNG decoder
│       │   ├── jpeg.rs           # JPEG decoder
│       │   ├── svg.rs            # SVG renderer
│       │   └── cache.rs          # Texture cache
│       ├── effects/
│       │   ├── mod.rs
│       │   ├── gradient.rs       # Gradient fills
│       │   ├── shadow.rs         # Shadow rendering
│       │   └── blur.rs           # Blur effects
│       └── backend/
│           ├── mod.rs
│           ├── opengl.rs         # OpenGL ES 3.0 backend
│           ├── vulkan.rs         # Vulkan backend (future)
│           └── software.rs       # CPU fallback
│
├── nxrender-core/                # Compositor (Week 3-4)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── compositor/
│       │   ├── mod.rs
│       │   ├── compositor.rs     # Main compositor
│       │   ├── surface.rs        # Drawing surfaces
│       │   ├── layer.rs          # Layer management
│       │   └── damage.rs         # Damage tracking
│       ├── window/
│       │   ├── mod.rs
│       │   ├── window.rs         # Window abstraction
│       │   ├── manager.rs        # Window manager
│       │   └── focus.rs          # Focus management
│       └── renderer/
│           ├── mod.rs
│           ├── renderer.rs       # Rendering pipeline
│           ├── painter.rs        # High-level painter
│           └── cache.rs          # Render cache
│
├── nxrender-widgets/             # Widget library (Week 5-6)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── base/
│       │   ├── mod.rs
│       │   ├── widget.rs         # Base widget trait
│       │   ├── view.rs           # Container view
│       │   └── window.rs         # Top-level window
│       ├── controls/
│       │   ├── mod.rs
│       │   ├── button.rs         # Button
│       │   ├── textfield.rs      # Text input
│       │   ├── checkbox.rs       # Checkbox
│       │   ├── radio.rs          # Radio button
│       │   ├── slider.rs         # Slider
│       │   ├── switch.rs         # Toggle switch
│       │   └── dropdown.rs       # Dropdown menu
│       ├── display/
│       │   ├── mod.rs
│       │   ├── label.rs          # Text label
│       │   ├── image.rs          # Image view
│       │   ├── icon.rs           # Icon
│       │   └── separator.rs      # Divider
│       ├── containers/
│       │   ├── mod.rs
│       │   ├── scroll.rs         # Scroll view
│       │   ├── split.rs          # Split view
│       │   ├── tab.rs            # Tab view
│       │   └── stack.rs          # Stack view
│       └── advanced/
│           ├── mod.rs
│           ├── list.rs           # List view
│           ├── table.rs          # Table view
│           ├── tree.rs           # Tree view
│           └── canvas.rs         # Custom canvas
│
├── nxrender-layout/              # Layout engine (Week 7-8)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── flexbox.rs            # Flexbox layout
│       ├── grid.rs               # Grid layout
│       ├── stack.rs              # Stack layout
│       ├── absolute.rs           # Absolute positioning
│       └── constraints.rs        # Constraint system
│
├── nxrender-theme/               # Theming (Week 9-10)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── theme.rs              # Theme definition
│       ├── colors.rs             # Color palette
│       ├── fonts.rs              # Font management
│       ├── icons.rs              # Icon set
│       └── presets/
│           ├── mod.rs
│           ├── light.rs          # Light theme
│           ├── dark.rs           # Dark theme
│           └── highcontrast.rs   # High contrast
│
├── nxrender-input/               # Input handling (Week 11-12)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── events.rs             # Event types
│       ├── mouse.rs              # Mouse input
│       ├── keyboard.rs           # Keyboard input
│       ├── touch.rs              # Touch input
│       └── gestures.rs           # Gesture recognition
│
├── nxrender-animation/           # Animation (Week 13-14)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── animator.rs           # Animation system
│       ├── spring.rs             # Spring physics
│       └── easing.rs             # Easing functions
│
└── examples/                     # Examples (Week 15-16)
    ├── hello_window.rs           # Basic window
    ├── calculator.rs             # Calculator app
    ├── text_editor.rs            # Simple text editor
    ├── file_browser.rs           # File browser
    └── widget_gallery.rs         # All widgets demo
```

---

## 🧩 Core Design Principles

### 1. No SDL Dependency
```rust
// ❌ NEVER do this in NXRender
use sdl2::*;  // SDL dependency

// ✅ Use our own abstractions
use nxgfx::GpuContext;
use nxrender_core::Compositor;
```

### 2. GPU-Accelerated Rendering
```rust
// All rendering goes through GPU context
let mut gpu = GpuContext::new()?;
gpu.fill_rect(Rect::new(10, 10, 100, 50), Color::RED);
gpu.draw_text("Hello", Point::new(20, 30), &font);
gpu.present();
```

### 3. Efficient Damage Tracking
```rust
// Only redraw what changed
if damage.is_empty() {
    return; // Skip frame, save power
}

for layer in layers.iter().filter(|l| damage.intersects(l.bounds)) {
    render_layer(layer);
}
```

### 4. Widget Composition
```rust
// Build UI with composable widgets
Column::new()
    .gap(8.0)
    .children(vec![
        Button::new("Click Me").on_click(|| println!("Clicked!")),
        Label::new("Hello World"),
    ])
```

### 5. Theme-Aware Widgets
```rust
// Widgets automatically use theme colors
let button = Button::new("Submit");
// Button will use theme.colors.primary automatically
// When theme changes, button updates automatically
```

---

## 🔧 Common Implementation Patterns

### Widget Implementation
```rust
pub struct Button {
    id: WidgetId,
    bounds: Rect,
    label: String,
    state: ButtonState,
    on_click: Option<Callback>,
}

impl Widget for Button {
    fn id(&self) -> WidgetId { self.id }
    
    fn bounds(&self) -> Rect { self.bounds }
    
    fn measure(&self, constraints: Constraints) -> Size {
        let text_size = measure_text(&self.label);
        Size::new(
            text_size.width + PADDING * 2.0,
            text_size.height + PADDING * 2.0,
        )
    }
    
    fn render(&self, painter: &mut Painter) {
        let color = match self.state {
            ButtonState::Normal => theme.colors.primary,
            ButtonState::Hovered => theme.colors.primary_hover,
            ButtonState::Pressed => theme.colors.primary_pressed,
        };
        
        painter.fill_rounded_rect(self.bounds, color, 4.0);
        painter.draw_text(&self.label, self.bounds.center(), Color::WHITE);
    }
    
    fn handle_event(&mut self, event: &Event) -> EventResult {
        match event {
            Event::MouseDown { pos, .. } if self.bounds.contains(*pos) => {
                self.state = ButtonState::Pressed;
                EventResult::NeedsRedraw
            }
            Event::MouseUp { pos, .. } if self.state == ButtonState::Pressed => {
                if self.bounds.contains(*pos) {
                    if let Some(callback) = &self.on_click {
                        callback();
                    }
                }
                self.state = ButtonState::Normal;
                EventResult::NeedsRedraw
            }
            _ => EventResult::Ignored
        }
    }
}
```

### Layout Implementation
```rust
pub struct FlexLayout {
    direction: FlexDirection,
    justify: JustifyContent,
    align: AlignItems,
    gap: f32,
}

impl Layout for FlexLayout {
    fn layout(&self, children: &mut [Box<dyn Widget>], space: Size) {
        // 1. Measure all children
        let sizes: Vec<Size> = children
            .iter()
            .map(|c| c.measure(Constraints::loose(space)))
            .collect();
        
        // 2. Calculate positions based on justify/align
        let mut offset = 0.0;
        
        for (i, child) in children.iter_mut().enumerate() {
            let pos = match self.direction {
                FlexDirection::Row => Point::new(offset, 0.0),
                FlexDirection::Column => Point::new(0.0, offset),
            };
            
            child.set_bounds(Rect::new(pos, sizes[i]));
            offset += self.main_size(sizes[i]) + self.gap;
        }
    }
}
```

### Animation Implementation
```rust
pub struct Animation {
    target: WidgetId,
    property: AnimatableProperty,
    from: f32,
    to: f32,
    duration: Duration,
    easing: EasingFunction,
    elapsed: Duration,
}

impl Animation {
    pub fn update(&mut self, dt: Duration) -> Option<f32> {
        self.elapsed += dt;
        
        let t = (self.elapsed.as_secs_f32() / self.duration.as_secs_f32())
            .min(1.0);
        
        let eased = self.easing.apply(t);
        let value = self.from + (self.to - self.from) * eased;
        
        Some(value)
    }
}

impl EasingFunction {
    pub fn apply(&self, t: f32) -> f32 {
        match self {
            Self::Linear => t,
            Self::EaseIn => t * t * t,
            Self::EaseOut => 1.0 - (1.0 - t).powi(3),
            Self::EaseInOut => {
                if t < 0.5 { 4.0 * t * t * t }
                else { 1.0 - (-2.0 * t + 2.0).powi(3) / 2.0 }
            }
            Self::Spring { stiffness, damping } => {
                spring_easing(t, *stiffness, *damping)
            }
        }
    }
}
```

---

## ⚠️ Common Pitfalls

### 1. Memory Leaks in Widgets
```rust
// ❌ BAD: Leaks memory
impl Widget for MyWidget {
    fn children(&self) -> &[Box<dyn Widget>] {
        // Creating new vector every call!
        &vec![Box::new(Label::new("Leak!"))]
    }
}

// ✅ GOOD: Store children
struct MyWidget {
    children: Vec<Box<dyn Widget>>,
}

impl Widget for MyWidget {
    fn children(&self) -> &[Box<dyn Widget>] {
        &self.children
    }
}
```

### 2. Not Handling Theme Changes
```rust
// ❌ BAD: Hardcoded colors
fn render(&self, painter: &mut Painter) {
    painter.fill_rect(self.bounds, Color::rgb(0, 122, 255));
}

// ✅ GOOD: Use theme
fn render(&self, painter: &mut Painter) {
    let theme = painter.theme();
    painter.fill_rect(self.bounds, theme.colors.primary);
}
```

### 3. Ignoring Damage Tracking
```rust
// ❌ BAD: Always returns NeedsRedraw
fn handle_event(&mut self, _: &Event) -> EventResult {
    EventResult::NeedsRedraw  // Wastes GPU!
}

// ✅ GOOD: Only redraw when needed
fn handle_event(&mut self, event: &Event) -> EventResult {
    match event {
        Event::MouseMove { pos, .. } => {
            let new_hovered = self.bounds.contains(*pos);
            if new_hovered != self.hovered {
                self.hovered = new_hovered;
                EventResult::NeedsRedraw
            } else {
                EventResult::Ignored
            }
        }
        _ => EventResult::Ignored
    }
}
```

### 4. Blocking Event Loop
```rust
// ❌ BAD: Blocks rendering
fn handle_event(&mut self, event: &Event) -> EventResult {
    std::thread::sleep(Duration::from_secs(1));  // NEVER!
    EventResult::Handled
}

// ✅ GOOD: Use async or spawn thread
fn handle_event(&mut self, event: &Event) -> EventResult {
    std::thread::spawn(|| {
        // Long operation in background
    });
    EventResult::Handled
}
```

---

## 🚀 Performance Targets

| Metric | Target |
|--------|--------|
| Frame Rate | 60 FPS |
| Frame Time | < 16ms |
| Layout (1000 widgets) | < 5ms |
| Event Dispatch | < 1ms |
| Memory (typical app) | < 100MB |
| First Frame | < 100ms |

---

## 🧪 Testing Guidelines

### Unit Tests
```rust
#[test]
fn test_button_click() {
    let mut button = Button::new("Test");
    let mut clicked = false;
    button = button.on_click(|| clicked = true);
    
    // Simulate click
    button.handle_event(&Event::MouseDown {
        pos: Point::new(10.0, 10.0),
        button: MouseButton::Left,
    });
    button.handle_event(&Event::MouseUp {
        pos: Point::new(10.0, 10.0),
        button: MouseButton::Left,
    });
    
    assert!(clicked);
}
```

### Layout Tests
```rust
#[test]
fn test_flexbox_row() {
    let layout = FlexLayout::row().gap(10.0);
    let mut children = vec![
        Box::new(Label::new("A")),  // 50px wide
        Box::new(Label::new("B")),  // 50px wide
    ];
    
    layout.layout(&mut children, Size::new(200.0, 100.0));
    
    assert_eq!(children[0].bounds().x, 0.0);
    assert_eq!(children[1].bounds().x, 60.0);  // 50 + 10 gap
}
```

### Render Tests
```rust
#[test]
fn test_button_render() {
    let button = Button::new("Click");
    let mut painter = TestPainter::new();
    
    button.render(&mut painter);
    
    assert!(painter.has_rounded_rect());
    assert!(painter.has_text("Click"));
}
```

---

## 📚 Resources

### Internal Docs
- [ARCHITECTURE.md](./ARCHITECTURE.md) - System architecture
- [Dev.md](./Dev.md) - Detailed development guide
- [TODO.md](./TODO.md) - Step-by-step task list

### External References
- [CSS Flexbox](https://css-tricks.com/snippets/css/a-guide-to-flexbox/) - Layout reference
- [FreeType](https://freetype.org/freetype2/docs/) - Text rendering
- [HarfBuzz](https://harfbuzz.github.io/) - Text shaping
- [Spring Physics](https://www.youtube.com/watch?v=ZOqb5UHqYWU) - Animation

---

## 📞 Quick Commands

```bash
# Build NXRender
cd source/nxrender
cargo build --release

# Run tests
cargo test

# Run example
cargo run --example calculator

# Check for errors
cargo clippy

# Format code
cargo fmt
```

---

*This guide is maintained alongside the NXRENDER codebase. Update when making architectural changes.*
