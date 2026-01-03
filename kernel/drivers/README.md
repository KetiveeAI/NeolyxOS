# NeolyxOS Kernel Drivers

All drivers are located in `/src/drivers/`.

## Driver Categories

| Category | Directory | Drivers |
|----------|-----------|---------|
| **ACPI** | acpi/ | Power management, sleep states |
| **Audio** | audio/ | nxaudio_kdrv |
| **Biometric** | biometric/ | nxfp_kdrv (fingerprint) |
| **Block** | block/ | block.c, nvme_block.c |
| **Camera** | camera/ | nxcam_kdrv |
| **CPU** | cpu/zen/ | AMD Zen support |
| **Display** | display/ | nxdisplay_kdrv (VRR, HDR) |
| **Graphics** | graphics/ | nxgfx_kdrv, nxrender_kdrv |
| **Input** | input/ | mouse, nxpen, nxtouch |
| **Network** | network/ | ethernet, wifi, nxnet_kdrv |
| **NXGame** | nxgame/ | nxgame_bridge |
| **PCI** | pci/ | nxpci_kdrv |
| **Power** | power/ | nxpower_kdrv (battery) |
| **Sensors** | sensors/ | nxsensor_kdrv (accel, gyro) |
| **Storage** | storage/ | nvme, ata, nxstor_kdrv |
| **System** | system/ | nxsysinfo, nxtask |
| **USB** | usb/ | nxusb_kdrv |
| **Wireless** | wireless/ | nxnfc_kdrv (NFC) |

## Key Driver APIs

### Display (nxdisplay_kdrv)
- Multi-monitor, EDID, VRR, HDR
- `nxdisplay_kdrv_get_caps()` - capability descriptor
- `nxdisplay_kdrv_request_mode()` - declarative mode request

### Graphics (nxgfx_kdrv)
- Framebuffer, GPU acceleration
- `nxgfx_accel_*()` - hardware-accelerated primitives

### Touch (nxtouch_kdrv)
- 10-point multi-touch, gestures
- Tap, swipe, pinch detection

### Power (nxpower_kdrv)
- Battery status, charge state
- Thermal monitoring

## Build
```bash
cd kernel && ninja
```