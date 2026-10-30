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

# Source files (see sources.mk)
include sources.mk

OBJS = $(ENTRY_SRC:.c=.o) $(STUBS_SRC:.c=.o) $(FREESTANDING_SRC:.c=.o) $(SHELL_SRC:.c=.o) $(WIDGET_SRC:.c=.o) \
       $(NXCANVAS_SRC:.c=.o) $(NXICONS_SRC:.c=.o) $(ASM_SRC:.S=.o)
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

# Assets reference files relative to desktop/ (.incbin)
%.o: %.S
	$(CC) -c $< -o $@

# The icon pack has no header/asset tracking from the pattern rules above:
# rebuild it whenever tools/build_icon_pack.py regenerates its inputs.
ICON_PACK_FILES = $(wildcard ../System/Desktop/Icons/app-*.nxi ../System/Desktop/Icons/folder-*.nxi \
                             ../System/Desktop/Icons/device-*.nxi)
shell/nxicon_pack.o: $(ICON_PACK_FILES)
lib/nxicons/nxicon_pack.o: lib/nxicons/nxicon_pack_index.h include/nxicon_pack.h
lib/nxicons/nxapp_icons.o lib/nxi_render.o shell/desktop_shell.o: include/nxicon_pack.h include/nxicons.h

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
