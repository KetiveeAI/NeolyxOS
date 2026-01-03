# Rivee

**Draw your imagination**

Professional vector graphics editor for NeolyxOS. Create stunning illustrations, icons, and designs with precision tools and powerful rendering.

## Features

### Drawing Tools
- **Selection** - Select, move, scale, rotate
- **Rectangle** - Rectangles with rounded corners
- **Ellipse** - Circles and ovals
- **Line** - Straight lines
- **Pen** - Bezier curves
- **Text** - Vector text with fonts

### Advanced Effects
- **PowerClip** - Clip content inside any shape
- **Image Trace** - Convert bitmaps to vectors
- **Magic Fill** - Intelligent fill by clicking areas
- **LiveSketch** - Smooth freehand drawing
- **Smart Carve** - Interactive edge refinement

### Boolean Operations
- Union, Intersect, Subtract, Exclude, Divide
- Combine, Weld, Trim

### Layers & Pages
- Unlimited layers with visibility/locking
- Multi-page documents
- Layer opacity and blend modes

### Styling
- Solid fills and gradients (linear, radial)
- Stroke width, caps, joins, dashes
- Drop shadows, glow, bevel effects
- Pattern fills

### Export Formats
- **NXI** - NeolyxOS vector icon format
- **PNG** - Raster export
- **SVG** - Standard vector format

## Keyboard Shortcuts

| Key | Tool |
|-----|------|
| `V` | Select |
| `R` | Rectangle |
| `E` | Ellipse |
| `L` | Line |
| `P` | Pen |
| `T` | Text |
| `Z` | Zoom |
| `H` | Pan |

| Shortcut | Action |
|----------|--------|
| `⌘S` | Save |
| `⌘Z` | Undo |
| `⌘⇧Z` | Redo |
| `⌘A` | Select All |
| `⌘G` | Group |
| `⌘⇧G` | Ungroup |

## Resources

```
resources/
├── components/      # Reusable symbols
├── templates/       # Document templates
│   ├── app_icon.template
│   ├── a4_document.template
│   └── logo_design.template
├── brushes/         # Art brushes
├── patterns/        # Fill patterns
├── gradients/       # Gradient presets
│   ├── sunset.gradient
│   ├── ocean.gradient
│   └── neon_glow.gradient
├── palettes/        # Color palettes
│   ├── material.palette
│   └── neolyx.palette
└── workspace/       # Saved workspaces
    ├── default.workspace
    └── illustration.workspace
```

## Workspace Customization

Switch workspaces via **Window > Workspace**:
- **Default** - Balanced for general work
- **Illustration** - Focus on drawing
- **Logo Design** - Branding workflow
- **Minimal** - Maximum canvas space

Customize panel layouts and save your own workspaces.

## Technology

- **Rendering**: NXRender (GPU-accelerated)
- **UI Framework**: ReoxUI
- **File Format**: `.rivee` (JSON-based)

## Build

```bash
cd Rivee.app
make
```

## License

Copyright (c) 2025 KetiveeAI. All Rights Reserved.
