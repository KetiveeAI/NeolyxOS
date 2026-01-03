┌─────────────────────────────────────────────────────────┐
│                   USER APPLICATIONS                      │
│  Calculator | Notes | Settings | File Manager | etc.    │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  NXRender API (Public)                   │
│  - Window creation                                       │
│  - Widget library                                        │
│  - Event handling                                        │
│  - Layout engine                                         │
└──────────────────────┬──────────────────────────────────┘
                       │
       ┌───────────────┴───────────────┐
       │                               │
       ▼                               ▼
┌──────────────────┐          ┌──────────────────┐
│   COMPOSITOR     │          │   UI TOOLKIT     │
│  - Surface mgmt  │          │  - Widgets       │
│  - Layer blend   │          │  - Layout        │
│  - Animations    │          │  - Theming       │
│  - Effects       │          │  - Animations    │
└────────┬─────────┘          └────────┬─────────┘
         │                             │
         └─────────────┬───────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│               GRAPHICS BACKEND (NXGFX)                   │
│  - GPU acceleration (Vulkan/OpenGL)                      │
│  - Text rendering (FreeType)                             │
│  - Image decoding (PNG, JPEG, SVG)                       │
│  - Primitive drawing (rect, circle, line, path)          │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  KERNEL DRIVERS                          │
│  - DRM/KMS (display management)                          │
│  - GPU drivers (Intel, AMD, NVIDIA)                      │
│  - Input drivers (mouse, keyboard, touchscreen)          │
└─────────────────────────────────────────────────────────┘

nxrender/
├── Cargo.toml
├── README.md
├── docs/
│   ├── architecture.md
│   ├── api_reference.md
│   └── widget_guide.md
│
├── nxrender-core/           # Core rendering engine
│   ├── compositor/
│   │   ├── compositor.rs    # Main compositor
│   │   ├── surface.rs       # Drawing surfaces
│   │   ├── layer.rs         # Layer management
│   │   └── damage.rs        # Damage tracking (optimization)
│   ├── renderer/
│   │   ├── renderer.rs      # Rendering pipeline
│   │   ├── painter.rs       # Drawing primitives
│   │   └── cache.rs         # Render cache
│   └── animation/
│       ├── animator.rs      # Animation system
│       ├── spring.rs        # Spring physics
│       └── easing.rs        # Easing functions
│
├── nxrender-layout/         # Layout engine
│   ├── flexbox.rs          # Flexbox layout
│   ├── grid.rs             # Grid layout
│   ├── stack.rs            # Stack layout
│   ├── absolute.rs         # Absolute positioning
│   └── constraints.rs      # Auto Layout constraints
│
├── nxrender-widgets/        # Widget library
│   ├── base/
│   │   ├── widget.rs       # Base widget trait
│   │   ├── view.rs         # Container view
│   │   └── window.rs       # Top-level window
│   ├── controls/
│   │   ├── button.rs       # Button
│   │   ├── textfield.rs    # Text input
│   │   ├── checkbox.rs     # Checkbox
│   │   ├── radio.rs        # Radio button
│   │   ├── slider.rs       # Slider
│   │   ├── switch.rs       # Toggle switch
│   │   └── dropdown.rs     # Dropdown menu
│   ├── display/
│   │   ├── label.rs        # Text label
│   │   ├── image.rs        # Image view
│   │   ├── icon.rs         # Icon
│   │   └── separator.rs    # Divider
│   ├── containers/
│   │   ├── scroll.rs       # Scroll view
│   │   ├── split.rs        # Split view
│   │   ├── tab.rs          # Tab view
│   │   └── stack.rs        # Stack view
│   └── advanced/
│       ├── list.rs         # List view
│       ├── table.rs        # Table view
│       ├── tree.rs         # Tree view
│       └── canvas.rs       # Custom drawing canvas
│
├── nxrender-theme/          # Theming system
│   ├── theme.rs            # Theme definition
│   ├── colors.rs           # Color palette
│   ├── fonts.rs            # Font management
│   ├── icons.rs            # Icon set
│   └── presets/
│       ├── light.rs        # Light theme
│       ├── dark.rs         # Dark theme
│       └── custom.rs       # Custom themes
│
├── nxrender-input/          # Input handling
│   ├── events.rs           # Event types
│   ├── mouse.rs            # Mouse input
│   ├── keyboard.rs         # Keyboard input
│   ├── touch.rs            # Touch input
│   └── gestures.rs         # Gesture recognition
│
├── nxrender-gpu/            # GPU backend (NXGFX integration)
│   ├── context.rs          # GPU context
│   ├── buffer.rs           # GPU buffers
│   ├── texture.rs          # Texture management
│   ├── shader.rs           # Shader programs
│   └── backend/
│       ├── vulkan.rs       # Vulkan backend
│       ├── opengl.rs       # OpenGL backend
│       └── software.rs     # Software fallback
│
├── nxrender-text/           # Text rendering
│   ├── font.rs             # Font loading
│   ├── layout.rs           # Text layout
│   ├── shaping.rs          # Text shaping (HarfBuzz)
│   └── rasterize.rs        # Glyph rasterization
│
└── examples/                # Example applications
    ├── hello_window.rs     # Basic window
    ├── calculator.rs       # Calculator app
    ├── text_editor.rs      # Simple text editor
    └── file_browser.rs     # File browser