# NeolyxOS App Installer

Native application installer for NeolyxOS with a wizard-style UI.

## Features

- **Wizard UI Flow**: 5-step installation process
  1. Welcome - App name, version, author
  2. Permissions - Display required permissions
  3. Location - Choose /Applications or custom path
  4. Progress - Progress bar during install
  5. Complete - Option to add to Dock/Desktop

- **Package Format**: `.nxpkg` (ZIP-like with `manifest.npa`)
- **Manifest Parsing**: JSON-based app metadata
- **Permission System**: 8 permission types
- **App Registry**: `/System/Library/Registry/apps.json`

## Package Format (.nxpkg)

A `.nxpkg` file contains:

```
package.nxpkg/
├── manifest.npa      # App metadata (JSON)
├── bin/              # Executables
├── lib/              # Libraries
└── resources/        # Icons, strings, assets
```

## Manifest Format (manifest.npa)

```json
{
  "name": "Calculator",
  "version": "1.0.0",
  "bundle_id": "com.neolyx.calculator",
  "category": "Utilities",
  "description": "A calculator app for NeolyxOS",
  "author": "KetiveeAI",
  "binary": "bin/calculator",
  "icon": "resources/calculator.nxi",
  "size": 4200000,
  "permissions": ["filesystem.read", "filesystem.write"]
}
```

## Permissions

| Permission         | Description            |
| ------------------ | ---------------------- |
| `filesystem.read`  | Read files from disk   |
| `filesystem.write` | Write files to disk    |
| `network`          | Access the network     |
| `camera`           | Use the camera         |
| `microphone`       | Use the microphone     |
| `location`         | Access user location   |
| `bluetooth`        | Use Bluetooth          |
| `system.registry`  | Modify system registry |

## Build

```bash
make
```

## Run

```bash
# Demo mode
./bin/installer

# With package
./bin/installer path/to/package.nxpkg
```

## Files

| File                  | Purpose                 |
| --------------------- | ----------------------- |
| `main.c`              | UI wizard flow          |
| `src/package.c`       | Package open/extract    |
| `src/manifest.c`      | JSON manifest parsing   |
| `src/registry.c`      | App registry management |
| `include/installer.h` | API definitions         |

---

**Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.**
