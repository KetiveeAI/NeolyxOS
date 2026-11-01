#!/bin/bash
#
# NeolyxOS - build a USB-stick image for hardware smoke tests.
#
# Takes the files of neolyx.img (built by ./rebuild_image.sh) and puts them
# on a GPT disk image with one EFI System Partition, the layout UEFI firmware
# on real PCs boots from removable media. It also adds \EFI\BOOT\diagnose.flg,
# which turns on hardware diagnostics for every boot from this stick:
#   - the loader shows display, memory and firmware facts for 20 seconds,
#   - the kernel shows its log on screen until the desktop starts,
#   - every disk driver refuses writes, so the PC's own disks are not changed.
# See docs/hardware/smoke-test.md.
#
# Usage: scripts/make_usb_image.sh [output.img]     (default: neolyx-usb.img)
# Needs: sgdisk (gdisk), mkfs.fat (dosfstools), mtools.
#
# Copyright (c) 2025 KetiveeAI

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_IMG="$ROOT/neolyx.img"
OUT="${1:-$ROOT/neolyx-usb.img}"

IMG_MIB=128              # whole image
PART_START=2048          # first sector of the ESP (1 MiB, aligned)
SECTOR=512

fail() { echo "[USB-IMG] ERROR: $*" >&2; exit 1; }

for tool in sgdisk mkfs.fat mcopy mmd mdir sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || fail "'$tool' not found (install gdisk, dosfstools, mtools)"
done
[ -f "$SRC_IMG" ] || fail "$SRC_IMG not found: run ./build_all.sh and ./rebuild_image.sh first"
case "$OUT" in
    /dev/*) fail "refusing to write to a device ($OUT): build the image file, then write it with your imaging tool" ;;
esac
for f in /EFI/BOOT/BOOTX64.EFI /EFI/BOOT/kernel.bin /EFI/BOOT/desktop.elf; do
    mdir -i "$SRC_IMG" "::$f" >/dev/null 2>&1 || fail "$SRC_IMG has no $f"
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "[USB-IMG] Copying files out of $SRC_IMG..."
mkdir -p "$WORK/files"
mcopy -s -n -i "$SRC_IMG" "::/*" "$WORK/files/" >/dev/null
echo "NeolyxOS hardware diagnostics: show boot facts and the kernel log, refuse disk writes." \
    > "$WORK/files/EFI/BOOT/diagnose.flg"

echo "[USB-IMG] Creating ${IMG_MIB} MiB GPT image..."
rm -f "$OUT.tmp"
truncate -s "${IMG_MIB}M" "$OUT.tmp"
sgdisk --clear \
       --new=1:${PART_START}:0 --typecode=1:EF00 --change-name=1:"NEOLYX-EFI" \
       "$OUT.tmp" >/dev/null || fail "sgdisk failed"

# Partition size from the partition table, in sectors
PART_END=$(sgdisk --info=1 "$OUT.tmp" | awk '/Last sector:/ {print $3}')
[ -n "$PART_END" ] || fail "could not read the partition size"
PART_SECTORS=$((PART_END - PART_START + 1))
PART_KIB=$((PART_SECTORS * SECTOR / 1024))

echo "[USB-IMG] Formatting the EFI System Partition (FAT32)..."
mkfs.fat -F 32 -n NEOLYX-EFI --offset=$PART_START "$OUT.tmp" $PART_KIB >/dev/null 2>&1 \
    || fail "mkfs.fat failed"

PART="$OUT.tmp@@$((PART_START * SECTOR))"
echo "[USB-IMG] Copying files into the partition..."
mcopy -s -i "$PART" "$WORK/files/"* "::/" || fail "mcopy failed"

for f in /EFI/BOOT/BOOTX64.EFI /EFI/BOOT/kernel.bin /EFI/BOOT/desktop.elf /EFI/BOOT/diagnose.flg; do
    mdir -i "$PART" "::$f" >/dev/null 2>&1 || fail "image check failed: $f missing"
    echo "  ✓ $f"
done

mv "$OUT.tmp" "$OUT"
sha256sum "$OUT" | awk '{print $1}' > "$OUT.sha256"
echo "[USB-IMG] Done: $OUT"
echo "[USB-IMG] SHA-256: $(cat "$OUT.sha256")"
