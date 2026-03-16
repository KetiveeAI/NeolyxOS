# NeolyxOS Desktop - Userspace Build
# 
# Builds the desktop environment as a userspace ELF binary.
# Desktop runs in Ring 3, communicates with kernel via syscalls.
# 
# Real driver integration - NO SIMULATION:
#   - Framebuffer: via syscall or direct mapping
#   - Mouse: via PS/2 driver interrupt
#   - Keyboard: via PS/2 driver interrupt
#   - Timer: via PIT ticks
#
# Copyright (c) 2025 KetiveeAI

CC = gcc
LD = ld

INCLUDES = -I./include \
           -I./lib/nximage \
           -I../gui/nxrender_c/include \
           -I../kernel/include \
           -I../kernel/include/drivers \
           -I../kernel/include/core \
           -I../config

CFLAGS = -Wall -Wextra -std=c11 -O2 \
         -ffreestanding -fno-stack-protector \
         -mno-red-zone \
         -D__NEOLYXOS__ \
         $(INCLUDES)

LDFLAGS = -nostdlib -T user.ld

# Source files
# Note: compositor.c is excluded - desktop_shell.c has all functionality
# userspace_stubs.c provides syscall-based implementations of kernel functions
# nxappearance.c provides theme/appearance preferences
# nx_malloc.c provides global heap allocator (must be single TU, not inline!)
SHELL_SRC = shell/main.c \
            shell/desktop_shell.c \
            shell/userspace_stubs.c \
            shell/app_drawer.c \
            shell/lock_screen.c \
            shell/window_manager.c \
            shell/dock.c \
            shell/wallpaper.c \
            shell/anveshan.c \
            shell/vm_detect.c \
            shell/control_center.c \
            shell/notification_center.c \
            shell/nxrender_bridge.c \
            shell/volume_osd.c \
            shell/keyboard_osd.c \
            shell/theme_osd.c \
            shell/battery_warning.c \
            shell/network_indicator.c \
            lib/nxappearance.c \
            lib/nxconfig.c \
            lib/nxevent.c \
            lib/nx_arena.c \
            lib/nx_malloc.c \
            lib/libc_stubs.c \
            lib/nxi_render.c \
            apps/Calendar.app/calendar_utils.c \
            ../config/nx_config.c

WIDGET_SRC = widgets/widgets.c

# Terminal is built as separate app in apps/Terminal.app/
# TERMINAL_SRC = apps/terminal/terminal_window.c

OBJS = $(SHELL_SRC:.c=.o) $(WIDGET_SRC:.c=.o)
TARGET = neolyx-desktop

# NXRender graphics library + NXImage loader
NXIMAGE_OBJS = lib/nximage/nximage.o lib/nximage/nxnxi.o lib/nximage/nxpng.o lib/nximage/nxavif_stub.o
LIBS = ../gui/nxrender_c/libnxrender.a

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(LD) $(LDFLAGS) -o bin/$@ $^ $(LIBS)
	@echo "Built userspace desktop: bin/$(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) bin/$(TARGET)
	@echo "Cleaned desktop build"

# Install to initramfs
install: $(TARGET)
	cp bin/$(TARGET) ../initramfs/usr/bin/
	@echo "Installed to initramfs"

# Debug build
debug: CFLAGS += -g -O0
debug: $(TARGET)
