# nxlibc - the NeolyxOS C library
#
#   make              build/libnxc.a and build/crt0.o for NeolyxOS (x86_64)
#   make check-host   build and run the host unit tests (tests/host/libc_*)
#   make clean
#
# Built on the development machine (Linux or Windows/MSYS2) with the host
# GCC; no cross-compiler is needed. Nothing from the host C library is
# used: -nostdinc keeps the host's headers out, and only the compiler's
# own freestanding headers (stddef.h, stdint.h, stdarg.h, stdbool.h,
# float.h) are on the include path.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

NXLIBC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
include $(NXLIBC_DIR)/nxlibc.mk

CC ?= gcc
AR ?= ar
BUILD := build

# Library code: everything warns, GCC 15's errors are errors here too
LIB_CFLAGS := $(NXLIBC_CFLAGS) -O2 -Wall -Wextra -Wshadow -Wstrict-prototypes \
              -Wmissing-prototypes -Werror=implicit-function-declaration \
              -Werror=implicit-int -Werror=int-conversion -Werror=incompatible-pointer-types \
              -I src

SRCS := \
    src/nx/syscall.c \
    src/nx/abi.c \
    src/start/start.c \
    src/start/log.c \
    src/errno/errno.c \
    src/ctype/ctype.c \
    src/string/mem.c \
    src/string/str.c \
    src/stdlib/malloc.c \
    src/stdlib/strtol.c \
    src/stdlib/strtod.c \
    src/stdlib/qsort.c \
    src/stdlib/misc.c \
    src/stdlib/exit.c \
    src/stdio/file.c \
    src/stdio/printf.c \
    src/stdio/printf_fp.c \
    src/stdio/scanf.c \
    src/time/time.c \
    src/math/kernels.c \
    src/math/math.c \
    src/assert/assert.c
ASRCS := src/setjmp/setjmp.S

OBJS := $(SRCS:src/%.c=$(BUILD)/obj/%.o) $(ASRCS:src/%.S=$(BUILD)/obj/%.o)

.PHONY: all clean check-host

all: $(NXLIBC_LIB) $(NXLIBC_CRT0)

$(NXLIBC_LIB): $(OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(OBJS)
	@echo "Built: $@"

# memcpy/memset/... must never be turned back into calls to themselves
$(BUILD)/obj/string/%.o: LIB_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns

$(BUILD)/obj/%.o: src/%.c $(wildcard include/*.h include/nx/*.h) src/internal.h
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(LIB_CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(NXLIBC_CFLAGS) -c $< -o $@

$(NXLIBC_CRT0): crt/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(NXLIBC_CFLAGS) -c $< -o $@

src/math/constants.h: tools/gen_math_constants.py
	python3 $< > $@

check-host:
	$(MAKE) -C ../../tests/host -f libc.mk run

clean:
	rm -rf $(BUILD)
