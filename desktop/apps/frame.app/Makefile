CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g -I./include -I../../../gui/nxrender_c/include -I../../lib/nximage -I./src
LDFLAGS = -lm

# Reox UI (Mocked for build stability)
REOX_SRC = ui/main_window.reox
REOX_GEN_C = src/ui/main_window.c
REOX_GEN_O = src/ui/main_window.o

# Sources
SRCS = src/main.c \
       src/app.c \
       ../../lib/nximage/nximage.c \
       ../../lib/nximage/nxnxi.c \
       ../../lib/nximage/nxpng.c \
       ../../../gui/nxrender_c/src/nxgfx/context.c \
       ../../../gui/nxrender_c/src/nxgfx/software_renderer.c \
       ../../../gui/nxrender_c/src/nxgfx/gpu_driver.c \
       ../../../gui/nxrender_c/src/nxgfx/text.c \
       ../../../gui/nxrender_c/src/nxrender/application.c \
       ../../../gui/nxrender_c/src/nxrender/window.c \
       ../../../gui/nxrender_c/src/nxrender/compositor.c \
       ../../../gui/nxrender_c/src/nxrender/device.c \
       ../../../gui/nxrender_c/src/widgets/widget.c

OBJS = $(SRCS:.c=.o)

# Binary
TARGET = bin/frame

.PHONY: all clean ui

all: $(TARGET)

# Skip reoxc for now to ensure build succeeds with mock
# $(REOX_GEN_C): $(REOX_SRC)
# 	@mkdir -p src/ui
# 	$(REOXC) $< -o $@ --emit c

$(REOX_GEN_O): $(REOX_GEN_C)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(REOX_GEN_O)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o src/ui/*.o \
          ../../lib/nximage/*.o \
          ../../../gui/nxrender_c/src/nxgfx/*.o \
          ../../../gui/nxrender_c/src/nxrender/*.o \
          ../../../gui/nxrender_c/src/widgets/*.o \
          $(TARGET)
