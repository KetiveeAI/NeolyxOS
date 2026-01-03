#!/bin/bash
#
# NeolyxOS Image Creation Script
#
# Creates a complete bootable neolyx.img with:
# - GPT partition table
# - EFI System Partition (FAT32)
# - Root partition (ext4)
# - Bootloader (GRUB or systemd-boot)
# - Kernel + initramfs
# - Root filesystem
#
# Copyright (c) 2025 KetiveeAI
#
# Usage: ./create_image.sh [options]
#   -s SIZE     Image size (default: 8G)
#   -o OUTPUT   Output file (default: neolyx.img)
#   -r ROOTFS   Root filesystem tarball
#   -k KERNEL   Kernel image (vmlinuz)
#   -i INITRD   Initramfs image
#   -b BOOT     Bootloader: grub|systemd-boot (default: grub)
#

set -e

# ==============================================================================
# Configuration
# ==============================================================================

IMAGE_SIZE="8G"
OUTPUT_FILE="neolyx.img"
ROOTFS_TAR=""
KERNEL_IMAGE=""
INITRAMFS=""
BOOTLOADER="grub"
ESP_SIZE="600M"
WORK_DIR="/tmp/neolyx-build-$$"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ==============================================================================
# Logging
# ==============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ==============================================================================
# Parse Arguments
# ==============================================================================

while getopts "s:o:r:k:i:b:h" opt; do
    case $opt in
        s) IMAGE_SIZE="$OPTARG" ;;
        o) OUTPUT_FILE="$OPTARG" ;;
        r) ROOTFS_TAR="$OPTARG" ;;
        k) KERNEL_IMAGE="$OPTARG" ;;
        i) INITRAMFS="$OPTARG" ;;
        b) BOOTLOADER="$OPTARG" ;;
        h)
            echo "Usage: $0 [options]"
            echo "  -s SIZE     Image size (default: 8G)"
            echo "  -o OUTPUT   Output file (default: neolyx.img)"
            echo "  -r ROOTFS   Root filesystem tarball"
            echo "  -k KERNEL   Kernel image (vmlinuz)"
            echo "  -i INITRD   Initramfs image"
            echo "  -b BOOT     Bootloader: grub|systemd-boot (default: grub)"
            exit 0
            ;;
        *)
            log_error "Invalid option: -$OPTARG"
            exit 1
            ;;
    esac
done

# ==============================================================================
# Validation
# ==============================================================================

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root"
        exit 1
    fi
}

check_dependencies() {
    local deps="truncate sfdisk losetup mkfs.vfat mkfs.ext4 mount umount"
    
    for dep in $deps; do
        if ! command -v $dep &> /dev/null; then
            log_error "Missing dependency: $dep"
            exit 1
        fi
    done
    
    log_success "All dependencies found"
}

# ==============================================================================
# Cleanup
# ==============================================================================

cleanup() {
    log_info "Cleaning up..."
    
    # Unmount if mounted
    if mountpoint -q "$WORK_DIR/root" 2>/dev/null; then
        umount "$WORK_DIR/root/boot/efi" 2>/dev/null || true
        umount "$WORK_DIR/root" 2>/dev/null || true
    fi
    
    if mountpoint -q "$WORK_DIR/efi" 2>/dev/null; then
        umount "$WORK_DIR/efi" 2>/dev/null || true
    fi
    
    # Detach loop device
    if [[ -n "$LOOP_DEVICE" ]]; then
        losetup -d "$LOOP_DEVICE" 2>/dev/null || true
    fi
    
    # Remove work directory
    rm -rf "$WORK_DIR" 2>/dev/null || true
}

trap cleanup EXIT

# ==============================================================================
# Image Creation
# ==============================================================================

create_image() {
    log_info "Creating disk image: $OUTPUT_FILE ($IMAGE_SIZE)"
    
    # Remove existing image
    rm -f "$OUTPUT_FILE"
    
    # Create sparse image
    truncate -s "$IMAGE_SIZE" "$OUTPUT_FILE"
    
    log_success "Disk image created"
}

partition_image() {
    log_info "Creating GPT partition table..."
    
    # Create partition table
    cat > /tmp/neolyx-partitions.sfdisk << EOF
label: gpt
first-lba: 2048
,${ESP_SIZE},UEFI,*
,,L
EOF
    
    sfdisk --quiet "$OUTPUT_FILE" < /tmp/neolyx-partitions.sfdisk
    rm -f /tmp/neolyx-partitions.sfdisk
    
    log_success "Partitions created (EFI: $ESP_SIZE, Root: remaining)"
}

setup_loop_device() {
    log_info "Setting up loop device..."
    
    # Attach loop device with partition scanning
    LOOP_DEVICE=$(losetup --show -fP "$OUTPUT_FILE")
    
    # Wait for partitions to appear
    sleep 1
    partprobe "$LOOP_DEVICE" 2>/dev/null || true
    sleep 1
    
    EFI_PART="${LOOP_DEVICE}p1"
    ROOT_PART="${LOOP_DEVICE}p2"
    
    if [[ ! -b "$EFI_PART" ]] || [[ ! -b "$ROOT_PART" ]]; then
        log_error "Partition devices not found"
        exit 1
    fi
    
    log_success "Loop device: $LOOP_DEVICE"
}

format_partitions() {
    log_info "Formatting partitions..."
    
    # Format EFI partition (FAT32)
    mkfs.vfat -F 32 -n "NEOLYX-EFI" "$EFI_PART" > /dev/null
    log_success "EFI partition formatted (FAT32)"
    
    # Format root partition (ext4)
    mkfs.ext4 -q -L "NEOLYX-ROOT" "$ROOT_PART" > /dev/null
    log_success "Root partition formatted (ext4)"
}

mount_partitions() {
    log_info "Mounting partitions..."
    
    mkdir -p "$WORK_DIR/root"
    mkdir -p "$WORK_DIR/efi"
    
    mount "$ROOT_PART" "$WORK_DIR/root"
    mount "$EFI_PART" "$WORK_DIR/efi"
    
    # Create boot/efi mount point in root
    mkdir -p "$WORK_DIR/root/boot/efi"
    mount --bind "$WORK_DIR/efi" "$WORK_DIR/root/boot/efi"
    
    log_success "Partitions mounted"
}

# ==============================================================================
# Filesystem Installation
# ==============================================================================

install_base_structure() {
    log_info "Creating base directory structure..."
    
    cd "$WORK_DIR/root"
    
    # Create standard directories
    mkdir -p bin sbin lib lib64 usr/{bin,sbin,lib,lib64,share}
    mkdir -p etc/{init.d,neo-init.d}
    mkdir -p var/{log,run,tmp,cache}
    mkdir -p run tmp dev proc sys
    mkdir -p home root
    mkdir -p boot/efi/EFI/Boot
    mkdir -p boot/efi/EFI/neolyx
    
    # Set permissions
    chmod 755 bin sbin lib usr etc var
    chmod 700 root
    chmod 1777 tmp var/tmp
    
    log_success "Base structure created"
}

install_rootfs() {
    if [[ -n "$ROOTFS_TAR" ]] && [[ -f "$ROOTFS_TAR" ]]; then
        log_info "Extracting root filesystem..."
        tar -xpf "$ROOTFS_TAR" -C "$WORK_DIR/root"
        log_success "Root filesystem installed"
    else
        log_warn "No rootfs tarball provided, creating minimal structure"
        install_base_structure
    fi
}

install_kernel() {
    log_info "Installing kernel and initramfs..."
    
    if [[ -n "$KERNEL_IMAGE" ]] && [[ -f "$KERNEL_IMAGE" ]]; then
        cp "$KERNEL_IMAGE" "$WORK_DIR/root/boot/vmlinuz-neolyx"
        cp "$KERNEL_IMAGE" "$WORK_DIR/efi/EFI/Boot/vmlinuz-neolyx"
        log_success "Kernel installed"
    else
        log_warn "No kernel provided, creating placeholder"
        touch "$WORK_DIR/root/boot/vmlinuz-neolyx"
    fi
    
    if [[ -n "$INITRAMFS" ]] && [[ -f "$INITRAMFS" ]]; then
        cp "$INITRAMFS" "$WORK_DIR/root/boot/initramfs-neolyx.img"
        cp "$INITRAMFS" "$WORK_DIR/efi/EFI/Boot/initramfs-neolyx.img"
        log_success "Initramfs installed"
    else
        log_warn "No initramfs provided"
    fi
}

# ==============================================================================
# Bootloader Installation
# ==============================================================================

install_grub() {
    log_info "Installing GRUB bootloader..."
    
    # Create GRUB directory
    mkdir -p "$WORK_DIR/efi/EFI/neolyx/grub"
    mkdir -p "$WORK_DIR/root/boot/grub"
    
    # Create grub.cfg
    cat > "$WORK_DIR/efi/EFI/neolyx/grub/grub.cfg" << 'EOF'
# NeolyxOS GRUB Configuration
# Copyright (c) 2025 KetiveeAI

set timeout=3
set default=0

# Load video and font modules
insmod all_video
insmod gfxterm

# Set background color
set color_normal=white/black
set color_highlight=black/white

menuentry "NeolyxOS" {
    linux /EFI/Boot/vmlinuz-neolyx root=/dev/sda2 rw rootwait quiet splash init=/sbin/neo-init
    initrd /EFI/Boot/initramfs-neolyx.img
}

menuentry "NeolyxOS (Recovery Mode)" {
    linux /EFI/Boot/vmlinuz-neolyx root=/dev/sda2 rw rootwait single init=/bin/sh
    initrd /EFI/Boot/initramfs-neolyx.img
}

menuentry "NeolyxOS (Debug Mode)" {
    linux /EFI/Boot/vmlinuz-neolyx root=/dev/sda2 rw rootwait debug earlyprintk=serial,ttyS0,115200 init=/sbin/neo-init
    initrd /EFI/Boot/initramfs-neolyx.img
}
EOF
    
    # Copy grub.cfg to root
    cp "$WORK_DIR/efi/EFI/neolyx/grub/grub.cfg" "$WORK_DIR/root/boot/grub/grub.cfg"
    
    # Install GRUB EFI binary if available
    if command -v grub-mkimage &> /dev/null; then
        grub-mkimage -o "$WORK_DIR/efi/EFI/Boot/BOOTX64.EFI" \
            -O x86_64-efi \
            -p /EFI/neolyx/grub \
            part_gpt fat ext2 normal boot linux configfile \
            gfxterm video 2>/dev/null || true
    fi
    
    # Try grub-install if available
    if command -v grub-install &> /dev/null; then
        grub-install --target=x86_64-efi \
            --efi-directory="$WORK_DIR/efi" \
            --boot-directory="$WORK_DIR/root/boot" \
            --removable \
            --no-nvram 2>/dev/null || true
    fi
    
    log_success "GRUB configuration installed"
}

install_systemd_boot() {
    log_info "Installing systemd-boot..."
    
    # Create loader directory
    mkdir -p "$WORK_DIR/efi/loader/entries"
    
    # Create loader.conf
    cat > "$WORK_DIR/efi/loader/loader.conf" << 'EOF'
# NeolyxOS systemd-boot Configuration
default neolyx.conf
timeout 3
console-mode auto
editor no
EOF
    
    # Create boot entry
    cat > "$WORK_DIR/efi/loader/entries/neolyx.conf" << 'EOF'
title   NeolyxOS
linux   /EFI/Boot/vmlinuz-neolyx
initrd  /EFI/Boot/initramfs-neolyx.img
options root=/dev/sda2 rw rootwait quiet splash init=/sbin/neo-init
EOF
    
    # Create recovery entry
    cat > "$WORK_DIR/efi/loader/entries/neolyx-recovery.conf" << 'EOF'
title   NeolyxOS (Recovery)
linux   /EFI/Boot/vmlinuz-neolyx
initrd  /EFI/Boot/initramfs-neolyx.img
options root=/dev/sda2 rw rootwait single init=/bin/sh
EOF
    
    # Install systemd-boot EFI binary if available
    if [[ -f /usr/lib/systemd/boot/efi/systemd-bootx64.efi ]]; then
        cp /usr/lib/systemd/boot/efi/systemd-bootx64.efi "$WORK_DIR/efi/EFI/Boot/BOOTX64.EFI"
    fi
    
    log_success "systemd-boot configuration installed"
}

install_bootloader() {
    case "$BOOTLOADER" in
        grub)
            install_grub
            ;;
        systemd-boot)
            install_systemd_boot
            ;;
        *)
            log_error "Unknown bootloader: $BOOTLOADER"
            exit 1
            ;;
    esac
}

# ==============================================================================
# Configuration Files
# ==============================================================================

install_config_files() {
    log_info "Installing configuration files..."
    
    # /etc/fstab
    cat > "$WORK_DIR/root/etc/fstab" << 'EOF'
# NeolyxOS /etc/fstab
# <device>          <mount>     <type>  <options>       <dump> <pass>
/dev/sda2           /           ext4    defaults        0      1
/dev/sda1           /boot/efi   vfat    umask=0077      0      2
tmpfs               /tmp        tmpfs   defaults        0      0
tmpfs               /run        tmpfs   defaults        0      0
EOF
    
    # /etc/hostname
    echo "neolyx" > "$WORK_DIR/root/etc/hostname"
    
    # /etc/hosts
    cat > "$WORK_DIR/root/etc/hosts" << 'EOF'
127.0.0.1   localhost
127.0.1.1   neolyx
::1         localhost ip6-localhost ip6-loopback
EOF
    
    # /etc/os-release
    cat > "$WORK_DIR/root/etc/os-release" << 'EOF'
NAME="NeolyxOS"
VERSION="1.0.0"
ID=neolyx
ID_LIKE=linux
VERSION_ID="1.0.0"
PRETTY_NAME="NeolyxOS 1.0.0"
HOME_URL="https://neolyx.ketivee.com"
BUG_REPORT_URL="https://github.com/ketivee/neolyx/issues"
EOF
    
    # /etc/neo-init.conf
    cat > "$WORK_DIR/root/etc/neo-init.conf" << 'EOF'
# NeolyxOS neo-init service configuration
# Format: name:priority:respawn:exec
# Priority: lower starts first
# Respawn: 1 = restart on exit, 0 = run once

neo-daemon:10:1:/sbin/neo-daemon
neo-network:20:1:/sbin/neo-network start
neo-shell:100:1:/sbin/neo-shell
EOF
    
    log_success "Configuration files installed"
}

# ==============================================================================
# Finalization
# ==============================================================================

finalize() {
    log_info "Finalizing image..."
    
    # Sync filesystems
    sync
    
    # Unmount
    umount "$WORK_DIR/root/boot/efi" 2>/dev/null || true
    umount "$WORK_DIR/root" 2>/dev/null || true
    umount "$WORK_DIR/efi" 2>/dev/null || true
    
    # Detach loop device
    losetup -d "$LOOP_DEVICE" 2>/dev/null || true
    LOOP_DEVICE=""
    
    # Get image size
    local size=$(du -h "$OUTPUT_FILE" | cut -f1)
    
    log_success "Image created: $OUTPUT_FILE ($size)"
}

# ==============================================================================
# Main
# ==============================================================================

main() {
    echo ""
    echo "==========================================="
    echo "  NeolyxOS Image Creation Tool"
    echo "  Copyright (c) 2025 KetiveeAI"
    echo "==========================================="
    echo ""
    
    check_root
    check_dependencies
    
    create_image
    partition_image
    setup_loop_device
    format_partitions
    mount_partitions
    
    install_rootfs
    install_kernel
    install_bootloader
    install_config_files
    
    finalize
    
    echo ""
    echo "==========================================="
    echo "  Image creation complete!"
    echo ""
    echo "  To test with QEMU:"
    echo "  qemu-system-x86_64 -m 4G \\"
    echo "    -drive file=$OUTPUT_FILE,format=raw \\"
    echo "    -bios /usr/share/ovmf/OVMF.fd \\"
    echo "    -boot menu=on"
    echo "==========================================="
    echo ""
}

main "$@"
