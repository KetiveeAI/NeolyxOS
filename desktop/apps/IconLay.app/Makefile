# IconLay.app Makefile
# 
# Builds with NXRender for GPU-accelerated vector graphics.
# Uses SDL2 only as window host for testing on Linux.

CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c11 -O2 -g -I./include
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -g -I./include
LDFLAGS = -lm

# NXRender library paths
NXRENDER_DIR = ../../../gui/nxrender_c
NXRENDER_CFLAGS = -I$(NXRENDER_DIR)/include
NXRENDER_LIBS = -L$(NXRENDER_DIR) -lnxrender

# Reox runtime paths
REOX_DIR = ../../../REOX/reolab/runtime
REOX_CFLAGS = -I$(REOX_DIR)
REOX_LIBS = -L$(REOX_DIR) -lreox_runtime

# SDL2 for window host (testing only)
SDL_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS = $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf 2>/dev/null || echo "-lSDL2 -lSDL2_image -lSDL2_ttf")

# Core sources (shared)
SRCS_CORE = src/nxi_format.c \
            src/layer.c \
            src/svg_import.c

# SDL2 legacy sources
SRCS_SDL = $(SRCS_CORE) src/main.c

# NXRender + SDL host sources
SRCS_NXRENDER_HOST = $(SRCS_CORE) \
            src/nxrender_backend.c \
            src/main_nxrender.c

# Pure nxgfx native sources (no SDL)
SRCS_NATIVE = $(SRCS_CORE) \
            src/iconlay_app.c \
            src/iconlay_effects.c \
            src/iconlay_ui.c \
            src/iconlay_widgets.c \
            src/colorpicker.c \
            src/iconlay_main.c

OBJS_SDL = $(SRCS_SDL:.c=.o)
OBJS_NXRENDER = $(SRCS_NXRENDER_HOST:.c=.o)
OBJS_NATIVE = $(SRCS_NATIVE:.c=.o)

# Targets
.PHONY: all clean nxrender sdl native test

all: sdl

# SDL2 legacy build (for development/testing)
sdl: CFLAGS += $(SDL_CFLAGS)
sdl: iconlay-sdl
iconlay-sdl: $(OBJS_SDL)
	$(CC) $(CFLAGS) -o iconlay-legacy $^ $(LDFLAGS) $(SDL_LIBS)

# NXRender + SDL host build
nxrender: CFLAGS += $(NXRENDER_CFLAGS) $(SDL_CFLAGS) -DUSE_NXRENDER
nxrender: iconlay-nxrender
iconlay-nxrender: $(OBJS_NXRENDER)
	$(CC) $(CFLAGS) -o iconlay $^ $(LDFLAGS) $(NXRENDER_LIBS) $(SDL_LIBS)

# Pure nxgfx native build (for NeolyxOS)
native: CFLAGS += $(NXRENDER_CFLAGS) $(REOX_CFLAGS) -DNXGFX_NATIVE
native: iconlay-native
iconlay-native: $(OBJS_NATIVE)
	$(CC) $(CFLAGS) -o iconlay-native $^ $(LDFLAGS) $(NXRENDER_LIBS) $(REOX_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: sdl
	./iconlay-legacy resource/Base.svg

clean:
	rm -f src/*.o iconlay iconlay-legacy iconlay-nxrender iconlay-native

# Dependencies
src/nxi_format.o: include/nxi_format.h
src/layer.o: include/layer.h include/nxi_format.h
src/svg_import.o: include/svg_import.h include/nxi_format.h include/layer.h
src/nxrender_backend.o: include/nxrender_backend.h include/nxi_format.h
src/main.o: include/nxi_format.h include/layer.h include/svg_import.h
src/main_nxrender.o: include/nxi_format.h include/layer.h include/svg_import.h include/nxrender_backend.h
src/iconlay_app.o: include/iconlay.h
src/iconlay_effects.o: include/iconlay.h
src/iconlay_ui.o: include/iconlay.h
src/colorpicker.o: include/colorpicker.h
