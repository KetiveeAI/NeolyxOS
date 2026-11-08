# libctest - nxlibc running on NeolyxOS itself
#
# Install as /apps/libctest on the boot disk and launch it from the app
# drawer; it prints "[LIBCTEST] RESULT: PASS" (or FAIL) to the serial log.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

ROOT := $(abspath ../../..)
include $(ROOT)/sdk/libc/nxlibc.mk

TARGET := libctest
OBJS   := libctest.o

.PHONY: all clean libc

all: $(TARGET)

libc:
	@$(MAKE) --no-print-directory -C $(NXLIBC_DIR)

$(TARGET): $(OBJS) libc
	$(LD) $(NXLIBC_LDFLAGS) -o $@ $(NXLIBC_CRT0) $(OBJS) $(NXLIBC_LIB)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(NXLIBC_CFLAGS) -O2 -Wall -Wextra -Werror=implicit-function-declaration \
	      -Werror=implicit-int -Werror=int-conversion -Werror=incompatible-pointer-types \
	      -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
