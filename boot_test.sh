#!/bin/bash
# NeolyxOS Boot Test Script
# Tests kernel with desktop shell and AHCI disk detection

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "     NeolyxOS Boot Test"
echo "========================================"
echo ""

# Check if disks exist
if [ ! -f neolyx.img ]; then
    echo "ERROR: neolyx.img not found"
    exit 1
fi

# Check kernel is built
if [ ! -f kernel/kernel.bin ]; then
    echo "ERROR: kernel/kernel.bin not found - run ./build.sh first"
    exit 1
fi

# Create 50GB Test Disk
if [ ! -f hdd_50g.img ]; then
    echo "Creating blank 50GB target disk (hdd_50g.img)..."
    qemu-img create -f raw hdd_50g.img 50G
fi

echo "Updating kernel in boot image..."
# Use mtools (no sudo required)
mcopy -i neolyx.img -o kernel/kernel.bin ::/EFI/BOOT/kernel.bin
echo "NeolyxOS Installed" | mcopy -i neolyx.img -o - ::/INSTALD.MRK
echo "Kernel updated!"
echo ""


echo "=== System Configuration ==="
echo "  Drive 0: neolyx.img (boot media)"
echo "  Drive 1: hdd_50g.img (install target - 50GB)"
echo "  RAM: 8GB | CPUs: 8 | Network: E1000"
echo ""
echo "=== Desktop Shell (Syscall-based) ==="
echo "  Framebuffer: SYS_FB_MAP (120), SYS_FB_INFO (121)"
echo "  Input:       SYS_INPUT_POLL (123)"
echo ""
echo "=== UEFI Boot Menu (5-second window) ==="
echo "  ESC     - Enter UEFI Firmware Setup"
echo "  F2      - NeolyxOS Boot Options"
echo "  Default - Boots NeolyxOS Desktop"
echo ""
echo "Starting QEMU..."
echo ""

qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
  -machine usb=off \
  -device ahci,id=ahci0 \
  -drive file=neolyx.img,format=raw,if=none,id=boot \
  -device ide-hd,drive=boot,bus=ahci0.0 \
  -drive file=hdd_50g.img,format=raw,if=none,id=target,cache=writethrough \
  -device ide-hd,drive=target,bus=ahci0.1 \
  -m 8G -smp 8 -display gtk -serial file:serial_debug.log \
  -netdev user,id=net0 -device e1000,netdev=net0 \
  -boot menu=on,splash-time=0

