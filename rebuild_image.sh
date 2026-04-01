#!/bin/bash
set -e

# Check if required files exist
if [ ! -f "kernel/kernel.bin" ]; then
    echo "[ERROR] kernel/kernel.bin not found! Run build_all.sh first."
    exit 1
fi

if [ ! -f "boot/BOOTX64.EFI" ]; then
    echo "[ERROR] boot/BOOTX64.EFI not found! Run build_all.sh first."
    exit 1
fi

echo "[IMG] Creating new neolyx.img (64MB)..."
dd if=/dev/zero of=neolyx_new.img bs=1M count=64 status=none
mkfs.fat -F 32 neolyx_new.img >/dev/null 2>&1

echo "[IMG] Creating directory structure..."
mmd -i neolyx_new.img ::/EFI
mmd -i neolyx_new.img ::/EFI/BOOT

echo "[IMG] Copying bootloader..."
mcopy -i neolyx_new.img boot/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI

echo "[IMG] Copying kernel..."
mcopy -i neolyx_new.img kernel/kernel.bin ::/EFI/BOOT/kernel.bin
mcopy -i neolyx_new.img kernel/kernel.bin ::/kernel.bin

echo "[IMG] Creating installation marker..."
echo "NeolyxOS Installed" | mcopy -i neolyx_new.img - ::/INSTALLED.MRK

echo "[IMG] Verifying image contents..."
mdir -i neolyx_new.img ::/EFI/BOOT/ | grep -q "kernel.bin" && echo "  ✓ Kernel verified"
mdir -i neolyx_new.img ::/EFI/BOOT/ | grep -q "BOOTX64.EFI" && echo "  ✓ Bootloader verified"

echo "[IMG] Replacing old image..."
if [ -f "neolyx.img" ]; then
    mv neolyx.img neolyx.img.bak
    echo "  ✓ Backup created: neolyx.img.bak"
fi
mv neolyx_new.img neolyx.img

echo "[IMG] Done! Image rebuilt successfully."
echo ""
echo "To test: ./boot_test.sh"
