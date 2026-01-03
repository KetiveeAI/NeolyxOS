# NeolyxOS Services Layer

User-space system services that run outside the kernel.

## Services

| Service | Description |
|---------|-------------|
| `audio/` | nxaudio-server + Ketivee Oohm DSP |
| `render/` | nxrender-service (compositor backend) |
| `input/` | Input event handling |
| `compositor/` | Window compositing service |
| `fs/` | Filesystem services |
| `nxaudio-server/` | Audio server (existing) |

## Characteristics
- Native (C/C++/Rust)
- Fast
- Restartable
- Replaceable
- **NOT kernel**

## Architecture
```
Kernel (DMA, IRQ, syscalls)
         ↓
Services (audio, render, input, compositor, fs)
         ↓
Engines (Ketivee Oohm, NXRender, Media)
         ↓
Apps (REOX)
```
