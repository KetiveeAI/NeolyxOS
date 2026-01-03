# Neolyx OS Bootloader

This directory contains the UEFI bootloader for Neolyx OS, written in C for 64-bit x86_64 systems.

## Files

- `main.c` - Main bootloader entry point with graphics support
- `main_simple.c` - Simple bootloader version without graphics
- `graphics.h/c` - Graphics functions for BMP loading and drawing
- `efilib.h/c` - EFI library functions (Print, InitializeLib)
- `efi.h` - Custom UEFI header with necessary types and protocols
- `Makefile` - Build script for main bootloader
- `Makefile_simple` - Build script for simple bootloader
- `test_compile.bat` - Windows batch script for building

## Building

### Using Makefile
```bash
make clean
make
```

### Using Batch Script (Windows)
```bash
test_compile.bat
```

### Manual Build
```bash
gcc -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -O2 -I./inc -I../include -I./gnu-efi-3.0.18/inc main.c -o main.o
gcc -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -O2 -I./inc -I../include -I./gnu-efi-3.0.18/inc efilib.c -o efilib.o
gcc -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -O2 -I./inc -I../include -I./gnu-efi-3.0.18/inc graphics.c -o graphics.o
ld -melf_x86_64 -T gnu-efi-3.0.18/gnuefi/elf_x86_64_efi.lds -static -o BOOTX64.EFI main.o efilib.o graphics.o
```

## Testing

The bootloader can be tested in QEMU or on real UEFI hardware:

1. Copy `BOOTX64.EFI` to a FAT32 formatted USB drive in the path `EFI/BOOT/`
2. Boot from the USB drive in UEFI mode
3. Or test in QEMU with UEFI firmware

## Features

- UEFI 64-bit bootloader
- Graphics Output Protocol support
- BMP image loading (stub implementation)
- Screen clearing and drawing functions
- Loading animations
- Welcome text with typewriter effect
- Modular design for easy extension 