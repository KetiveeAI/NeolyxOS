# NeolyxOS Application Structure Standard

All NeolyxOS applications MUST follow this directory structure:

```
AppName.app/
├── main.c                    # Main entry point
├── Makefile                  # Build configuration
├── manifest.npa              # App manifest (JSON)
├── README.md                 # App documentation
├── LICENSE                   # Copy of apps/LICENSE
│
├── bin/                      # Compiled binaries
│   └── appname               # Main executable
│
├── lib/                      # Shared libraries
│   └── libappname.so         # App library (optional)
│
├── resources/                # App resources
│   ├── appname.nxi           # Main app icon (NXI format)
│   ├── icons/                # Additional icons
│   │   ├── icon_16.nxi       # 16x16 icon
│   │   ├── icon_32.nxi       # 32x32 icon
│   │   ├── icon_48.nxi       # 48x48 icon
│   │   └── icon_128.nxi      # 128x128 icon
│   └── themes/               # Theme files
│       ├── light.theme       # Light theme
│       └── dark.theme        # Dark theme
│
├── include/                  # Header files (optional)
│   └── appname.h             # Main header
│
└── src/                      # Additional source files (optional)
    └── *.c                   # Component sources
```

## Manifest Format (manifest.npa)

```json
{
    "name": "AppName",
    "version": "1.0.0",
    "bundle_id": "com.neolyx.appname",
    "category": "Utilities|System|Games|Productivity|Development",
    "description": "Brief description of the app",
    
    "binary": "bin/appname",
    "library": "lib/libappname.so",
    "icon": "resources/appname.nxi",
    
    "permissions": [
        "filesystem.read",
        "filesystem.write",
        "network",
        "camera",
        "microphone"
    ],
    
    "requirements": {
        "os_version": "1.0.0",
        "drivers": ["nxgfx", "nxaudio"]
    },
    
    "author": "KetiveeAI",
    "copyright": "Copyright (c) 2025 KetiveeAI",
    "license": "Proprietary"
}
```

## License Header for Source Files

All source files MUST include this header:

```c
/*
 * AppName - Brief description
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 * 
 * This file is part of NeolyxOS and may not be copied,
 * modified, or distributed without express permission.
 */
```

## Required Files

- [x] main.c - Entry point with proper header
- [x] Makefile - Build system
- [x] manifest.npa - JSON manifest
- [x] README.md - Documentation
- [x] resources/appname.nxi - App icon

## Build Output

All builds output to:
- `bin/appname` - Main executable
- `lib/libappname.so` - Shared library (if applicable)
