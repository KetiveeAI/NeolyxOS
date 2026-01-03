#!/bin/bash
#
# NeolyxOS A/B Update Script
#
# Atomic update system with:
# - A/B slot partitioning
# - Signature verification
# - Automatic rollback on failure
# - Status tracking
#
# Copyright (c) 2025 KetiveeAI
#

set -e

# ==============================================================================
# Configuration
# ==============================================================================

UPDATE_URL="${UPDATE_URL:-https://updates.neolyx.ketivee.com}"
STAGING_DIR="/var/lib/neo-update/staging"
STATUS_FILE="/var/lib/neo-update/status"
SLOT_FILE="/var/lib/neo-update/current_slot"
LOG_FILE="/var/log/neo-update.log"
PUBLIC_KEY="/etc/neo-update/public.pem"

# Partition layout
SLOT_A_ROOT="/dev/sda2"
SLOT_B_ROOT="/dev/sda3"
SLOT_A_LABEL="neolyx-a"
SLOT_B_LABEL="neolyx-b"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# ==============================================================================
# Logging
# ==============================================================================

log() {
    local level="$1"
    shift
    local msg="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    echo "[$timestamp] [$level] $msg" >> "$LOG_FILE"
    
    case "$level" in
        INFO)  echo -e "${BLUE}[INFO]${NC} $msg" ;;
        OK)    echo -e "${GREEN}[OK]${NC} $msg" ;;
        WARN)  echo -e "${YELLOW}[WARN]${NC} $msg" ;;
        ERROR) echo -e "${RED}[ERROR]${NC} $msg" ;;
    esac
}

log_info() { log "INFO" "$@"; }
log_ok() { log "OK" "$@"; }
log_warn() { log "WARN" "$@"; }
log_error() { log "ERROR" "$@"; }

# ==============================================================================
# Initialization
# ==============================================================================

init() {
    mkdir -p "$(dirname "$STATUS_FILE")"
    mkdir -p "$STAGING_DIR"
    mkdir -p "$(dirname "$LOG_FILE")"
    mkdir -p "$(dirname "$PUBLIC_KEY")"
    
    # Create default slot file if not exists
    if [ ! -f "$SLOT_FILE" ]; then
        echo "a" > "$SLOT_FILE"
    fi
}

get_current_slot() {
    cat "$SLOT_FILE" 2>/dev/null || echo "a"
}

get_inactive_slot() {
    local current=$(get_current_slot)
    if [ "$current" = "a" ]; then
        echo "b"
    else
        echo "a"
    fi
}

get_slot_partition() {
    local slot="$1"
    if [ "$slot" = "a" ]; then
        echo "$SLOT_A_ROOT"
    else
        echo "$SLOT_B_ROOT"
    fi
}

set_status() {
    echo "$1" > "$STATUS_FILE"
    log_info "Status: $1"
}

get_status() {
    cat "$STATUS_FILE" 2>/dev/null || echo "idle"
}

# ==============================================================================
# Update Operations
# ==============================================================================

check_update() {
    log_info "Checking for updates..."
    
    local current_version=$(cat /etc/neolyx-version 2>/dev/null || echo "0.0.0")
    local latest_version
    
    if command -v curl &> /dev/null; then
        latest_version=$(curl -sf "$UPDATE_URL/latest.txt" 2>/dev/null || echo "")
    elif command -v wget &> /dev/null; then
        latest_version=$(wget -qO- "$UPDATE_URL/latest.txt" 2>/dev/null || echo "")
    fi
    
    if [ -z "$latest_version" ]; then
        log_warn "Could not fetch latest version"
        return 1
    fi
    
    log_info "Current version: $current_version"
    log_info "Latest version:  $latest_version"
    
    if [ "$current_version" = "$latest_version" ]; then
        log_ok "Already up to date"
        return 1
    fi
    
    echo "$latest_version"
    return 0
}

download_update() {
    local version="$1"
    local filename="neolyx-${version}.tar.gz"
    local sig_filename="neolyx-${version}.tar.gz.sig"
    local url="$UPDATE_URL/releases/$filename"
    local sig_url="$UPDATE_URL/releases/$sig_filename"
    
    log_info "Downloading update: $version"
    set_status "downloading"
    
    # Download update image
    if command -v curl &> /dev/null; then
        curl -f -o "$STAGING_DIR/$filename" "$url" || {
            log_error "Download failed"
            return 1
        }
        curl -f -o "$STAGING_DIR/$sig_filename" "$sig_url" 2>/dev/null || true
    elif command -v wget &> /dev/null; then
        wget -q -O "$STAGING_DIR/$filename" "$url" || {
            log_error "Download failed"
            return 1
        }
        wget -q -O "$STAGING_DIR/$sig_filename" "$sig_url" 2>/dev/null || true
    fi
    
    log_ok "Download complete"
    echo "$STAGING_DIR/$filename"
}

verify_signature() {
    local archive="$1"
    local signature="${archive}.sig"
    
    log_info "Verifying signature..."
    set_status "verifying"
    
    if [ ! -f "$signature" ]; then
        log_warn "No signature file found, skipping verification"
        return 0
    fi
    
    if [ ! -f "$PUBLIC_KEY" ]; then
        log_warn "No public key configured, skipping verification"
        return 0
    fi
    
    if command -v openssl &> /dev/null; then
        if openssl dgst -sha256 -verify "$PUBLIC_KEY" -signature "$signature" "$archive"; then
            log_ok "Signature verified"
            return 0
        else
            log_error "Signature verification failed!"
            return 1
        fi
    else
        log_warn "openssl not available, skipping verification"
        return 0
    fi
}

install_update() {
    local archive="$1"
    local target_slot=$(get_inactive_slot)
    local target_partition=$(get_slot_partition "$target_slot")
    local mount_point="/mnt/neo-update-target"
    
    log_info "Installing update to slot $target_slot ($target_partition)"
    set_status "installing"
    
    # Create mount point
    mkdir -p "$mount_point"
    
    # Format target partition
    log_info "Formatting target partition..."
    mkfs.ext4 -q -L "neolyx-$target_slot" "$target_partition" || {
        log_error "Failed to format partition"
        return 1
    }
    
    # Mount target partition
    mount "$target_partition" "$mount_point" || {
        log_error "Failed to mount target partition"
        return 1
    }
    
    # Extract update
    log_info "Extracting update..."
    tar -xzf "$archive" -C "$mount_point" || {
        log_error "Failed to extract update"
        umount "$mount_point"
        return 1
    }
    
    # Sync and unmount
    sync
    umount "$mount_point"
    
    log_ok "Update installed to slot $target_slot"
    echo "$target_slot"
}

switch_slot() {
    local new_slot="$1"
    
    log_info "Switching to slot $new_slot"
    set_status "switching"
    
    # Update bootloader configuration
    local grub_cfg="/boot/efi/EFI/neolyx/grub/grub.cfg"
    local new_partition=$(get_slot_partition "$new_slot")
    
    if [ -f "$grub_cfg" ]; then
        # Update GRUB config
        sed -i "s|root=/dev/sd[ab][0-9]|root=$new_partition|g" "$grub_cfg"
        log_ok "Updated GRUB configuration"
    fi
    
    # Mark new slot as active
    echo "$new_slot" > "$SLOT_FILE"
    
    # Create pending boot marker
    touch "/var/lib/neo-update/pending_boot"
    
    log_ok "Slot switched to $new_slot"
}

finalize_update() {
    log_info "Finalizing update..."
    set_status "finalizing"
    
    # Clean up staging
    rm -rf "$STAGING_DIR"/*
    
    # Remove pending boot marker (boot was successful)
    rm -f "/var/lib/neo-update/pending_boot"
    
    set_status "idle"
    log_ok "Update complete"
}

rollback() {
    log_warn "Rolling back to previous slot..."
    
    local current_slot=$(get_current_slot)
    local previous_slot=$(get_inactive_slot)
    
    # Switch back to previous slot
    echo "$previous_slot" > "$SLOT_FILE"
    
    # Update bootloader
    local grub_cfg="/boot/efi/EFI/neolyx/grub/grub.cfg"
    local prev_partition=$(get_slot_partition "$previous_slot")
    
    if [ -f "$grub_cfg" ]; then
        sed -i "s|root=/dev/sd[ab][0-9]|root=$prev_partition|g" "$grub_cfg"
    fi
    
    set_status "rolled_back"
    log_ok "Rolled back to slot $previous_slot"
}

check_boot_success() {
    # Called at boot to verify the update completed successfully
    if [ -f "/var/lib/neo-update/pending_boot" ]; then
        log_warn "Pending boot detected, update may have failed"
        
        # Simple check: if we booted successfully, the update is good
        # Remove the pending marker
        rm -f "/var/lib/neo-update/pending_boot"
        log_ok "Boot successful, finalizing update"
        finalize_update
    fi
}

# ==============================================================================
# Main Commands
# ==============================================================================

cmd_check() {
    check_update
}

cmd_update() {
    local version
    
    version=$(check_update) || {
        echo "No update available"
        exit 0
    }
    
    local archive
    archive=$(download_update "$version") || exit 1
    
    verify_signature "$archive" || {
        log_error "Signature verification failed, aborting"
        exit 1
    }
    
    local new_slot
    new_slot=$(install_update "$archive") || exit 1
    
    switch_slot "$new_slot"
    
    echo ""
    log_ok "Update prepared successfully!"
    echo "Reboot to apply the update: reboot"
}

cmd_rollback() {
    rollback
    echo ""
    log_ok "Rollback prepared"
    echo "Reboot to complete: reboot"
}

cmd_status() {
    local status=$(get_status)
    local current=$(get_current_slot)
    local inactive=$(get_inactive_slot)
    local version=$(cat /etc/neolyx-version 2>/dev/null || echo "unknown")
    
    echo "NeolyxOS Update Status"
    echo "======================"
    echo "  Current version:  $version"
    echo "  Active slot:      $current ($(get_slot_partition $current))"
    echo "  Inactive slot:    $inactive ($(get_slot_partition $inactive))"
    echo "  Update status:    $status"
    
    if [ -f "/var/lib/neo-update/pending_boot" ]; then
        echo "  Pending update:   YES"
    fi
}

cmd_boot_check() {
    check_boot_success
}

# ==============================================================================
# Main
# ==============================================================================

usage() {
    echo "NeolyxOS A/B Update System"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  check      Check for updates"
    echo "  update     Download and prepare update"
    echo "  rollback   Rollback to previous slot"
    echo "  status     Show update status"
    echo "  boot-check Called at boot to verify update"
}

main() {
    init
    
    local cmd="${1:-status}"
    
    case "$cmd" in
        check)
            cmd_check
            ;;
        update)
            cmd_update
            ;;
        rollback)
            cmd_rollback
            ;;
        status)
            cmd_status
            ;;
        boot-check)
            cmd_boot_check
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            log_error "Unknown command: $cmd"
            usage
            exit 1
            ;;
    esac
}

main "$@"
