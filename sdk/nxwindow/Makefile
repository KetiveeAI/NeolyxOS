# NeolyxOS SDK - libnxwindow (client window library)
#
# Builds libnxwindow.a for NeolyxOS userspace (freestanding, static).
# Spec: docs/desktop/proposals/libnxwindow.md. Apps include nxwindow.h with
#   -I<root>/sdk/nxwindow -I<root>/desktop/include -I<root>/kernel/include/core
# and link libnxwindow.a. The archive needs no libc: `make check` verifies
# that it references no symbol outside itself.
#
# Copyright (c) 2025 KetiveeAI

CC ?= gcc
AR ?= ar
NM ?= nm

ROOT := $(abspath ../..)

NXWINDOW_INCLUDES := -I$(ROOT)/sdk/nxwindow -I$(ROOT)/desktop/include -I$(ROOT)/kernel/include/core
EXTRA_CFLAGS ?=
CFLAGS := -Wall -Wextra -std=c11 -O2 -ffreestanding -fno-stack-protector -mno-red-zone \
          -fno-pie -m64 -fno-builtin -fno-tree-loop-distribute-patterns \
          -U_FORTIFY_SOURCE -D__NEOLYXOS__ $(EXTRA_CFLAGS) $(NXWINDOW_INCLUDES)

LIB := libnxwindow.a
OBJ := nxwindow.o

.PHONY: all clean check
all: $(LIB) check

$(OBJ): nxwindow.c nxwindow.h nxwin_proto.h $(ROOT)/desktop/include/nxsyscall.h
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	rm -f $@
	$(AR) rcs $@ $^

# Self-contained: no undefined symbols (no memcpy/memset or libc calls)
check: $(LIB)
	@u="$$($(NM) -u $(LIB) | grep -v '^$$' | grep -v ':$$')"; \
	if [ -n "$$u" ]; then echo "libnxwindow: undefined symbols:"; echo "$$u"; exit 1; fi; \
	echo "libnxwindow: self-contained"

clean:
	rm -f $(OBJ) $(LIB)
