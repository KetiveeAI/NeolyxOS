#!/bin/bash
# NeolyxOS Smart Build System
# Copyright (c) 2025 KetiveeAI

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Build configuration
BUILD_TARGET=${1:-all}
BUILD_TYPE=${2:-release}
KERNEL_DIR="kernel"
BOOT_DIR="boot"
OUTPUT_DIR="build"

# Function to print section headers
print_header() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}================================${NC}"
}

# Function to print success
print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

# Function to print warning
print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

# Function to print error
print_error() {
    echo -e "${RED}✗ $1${NC}"
}

# Function to print info
print_info() {
    echo -e "${CYAN}→ $1${NC}"
}

# Check dependencies
check_dependencies() {
    print_header "Checking Dependencies"
    
    local missing_deps=0
    
    if ! command -v gcc &> /dev/null; then
        print_error "GCC not found"
        missing_deps=1
    else
        print_success "GCC found: $(gcc --version | head -n1)"
    fi
    
    if ! command -v ld &> /dev/null; then
        print_error "GNU linker not found"
        missing_deps=1
    else
        print_success "Linker found"
    fi
    
    if ! command -v as &> /dev/null; then
        print_error "GNU assembler not found"
        missing_deps=1
    else
        print_success "Assembler found"
    fi
    
    if ! command -v objcopy &> /dev/null; then
        print_error "objcopy not found"
        missing_deps=1
    else
        print_success "objcopy found"
    fi
    
    if ! command -v mcopy &> /dev/null; then
        print_warning "mtools (mcopy) not found - image update will fail"
        print_info "Install with: sudo apt install mtools"
    else
        print_success "mtools found"
    fi
    
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        print_warning "QEMU not found - testing will be unavailable"
        print_info "Install with: sudo apt install qemu-system-x86"
    else
        print_success "QEMU found"
    fi
    
    if [ $missing_deps -eq 1 ]; then
        print_error "Missing required dependencies. Please install them first."
        exit 1
    fi
    
    echo ""
}

# Build bootloader
build_bootloader() {
    print_header "Building UEFI Bootloader"
    
    if [ ! -f "$BOOT_DIR/uefi_bootloader.c" ]; then
        print_error "Bootloader source not found"
        return 1
    fi
    
    cd "$BOOT_DIR"
    print_info "Compiling bootloader..."
    
    gcc -ffreestanding -fno-stack-protector -fpic -fshort-wchar \
        -mno-red-zone -DEFI_FUNCTION_WRAPPER -I. \
        -c uefi_bootloader.c -o uefi_bootloader.o
    
    print_info "Linking bootloader..."
    ld -shared -Bsymbolic -L. -T uefi.lds uefi_bootloader.o \
        -o BOOTX64.EFI -lgnuefi -lefi
    
    if [ -f "BOOTX64.EFI" ]; then
        print_success "Bootloader built: BOOTX64.EFI ($(stat -c%s BOOTX64.EFI) bytes)"
    else
        print_error "Bootloader build failed"
        cd ..
        return 1
    fi
    
    cd ..
    echo ""
}

# Build kernel
build_kernel() {
    print_header "Building NeolyxOS Kernel"
    
    if [ ! -d "$KERNEL_DIR" ]; then
        print_error "Kernel directory not found"
        return 1
    fi
    
    cd "$KERNEL_DIR"
    
    # Fix nlc_config.o issue if it exists
    if [ -f "src/config/nlc_config.c" ] && [ ! -f "nlc_config.o" ]; then
        print_info "Pre-compiling nlc_config.o..."
        gcc -ffreestanding -nostdlib -mno-red-zone -Wall -Wextra -m64 -fno-pie -no-pie \
            -Iinclude -I../include -Isrc/config \
            -c src/config/nlc_config.c -o nlc_config.o 2>/dev/null || true
    fi
    
    print_info "Running make all..."
    if make all 2>&1 | tee build.log; then
        if [ -f "kernel.bin" ]; then
            local kernel_size=$(stat -c%s kernel.bin)
            print_success "Kernel built: kernel.bin ($kernel_size bytes)"
        else
            print_error "kernel.bin not found after build"
            cd ..
            return 1
        fi
    else
        print_error "Kernel build failed - check kernel/build.log"
        cd ..
        return 1
    fi
    
    cd ..
    echo ""
}

# Build userspace desktop
build_desktop() {
    print_header "Building Userspace Desktop"
    
    if [ ! -f "desktop/shell/desktop_shell.c" ]; then
        print_warning "Desktop source not found - skipping"
        return 0
    fi
    
    print_info "Compiling desktop shell (userspace)..."
    
    # Create desktop build directory
    mkdir -p desktop/build
    
    # Compile desktop shell as freestanding userspace binary
    gcc -ffreestanding -nostdlib -nostartfiles -fno-builtin \
        -mno-red-zone -m64 -fno-pie -no-pie \
        -Idesktop/include \
        -c desktop/shell/desktop_shell.c -o desktop/build/desktop_shell.o 2>&1 | head -20
    
    if [ $? -eq 0 ]; then
        print_success "Desktop compiled: desktop/build/desktop_shell.o"
    else
        print_warning "Desktop compilation had warnings (may still work)"
    fi
    
    # Also compile app_drawer if it exists
    if [ -f "desktop/shell/app_drawer.c" ]; then
        gcc -ffreestanding -nostdlib -nostartfiles -fno-builtin \
            -mno-red-zone -m64 -fno-pie -no-pie \
            -Idesktop/include \
            -c desktop/shell/app_drawer.c -o desktop/build/app_drawer.o 2>/dev/null || true
        print_info "Compiled app_drawer.o"
    fi
    
    echo ""
}

# Update disk image
update_image() {
    print_header "Updating Boot Image"
    
    if [ ! -f "neolyx.img" ]; then
        print_error "neolyx.img not found"
        return 1
    fi
    
    if [ ! -f "$KERNEL_DIR/kernel.bin" ]; then
        print_error "kernel.bin not found - build kernel first"
        return 1
    fi
    
    print_info "Copying kernel to boot image..."
    if mcopy -o -i neolyx.img $KERNEL_DIR/kernel.bin ::/EFI/BOOT/kernel.bin 2>/dev/null; then
        print_success "Updated ::/EFI/BOOT/kernel.bin"
    else
        print_error "Failed to update EFI partition"
        return 1
    fi
    
    if mcopy -o -i neolyx.img $KERNEL_DIR/kernel.bin ::/kernel.bin 2>/dev/null; then
        print_success "Updated ::/kernel.bin"
    else
        print_warning "Failed to update root kernel.bin (non-critical)"
    fi
    
    if [ -f "$BOOT_DIR/BOOTX64.EFI" ]; then
        print_info "Copying bootloader to boot image..."
        if mcopy -o -i neolyx.img $BOOT_DIR/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null; then
            print_success "Updated ::/EFI/BOOT/BOOTX64.EFI"
        else
            print_warning "Failed to update bootloader (non-critical)"
        fi
    fi
    
    echo ""
}

# Build Rust applications
build_apps() {
    print_header "Building Rust Applications"
    
    if ! command -v rustc &> /dev/null; then
        print_warning "Rust not installed - skipping applications"
        echo ""
        return 0
    fi
    
    print_success "Rust found: $(rustc --version)"
    
    local apps=("terminal" "icon-converter" "reolab")
    local built_count=0
    
    for app in "${apps[@]}"; do
        if [ -d "apps/$app" ]; then
            print_info "Building $app..."
            cd "apps/$app"
            if cargo build --$BUILD_TYPE --quiet 2>/dev/null; then
                print_success "$app built"
                ((built_count++))
            else
                print_warning "$app build failed (non-critical)"
            fi
            cd ../..
        fi
    done
    
    if [ $built_count -gt 0 ]; then
        print_success "Built $built_count/$((${#apps[@]})) applications"
    fi
    
    # Build C-based applications
    print_info "Building C applications..."
    local c_apps=("settings")
    
    for app in "${c_apps[@]}"; do
        if [ -d "apps/$app" ] && [ -f "apps/$app/Makefile" ]; then
            print_info "Building $app (C)..."
            cd "apps/$app"
            if make --quiet 2>/dev/null; then
                print_success "$app built"
            else
                print_warning "$app build failed (check REOX runtime)"
            fi
            cd ../..
        fi
    done
    
    echo ""
}

# Create summary
print_summary() {
    print_header "Build Summary"
    
    echo -e "${CYAN}Bootloader:${NC}"
    if [ -f "$BOOT_DIR/BOOTX64.EFI" ]; then
        ls -lh "$BOOT_DIR/BOOTX64.EFI" | awk '{print "  " $9 " - " $5}'
    else
        echo "  Not built"
    fi
    
    echo -e "${CYAN}Kernel:${NC}"
    if [ -f "$KERNEL_DIR/kernel.bin" ]; then
        ls -lh "$KERNEL_DIR/kernel.bin" | awk '{print "  " $9 " - " $5}'
    else
        echo "  Not built"
    fi
    
    echo -e "${CYAN}Boot Image:${NC}"
    if [ -f "neolyx.img" ]; then
        ls -lh neolyx.img | awk '{print "  " $9 " - " $5}'
        print_success "Ready to boot!"
    else
        echo "  Not found"
    fi
    
    echo ""
}

# Main build logic
main() {
    print_header "NeolyxOS Smart Build System"
    echo ""
    
    check_dependencies
    
    case "$BUILD_TARGET" in
        all)
            build_bootloader || print_warning "Bootloader build skipped"
            build_kernel || exit 1
            build_desktop
            update_image || exit 1
            build_apps
            ;;
        kernel)
            build_kernel || exit 1
            update_image || exit 1
            ;;
        desktop)
            build_desktop || exit 1
            ;;
        bootloader)
            build_bootloader || exit 1
            update_image || exit 1
            ;;
        apps)
            build_apps
            ;;
        clean)
            print_header "Cleaning Build Artifacts"
            cd "$KERNEL_DIR"
            make clean 2>/dev/null || true
            cd ..
            if [ -d "$BOOT_DIR" ]; then
                rm -f "$BOOT_DIR"/*.o "$BOOT_DIR"/*.EFI 2>/dev/null || true
            fi
            print_success "Clean complete"
            echo ""
            exit 0
            ;;
        *)
            echo "Usage: $0 [target] [build_type]"
            echo ""
            echo "Targets:"
            echo "  all         - Build everything (default)"
            echo "  kernel      - Build kernel only"
            echo "  bootloader  - Build bootloader only"
            echo "  apps        - Build Rust applications only"
            echo "  clean       - Clean build artifacts"
            echo ""
            echo "Build types:"
            echo "  release     - Optimized build (default)"
            echo "  debug       - Debug build"
            echo ""
            exit 1
            ;;
    esac
    
    print_summary
    
    print_header "Build Complete!"
    echo -e "${GREEN}To test in QEMU, run:${NC}"
    echo -e "  ${CYAN}./boot_test.sh${NC}"
    echo ""
}

# Run main
main