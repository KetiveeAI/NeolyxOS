#!/bin/bash
# NeolyxOS Full Build Script
# Rebuilds desktop ELF, generates header, and rebuilds kernel
# Usage: ./build_all.sh [clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "     NeolyxOS Full Build"
echo "========================================"
echo ""

# Optional clean
if [ "$1" = "clean" ]; then
    echo "[1/5] Cleaning..."
    (cd desktop && make clean 2>/dev/null || true)
    (cd kernel && make clean 2>/dev/null || true)
fi

# Step 1: Build desktop
echo "[1/5] Building desktop..."
cd desktop
make clean >/dev/null 2>&1 || true
make 2>&1 | grep -E "(error|Built|warning:.*error)" || true
if [ ! -f bin/neolyx-desktop ]; then
    echo "ERROR: Desktop build failed!"
    exit 1
fi
echo "      Desktop built: bin/neolyx-desktop"

# Step 2: Verify entry point
echo "[2/5] Verifying ELF entry point..."
ENTRY=$(readelf -h bin/neolyx-desktop | grep "Entry point" | awk '{print $4}')
echo "      Entry point: $ENTRY"
if [ "$ENTRY" != "0x1000000" ]; then
    echo "WARNING: Entry point is not 0x1000000! Check user.ld"
fi

# Step 3: Bootloader. The desktop is no longer compiled into the kernel:
# the UEFI loader reads \EFI\BOOT\desktop.elf from the boot image.
echo "[3/5] Building bootloader..."
cd ../boot
# Keep the committed BOOTX64.EFI as a fallback: some gnu-efi/binutils
# combinations cannot link it (e.g. "bootloader.so: file format not
# recognized"), and that must not stop the kernel and image steps.
BOOT_FALLBACK=$(mktemp)
[ -f BOOTX64.EFI ] && cp BOOTX64.EFI "$BOOT_FALLBACK"
if [ -f /usr/include/efi/efi.h ]; then
    rm -f ./*.o ./*.so
    if make >/dev/null 2>&1 && [ -s BOOTX64.EFI ] && [ BOOTX64.EFI -nt main.c ]; then
        echo "      Bootloader built: boot/BOOTX64.EFI"
    elif [ -s "$BOOT_FALLBACK" ]; then
        cp "$BOOT_FALLBACK" BOOTX64.EFI
        echo "      WARNING: bootloader build failed (run 'make' in boot/ for details);"
        echo "               using the committed boot/BOOTX64.EFI"
    fi
else
    echo "      gnu-efi not installed; using the committed boot/BOOTX64.EFI"
fi
rm -f "$BOOT_FALLBACK"
if [ ! -f BOOTX64.EFI ]; then
    echo "ERROR: boot/BOOTX64.EFI missing"
    exit 1
fi
cd ../desktop

# Step 4: Build kernel
echo "[4/5] Building kernel..."
cd ../kernel
rm -f main.o kernel.bin 2>/dev/null || true
# -B: object files are committed to git, so their timestamps cannot be
# trusted after a checkout; always rebuild every kernel object.
make -B 2>&1 | grep -E "(error|kernel.bin|warning:.*error)" | head -10 || true
if [ ! -f kernel.bin ]; then
    echo "ERROR: Kernel build failed!"
    exit 1
fi
# The loader places the raw kernel at 1 MB; firmware memory can start at
# 8 MB (OVMF puts ACPI NVS there), so the image must stay below 7 MB.
KSIZE=$(stat -c%s kernel.bin)
if [ "$KSIZE" -gt $((7 * 1024 * 1024)) ]; then
    echo "ERROR: kernel.bin is $KSIZE bytes; it must stay below 7 MB to load at 1 MB"
    exit 1
fi
echo "      Kernel built: kernel.bin"

cd ..

# Step 4b: Build user programs installed on the boot disk (/apps)
echo "[4b] Building user programs..."
make -C tests/userland/ticker >/dev/null
if [ ! -f tests/userland/ticker/ticker ]; then
    echo "ERROR: ticker build failed!"
    exit 1
fi
echo "      Built tests/userland/ticker/ticker"

# Step 5: Update boot image
echo "[5/5] Updating boot image..."
# NOTE: If mcopy fails ("Bad media types"), run ./rebuild_image.sh first
if mcopy -i neolyx.img -o kernel/kernel.bin ::/EFI/BOOT/kernel.bin 2>/dev/null; then
    mcopy -i neolyx.img -o desktop/bin/neolyx-desktop ::/EFI/BOOT/desktop.elf
    mcopy -i neolyx.img -o boot/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
    echo "      Boot image updated (kernel.bin, desktop.elf, BOOTX64.EFI)"
    mmd -i neolyx.img ::/apps 2>/dev/null || true
    mcopy -i neolyx.img -o tests/userland/ticker/ticker ::/apps/ticker && echo "      Installed /apps/ticker"
else
    echo "      mcopy failed - run ./rebuild_image.sh to fix image"
fi

echo ""
echo "========================================"
echo "     Build Complete!"
echo "========================================"
echo ""
echo "Run: ./boot_test.sh"
