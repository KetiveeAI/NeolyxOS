# nxlibc - make include for NeolyxOS C programs
#
# Usage (from any Makefile):
#
#   include $(NEOLYX_ROOT)/sdk/libc/nxlibc.mk
#
#   hello: hello.o $(NXLIBC_LIB) $(NXLIBC_CRT0)
#   	$(LD) $(NXLIBC_LDFLAGS) -o $@ $(NXLIBC_CRT0) hello.o $(NXLIBC_LIB)
#   %.o: %.c
#   	$(CC) $(NXLIBC_CFLAGS) -O2 -c $< -o $@
#
# Build the library first with `make -C $(NXLIBC_DIR)` (or add
# $(NXLIBC_LIB) as a prerequisite and a rule that runs it).
#
# Variables
#   NXLIBC_DIR      this directory
#   NXLIBC_CFLAGS   compile flags: freestanding x86_64, nxlibc headers only
#   NXLIBC_LDFLAGS  link flags: static, no host libraries, nxapp.ld layout
#   NXLIBC_CRT0     the _start object; link it first
#   NXLIBC_LIB      the library archive; link it after your objects
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

NXLIBC_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

CC ?= gcc
LD ?= ld

# The compiler's own freestanding headers (stddef.h, stdint.h, stdarg.h,
# stdbool.h, float.h); never the host C library's.
NXLIBC_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

NXLIBC_CFLAGS := -std=c11 -m64 -ffreestanding -fno-pie -fno-stack-protector \
                 -mno-red-zone -fno-asynchronous-unwind-tables \
                 -nostdinc -I$(NXLIBC_DIR)/include -isystem $(NXLIBC_GCC_INCLUDE)

NXLIBC_LDFLAGS := -nostdlib -static -no-pie -z noexecstack -T $(NXLIBC_DIR)/crt/nxapp.ld

NXLIBC_CRT0 := $(NXLIBC_DIR)/build/crt0.o
NXLIBC_LIB  := $(NXLIBC_DIR)/build/libnxc.a
