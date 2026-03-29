#!/bin/bash
#
# NeolyxOS QEMU Test Runner
# Uses same AHCI config as boot_test.sh but captures serial output
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

KERNEL_DIR="kernel"
IMG_FILE="neolyx.img"
HDD_FILE="hdd_50g.img"
SERIAL_LOG="tests/test_output.log"

echo "════════════════════════════════════════"
echo "  NeolyxOS QEMU Test Runner"
echo "════════════════════════════════════════"
echo ""

# Build kernel if needed
if [ "$1" == "--build" ] || [ ! -f "$KERNEL_DIR/kernel.bin" ]; then
    echo "[1/3] Building kernel..."
    cd "$KERNEL_DIR"
    ninja
    cd ..
    echo ""
fi

# Check for boot image
if [ ! -f "$IMG_FILE" ]; then
    echo "ERROR: Boot image not found at $IMG_FILE"
    echo "Run build.sh first to create the boot image."
    exit 1
fi

# Create test disk if needed (same as boot_test.sh)
if [ ! -f "$HDD_FILE" ]; then
    echo "[2/3] Creating blank 50GB test disk..."
    qemu-img create -f raw "$HDD_FILE" 50G
else
    echo "[2/3] Test disk ready."
fi
echo ""

# Update kernel
echo "[3/3] Updating kernel in boot image..."
mcopy -o -i "$IMG_FILE" "$KERNEL_DIR/kernel.bin" ::/EFI/BOOT/kernel.bin
mcopy -o -i "$IMG_FILE" "$KERNEL_DIR/kernel.bin" ::/kernel.bin
echo "  Kernel updated!"
echo ""

if [ "$1" == "--interactive" ]; then
    echo "Starting QEMU in interactive mode..."
    echo "To run tests: Boot NeolyxOS > Open Terminal > Type 'test'"
    echo ""
    
    qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
      -device ahci,id=ahci0 \
      -drive file="$IMG_FILE",format=raw,if=none,id=boot \
      -device ide-hd,drive=boot,bus=ahci0.0 \
      -drive file="$HDD_FILE",format=raw,if=none,id=target,cache=writethrough \
      -device ide-hd,drive=target,bus=ahci0.1 \
      -m 512M -serial stdio \
      -boot menu=on
else
    echo "Starting QEMU (30s timeout, headless)..."
    echo "  Serial output → $SERIAL_LOG"
    echo ""
    echo "NOTE: In headless mode, tests won't run automatically."
    echo "      Use --interactive for manual testing."
    echo ""
    
    timeout 30 qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
      -device ahci,id=ahci0 \
      -drive file="$IMG_FILE",format=raw,if=none,id=boot \
      -device ide-hd,drive=boot,bus=ahci0.0 \
      -drive file="$HDD_FILE",format=raw,if=none,id=target,cache=writethrough \
      -device ide-hd,drive=target,bus=ahci0.1 \
      -m 512M -serial file:"$SERIAL_LOG" \
      -display none \
      -no-reboot \
      2>/dev/null || true
    
    echo ""
    echo "════════════════════════════════════════"
    echo "  Serial Log Output"
    echo "════════════════════════════════════════"
    
    if [ -f "$SERIAL_LOG" ]; then
        cat "$SERIAL_LOG"
    fi
fi
