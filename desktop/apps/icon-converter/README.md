# Neolyx Icon Converter

A tool to convert various image formats to the custom `.nix` icon format used by Neolyx OS.

## 🚀 Features

- **Multiple Input Formats**: SVG, PNG, JPEG, BMP, ICO, GIF
- **Custom .nix Output**: Native Neolyx icon format
- **Batch Processing**: Convert multiple files at once
- **Size Optimization**: Automatic resizing and optimization
- **Animation Support**: Convert animated GIFs to animated .nix icons
- **Quality Control**: Preview and adjust conversion settings

## 📁 Project Structure

```
/icon-converter/
├── src/
│   ├── main.rs              # Main application entry
│   ├── converter.rs          # Core conversion logic
│   ├── formats/              # Format-specific handlers
│   │   ├── svg.rs           # SVG to .nix conversion
│   │   ├── png.rs           # PNG to .nix conversion
│   │   ├── jpeg.rs          # JPEG to .nix conversion
│   │   └── gif.rs           # GIF to .nix conversion
│   ├── utils/               # Utility functions
│   │   ├── image_utils.rs   # Image processing utilities
│   │   └── file_utils.rs    # File handling utilities
│   └── cli/                 # Command-line interface
│       └── args.rs          # CLI argument parsing
├── assets/                  # Sample icons and resources
├── Cargo.toml              # Dependencies
└── build.bat               # Build script
```

## 🔧 Building

```bash
cd neolyx-os/apps/icon-converter
cargo build --release
```

## 📖 Usage

### Basic Conversion
```bash
# Convert single file
icon-converter input.svg output.nix

# Convert with specific size
icon-converter input.png output.nix --size 64x64

# Convert with quality settings
icon-converter input.jpg output.nix --quality high
```

### Batch Conversion
```bash
# Convert all SVG files in directory
icon-converter --batch svg/*.svg --output-dir icons/

# Convert with size optimization
icon-converter --batch images/ --sizes 16,32,64,128
```

### Advanced Options
```bash
# Convert animated GIF
icon-converter animation.gif output.nix --animation-delay 100

# Convert with compression
icon-converter input.png output.nix --compress

# Preview conversion
icon-converter input.svg output.nix --preview
```

## 🎨 .nix Format Specification

### File Header
```
Magic: NIXICON\x01 (8 bytes)
Version: 1 (1 byte)
Width: u32 (4 bytes)
Height: u32 (4 bytes)
Color Depth: u8 (1 byte)
Frame Count: u32 (4 bytes)
Animation Delay: u32 (4 bytes)
Compression: u8 (1 byte)
```

### Color Depths
- `8`: Grayscale (1 byte per pixel)
- `16`: RGB16 (2 bytes per pixel)
- `24`: RGB24 (3 bytes per pixel)
- `32`: RGBA32 (4 bytes per pixel)

### Supported Features
- **Multiple Sizes**: Icons can contain multiple resolutions
- **Animation**: Multiple frames with configurable delay
- **Compression**: Optional compression for smaller file sizes
- **Transparency**: Full alpha channel support

## 📋 Supported Input Formats

| Format | Extension | Features |
|--------|-----------|----------|
| SVG | .svg | Vector graphics, scaling |
| PNG | .png | Transparency, lossless |
| JPEG | .jpg, .jpeg | Photographic images |
| BMP | .bmp | Windows bitmap |
| ICO | .ico | Windows icon format |
| GIF | .gif | Animation support |
| WebP | .webp | Modern web format |

## 📄 License

Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License. 