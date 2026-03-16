# NeolyxOS App Installer - Makefile
# Wizard-style installer with NXRender GUI support

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -lm

# NXRender paths (relative to Installer.app)
NXRENDER_ROOT = ../../gui/nxrender_c
NXRENDER_INC = $(NXRENDER_ROOT)/include
NXRENDER_LIB = $(NXRENDER_ROOT)/libnxrender.a

# NXZIP compression library
NXZIP_ROOT = ../../../lib/nxzip
NXZIP_INC = $(NXZIP_ROOT)/include
NXZIP_LIB = $(NXZIP_ROOT)/libnxzip.a

# Always include nxzip
CFLAGS += -I$(NXZIP_INC)
LDFLAGS += $(NXZIP_LIB)

# Check if NXRender is available
NXRENDER_EXISTS := $(shell test -f $(NXRENDER_LIB) && echo yes)

ifeq ($(NXRENDER_EXISTS),yes)
    CFLAGS += -DNXRENDER_UI -I$(NXRENDER_INC)
    LDFLAGS += $(NXRENDER_LIB)
    BUILD_MODE = NXRender GUI
else
    BUILD_MODE = Text Mode (NXRender not found)
endif

TARGET = bin/installer
SRCS = main.c \
       src/package.c \
       src/manifest.c \
       src/registry.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run test demo nxrender-info

all: nxrender-info $(TARGET)

nxrender-info:
	@echo "=== Build Mode: $(BUILD_MODE) ==="
	@test -f $(NXRENDER_LIB) || echo "Note: Build nxrender_c first for GUI mode"

$(TARGET): $(SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
	rm -rf bin/*.o src/*.o

run: $(TARGET)
	./$(TARGET)

# Run with demo package
demo: $(TARGET)
	./$(TARGET) manifest.npa

# Build NXRender first, then rebuild installer
with-nxrender:
	@echo "Building NXRender..."
	cd $(NXRENDER_ROOT) && make
	@echo "Rebuilding Installer with NXRender..."
	$(MAKE) clean
	$(MAKE) all

# Install to system (requires root)
install: $(TARGET)
	@mkdir -p /Applications/Installer.app/bin
	@mkdir -p /Applications/Installer.app/resources
	cp $(TARGET) /Applications/Installer.app/bin/
	cp manifest.npa /Applications/Installer.app/
	@echo "Installed to /Applications/Installer.app"

test: $(TARGET)
	@echo "=== Running Installer Tests ==="
	./$(TARGET) manifest.npa
