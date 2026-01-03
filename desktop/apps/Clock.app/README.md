# Clock.app

NeolyxOS Clock Application with NTP time synchronization.

## Features

- **NTP Sync**: Uses Indian NTP pool (`in.pool.ntp.org`) for accurate time
- **IST Timezone**: Displays time in Indian Standard Time (UTC+5:30)
- **Large Display**: Full-screen clock with date and day
- **Network Aware**: Falls back to system timer if network unavailable

## Files

| File | Description |
|------|-------------|
| `main.c` | Main application entry and UI |
| `ntp_client.c` | NTP protocol implementation |
| `clock.h` | Data structures and API |
| `manifest.npa` | Application manifest |

## NTP Servers

- Primary: `in.pool.ntp.org`
- Fallback: `time.google.com`
- Port: 123 (UDP)
- Sync Interval: 5 minutes

## Build

```bash
cd apps/Clock.app
make
```

## Copyright

Copyright (c) 2025 KetiveeAI
