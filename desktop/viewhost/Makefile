# NeolyxOS view host - built-in desktop views as their own processes
#
#   files/Files   the Path view (system file browser) -> /apps/Files
#
# Each program links one view from desktop/shell/views with the NxUI
# toolkit (desktop/shell/nxui.c in app mode), NXCanvas, the icon set,
# libnxwindow and the view host. Objects go to build/, never next to the
# desktop's own objects (the desktop links the same sources).
#
# Copyright (c) 2025 KetiveeAI

CC ?= gcc
LD ?= ld

ROOT    := $(abspath ../..)
DESKTOP := $(ROOT)/desktop
NXWINDOW_DIR := $(ROOT)/sdk/nxwindow
BUILD   := build

EXTRA_CFLAGS ?=
CFLAGS := -Wall -Wextra -std=c11 -O2 -ffreestanding -fno-stack-protector -mno-red-zone \
          -fno-builtin -fno-tree-loop-distribute-patterns \
          -U_FORTIFY_SOURCE -D__NEOLYXOS__ $(EXTRA_CFLAGS) \
          -I. -I$(DESKTOP)/include -I$(DESKTOP)/shell -I$(DESKTOP)/lib/nximage \
          -I$(ROOT)/gui/nxrender_c/include -I$(ROOT)/kernel/include \
          -I$(ROOT)/kernel/include/drivers -I$(ROOT)/kernel/include/core \
          -I$(NXWINDOW_DIR) -I$(ROOT)/config
LDFLAGS := -nostdlib -T $(DESKTOP)/user.ld

# Toolkit shared by every hosted view (paths relative to desktop/)
TOOLKIT_SRC := shell/nxui.c lib/nxframe.c lib/nxfont_registry.c \
               lib/nxicons/nxicons.c lib/nxicons/nxapp_icons.c lib/nxicons/nxicon_pack.c \
               lib/nxcanvas/nxc_core.c lib/nxcanvas/nxc_math.c lib/nxcanvas/nxc_paint.c \
               lib/nxcanvas/nxc_raster.c lib/nxcanvas/nxc_shapes.c lib/nxcanvas/nxc_image.c \
               lib/nxcanvas/nxc_effects.c lib/nxcanvas/nxc_font.c lib/nxcanvas/nxc_text.c \
               lib/nx_malloc.c lib/libc_stubs.c
TOOLKIT_ASM := shell/nxui_assets.S shell/nxicon_pack.S
HOST_SRC    := viewhost.c viewhost_open.c

FILES_VIEW_SRC := shell/views/path_view.c lib/nxpath_relay.c

d2o = $(patsubst %.c,$(BUILD)/desktop/%.o,$(1))
TOOLKIT_OBJ := $(call d2o,$(TOOLKIT_SRC)) $(patsubst %.S,$(BUILD)/desktop/%.o,$(TOOLKIT_ASM))
HOST_OBJ    := $(patsubst %.c,$(BUILD)/host/%.o,$(HOST_SRC))
FILES_OBJ   := $(BUILD)/host/files/files_main.o $(call d2o,$(FILES_VIEW_SRC))

.PHONY: all clean $(NXWINDOW_DIR)/libnxwindow.a
all: files/Files

$(NXWINDOW_DIR)/libnxwindow.a:
	$(MAKE) -C $(NXWINDOW_DIR)

files/Files: $(FILES_OBJ) $(HOST_OBJ) $(TOOLKIT_OBJ) $(NXWINDOW_DIR)/libnxwindow.a $(DESKTOP)/user.ld
	$(LD) $(LDFLAGS) -o $@ $(FILES_OBJ) $(HOST_OBJ) $(TOOLKIT_OBJ) $(NXWINDOW_DIR)/libnxwindow.a
	@echo "Built $@"

$(BUILD)/desktop/%.o: $(DESKTOP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# .incbin paths are relative to desktop/
$(BUILD)/desktop/%.o: $(DESKTOP)/%.S
	@mkdir -p $(dir $@)
	cd $(DESKTOP) && $(CC) -c $*.S -o $(abspath $@)

$(BUILD)/host/%.o: %.c viewhost.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) files/Files
