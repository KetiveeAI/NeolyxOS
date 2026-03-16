# NeolyxOS Package Format Specification (.nxpkg)

Version: 1.0

## Overview

`.nxpkg` is the official package format for distributing NeolyxOS applications.

## File Extension Convention

| Extension | Use                           |
| --------- | ----------------------------- |
| `.nxpkg`  | Distributable package archive |
| `.app/`   | Installed application bundle  |
| `.npa`    | Package manifest (JSON)       |

## Binary Format

```
Offset 0x00: Header (16 bytes)
├── magic[4]      = "NXPK"
├── version[2]    = 0x0001
├── flags[2]      = Reserved
├── manifest_off  = Offset to manifest.npa
└── payload_off   = Offset to ZIP payload

Offset manifest_off: Manifest
├── size[4]       = JSON length
└── data[size]    = manifest.npa content

Offset payload_off: Payload
└── data[...]     = ZIP-compressed .app directory
```

## Manifest (manifest.npa)

```json
{
  "name": "AppName",
  "version": "1.0.0",
  "bundle_id": "com.developer.appname",
  "category": "Utilities",
  "author": "Developer Name",
  "description": "Application description",
  "binary": "bin/appname",
  "icon": "resources/icon.nxi",
  "size": 4200000,
  "permissions": ["filesystem.read", "network"]
}
```

## Permissions

| Permission         | Description            |
| ------------------ | ---------------------- |
| `filesystem.read`  | Read user files        |
| `filesystem.write` | Write user files       |
| `network`          | Network access         |
| `camera`           | Camera access          |
| `microphone`       | Microphone access      |
| `location`         | Location services      |
| `bluetooth`        | Bluetooth access       |
| `system.registry`  | Modify system registry |

## Tools

- `nxpkg-pack` - Create .nxpkg from .app
- `Installer.app` - Install .nxpkg to system

## CLI Integration

```bash
sys pkg install app.nxpkg    # Install package
sys pkg remove bundle_id     # Uninstall
pkg info app.nxpkg           # Show manifest
```

---

Copyright (c) 2025-2026 KetiveeAI
