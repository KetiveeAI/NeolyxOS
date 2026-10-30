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

### System icon pack (dock, app drawer, folders, devices)

The NeolyxOS app, folder and device icons are listed in
`resources/icons/pack/iconpack.manifest` and built from the NeolyxOS art in
the repo (`icons/`, `icons/DirIcons/`, app bundles, `desktop/resources/icons`):

```bash
pip install pillow cairosvg
python3 tools/build_icon_pack.py          # writes System/Desktop/Icons/{app,folder,device}-*.nxi
python3 tools/build_icon_pack.py --check  # fails if committed output is stale
```

The generator also rewrites `desktop/shell/nxicon_pack.S` and
`desktop/lib/nxicons/nxicon_pack_index.h`, which link the icons into the
desktop image (the boot image ships no files the desktop can open yet).
Look icons up with `nxicon_pack_find()` / `nxicon_pack_draw()`
(`desktop/include/nxicon_pack.h`); `nxapp_icon_draw()` and the NXI renderer
already use the pack. To use exact Figma artwork for an icon, export the
1024 x 1024 frame as SVG into `resources/icons/pack/figma/` and point its
manifest line at it with mode `art`. Figma glass icons (inner shadows,
backdrop blur) need `render=browser` on their line: cairosvg drops SVG filters,
so those are rasterized in headless Chromium (Node.js + Playwright).

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
