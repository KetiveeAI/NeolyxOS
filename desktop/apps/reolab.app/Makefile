# Reolab - REOX Development Environment
# NeolyxOS Application Build System
# Uses NXRender (native), NOT SDL

APP_NAME = reolab
VERSION = 1.0.0

# Directories
SRC_DIR = src
BIN_DIR = bin
INC_DIR = include

# NXRender/NXGFX path (3 levels up from reolab.app)
NXRENDER_DIR = ../../../gui/nxrender_c
NXRENDER_INC = $(NXRENDER_DIR)/include
NXRENDER_LIB = $(NXRENDER_DIR)

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -I$(INC_DIR) -I$(SRC_DIR)
CFLAGS += -I$(NXRENDER_INC)
CFLAGS += -DNEOLYXOS

LDFLAGS = -L$(NXRENDER_LIB) -lnxrender -lm

# Source files (NXRender backend)
SOURCES = main.c \
          $(SRC_DIR)/app/app.c \
          $(SRC_DIR)/editor/buffer.c \
          $(SRC_DIR)/graphics/graphics_nxrender.c \
          $(SRC_DIR)/theme/theme.c \
          $(SRC_DIR)/ui/ui.c

OBJECTS = $(SOURCES:.c=.o)

# Targets
.PHONY: all clean install run

all: $(BIN_DIR)/$(APP_NAME)

$(BIN_DIR)/$(APP_NAME): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "[BUILD] $(APP_NAME) built with NXRender"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS)
	rm -f $(BIN_DIR)/$(APP_NAME)
	@echo "[CLEAN] Cleaned build files"

install: $(BIN_DIR)/$(APP_NAME)
	@echo "[INSTALL] Installing $(APP_NAME)..."

run: $(BIN_DIR)/$(APP_NAME)
	./$(BIN_DIR)/$(APP_NAME)

debug: CFLAGS += -g -DDEBUG
debug: clean all

# SDL fallback (for development on non-NeolyxOS)
sdl:
	$(MAKE) -f Makefile.sdl

help:
	@echo "Reolab Makefile (NXRender)"
	@echo "  make        - Build with NXRender (native)"
	@echo "  make sdl    - Build with SDL2 (dev fallback)"
	@echo "  make run    - Build and run"
	@echo "  make clean  - Clean build files"
