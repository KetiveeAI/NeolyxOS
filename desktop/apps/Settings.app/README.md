# NeolyxOS Settings.app

System preferences and configuration application for NeolyxOS.

## Features

- **System Panel**: Hostname, timezone, language settings
- **Network Panel**: WiFi, Ethernet, proxy configuration
- **Bluetooth Panel**: Device pairing and management
- **Display Panel**: Resolution, brightness, night shift
- **Sound Panel**: Volume, input/output devices
- **Storage Panel**: Disk usage, cleanup tools
- **Accounts Panel**: User management, login options
- **Security Panel**: Firewall, FileVault, passwords
- **Privacy Panel**: App permissions, location services
- **Appearance Panel**: Theme, accent color, dock settings
- **Power Panel**: Sleep, battery, energy saver
- **Processes Panel**: Task manager, memory usage
- **Startup Panel**: Login items, boot applications
- **Extensions Panel**: System extensions management
- **Updates Panel**: OS and app updates
- **Developer Panel**: Debug options, logging
- **About Panel**: System information

## Directory Structure

```
Settings.app/
├── main.c              # Application entry point
├── settings.h          # Common definitions
├── nxrender.h          # NXRender API
├── manifest.npa        # App manifest
├── Makefile
├── panels/             # Settings panels
│   ├── system_panel.c
│   ├── network_panel.c
│   └── ...
├── drivers/            # Hardware drivers interface
├── resources/
│   ├── icons/          # App and panel icons
│   └── themes/         # Light/dark themes
├── bin/                # Compiled output
└── lib/                # Libraries
```

## Build

```bash
cd apps/Settings.app
make clean
make
```

## License

Copyright (c) 2025 KetiveeAI. All Rights Reserved. Proprietary License.
