# hello - template Makefile for a NeolyxOS C program on nxlibc
#
#   make            builds ./hello (static ELF, links at 0x1000000)
#   make clean
#
# Copy this directory to start a new program: change TARGET and OBJS.
# Any host GCC for x86_64 works; no cross-compiler is needed.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

NXLIBC_DIR := $(abspath ../..)
include $(NXLIBC_DIR)/nxlibc.mk

TARGET := hello
OBJS   := hello.o

CFLAGS := $(NXLIBC_CFLAGS) -O2 -Wall -Wextra \
          -Werror=implicit-function-declaration -Werror=implicit-int \
          -Werror=int-conversion -Werror=incompatible-pointer-types

.PHONY: all clean libc

all: $(TARGET)

libc:
	@$(MAKE) --no-print-directory -C $(NXLIBC_DIR)

$(TARGET): $(OBJS) libc
	$(LD) $(NXLIBC_LDFLAGS) -o $@ $(NXLIBC_CRT0) $(OBJS) $(NXLIBC_LIB)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
