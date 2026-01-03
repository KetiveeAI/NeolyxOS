# NeolyxOS NXRender C Library Makefile
# Copyright (c) 2025 KetiveeAI

CC = gcc
AR = ar

CFLAGS = -Wall -Wextra -std=c11 -O2 -g
CFLAGS += -I./include
LDFLAGS = -lm

# Source files
NXGFX_SRC = src/nxgfx/context.c src/nxgfx/text.c src/nxgfx/lighting.c src/nxgfx/gpu_driver.c
WIDGETS_SRC = src/widgets/widget.c src/widgets/button.c src/widgets/label.c src/widgets/textfield.c src/widgets/container.c
NXRENDER_SRC = src/nxrender/compositor.c src/nxrender/window.c src/nxrender/application.c
LAYOUT_SRC = src/layout/layout.c
THEME_SRC = src/theme/theme.c
ANIM_SRC = src/animation/animation.c src/animation/animation_advanced.c

ALL_SRC = $(NXGFX_SRC) $(WIDGETS_SRC) $(NXRENDER_SRC) $(LAYOUT_SRC) $(THEME_SRC) $(ANIM_SRC)
ALL_OBJ = $(ALL_SRC:.c=.o)

# Output
LIB = libnxrender.a

.PHONY: all clean test

all: $(LIB)

$(LIB): $(ALL_OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(ALL_OBJ) $(LIB)
	rm -f tests/*.o tests/test_*

# Test targets
test: tests/test_basic
	./tests/test_basic

tests/test_basic: tests/test_basic.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lnxrender -o $@

# Dependencies
src/nxgfx/context.o: include/nxgfx/nxgfx.h
src/nxgfx/text.o: include/nxgfx/nxgfx.h
src/widgets/widget.o: include/widgets/widget.h include/nxgfx/nxgfx.h
src/widgets/button.o: include/widgets/button.h include/widgets/widget.h
src/widgets/label.o: include/widgets/label.h include/widgets/widget.h
src/nxrender/compositor.o: include/nxrender/compositor.h include/nxgfx/nxgfx.h

.PHONY: info
info:
	@echo "NXRender C Library for NeolyxOS"
	@echo "Sources: $(ALL_SRC)"
	@echo "Output: $(LIB)"
