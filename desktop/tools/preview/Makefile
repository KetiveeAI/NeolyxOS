# NeolyxOS - Desktop preview harness (host build)
#
# Compiles the real desktop shell for the development machine with
# NX_HOST_PREVIEW so syscalls go to host_syscalls.c, then renders frames
# to PNG. Use a private OBJ dir when several people/agents build at once.
#
#   make -C desktop/tools/preview OBJ=out/me            build out/me/preview
#   out/me/preview -n 90 -o shot.png [-s script.txt]    render frame 90
#
# Copyright (c) 2025 KetiveeAI

DESKTOP := ../..
include $(DESKTOP)/sources.mk

HOSTCC  ?= gcc
OBJ     ?= out/default
SAN     ?= -fsanitize=address,undefined
CFLAGS  := -std=gnu11 -O1 -g -fno-omit-frame-pointer $(SAN) -DNX_HOST_PREVIEW -D__NEOLYXOS__ \
           -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable \
           -I$(DESKTOP)/include -I$(DESKTOP)/lib/nximage -idirafter $(DESKTOP)/../kernel/include \
           -idirafter $(DESKTOP)/../kernel/include/drivers -idirafter $(DESKTOP)/../kernel/include/core -I$(DESKTOP)/../config
LDFLAGS := $(SAN) -rdynamic -ldl -lm

SRCS := $(STUBS_SRC) $(SHELL_SRC) $(WIDGET_SRC) $(NXCANVAS_SRC) $(NXICONS_SRC)
OBJS := $(addprefix $(OBJ)/,$(SRCS:.c=.o)) $(OBJ)/shell/nxui_assets.o $(OBJ)/host_main.o $(OBJ)/host_syscalls.o

.PHONY: all clean
all: $(OBJ)/preview

$(OBJ)/preview: $(OBJS)
	$(HOSTCC) -o $@ $^ $(LDFLAGS)
	@echo "preview built: $@"

$(OBJ)/%.o: $(DESKTOP)/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(CFLAGS) -c $< -o $@

$(OBJ)/shell/nxui_assets.o: $(DESKTOP)/shell/nxui_assets.S
	@mkdir -p $(dir $@)
	cd $(DESKTOP) && $(HOSTCC) -c shell/nxui_assets.S -o $(abspath $@)

$(OBJ)/host_main.o: host_main.c preview.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(CFLAGS) -c $< -o $@

$(OBJ)/host_syscalls.o: host_syscalls.c preview.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf out
