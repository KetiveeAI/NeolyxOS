# NeolyxOS NXRender C Library Makefile
# Copyright (c) 2025 KetiveeAI

CC = gcc
AR = ar

CFLAGS = -Wall -Wextra -std=c11 -O2 -g
CFLAGS += -I./include
LDFLAGS = -lm

# Source files
NXGFX_SRC = src/nxgfx/context.c src/nxgfx/text.c src/nxgfx/nxgfx_canvas.c src/nxgfx/lighting.c src/nxgfx/gpu_driver.c src/nxgfx/nxsvg.c src/nxgfx/nxgfx_effects.c src/nxgfx/software_renderer.c src/nxgfx/hdr.c
WIDGETS_SRC = src/widgets/widget.c src/widgets/button.c src/widgets/label.c src/widgets/textfield.c src/widgets/container.c src/widgets/slider.c src/widgets/checkbox.c src/widgets/listview.c src/widgets/tabview.c
NXRENDER_SRC = src/nxrender/compositor.c src/nxrender/window.c src/nxrender/application.c src/nxrender/device.c
LAYOUT_SRC = src/layout/layout.c src/layout/nx_layout_node.c src/layout/nx_layout_math.c src/layout/nx_layout_flex.c src/layout/nx_layout_grid.c src/layout/nx_layout_absolute.c
THEME_SRC = src/theme/theme.c
ANIM_SRC = src/animation/animation.c src/animation/animation_advanced.c
INPUT_SRC = src/input/input.c
FFI_SRC = src/reox_ffi.c

ALL_SRC = $(NXGFX_SRC) $(WIDGETS_SRC) $(NXRENDER_SRC) $(LAYOUT_SRC) $(THEME_SRC) $(ANIM_SRC) $(INPUT_SRC) $(FFI_SRC)

# NXCanvas (desktop/lib/nxcanvas): the rasterizer and TrueType engine NXGFX
# draws through. Built from where it lives, into a separate object dir per
# build so these objects never land next to the desktop's own .o files.
NXCANVAS_DIR = ../../desktop/lib/nxcanvas
NXCANVAS_SRC = $(wildcard $(NXCANVAS_DIR)/nxc_*.c)
NXCANVAS_HDR = $(wildcard $(NXCANVAS_DIR)/*.h) ../../desktop/include/nxcanvas.h
NXGFX_HDR = include/nxgfx/nxgfx.h src/nxgfx/nxgfx_internal.h $(NXCANVAS_HDR)

HOSTLIB_NXC_OBJ = $(patsubst $(NXCANVAS_DIR)/%.c,build/hostlib/nxcanvas/%.o,$(NXCANVAS_SRC))
ALL_OBJ = $(ALL_SRC:.c=.o) $(HOSTLIB_NXC_OBJ)

build/hostlib/nxcanvas/%.o: $(NXCANVAS_DIR)/%.c $(NXCANVAS_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

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
src/nxgfx/context.o src/nxgfx/text.o src/nxgfx/nxgfx_canvas.o: $(NXGFX_HDR)
src/widgets/widget.o: include/widgets/widget.h include/nxgfx/nxgfx.h
src/widgets/button.o: include/widgets/button.h include/widgets/widget.h
src/widgets/label.o: include/widgets/label.h include/widgets/widget.h
src/nxrender/compositor.o: include/nxrender/compositor.h include/nxgfx/nxgfx.h

.PHONY: info
info:
	@echo "NXRender C Library for NeolyxOS"
	@echo "Sources: $(ALL_SRC)"
	@echo "Output: $(LIB)"

# ============================================================================
# NeolyxOS userspace build (freestanding, consumed by sdk/reox.mk)
#
# Same sources as the host library minus the device/GPU/SVG back ends that
# depend on a hosted libc (fopen, sscanf, usleep).  Objects live under
# build/neolyx/ so they never mix with the host objects above.
# ============================================================================
NEOLYX_CFLAGS = -Wall -Wextra -std=c11 -O2 \
                -ffreestanding -fno-stack-protector -mno-red-zone \
                -U_FORTIFY_SOURCE -D__NEOLYXOS__ -I./include

NEOLYX_SRC = src/nxgfx/context.c src/nxgfx/text.c src/nxgfx/nxgfx_canvas.c src/nxgfx/lighting.c \
             src/nxgfx/nxgfx_effects.c src/nxgfx/software_renderer.c src/nxgfx/hdr.c \
             $(WIDGETS_SRC) \
             src/nxrender/window.c \
             $(LAYOUT_SRC) $(THEME_SRC) $(ANIM_SRC) $(INPUT_SRC) $(FFI_SRC)

NEOLYX_OBJ = $(patsubst src/%.c,build/neolyx/%.o,$(NEOLYX_SRC)) \
             $(patsubst $(NXCANVAS_DIR)/%.c,build/neolyx/nxcanvas/%.o,$(NXCANVAS_SRC))
NEOLYX_LIB = libnxrender_neolyx.a

.PHONY: neolyx neolyx-clean
neolyx: $(NEOLYX_LIB)

$(NEOLYX_LIB): $(NEOLYX_OBJ)
	$(AR) rcs $@ $^
	@echo "Built freestanding NXRender: $@"

build/neolyx/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(NEOLYX_CFLAGS) -c $< -o $@

build/neolyx/nxcanvas/%.o: $(NXCANVAS_DIR)/%.c $(NXCANVAS_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(NEOLYX_CFLAGS) -c $< -o $@

build/neolyx/nxgfx/context.o build/neolyx/nxgfx/text.o build/neolyx/nxgfx/nxgfx_canvas.o: $(NXGFX_HDR)

neolyx-clean:
	rm -rf build/neolyx $(NEOLYX_LIB)

# ============================================================================
# Host test library: the same sources as the freestanding build, compiled
# with hosted flags into build/host/, for app-level host tests.  Never
# touches the tracked libnxrender.a or src/**/*.o.
# ============================================================================
HOST_TEST_CFLAGS = -Wall -Wextra -std=c11 -O1 -g -I./include
HOST_TEST_OBJ = $(patsubst src/%.c,build/host/%.o,$(NEOLYX_SRC)) \
                $(patsubst $(NXCANVAS_DIR)/%.c,build/host/nxcanvas/%.o,$(NXCANVAS_SRC))
HOST_TEST_LIB = build/host/libnxrender_host.a

.PHONY: host-test-lib
host-test-lib: $(HOST_TEST_LIB)

$(HOST_TEST_LIB): $(HOST_TEST_OBJ)
	$(AR) rcs $@ $^

build/host/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_TEST_CFLAGS) -c $< -o $@

build/host/nxcanvas/%.o: $(NXCANVAS_DIR)/%.c $(NXCANVAS_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(HOST_TEST_CFLAGS) -c $< -o $@

build/host/nxgfx/context.o build/host/nxgfx/text.o build/host/nxgfx/nxgfx_canvas.o: $(NXGFX_HDR)
