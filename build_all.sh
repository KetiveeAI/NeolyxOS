#!/bin/bash
# NeolyxOS Full Build Script
# Rebuilds desktop ELF, generates header, and rebuilds kernel
# Usage: ./build_all.sh [clean] [--werror]
#   clean     clean the desktop and kernel first
#   --werror  build every C file with the flags GCC 15 treats as errors
#             (what CI and the owner's machine use); also NX_WERROR=1

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "     NeolyxOS Full Build"
echo "========================================"
echo ""

DO_CLEAN=0
WERROR="${NX_WERROR:-0}"
for arg in "$@"; do
    case "$arg" in
        clean)    DO_CLEAN=1 ;;
        --werror) WERROR=1 ;;
        *)        echo "ERROR: unknown argument '$arg' (usage: ./build_all.sh [clean] [--werror])"; exit 2 ;;
    esac
done

# GCC 15 makes these errors by default; GCC 13/14 only warn. Passing them
# through CC keeps every Makefile unchanged.
MAKE_CC=()
if [ "$WERROR" = "1" ]; then
    MAKE_CC=(CC="gcc -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion -Werror=incompatible-pointer-types")
    echo "Building with the GCC 15 error flags (--werror)"
fi

# Optional clean
if [ "$DO_CLEAN" = "1" ]; then
    echo "[1/5] Cleaning..."
    (cd desktop && make clean 2>/dev/null || true)
    (cd kernel && make clean 2>/dev/null || true)
fi

# Step 1: Build desktop
echo "[1/5] Building desktop..."
cd desktop
make clean >/dev/null 2>&1 || true
make "${MAKE_CC[@]}" 2>&1 | grep -E "(error:|\*\*\*|^Built)" || true
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
# A failed objcopy deletes BOOTX64.EFI, so an earlier failed run can leave
# the working copy without it: restore it from git before building.
if [ ! -s BOOTX64.EFI ] && git cat-file -e HEAD:boot/BOOTX64.EFI 2>/dev/null; then
    git checkout -- BOOTX64.EFI
    echo "      Restored boot/BOOTX64.EFI from git"
fi
BOOT_FALLBACK=$(mktemp)
# -p keeps the executable mode so the restored file does not show as modified.
[ -f BOOTX64.EFI ] && cp -p BOOTX64.EFI "$BOOT_FALLBACK"
if [ -f /usr/include/efi/efi.h ]; then
    rm -f ./*.o ./*.so
    if make "${MAKE_CC[@]}" >/dev/null 2>&1 && [ -s BOOTX64.EFI ] && [ BOOTX64.EFI -nt main.c ]; then
        echo "      Bootloader built: boot/BOOTX64.EFI"
    elif [ -s "$BOOT_FALLBACK" ]; then
        cp -p "$BOOT_FALLBACK" BOOTX64.EFI
        echo "      WARNING: bootloader build failed (run 'make' in boot/ for details);"
        echo "               using the committed boot/BOOTX64.EFI"
    else
        echo "      ERROR: bootloader build failed and no committed BOOTX64.EFI to fall back to"
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
make -B "${MAKE_CC[@]}" 2>&1 | grep -E "(error:|\*\*\*|^objcopy .*kernel\.bin)" | awk 'NR <= 20' || true
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

# Step 4b: Build the programs installed on the boot image.
# The list is config/image-apps.list; add programs there, not here.
echo "[4b] Building user programs (config/image-apps.list)..."
# shellcheck source=scripts/lib/image_apps.sh
. "$SCRIPT_DIR/scripts/lib/image_apps.sh"
if ! image_apps_load "$SCRIPT_DIR"; then
    echo "ERROR: config/image-apps.list is invalid"
    exit 1
fi
for i in "${!IMAGE_APP_DESTS[@]}"; do
    dir="${IMAGE_APP_DIRS[$i]}"
    file="${IMAGE_APP_FILES[$i]}"
    # -B: object files are committed for some programs, so their
    # timestamps cannot be trusted after a checkout.
    if ! make -B -C "$dir" "${MAKE_CC[@]}" >/dev/null || [ ! -f "$dir/$file" ]; then
        echo "ERROR: $dir/$file build failed! (run 'make -C $dir' for details)"
        exit 1
    fi
    echo "      Built $dir/$file"
done

# Step 5: Update boot image
echo "[5/5] Updating boot image..."
# NOTE: If mcopy fails ("Bad media types"), run ./rebuild_image.sh first
if mcopy -i neolyx.img -o kernel/kernel.bin ::/EFI/BOOT/kernel.bin 2>/dev/null; then
    mcopy -i neolyx.img -o desktop/bin/neolyx-desktop ::/EFI/BOOT/desktop.elf
    mcopy -i neolyx.img -o boot/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
    echo "      Boot image updated (kernel.bin, desktop.elf, BOOTX64.EFI)"
    # mmd asks an interactive question when ::/apps already exists (and
    # 2>/dev/null hides it, so the build looks hung): create it only if missing.
    if ! mdir -i neolyx.img ::/apps >/dev/null 2>&1; then
        mmd -i neolyx.img ::/apps || { echo "ERROR: cannot create /apps in neolyx.img"; exit 1; }
    fi
    for i in "${!IMAGE_APP_DESTS[@]}"; do
        src="${IMAGE_APP_DIRS[$i]}/${IMAGE_APP_FILES[$i]}"
        dest="${IMAGE_APP_DESTS[$i]}"
        if ! image_apps_mkdirs neolyx.img "$dest" || ! mcopy -i neolyx.img -o "$src" "::$dest"; then
            echo "ERROR: cannot install $src as $dest in neolyx.img"
            exit 1
        fi
        echo "      Installed $dest"
    done
else
    echo "      mcopy failed - run ./rebuild_image.sh to fix image"
fi

echo ""
echo "========================================"
echo "     Build Complete!"
echo "========================================"
echo ""
echo "Run: ./boot_test.sh"
