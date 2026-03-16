# NeolyxOS Resources

This directory contains system-wide UI resources including icons, fonts, themes, wallpapers, sounds, and cursors.

## Directory Structure

```
resources/
├── fonts/              # System fonts
│   ├── NXSans/         # Default sans-serif font
│   └── NXSansMono/     # Default monospace font
│
├── icons/              # System icons (NXIB format)
│   ├── wifi_24.nxi     # Size-specific icons
│   ├── wifi.nxi        # Default size (32px)
│   └── ...
│
├── themes/             # UI theme definitions
│   ├── default.theme   # Dark theme (default)
│   └── light.theme     # Light theme
│
├── wallpapers/         # Desktop wallpapers
│   └── default.png     # Default wallpaper
│
├── sounds/             # UI sound effects
│   ├── login.wav
│   ├── logout.wav
│   ├── notification.wav
│   └── ...
│
└── cursors/            # Mouse cursor themes
    └── default.cursors # Cursor configuration
```

## Icon Format (NXIB)

Binary format for fast loading:

- Header: 10 bytes (magic[4] + version[1] + width[2] + height[2] + flags[1])
- Pixels: RGBA32, width × height × 4 bytes

To generate icons:

```bash
python3 tools/generate_nxib_icons.py resources/icons/
```

## Theme Format

INI-style configuration with sections:

- `[colors]` - Color palette
- `[opacity]` - Transparency settings
- `[fonts]` - Typography
- `[shadows]` - Drop shadows
- `[animations]` - Animation timings
- `[dimensions]` - Spacing and sizes

## Adding New Resources

1. **Icons**: Add via IconLay.app or `generate_nxib_icons.py`
2. **Fonts**: Place .ttf/.otf files in `fonts/` subdirectory
3. **Wallpapers**: Add .png/.jpg files to `wallpapers/`
4. **Themes**: Copy `default.theme` and modify colors

## Deployment

At build time, resources are copied to:

- `/System/Desktop/Icons/` - Icons
- `/System/Desktop/Themes/` - Themes
- `/System/Desktop/Wallpapers/` - Wallpapers
- `/System/Library/Fonts/` - Fonts

---

Copyright (c) 2025-2026 KetiveeAI
