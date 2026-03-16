#!/bin/bash
set -e

echo "[IMG] Creating new neolyx.img..."
dd if=/dev/zero of=neolyx_new.img bs=1M count=64 status=none
mkfs.fat -F 32 neolyx_new.img >/dev/null

echo "[IMG] Creating directory structure..."
mmd -i neolyx_new.img ::/EFI
mmd -i neolyx_new.img ::/EFI/BOOT

echo "[IMG] Copying kernel..."
mcopy -i neolyx_new.img kernel/kernel.bin ::/EFI/BOOT/kernel.bin
mcopy -i neolyx_new.img boot/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI

echo "[IMG] Creating installation marker..."
echo "NeolyxOS Installed" | mcopy -i neolyx_new.img - ::/INSTALD.MRK

echo "[IMG] Replacing old image..."
mv neolyx.img neolyx.img.bak
mv neolyx_new.img neolyx.img

echo "[IMG] Done! Image rebuilt and kernel updated."
