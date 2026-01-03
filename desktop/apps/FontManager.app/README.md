# NeolyxOS Font Manager

System font management application - install, preview, and manage fonts.

## Features

- **Font Preview**: Live preview with custom sample text
- **Install Fonts**: Add new TTF, OTF, WOFF fonts to the system
- **Remove Fonts**: Safely remove user-installed fonts
- **Font Categories**: System fonts, user fonts, recently added
- **Family Grouping**: Group fonts by family (Regular, Bold, Italic, etc.)
- **OpenType Features**: Preview OpenType features (ligatures, stylistic sets)
- **Font Info**: View metadata (author, version, license, glyph count)

## Architecture

```
FontManager.app/
├── main.c              # Application entry point
├── ttf.c/h             # TTF/OTF font parser
├── reox.c/h            # REOX UI integration
├── state.h             # Redux-style state management
├── ui.c/h              # UI components
├── font_service.h      # Public font API for other apps
├── nxrender.h          # NXRender API
├── manifest.npa        # App manifest
├── Makefile
├── resources/
│   ├── icons/          # App icons
│   ├── themes/         # Light/dark themes
│   └── fonts/          # Bundled default fonts
├── bin/                # Compiled output
└── lib/                # Libraries
```

## Font API

Other apps can **read** fonts via `font_service.h`:

```c
#include <font_service.h>

// List all fonts
nx_font_info_t fonts[100];
int count = nx_fonts_list(fonts, 100);

// Find font family
nx_font_info_t info;
nx_fonts_get_style("Inter", "Bold", &info);

// Load for rendering
void *font = nx_font_load(info.font_id, 14);
```

**Only FontManager.app** can install/remove fonts.

## Font Directories

| Location | Type | Managed By |
|----------|------|------------|
| `/System/Library/Fonts` | System fonts | OS (read-only) |
| `~/Library/Fonts` | User fonts | Font Manager |
| `/Applications/*/Resources/fonts` | App fonts | Each app |

## Build

```bash
cd apps/FontManager.app
make clean
make
```

## License

Copyright (c) 2025 KetiveeAI. All Rights Reserved. Proprietary License.
