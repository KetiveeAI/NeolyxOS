# NeolyxOS Settings.app Makefile
# Copyright (c) 2025 KetiveeAI

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Wno-unused-parameter
CFLAGS += -I../../REOX/reolab/runtime
CFLAGS += -I../../kernel/include
CFLAGS += -I.

# REOX Runtime Library
REOX_LIB = ../../REOX/reolab/runtime/libreox_runtime.a

# Source files
SOURCES = main.c \
          sidebar.c \
          search.c \
          drivers/settings_drivers.c \
          panels/system_panel.c \
          panels/network_panel.c \
          panels/bluetooth_panel.c \
          panels/display_panel.c \
          panels/sound_panel.c \
          panels/storage_panel.c \
          panels/accounts_panel.c \
          panels/security_panel.c \
          panels/privacy_panel.c \
          panels/appearance_panel.c \
          panels/power_panel.c \
          panels/processes_panel.c \
          panels/startup_panel.c \
          panels/scheduler_panel.c \
          panels/extensions_panel.c \
          panels/updates_panel.c \
          panels/bootloader_panel.c \
          panels/developer_panel.c \
          panels/about_panel.c

OBJECTS = $(SOURCES:.c=.o)
TARGET = bin/settings

.PHONY: all clean check dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p bin lib

$(TARGET): $(OBJECTS) $(REOX_LIB)
	$(CC) $(OBJECTS) $(REOX_LIB) -o $(TARGET)
	@echo "Settings.app built successfully"

%.o: %.c settings.h nxrender.h panels/panel_common.h
	$(CC) $(CFLAGS) -c $< -o $@

check:
	@echo "Checking syntax..."
	$(CC) -fsyntax-only $(CFLAGS) $(SOURCES)
	@echo "Syntax check passed!"

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

