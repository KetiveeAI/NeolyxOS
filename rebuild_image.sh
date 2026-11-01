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

if [ ! -f "desktop/bin/neolyx-desktop" ]; then
    echo "[ERROR] desktop/bin/neolyx-desktop not found! Run build_all.sh first."
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

echo "[IMG] Copying desktop..."
mcopy -i neolyx_new.img desktop/bin/neolyx-desktop ::/EFI/BOOT/desktop.elf

echo "[IMG] Installing user programs into /apps..."
mmd -i neolyx_new.img ::/apps
for prog in ticker fdtest shmtest ipc_test fbtest wintest nxwtest nxuitest memstress cpuhog; do
    if [ -f "tests/userland/$prog/$prog" ]; then
        mcopy -i neolyx_new.img "tests/userland/$prog/$prog" "::/apps/$prog"
        echo "  ✓ /apps/$prog"
    else
        echo "  (tests/userland/$prog/$prog not built - run build_all.sh)"
    fi
done

echo "[IMG] Installing system fonts into /System/Fonts..."
# NxUI apps load their UI face from here at start (NXGFX on NXCanvas);
# without it they fall back to the bitmap font and log why.
mmd -i neolyx_new.img ::/System
mmd -i neolyx_new.img ::/System/Fonts
for font in NXSans-Text.ttf; do
    if [ -f "resources/fonts/NXSans/$font" ]; then
        mcopy -i neolyx_new.img "resources/fonts/NXSans/$font" "::/System/Fonts/$font"
        echo "  ✓ /System/Fonts/$font"
    else
        echo "  ✗ resources/fonts/NXSans/$font missing - NxUI apps will use the bitmap font"
    fi
done

echo "[IMG] Creating installation marker..."
echo "NeolyxOS Installed" | mcopy -i neolyx_new.img - ::/INSTALLED.MRK

echo "[IMG] Verifying image contents..."
mdir -i neolyx_new.img ::/EFI/BOOT/ | grep -q "kernel.bin" && echo "  ✓ Kernel verified"
mdir -i neolyx_new.img ::/EFI/BOOT/ | grep -q "BOOTX64.EFI" && echo "  ✓ Bootloader verified"
mdir -i neolyx_new.img ::/EFI/BOOT/ | grep -qi "desktop" && echo "  ✓ Desktop verified"

echo "[IMG] Replacing old image..."
if [ -f "neolyx.img" ]; then
    mv neolyx.img neolyx.img.bak
    echo "  ✓ Backup created: neolyx.img.bak"
fi
mv neolyx_new.img neolyx.img

echo "[IMG] Done! Image rebuilt successfully."
echo ""
echo "To test: ./boot_test.sh"
