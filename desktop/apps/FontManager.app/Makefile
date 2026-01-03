# NeolyxOS FontManager.app Makefile
# Copyright (c) 2025 KetiveeAI

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -Wno-unused-parameter
CFLAGS += -I../../gui/nxrender_c/include
CFLAGS += -I../../kernel/include
CFLAGS += -I.

# NXRender library
NXRENDER_LIB = ../../gui/nxrender_c/libnxrender.a

# Source files (now in root, not src/)
SOURCES = main.c ttf.c ui.c reox.c font_install.c

OBJECTS = $(SOURCES:.c=.o)
TARGET = bin/fontmanager

.PHONY: all clean dirs kernel-objs

all: dirs $(TARGET)

dirs:
	@mkdir -p bin lib

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(NXRENDER_LIB)
	@echo "FontManager.app built successfully"

%.o: %.c state.h ttf.h ui.h font_service.h nxrender.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

# For kernel integration (builds object files only)
kernel-objs:
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -D__KERNEL__ -c main.c -o fontmanager.o
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -D__KERNEL__ -c ttf.c -o fontmanager_ttf.o
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -D__KERNEL__ -c ui.c -o fontmanager_ui.o
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -D__KERNEL__ -c reox.c -o fontmanager_reox.o
