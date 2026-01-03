# NeolyxOS Kernel Makefile - 64-bit x86_64
# Copyright (c) 2025 KetiveeAI
CC = gcc
LD = ld
AS = as
OBJCOPY = objcopy

# Flags for freestanding kernel
CFLAGS = -ffreestanding -nostdlib -mno-red-zone -Wall -Wextra -m64 -fno-pie -no-pie \
	-Iinclude \
	-I../include \
	-I../include/core \
	-I../include/utils \
	-I../include/fs \
	-I../include/drivers \
	-I../include/mm \
	-I../include/io \
	-I../include/video \
	-I../include/network \
	-I../include/storage \
	-I../include/runtime \
	-I../include/net \
	-I../include/ui \
	-Isrc/core \
	-Isrc/video \
	-Isrc/drivers \
	-Isrc/network \
	-Isrc/storage \
	-Isrc/utils \
	-Isrc/io \
	-Isrc/arch/x86_64 \
	-Isrc/mm \
	-Isrc/runtime \
	-Isrc/net \
	-Isrc/ui \
	-Isrc/drivers/network \
	-Isrc/drivers/storage \
	-Isrc/drivers/input \
	-Isrc/drivers/nxgame \
	-Isrc/fs \
	-Isrc/config

LDFLAGS = -T linker.ld -static

# Entry point (MUST BE FIRST!)
obj-y += startup.o

# Boot menu (provides kernel_main entry point)
obj-y += boot_firmware.o

# Core kernel components
obj-y += main.o
obj-y += kernel.o
obj-y += boot.o
obj-y += power.o
obj-y += installer.o
obj-y += sysconfig.o

# Architecture-specific components
obj-y += gdt.o
obj-y += idt.o
obj-y += isr.o
obj-y += isr_handler.o
obj-y += paging.o
obj-y += arch.o
obj-y += bsp.o

# Memory management
obj-y += heap.o
obj-y += pmm.o
obj-y += vmm.o

# System calls and process management
obj-y += syscall.o
obj-y += process.o
obj-y += elf.o
obj-y += runtime.o
obj-y += recovery.o
obj-y += stubs.o
obj-y += boot_guard.o
obj-y += session_manager.o
obj-y += app_launcher.o
obj-y += browser_session.o

# NXGame Bridge (games/heavy apps)
obj-y += nxgame_bridge.o

# Phase 12: Security hardening
obj-y += security.o
obj-y += safe_mem.o
obj-y += panic_handler.o
obj-y += nxpt.o

# XNU-inspired security (NeolyxOS implementation)
obj-y += code_integrity.o
obj-y += csr.o
obj-y += task_port_protection.o

# Fault Tolerance Subsystem (kernel never crashes, never reboots)
obj-y += fault_router.o
obj-y += emergency_stack.o
obj-y += reboot_guard.o
obj-y += watchdog.o

# Platform detection and optimizations
obj-y += platform_detect.o
obj-y += amd_platform.o
obj-y += intel_platform.o
obj-y += arm_platform.o
obj-y += generic_platform.o
obj-y += platform_kdrv.o

# Device drivers - Core
obj-y += drivers.o
obj-y += timer.o
obj-y += pit.o
obj-y += rtc.o
obj-y += serial.o
obj-y += keyboard.o
obj-y += mouse.o
obj-y += pci.o

# Device drivers - Storage
obj-y += ahci.o
obj-y += ata.o
obj-y += nvme.o

# Device drivers - Network
obj-y += e1000.o
obj-y += wifi_drv.o
obj-y += bluetooth.o
obj-y += nxnet_kdrv.o

# Device drivers - Audio/Video
obj-y += nxaudio.o
obj-y += nxaudio_kdrv.o
obj-y += nxaudio_spatial.o
obj-y += gpu.o
obj-y += nxgfx_kdrv.o
obj-y += nxbridge.o

# Device drivers - USB/Other
obj-y += usb.o
obj-y += nxusb_kdrv.o
obj-y += nxpci_kdrv.o
obj-y += nxstor_kdrv.o
obj-y += nxsysinfo_kdrv.o
obj-y += nxdisplay_kdrv.o
obj-y += nxpen_kdrv.o
obj-y += nxtask_kdrv.o
obj-y += print.o

# Network subsystem
obj-y += network_core.o
obj-y += tcpip.o
obj-y += netinfra.o
obj-y += ethernet.o
# wifi.o removed - already included as wifi_drv.o

# File systems
obj-y += file.o
obj-y += nxfs.o
obj-y += fat.o
obj-y += vfs.o
obj-y += bcache.o
obj-y += ramfs.o
obj-y += sip.o
obj-y += fs_init.o
obj-y += npa_archive.o

# Storage subsystem
obj-y += storage.o
obj-y += ahci_block.o

# Block layer
obj-y += block.o
obj-y += nvme_block.o

# UI subsystem (kernel-level)
obj-y += font.o
# REMOVED: desktop.o - GUI runs in userspace, not kernel
# obj-y += desktop.o

# User authentication (kernel-level for login)
obj-y += user_auth.o

# Utilities
obj-y += log.o
obj-y += string.o
obj-y += nucleus.o
obj-y += nucleus_write.o

# Video subsystem
obj-y += video.o
obj-y += console.o
obj-y += splash.o
obj-y += fbuffer.o
obj-y += nxgfx_accel.o
obj-y += nx3d.o

# Shell subsystem
obj-y += terminal.o
obj-y += terminal_shell.o
obj-y += stress_test.o

# NPA Package Manager and NLC Config
obj-y += npa_manager.o
obj-y += nlc_config.o

# Build targets
all: kernel.elf kernel.bin

kernel.elf: $(obj-y) linker.ld
	$(LD) $(LDFLAGS) $(obj-y) -o kernel.elf

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin

# Boot Firmware (provides kernel_main entry point with boot menu UI)
boot_firmware.o: boot_firmware.c include/ui/disk_icons.h
	$(CC) $(CFLAGS) -c boot_firmware.c -o boot_firmware.o

# Core kernel rules
main.o: src/core/main.c include/core/kernel.h include/video/video.h include/drivers/drivers.h include/network/network.h include/storage/storage.h include/utils/log.h include/io/keyboard.h include/core/syscall.h include/core/process.h include/core/runtime.h
	$(CC) $(CFLAGS) -c src/core/main.c -o main.o

kernel.o: src/core/kernel.c include/core/kernel.h
	$(CC) $(CFLAGS) -c src/core/kernel.c -o kernel.o

# Architecture-specific rules
startup.o: src/arch/x86_64/startup.S
	$(AS) -o startup.o src/arch/x86_64/startup.S

gdt.o: src/arch/x86_64/gdt.c
	$(CC) $(CFLAGS) -c src/arch/x86_64/gdt.c -o gdt.o

idt.o: src/arch/x86_64/idt.c
	$(CC) $(CFLAGS) -c src/arch/x86_64/idt.c -o idt.o

isr.o: src/arch/x86_64/isr.S
	$(AS) --64 -o isr.o src/arch/x86_64/isr.S

isr_handler.o: src/arch/x86_64/isr_handler.c
	$(CC) $(CFLAGS) -c src/arch/x86_64/isr_handler.c -o isr_handler.o

paging.o: src/arch/x86_64/paging.c
	$(CC) $(CFLAGS) -c src/arch/x86_64/paging.c -o paging.o

arch.o: src/arch/x86_64/arch.S
	$(AS) -o arch.o src/arch/x86_64/arch.S


# Memory management rules
heap.o: src/mm/heap.c
	$(CC) $(CFLAGS) -c src/mm/heap.c -o heap.o

pmm.o: src/mm/pmm.c
	$(CC) $(CFLAGS) -c src/mm/pmm.c -o pmm.o

vmm.o: src/mm/vmm.c
	$(CC) $(CFLAGS) -c src/mm/vmm.c -o vmm.o

# System calls and process management rules
syscall.o: src/core/syscall.c include/core/syscall.h include/core/kernel.h include/core/process.h include/network/network.h include/storage/storage.h include/utils/log.h include/fs/file.h
	$(CC) $(CFLAGS) -c src/core/syscall.c -o syscall.o

process.o: src/core/process.c include/core/process.h include/core/kernel.h include/utils/log.h
	$(CC) $(CFLAGS) -c src/core/process.c -o process.o

elf.o: src/core/elf.c include/core/elf.h
	$(CC) $(CFLAGS) -c src/core/elf.c -o elf.o

context.o: src/core/context.S
	$(CC) $(CFLAGS) -c src/core/context.S -o context.o

runtime.o: src/runtime/runtime.c include/core/runtime.h include/core/kernel.h include/utils/log.h
	$(CC) $(CFLAGS) -c src/runtime/runtime.c -o runtime.o

recovery.o: src/core/recovery.c include/core/recovery.h src/video/font.h
	$(CC) $(CFLAGS) -c src/core/recovery.c -o recovery.o

stubs.o: src/core/stubs.c
	$(CC) $(CFLAGS) -c src/core/stubs.c -o stubs.o

# Device drivers rules
drivers.o: src/drivers/drivers.c
	$(CC) $(CFLAGS) -c src/drivers/drivers.c -o drivers.o

ethernet.o: src/drivers/network/ethernet.c
	$(CC) $(CFLAGS) -c src/drivers/network/ethernet.c -o ethernet.o

nlc_config.o: src/config/nlc_config.c
	$(CC) $(CFLAGS) -c src/config/nlc_config.c -o nlc_config.o

wifi.o: src/drivers/network/wifi.c
	$(CC) $(CFLAGS) -c src/drivers/network/wifi.c -o wifi.o

ata.o: src/drivers/storage/ata.c
	$(CC) $(CFLAGS) -c src/drivers/storage/ata.c -o ata.o

mouse.o: src/drivers/input/mouse.c
	$(CC) $(CFLAGS) -c src/drivers/input/mouse.c -o mouse.o

keyboard.o: src/drivers/keyboard.c include/drivers/keyboard.h
	$(CC) $(CFLAGS) -c src/drivers/keyboard.c -o keyboard.o

timer.o: src/drivers/timer.c src/io/io.h
	$(CC) $(CFLAGS) -c src/drivers/timer.c -o timer.o

pit.o: src/drivers/pit.c include/drivers/pit.h
	$(CC) $(CFLAGS) -c src/drivers/pit.c -o pit.o

rtc.o: src/drivers/rtc.c include/drivers/rtc.h
	$(CC) $(CFLAGS) -c src/drivers/rtc.c -o rtc.o

serial.o: src/drivers/serial.c src/drivers/serial.h
	$(CC) $(CFLAGS) -c src/drivers/serial.c -o serial.o

pci.o: src/drivers/pci/pci.c src/drivers/pci/pci.h
	$(CC) $(CFLAGS) -c src/drivers/pci/pci.c -o pci.o

# File system rules
file.o: src/fs/file.c include/fs/file.h include/utils/log.h include/storage/storage.h
	$(CC) $(CFLAGS) -c src/fs/file.c -o file.o

# Network subsystem rules
network.o: src/network/network.c include/network/network.h include/core/kernel.h
	$(CC) $(CFLAGS) -c src/network/network.c -o network.o

# Storage subsystem rules
storage.o: src/storage/storage.c include/storage/storage.h include/core/kernel.h
	$(CC) $(CFLAGS) -c src/storage/storage.c -o storage.o

ahci_block.o: src/storage/ahci_block.c include/storage/ahci_block.h include/drivers/ahci.h include/fs/nxfs.h
	$(CC) $(CFLAGS) -c src/storage/ahci_block.c -o ahci_block.o

# Block layer rules
block.o: src/drivers/block/block.c src/drivers/block/block.h
	$(CC) $(CFLAGS) -c src/drivers/block/block.c -o block.o

nvme_block.o: src/drivers/block/nvme_block.c src/drivers/block/block.h
	$(CC) $(CFLAGS) -c src/drivers/block/nvme_block.c -o nvme_block.o

# Audio driver rules
nxaudio.o: src/drivers/nxaudio.c include/drivers/nxaudio.h
	$(CC) $(CFLAGS) -c src/drivers/nxaudio.c -o nxaudio.o

nxaudio_kdrv.o: src/drivers/audio/nxaudio_kdrv.c include/drivers/nxaudio_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/audio/nxaudio_kdrv.c -o nxaudio_kdrv.o

# NXAudio Spatial Audio Engine (Brand: Ketivee Oohm)
nxaudio_spatial.o: src/drivers/audio/nxaudio_spatial.c include/drivers/nxaudio.h
	$(CC) $(CFLAGS) -c src/drivers/audio/nxaudio_spatial.c -o nxaudio_spatial.o

# Graphics driver rules
nxgfx_kdrv.o: src/drivers/graphics/nxgfx_kdrv.c include/drivers/nxgfx_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/graphics/nxgfx_kdrv.c -o nxgfx_kdrv.o

nxbridge.o: src/drivers/graphics/nxbridge.c include/drivers/nxbridge.h
	$(CC) $(CFLAGS) -c src/drivers/graphics/nxbridge.c -o nxbridge.o

# Network driver rules
nxnet_kdrv.o: src/drivers/network/nxnet_kdrv.c include/drivers/nxnet_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/network/nxnet_kdrv.c -o nxnet_kdrv.o

# USB driver rules
nxusb_kdrv.o: src/drivers/usb/nxusb_kdrv.c include/drivers/nxusb_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/usb/nxusb_kdrv.c -o nxusb_kdrv.o

# PCI driver rules
nxpci_kdrv.o: src/drivers/pci/nxpci_kdrv.c include/drivers/nxpci_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/pci/nxpci_kdrv.c -o nxpci_kdrv.o

# Storage driver rules
nxstor_kdrv.o: src/drivers/storage/nxstor_kdrv.c include/drivers/nxstor_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/storage/nxstor_kdrv.c -o nxstor_kdrv.o

# System info driver rules
nxsysinfo_kdrv.o: src/drivers/system/nxsysinfo_kdrv.c include/drivers/nxsysinfo_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/system/nxsysinfo_kdrv.c -o nxsysinfo_kdrv.o

# Display driver rules
nxdisplay_kdrv.o: src/drivers/display/nxdisplay_kdrv.c include/drivers/nxdisplay_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/display/nxdisplay_kdrv.c -o nxdisplay_kdrv.o

# Pen driver rules
nxpen_kdrv.o: src/drivers/input/nxpen_kdrv.c include/drivers/nxpen_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/input/nxpen_kdrv.c -o nxpen_kdrv.o

# Task manager driver rules
nxtask_kdrv.o: src/drivers/system/nxtask_kdrv.c include/drivers/nxtask_kdrv.h
	$(CC) $(CFLAGS) -c src/drivers/system/nxtask_kdrv.c -o nxtask_kdrv.o

# Utilities rules
log.o: src/utils/log.c include/utils/log.h include/core/kernel.h
	$(CC) $(CFLAGS) -c src/utils/log.c -o log.o

string.o: src/utils/string.c ../include/string.h
	$(CC) $(CFLAGS) -c src/utils/string.c -o string.o

# Video subsystem rules
video.o: src/video/video.c include/video/video.h
	$(CC) $(CFLAGS) -c src/video/video.c -o video.o

console.o: src/video/console.c src/video/font.h include/video/console.h
	$(CC) $(CFLAGS) -c src/video/console.c -o console.o

splash.o: src/video/splash.c include/video/splash.h
	$(CC) $(CFLAGS) -c src/video/splash.c -o splash.o

fbuffer.o: src/video/framebuffer.c
	$(CC) $(CFLAGS) -c src/video/framebuffer.c -o fbuffer.o

# Test target
test: kernel.bin
	@echo "Kernel build successful!"
	@echo "Kernel size: $$(stat -c%s kernel.bin 2>/dev/null || stat -f%z kernel.bin 2>/dev/null || echo unknown) bytes"
	@echo "Testing kernel in QEMU..."
	qemu-system-x86_64 -kernel kernel.bin -nographic -serial stdio

# === NEW DRIVER BUILD RULES ===

# Core - Boot and Power
boot.o: src/core/boot.c
	$(CC) $(CFLAGS) -c src/core/boot.c -o boot.o

power.o: src/core/power.c
	$(CC) $(CFLAGS) -c src/core/power.c -o power.o

installer.o: src/core/installer.c
	$(CC) $(CFLAGS) -c src/core/installer.c -o installer.o

sysconfig.o: src/core/sysconfig.c
	$(CC) $(CFLAGS) -c src/core/sysconfig.c -o sysconfig.o

# Memory - PMM
pmm.o: src/mm/pmm.c
	$(CC) $(CFLAGS) -c src/mm/pmm.c -o pmm.o

# Drivers - Storage
ahci.o: src/drivers/ahci.c
	$(CC) $(CFLAGS) -c src/drivers/ahci.c -o ahci.o

nvme.o: src/drivers/storage/nvme.c src/drivers/storage/nvme.h
	$(CC) $(CFLAGS) -c src/drivers/storage/nvme.c -o nvme.o

# Drivers - Network
e1000.o: src/drivers/e1000.c
	$(CC) $(CFLAGS) -c src/drivers/e1000.c -o e1000.o

wifi_drv.o: src/drivers/wifi.c
	$(CC) $(CFLAGS) -c src/drivers/wifi.c -o wifi_drv.o

bluetooth.o: src/drivers/bluetooth.c
	$(CC) $(CFLAGS) -c src/drivers/bluetooth.c -o bluetooth.o

# Drivers - Audio/Video
nxaudio.o: src/drivers/nxaudio.c
	$(CC) $(CFLAGS) -c src/drivers/nxaudio.c -o nxaudio.o

gpu.o: src/drivers/gpu.c
	$(CC) $(CFLAGS) -c src/drivers/gpu.c -o gpu.o

# Drivers - USB/Print
usb.o: src/drivers/usb/usb.c
	$(CC) $(CFLAGS) -c src/drivers/usb/usb.c -o usb.o

print.o: src/drivers/print.c
	$(CC) $(CFLAGS) -c src/drivers/print.c -o print.o

# Network Subsystem
network_core.o: src/net/network.c
	$(CC) $(CFLAGS) -c src/net/network.c -o network_core.o

tcpip.o: src/net/tcpip.c
	$(CC) $(CFLAGS) -c src/net/tcpip.c -o tcpip.o

netinfra.o: src/net/netinfra.c
	$(CC) $(CFLAGS) -c src/net/netinfra.c -o netinfra.o

# Filesystem - NXFS
nxfs.o: src/fs/nxfs.c
	$(CC) $(CFLAGS) -c src/fs/nxfs.c -o nxfs.o

# Filesystem - FAT32
fat.o: src/fs/fat.c
	$(CC) $(CFLAGS) -c src/fs/fat.c -o fat.o

# Filesystem - VFS
vfs.o: src/fs/vfs.c
	$(CC) $(CFLAGS) -c src/fs/vfs.c -o vfs.o

# Filesystem - Block Cache
bcache.o: src/fs/bcache.c include/fs/bcache.h
	$(CC) $(CFLAGS) -c src/fs/bcache.c -o bcache.o

# Filesystem - System Integrity Protection
sip.o: src/fs/sip.c include/fs/sip.h
	$(CC) $(CFLAGS) -c src/fs/sip.c -o sip.o

# Filesystem - Initialization (directory structure)
fs_init.o: src/fs/fs_init.c include/fs/fs_init.h include/fs/vfs.h
	$(CC) $(CFLAGS) -c src/fs/fs_init.c -o fs_init.o

# Filesystem - RAM Filesystem
ramfs.o: src/fs/ramfs.c include/fs/ramfs.h
	$(CC) $(CFLAGS) -c src/fs/ramfs.c -o ramfs.o

# Filesystem - NPA Archive
npa_archive.o: src/fs/npa_archive.c include/fs/npa_archive.h
	$(CC) $(CFLAGS) -c src/fs/npa_archive.c -o npa_archive.o

# Core - NPA Package Manager
npa_manager.o: src/core/npa_manager.c include/core/npa_manager.h
	$(CC) $(CFLAGS) -c src/core/npa_manager.c -o npa_manager.o

# Core - NeolyxOS Program Tree (.nxpt) parser
nxpt.o: src/core/nxpt.c include/core/nxpt.h
	$(CC) $(CFLAGS) -c src/core/nxpt.c -o nxpt.o

# UI - Font
font.o: src/ui/font.c
	$(CC) $(CFLAGS) -c src/ui/font.c -o font.o

# REMOVED: Desktop shell - runs in userspace now
# desktop.o: src/ui/desktop.c include/ui/desktop.h
# 	$(CC) $(CFLAGS) -c src/ui/desktop.c -o desktop.o

# User authentication (kernel-level for login)
user_auth.o: src/core/user_auth.c include/core/user_auth.h
	$(CC) $(CFLAGS) -c src/core/user_auth.c -o user_auth.o

# Shell - Terminal
terminal.o: src/shell/terminal.c include/shell/terminal.h
	$(CC) $(CFLAGS) -c src/shell/terminal.c -o terminal.o

terminal_shell.o: src/shell/terminal_shell.c include/shell/terminal.h include/video/console.h
	$(CC) $(CFLAGS) -c src/shell/terminal_shell.c -o terminal_shell.o

# Utilities - Nucleus Disk (suppress sign-compare warnings in UI code)
nucleus.o: src/utils/nucleus.c include/utils/nucleus.h include/utils/nucleus_write.h
	$(CC) $(CFLAGS) -Wno-sign-compare -c src/utils/nucleus.c -o nucleus.o

nucleus_write.o: src/utils/nucleus_write.c include/utils/nucleus_write.h include/drivers/ahci.h include/mm/pmm.h
	$(CC) $(CFLAGS) -c src/utils/nucleus_write.c -o nucleus_write.o

# Test - Stress tests
stress_test.o: src/test/stress_test.c include/test/stress_test.h
	$(CC) $(CFLAGS) -c src/test/stress_test.c -o stress_test.o

# Boot Guard - crash-proof boot protection
boot_guard.o: src/core/boot_guard.c include/core/boot_guard.h
	$(CC) $(CFLAGS) -c src/core/boot_guard.c -o boot_guard.o

# Session Manager - crash isolation and GUI restart
session_manager.o: src/core/session_manager.c include/core/session_manager.h
	$(CC) $(CFLAGS) -c src/core/session_manager.c -o session_manager.o

# App Launcher - REOX app execution
app_launcher.o: src/core/app_launcher.c
	$(CC) $(CFLAGS) -c src/core/app_launcher.c -o app_launcher.o

# Browser Session Manager - ZepraBrowser firmware integration
browser_session.o: browser_session.c browser_session.h
	$(CC) $(CFLAGS) -c browser_session.c -o browser_session.o

# NXGame Bridge - direct hardware access for games
nxgame_bridge.o: src/drivers/nxgame/nxgame_bridge.c include/drivers/nxgame/nxgame_bridge.h
	$(CC) $(CFLAGS) -c src/drivers/nxgame/nxgame_bridge.c -o nxgame_bridge.o

# Phase 12: Security hardening
security.o: src/core/security.c include/core/security.h
	$(CC) $(CFLAGS) -c src/core/security.c -o security.o

safe_mem.o: src/core/safe_mem.c include/core/safe_mem.h
	$(CC) $(CFLAGS) -c src/core/safe_mem.c -o safe_mem.o

panic_handler.o: src/core/panic_handler.c include/core/panic_handler.h
	$(CC) $(CFLAGS) -c src/core/panic_handler.c -o panic_handler.o

# XNU-inspired security modules (NeolyxOS original implementations)
code_integrity.o: src/security/code_integrity.c include/security/code_integrity.h
	$(CC) $(CFLAGS) -c src/security/code_integrity.c -o code_integrity.o

csr.o: src/security/csr.c include/security/csr.h
	$(CC) $(CFLAGS) -c src/security/csr.c -o csr.o

task_port_protection.o: src/security/task_port_protection.c include/security/task_port.h
	$(CC) $(CFLAGS) -c src/security/task_port_protection.c -o task_port_protection.o

# Phase D: GPU Acceleration and 3D
nxgfx_accel.o: src/drivers/graphics/nxgfx_accel.c include/drivers/nxgfx_accel.h
	$(CC) $(CFLAGS) -c src/drivers/graphics/nxgfx_accel.c -o nxgfx_accel.o

nx3d.o: src/video/nx3d.c include/video/nx3d.h
	$(CC) $(CFLAGS) -c src/video/nx3d.c -o nx3d.o

# Fault Tolerance Subsystem - kernel never crashes, never reboots
fault_router.o: src/core/fault_router.c include/core/fault_tolerance.h
	$(CC) $(CFLAGS) -c src/core/fault_router.c -o fault_router.o

emergency_stack.o: src/core/emergency_stack.c include/core/fault_tolerance.h
	$(CC) $(CFLAGS) -c src/core/emergency_stack.c -o emergency_stack.o

reboot_guard.o: src/core/reboot_guard.c include/core/fault_tolerance.h
	$(CC) $(CFLAGS) -c src/core/reboot_guard.c -o reboot_guard.o

watchdog.o: src/core/watchdog.c include/core/fault_tolerance.h
	$(CC) $(CFLAGS) -c src/core/watchdog.c -o watchdog.o

# Clean target
clean:
	rm -f *.o *.bin *.elf 2>/dev/null || del /Q *.o *.bin *.elf 2>nul || echo "No files to clean"

# Debug target
debug: CFLAGS += -g -O0
debug: kernel.bin

# Release target
release: CFLAGS += -O2 -DNDEBUG
release: kernel.bin

.PHONY: all clean test debug release
# Include platform-specific build rules
include Makefile.platform

